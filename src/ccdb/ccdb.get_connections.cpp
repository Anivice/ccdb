// ccdb.get_connections.cpp
//
// Copyright 2026 Anivice Ives
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY// without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

static constexpr int max_closed_connection_count = 4096;

#include <algorithm>
#include <chrono>
#include <utility>
#include <fstream>
#include "print.h"
#include "ccdb.h"
#include "utils.h"

#ifdef __WINDOWS_MSVC_BUILD__
#include <windows.h>
#include <iostream>
#include <streambuf>
#include <stdexcept>
#include <system_error>

class ConsoleInput final : private std::streambuf {
    HANDLE handle_;
    DWORD original_mode_{};
    std::streambuf* original_buffer_{};
    char character_{};

    [[noreturn]] static void fail(const char* operation) {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            operation
        );
    }

    int_type underflow() override {
        if (gptr() < egptr())
            return traits_type::to_int_type(*gptr());

        DWORD count = 0;

        // Read exactly one character, without CRT buffering.
        if (!ReadConsoleA(
            handle_, &character_, 1, &count, nullptr))
            fail("ReadConsoleA");

        if (count == 0)
            return traits_type::eof();

        setg(&character_, &character_, &character_ + 1);
        return traits_type::to_int_type(character_);
    }

public:
    ConsoleInput()
        : handle_(GetStdHandle(STD_INPUT_HANDLE)) {

        if (!handle_ || handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("No console input handle");

        if (!GetConsoleMode(handle_, &original_mode_))
            fail("GetConsoleMode");

        DWORD mode = original_mode_;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

        if (!SetConsoleMode(handle_, mode))
            fail("SetConsoleMode");

        if (!FlushConsoleInputBuffer(handle_)) {
            DWORD error = GetLastError();
            SetConsoleMode(handle_, original_mode_);
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                "FlushConsoleInputBuffer"
            );
        }

        // Replace std::cin's input source.
        original_buffer_ = std::cin.rdbuf(this);
        std::cin.clear();
    }

    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;

    ~ConsoleInput() {
        std::cin.rdbuf(original_buffer_);
        std::cin.clear();
        SetConsoleMode(handle_, original_mode_);
    }

    void discard_pending() {
        // Discard the character cached by our streambuf.
        setg(nullptr, nullptr, nullptr);

        // Discard all queued Windows console input events.
        if (!FlushConsoleInputBuffer(handle_))
            fail("FlushConsoleInputBuffer");

        std::cin.clear();
    }
};
#endif //__WINDOWS_MSVC_BUILD__

namespace
{
    struct connection_frame_t
    {
        general_info_pulling::connection_t connection_data;
        std::chrono::system_clock::time_point time_of_the_closure;
        bool connection_is_closed = false;
    };
}

// --------------------------------------------- CCDB --------------------------------------------- //
using namespace ccdb::utils;
#define pad_string(NAME)                                                                            \
static void pad_##NAME(std::vector < connection_frame_t > & conns)                                  \
{                                                                                                   \
    int max = -1;                                                                                   \
    std::ranges::for_each(conns, [&](const connection_frame_t & t)                                  \
    {                                                                                               \
        if (const auto pos = t.connection_data.NAME.find_last_of(':'); pos != std::string::npos)    \
        {                                                                                           \
            if (const auto len = t.connection_data.NAME.size() - pos; len > 0) {                    \
                if (max < static_cast<int>(len)) max = len;                                         \
            }                                                                                       \
        }                                                                                           \
    });                                                                                             \
                                                                                                    \
    std::ranges::for_each(conns, [&max](connection_frame_t & t)                                     \
    {                                                                                               \
        if (const auto pos = t.connection_data.NAME.find_last_of(':'); pos != std::string::npos)    \
        {                                                                                           \
            if (const auto len = t.connection_data.NAME.size() - pos; len > 0) {                    \
                t.connection_data.NAME += std::string(((max - len) > 0) ? (max - len) : 0, ' ');    \
            }                                                                                       \
        }                                                                                           \
    });                                                                                             \
}

namespace
{
    pad_string(host)
    pad_string(src)

    template < typename T >
    class compare_t
    {
        const T & host;
        std::function<bool(const T&a, const T&b)> comp;

    public:
        explicit compare_t(const T & host_, std::function<bool(const T&a, const T&b)> comp_) : host(host_), comp(std::move(comp_)) {}

        bool operator > (const compare_t & other) const {
            return comp(host, other.host);
        }

        bool operator == (const compare_t & other) const {
            return host == other.host;
        }

        bool operator < (const compare_t & other) const {
            return !(*this == other || *this > other); // < --> !(>=)
        }

        compare_t & operator = (const compare_t& other) = default;
        compare_t & operator = (compare_t&& other) = default;
        compare_t(const compare_t & other) = default;
        compare_t(compare_t&& other) = default;
    };

    class host_compare_t : public compare_t < std::string >
    {
    public:
        explicit host_compare_t(const std::string & h) : compare_t(h, sort_url_if_fit) { }
        host_compare_t() : compare_t("", [](const auto &, const auto &){return false;}) { }
    };
}

void ccdb::ccdb::get_connections(const std::vector<std::string>& command_vector)
{
#ifdef __DEBUG__
    uint64_t total = 0, skip = 0;
#endif
    std::vector<bool> do_col_hide;
    do_col_hide.resize(get_conn_titles.size(), false);
    auto hide_col = [](const std::string & hides, std::vector<bool> & do_col_hide)
    {
        std::string str_num;
        std::vector<int> numeric_values;
        std::istringstream numeric_stream(hides);
        while (std::getline(numeric_stream, str_num, ','))
        {
            if (str_num.find('-') == std::string::npos)
            {
                numeric_values.push_back(convertToNumber<int>(str_num));
            }
            else
            {
                std::string start = str_num.substr(0, str_num.find('-'));
                std::string stop = str_num.substr(str_num.find('-') + 1);
                const auto begin = convertToNumber<int>(start);
                const auto end = convertToNumber<int>(stop);
                for (int i = begin; i <= end; i++) {
                    numeric_values.push_back(i);
                }
            }
        }

        for (const auto & i : numeric_values)
        {
            if (do_col_hide.size() > i) {
                do_col_hide[i] = true;
            }
        }
    };

    if (command_vector.size() == 4)
    {
        if (command_vector[2] == "hide") {
            hide_col(command_vector[3], do_col_hide);
        } else {
            throw std::invalid_argument(sprint("Unknown command `") + command_vector[2] + "`");
        }
    }

    const auto color_for_closed_connections = color::color24(255,255,255,128,128,128);

    tsl::hopscotch_map < std::string, connection_frame_t > connection_frame;
    static tsl::hopscotch_map < std::string, connection_frame_t > closed_connection_frame; // preserved
    std::vector < connection_frame_t > connections_filtered;
    auto subinfo_ball = std::make_unique<ccdb_atomic_t<subinfo_ball_t>>();
    subinfo_worker_t subinfo_worker;
    std::vector < std::string > title_this_session;
    bool pause_update = false;

    std::vector<int> sort_by_local;
    bool reverse_sort_local = sort_reverse;
    using ScopeType = std::pair<std::vector<connection_frame_t>::const_iterator, std::vector<connection_frame_t>::const_iterator>;
    using ArgsCopyScope = const ScopeType &;
    bool sorted_already = false;
    auto before = std::chrono::system_clock::now() - std::chrono::seconds(2);
    std::vector < std::vector < std::string > > table_vals;
    bool show_only_closed_connection_view = false;
    int closed_connection_view_max_size = max_closed_connection_count;
    int last_mouse_scrolling_vex = 0;
    auto unstable_timepoint = std::chrono::system_clock::now() - std::chrono::seconds(2);

    auto sort = [](std::vector<connection_frame_t> & connections, const std::vector<int> & sort_by_final)
    {
        std::ranges::sort(connections,
        [&](const connection_frame_t& a_, const connection_frame_t& b_)
        {
            const auto& a = a_.connection_data;
            const auto& b = b_.connection_data;

            const auto compare = [](const auto& x, const auto& y) -> int {
                if (x > y) return 1;
                if (y > x) return -1;
                return 0;
            };

            const auto compare_src = [&](const std::string& x,
                                         const std::string& y) -> int
            {
                const auto split = [](const std::string_view s)->std::string_view
                {
                    const auto pos = s.rfind(':');
                    return s.substr(0, pos);
                };

                return compare(split(x), split(y));
            };

            // Lazy construction, only needed for host comparisons.
            const auto compare_host = [&]() -> int {
                return compare(
                    host_compare_t(a.host),
                    host_compare_t(b.host)
                );
            };

            for (const int key : sort_by_final)
            {
                int result = 0;

                switch (key) {
                case 0:
                        result = compare_host();
                        break;
                    case 1:
                        result = compare(a.processName, b.processName);
                        break;
                    case 2:
                        result = compare(a.totalDownloadedBytes, b.totalDownloadedBytes);
                        break;
                    case 3:
                        result = compare(a.totalUploadedBytes, b.totalUploadedBytes);
                        break;
                    case 4:
                        result = compare(a.downloadSpeed, b.downloadSpeed);
                        break;
                    case 5:
                        result = compare(a.uploadSpeed, b.uploadSpeed);
                        break;
                    case 6:
                        result = compare(a.ruleName, b.ruleName);
                        break;
                    case 7:
                        result = compare(
                            a.timeElapsedSinceConnectionEstablished,
                            b.timeElapsedSinceConnectionEstablished
                        );
                        break;
                    case 8:
                        result = compare_src(a.src, b.src);
                        break;
                    case 9:
                        result = compare(a.destination, b.destination);
                        break;
                    case 10:
                        result = compare(a.networkType, b.networkType);
                        break;
                    case 11:
                        result = compare(a.chainName, b.chainName);
                        break;
                    default:
                        result = compare(
                            a.downloadSpeed + a.uploadSpeed,
                            b.downloadSpeed + b.uploadSpeed
                        );
                        break;
                    }

                    if (result != 0)
                        return result > 0;
                }

                return compare_host() > 0;
        });
    };

    continuous_table <connection_frame_t, std::vector<connection_frame_t>::const_iterator, ScopeType >
    (
        1,
        do_col_hide,
        // hard coded alignment justification: 0 left, 1: right, 2 center
    {
            1 /* host */, 2 /* process */, 1 /* DL */, 1 /* UP */, 1 /* DL Speed */, 1 /* UP Speed */,
            0 /* Rules */, 1 /* Time */, 1 /* Src IP */, 0 /* Dest IP */, 2 /* Type */, 0 /* Chains */
        },
        {
                {
                    "hide",
                        [&](ArgsCopyScope, CommandVectorType cmd, const session_compliment_data_t *)->std::string
                        {
                            if (cmd.size() == 2) {
                                hide_col(cmd[1], do_col_hide);
                                return {};
                            }

                            return sprint("Unknown command");
                        },
                },
                {
                    "closeAll",
                        [this](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            if (!backend_instance.close_all_connections())
                            {
                                return sprint("Failed to close all connections");
                            }
                            return {};
                        },
                },
                {
                    "closeOnScreen",
                        [this, &show_only_closed_connection_view](ArgsCopyScope conns, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            if (show_only_closed_connection_view) return {}; // silent drop
                            std::stringstream ss;
                            std::for_each(conns.first, conns.second,[&](const auto & conn) {
                                if (!backend_instance.close_connection(conn.connection_data.metadata.connectionID)) {
                                    ss << sprint("Failed to close connection", conn.connection_data.host, " ");
                                }
                            });

                            return ss.str();
                        }
                },
                {
                    "filterReverse",
                        [this](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            reverse_filter_list = !reverse_filter_list;
                            return sprint("Filter reversed, current mode: ", reverse_filter_list ? "reverse" : "normal");
                        },
                },
                {
                    "clearFilters",
                        [this](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            clear_filter(); return {};
                        },
                },
                {
                    "reverseChainParser",
                        [&](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            if (show_only_closed_connection_view) return {}; // silent drop
                            backend_instance.parse_chains = !backend_instance.parse_chains;
                            connection_frame.clear();
                            return {};
                        },
                },
                {
                    "filter",
                        [this](ArgsCopyScope, CommandVectorType vec, const session_compliment_data_t *)->std::string
                        {
                            if (vec.size() == 3)
                            {
                                try
                                {
                                    const auto filter_id = convertToNumber<uint64_t>(vec[1]);
                                    if (filter_id >= get_conn_titles.size()) throw std::invalid_argument(sprint("Invalid filter ID"));
                                    std::regex r(vec[2]);
                                    filter_patterns[filter_id] = vec[2];
                                }
                                catch (const std::exception & e) {
                                    return sprint("Failed to parse filter pattern: ", e.what());
                                }

                                return { };
                            }

                            return sprint("Invalid filter command");
                        },
                },
                {
                    "pause",
                        [&](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            pause_update = true;
                            return sprint("Update paused");
                        }
                },
                {
                    "resume",
                        [&](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            pause_update = false;
                            return sprint("Update resumed");
                        }
                },
                {
                    "sort",
                        [&](ArgsCopyScope, CommandVectorType vec, const session_compliment_data_t *)->std::string
                        {
                            if (vec.size() == 2)
                            {
                                try
                                {
                                    const auto sort_id = convertToNumber<uint64_t>(vec[1]);
                                    if (sort_id >= get_conn_titles.size()) throw std::invalid_argument(sprint("Invalid sort ID"));
                                    sort_by.set({static_cast<int>(sort_id)});
                                }
                                catch (const std::exception& e) {
                                    return sprint("Invalid sort: ", e.what());
                                }

                                return { };
                            }

                            return sprint("Invalid sort command");
                        }
                },
                {
                    "sortReverse",
                        [&](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            sort_reverse = !sort_reverse; return {};
                        }
                },
                {
                    "switchView",
                        [&](ArgsCopyScope, CommandVectorType, const session_compliment_data_t *)->std::string
                        {
                            show_only_closed_connection_view = !show_only_closed_connection_view;
                            return sprint(show_only_closed_connection_view ? "Closed connection view" : "Continuous connection view");
                        }
                },
                {
                    "move",
                        [&](ArgsCopyScope, CommandVectorType cmd, const session_compliment_data_t * data_)->std::string
                        {
                            try {
                                if (cmd.size() == 3)
                                {
                                    if (cmd[1] == "vertical") {
                                        *data_->skip_lines_ += convertToNumber<int>(cmd[2]);
                                    } else if (cmd[1] == "horizontal") {
                                        *data_->leading_spaces_ += convertToNumber<int>(cmd[2]);
                                    } else {
                                        return sprint("Unknown command");
                                    }
                                }
                            } catch (const std::exception& e) {
                                return e.what();
                            }

                            return { };
                        }
                },
                {
                    "setClosedConnectionBufferSpace",
                        [&](ArgsCopyScope, CommandVectorType vec, const session_compliment_data_t *)->std::string
                        {
                            if (vec.size() == 2)
                            {
                                try
                                {
                                    closed_connection_view_max_size = convertToNumber<int>(vec[1]);
                                    if (closed_connection_view_max_size <= 0) closed_connection_view_max_size = max_closed_connection_count;
                                    return sprint("Size set to ", closed_connection_view_max_size);
                                }
                                catch (const std::exception& e) {
                                    return sprint("Invalid arguments: ", e.what());
                                }
                            }

                            return sprint("Invalid command");
                        }
                }
            },
            {
                {"filter", {{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"}}},
                {"sort", {{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"}}},
                {"move", {{"vertical", "horizontal"}}}
            },
        [&](const session_compliment_data_t * data_)->ScopeType
        {
#ifdef __DEBUG__
            ++total;
#endif
            const auto now = std::chrono::system_clock::now();
            if (last_mouse_scrolling_vex != data_->skip_lines_->load()) { // skip fast scrolling
#ifdef __DEBUG__
                ++skip;
#endif
                unstable_timepoint = now;
                last_mouse_scrolling_vex = data_->skip_lines_->load();
                return ScopeType{connections_filtered.begin(), connections_filtered.end()};
            }

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - unstable_timepoint).count() < 500) {
                return ScopeType{connections_filtered.begin(), connections_filtered.end()};
            }

            unstable_timepoint = now;

            std::vector<int> sort_by_global = sort_by.get();
            if (sort_by_global.empty()) {
                sort_by_global = {0, 4, 5, 2, 3, 11};
                sort_by.set(sort_by_global);
            }

            // final sort value
            std::vector<int> sort_by_final;
            if (*data_->sort_by_from_watcher == -1) {
                sort_by_final = sort_by_global;
            }
            else
            {
                if (sort_by_global.size() == 1 && sort_by_global.front() == *data_->sort_by_from_watcher) {
                    sort_reverse = !sort_reverse;
                    sort_by_final = sort_by_global;
                } else if (sort_by_global.size() <= 1 /* && sort_by_global.front() != *data_->sort_by_from_watcher */) {
                    sort_by.set({data_->sort_by_from_watcher->load()});
                    sort_by_final = { *data_->sort_by_from_watcher };
                } else {
                    // multi sort chain, swap the first sort qualification
                    if (sort_by_global.front() != *data_->sort_by_from_watcher) {
                        sort_by_global.erase(sort_by_global.begin());
                        sort_by_global.insert(sort_by_global.begin(), *data_->sort_by_from_watcher);
                        // update global sort chain
                        sort_by.set(sort_by_global);
                        sort_by_final = sort_by_global;
                    } else {
                        sort_reverse = !sort_reverse;
                        sort_by_final = sort_by_global;
                    }
                }

                *data_->sort_by_from_watcher = -1;
            }

            if (sort_by_local != sort_by_final || sort_reverse != reverse_sort_local) sorted_already = false;
            sort_by_local = sort_by_final;
            sort_by = sort_by_final;

            // set existing ones as all closed
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - before).count() > 500 && !pause_update)
            {
                before = now;
                sorted_already = false; // sort all after an update
                const auto cur_time = std::chrono::system_clock::now();
                for (auto it = connection_frame.begin(); it != connection_frame.end(); ++it)
                {
                    if (auto & c_ = it.value(); !c_.connection_is_closed) {
                        c_.connection_is_closed = true;
                        c_.time_of_the_closure = cur_time;
                    }
                }

                // get current connections, if missing, add it, if exist, update info and remove the close flag
                std::ranges::for_each(backend_instance.get_active_connections(),
                    [&connection_frame](auto & c_)
                    {
                        auto it = connection_frame.find(c_.metadata.connectionID);
                        if (it != connection_frame.end()) {
                            it.value().connection_is_closed = false;
                            it.value().connection_data = c_;
                        } else {
                            connection_frame.emplace(c_.metadata.connectionID, c_);
                        }
                    });

                // remove closed connections lasting more than 3s
                auto it = connection_frame.begin();
                while (it != connection_frame.end())
                {
                    auto& c_ = it->second;
                    const bool should_delete = c_.connection_is_closed &&
                        std::chrono::duration_cast<std::chrono::seconds>(cur_time - c_.time_of_the_closure).count() > 3;

                    if (should_delete) {
                        auto new_data = it->second.connection_data;
                        new_data.chainName = "Closed at " + format_time_local(it->second.time_of_the_closure) + " | " + new_data.chainName;
                        closed_connection_frame.emplace(it->first, connection_frame_t{
                            .connection_data = new_data,
                            .time_of_the_closure = it->second.time_of_the_closure,
                            .connection_is_closed = false, // remove the color override
                        });
                        it = connection_frame.erase(it);
                    } else {
                        ++it;
                    }
                }

                const auto old_size = connections_filtered.size();
                connections_filtered.clear();
                auto filter = [&](const connection_frame_t & connection)
                {
                    // determine if we need to filter out the result
                    if (is_connection_valid(connection.connection_data)) {
                        connections_filtered.emplace_back(connection);
                    }
                };

                if (closed_connection_frame.size() > closed_connection_view_max_size)
                {
                    const auto & map_closed = closed_connection_frame | std::views::values;
                    std::vector<connection_frame_t> map_closed_vector { map_closed.begin(), map_closed.end() };
                    std::ranges::sort(map_closed_vector, [](const connection_frame_t & a_, const connection_frame_t & b_)->bool {
                        return a_.time_of_the_closure < b_.time_of_the_closure; // older ones goes first
                    });
                    map_closed_vector.resize(map_closed_vector.size() - closed_connection_view_max_size);
                    for (const auto & connection : map_closed_vector) {
                        closed_connection_frame.erase(connection.connection_data.metadata.connectionID);
                    }
                }

                for (const auto & map_active =
                    (show_only_closed_connection_view ? closed_connection_frame : connection_frame) | std::views::values;
                        const auto & connection : map_active)
                {
                    filter(connection);
                }

                const auto new_size = connections_filtered.size();
                if (show_only_closed_connection_view && *data_->skip_lines_ != 0 && new_size > old_size) {
                    *data_->skip_lines_ += static_cast<int>(new_size - old_size);
                }
            }
            else
            {
#ifdef __DEBUG__
                ++skip;
#endif
            }

            if (!sorted_already && (!pause_update || (pause_update && (sort_by_local != sort_by_global || reverse_sort_local != sort_reverse))))
            {
                sort(connections_filtered, sort_by_final);
                sorted_already = true; // skip sort if no updates
                pad_host(connections_filtered);
                pad_src(connections_filtered);

                if (sort_reverse)
                {
                    std::ranges::reverse(connections_filtered);
                    sort_by_local = sort_by_global;
                    reverse_sort_local = sort_reverse;
                }
            }

            return ScopeType{connections_filtered.begin(), connections_filtered.end()};
        },
        [&](const message_type_t type, const connection_frame_t & current_focus, std::vector<std::string> & buffer)->void
        {
            switch (type)
            {
            default:
            case NORMAL:
                {
                    std::stringstream ss;
                    auto append_msg = [&](const std::string & msg) {
                        ss << msg;
                    };

                    append_msg(sprint("Backend memory usage: ") + value_to_size(backend_instance.current_memory_in_use_by_mihomo.load(std::memory_order_relaxed)));
                    append_msg("   ");
                    append_msg(sprint("Frontend memory usage: ") + value_to_size(cur_mem_size()));
                    append_msg("   ");
                    append_msg("Sort chain: " + sprint(sort_by_local));
                    append_msg("   ");
#ifndef __WINDOWS_MSVC_BUILD__
                    append_msg(update_subinfo(subinfo_ball, subinfo_worker));
#endif //__WINDOWS_MSVC_BUILD__
                    buffer = {ss.str()};
                }
            break;
            case FOCUSED_ON_NON_PRESENCE:
                buffer = {sprint("Connection ", current_focus.connection_data.host, " not present, deleted")};
            break;
            case KILL:
                buffer = {sprint("Closing ", current_focus.connection_data.host, "...")};
            break;
            }
        },
        [](const connection_frame_t & conn)->std::string { return conn.connection_data.metadata.connectionID; },
        [&color_for_closed_connections](const ScopeType & on_screen_conns,
            uint64_t current_skip_lines)->tsl::hopscotch_map<uint64_t, std::string>
        {
            tsl::hopscotch_map<uint64_t, std::string> ret;
            std::for_each(on_screen_conns.first, on_screen_conns.second, [&](const connection_frame_t & conn)
            {
                if (conn.connection_is_closed) ret.emplace(current_skip_lines, color_for_closed_connections);
                current_skip_lines++;
            });
            return ret;
        },
        [this](const connection_frame_t * matched_connection)
        {
            if (matched_connection)
            {
#ifndef __WINDOWS_MSVC_BUILD__
                if (!jq.empty()) {
                    exec_command("/bin/sh", matched_connection->connection_data.metadata.raw_json,
                        "-c", jq + (color::is_no_color() ? "" : " --color-output") + " | " + less);
                } else {
                    exec_command("/bin/sh", matched_connection->connection_data.metadata.raw_json, "-c", less);
                }
#else
                pager(json::parse(matched_connection->connection_data.metadata.raw_json).dump(2));
                ConsoleInput input;
                input.discard_pending();
                std::cin.get();
#endif //__WINDOWS_MSVC_BUILD__
            }
        },
        [this, &show_only_closed_connection_view](const connection_frame_t * matched_connection)
        {
            if (matched_connection && !show_only_closed_connection_view) {
                try {
                    (void)backend_instance.close_connection(matched_connection->connection_data.metadata.connectionID);
                } catch (...) { }
            }
        },
        [&]()->StringScopeType
        {
            title_this_session.clear();
            int index_title = 0;
            std::ranges::for_each(get_conn_titles, [&](const std::string & title)
            {
                switch (index_title) {
                    case 2: // DL
                        title_this_session.emplace_back(title + " [" + value_to_size(backend_instance.get_total_downloaded_bytes()) + "]");
                        break;
                    case 3: // UP
                        title_this_session.emplace_back(title + " [" + value_to_size(backend_instance.get_total_uploaded_bytes()) + "]");
                        break;
                    case 4: // DL Speed
                        title_this_session.emplace_back(title + " [" + value_to_speed(backend_instance.get_current_download_speed()) + "]");
                        break;
                    case 5: // UP Speed
                        title_this_session.emplace_back(title + " [" + value_to_speed(backend_instance.get_current_upload_speed()) + "]");
                        break;
                    default:
                        title_this_session.emplace_back(title);
                        break;
                }

                if (!sort_by_local.empty() && index_title == sort_by_local.front()) {
                    title_this_session.back() += (sort_reverse ? " + " : " - ");
                }

                title_this_session.back() += " <F" + std::to_string(index_title + 1) + ">";
                index_title++;
            });

            return {title_this_session.begin(), title_this_session.end()};
        },
        [&table_vals](const auto & current_frame)->PrintTableValScopeType
        {
            table_vals.clear();
            table_vals.reserve(current_frame.second - current_frame.first);
            std::for_each(current_frame.first, current_frame.second, [&](const auto & connection)
            {
                table_vals.emplace_back(std::vector{
                    connection.connection_data.host,
                    connection.connection_data.processName,
                    value_to_size(connection.connection_data.totalDownloadedBytes),
                    value_to_size(connection.connection_data.totalUploadedBytes),
                    value_to_speed(connection.connection_data.downloadSpeed),
                    value_to_speed(connection.connection_data.uploadSpeed),
                    connection.connection_data.ruleName,
                    second_to_human_readable(connection.connection_data.timeElapsedSinceConnectionEstablished),
                    connection.connection_data.src,
                    connection.connection_data.destination,
                    connection.connection_data.networkType,
                    connection.connection_data.chainName,
                });
            });

            return {table_vals.begin(), table_vals.end()};
        },
        [&](session_compliment_data_t *)
        {
            sort_by_local = sort_by.get();
            reverse_sort_local = sort_reverse;
        }
    );

#ifdef __DEBUG__
    std::cout << " total " << total << ", skipped " << skip << std::endl;
#endif
}
