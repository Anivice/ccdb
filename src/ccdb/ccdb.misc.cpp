// ccdb.misc.cpp
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

#include <chrono>
#include <thread>
#include <utility>
#include <cmath>
#include <string>
#ifndef __WINDOWS_MSVC_BUILD__
#include "additional_help.h"
#endif //__WINDOWS_MSVC_BUILD__
#include "Readline.h"
#include "print.h"
#ifndef __WINDOWS_MSVC_BUILD__
#include "ncursesw/ncurses.h"
#endif //__WINDOWS_MSVC_BUILD__
#include "ccdb.h"
#ifndef __WINDOWS_MSVC_BUILD__
#include "versions.h"
#endif //__WINDOWS_MSVC_BUILD__

// --------------------------------------------- CCDB --------------------------------------------- //
using namespace ccdb::utils;

void ccdb::ccdb::update_providers()
{
    backend_instance.update_proxy_list();
    auto proxy_list = backend_instance.get_proxies_and_latencies_as_pair().first;
    tsl::hopscotch_map <std::string, std::vector < std::string> > groups;
    for (const auto & [group, proxy] : proxy_list) {
        groups[group] = proxy.first;
    }

    g_proxy_list = groups;
}

#ifndef __WINDOWS_MSVC_BUILD__
void ccdb::ccdb::pager(const std::string &str, const bool override_less_check, bool use_pager)
{
    if (!override_less_check) {
        use_pager = true;
    }

    if (use_pager)
    {
        if (const auto [fd_stdout, fd_stderr, exit_status]
                    = exec_command("/bin/sh", str, "-c", less);
            exit_status != 0)
        {
            inferior_less(str);
        }
    } else {
        std::cout << str << std::flush;
    }
}
#else
void ccdb::ccdb::pager(const std::string &str, const bool, bool)
{
    inferior_less(str);
}
#endif //__WINDOWS_MSVC_BUILD__


bool ccdb::ccdb::is_connection_valid(const general_info_pulling::connection_t &conn)
{
    try {
        bool result = false;
        if (filter_patterns.contains(0)) {
            result |= std::regex_search(conn.host, std::regex(filter_patterns.at(0)));
        }

        if (filter_patterns.contains(1)) {
            result |= std::regex_search(conn.processName, std::regex(filter_patterns.at(1)));
        }

        if (filter_patterns.contains(6)) {
            result |= std::regex_search(conn.ruleName, std::regex(filter_patterns.at(6)));
        }

        if (filter_patterns.contains(8)) {
            result |= std::regex_search(conn.src, std::regex(filter_patterns.at(8)));
        }

        if (filter_patterns.contains(9)) {
            result |= std::regex_search(conn.destination, std::regex(filter_patterns.at(9)));
        }

        if (filter_patterns.contains(10)) {
            result |= std::regex_search(conn.networkType, std::regex(filter_patterns.at(10)));
        }

        if (filter_patterns.contains(11)) {
            result |= std::regex_search(conn.chainName, std::regex(filter_patterns.at(11)));
        }

        if (reverse_filter_list) return result;
        return !result;
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return true; // pattern failed, show the result
    }
}

void ccdb::ccdb::interactive_verification() const
{
    if (execute_and_no_interactive) {
        print<is_error>("This command requires an interactive terminal.\n");
        exit(1);
    }
}

static std::string g_help_additional;

#ifndef __WINDOWS_MSVC_BUILD__
void ccdb::ccdb::help()
{
    const auto str = Readline::command_template_tree.get_help();
    if (g_help_additional.empty()) {
        unsigned additional_help_len = 0;
        unsigned char * additional_help = nullptr;
        auto lang = utils::getenv("LANG");
        auto cut = [&](const char c) {
            if (lang.find(c) != std::string::npos) {
                lang = lang.substr(0, lang.find_first_of(c));
            }
        };

        cut('.');

        if (lang == "zh_CN") {
            additional_help_len = additional_help_zh_CN_len;
            additional_help = additional_help_zh_CN;
        } else {
            additional_help = additional_help_en;
            additional_help_len = additional_help_en_len;
        }
        std::vector<uint8_t> str_additional_compressed(additional_help_len, 0);
        std::memcpy(str_additional_compressed.data(), additional_help, additional_help_len);
        auto decompressed_help = utils::decompress(str_additional_compressed);
        decompressed_help.push_back(0);
        g_help_additional = reinterpret_cast<const char *>(decompressed_help.data());
    }

    std::stringstream oss;
    oss << static_cast<std::string>(version_string) << str << g_help_additional << std::endl;
    // pager(oss.str());
    std::cout << oss.str() << std::flush;
}
#else
void ccdb::ccdb::help()
{
    std::cout << Readline::command_template_tree.get_help() << std::endl;
}
#endif //__WINDOWS_MSVC_BUILD__


#ifndef __WINDOWS_MSVC_BUILD__
static int read_proc_exe(const pid_t pid, char *buf, size_t buflen)
{
    char linkpath[64] { };
    snprintf(linkpath, sizeof(linkpath), "/proc/%ld/exe", static_cast<long>(pid));
    const ssize_t n = readlink(linkpath, buf, buflen - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
}

void ccdb::ccdb::reset_terminal_mode_forcefully()
{
    const pid_t ppid = getppid();
    const pid_t sid = getsid(0);
    char exe[PATH_MAX] { };
    std::string exe_path, sid_path;
    if (read_proc_exe(ppid, exe, sizeof(exe)) == 0) {
        exe_path = exe;
    } else {
        print<is_error>("Read parent exe failed: ", strerror(errno), "\n");
    }

    if (read_proc_exe(sid, exe, sizeof(exe)) == 0) {
        sid_path = exe;
    } else {
        print<is_error>("Read session leader exe failed: ", strerror(errno), "\n");
    }

    // system dependent reset terminal mode
    if (std::system((sid_path + " -c 'reset'").c_str()) != 0)
    {
        if (std::system((exe_path + " -c 'reset'").c_str()) != 0)
        {
            if (std::system("/bin/sh -c 'reset'") != 0) {
                print<is_error>("Failed to reset shell mode even after exausting all means.\n");
            }
        }
    }
}

#else
void ccdb::ccdb::reset_terminal_mode_forcefully() { windows::reset_terminal(); }

#endif //__WINDOWS_MSVC_BUILD__

#ifdef __WINDOWS_MSVC_BUILD__
# include "windows_platform.h"
#endif //__WINDOWS_MSVC_BUILD__

void ccdb::ccdb::display(NotificationType < frame_data_t > & frame, const std::atomic_bool* running)
{
    setup_term setup_term;
    uint64_t current_frame_index = -1;
    while (*running)
    {
        if (const auto frame_data = frame.wait_for(500);
            frame_data && !frame_data->pause && frame_data->frame_index != current_frame_index)
        {
            if (frame_data->clear) // updated frame
            {
#ifdef __WINDOWS_MSVC_BUILD__
                ::ccdb::windows::terminal_write(setup_term.clear);
                ::ccdb::windows::terminal_write("\033[H\033[2J\033[3J");
#else //__WINDOWS_MSVC_BUILD__
                std::cout << setup_term.clear << std::flush;
                std::cout << "\033[H\033[2J\033[3J" << std::flush;
#endif //__WINDOWS_MSVC_BUILD__
            }
            else
            {
                setup_term.move_home();
#ifdef __WINDOWS_MSVC_BUILD__
                ::ccdb::windows::terminal_write(frame_data->frame);
#else //__WINDOWS_MSVC_BUILD__
                std::cout << frame_data->frame << std::flush;
#endif //__WINDOWS_MSVC_BUILD__
                setup_term.ed_clear();
            }

            current_frame_index = frame_data->frame_index;
        }
    }
}

// #ifdef __WINDOWS_MSVC_BUILD__
static std::string slide_window(
    const std::vector<std::string>& frame,
    int frame_width,
    int col,
    int line,
    int skip_lines,
    int skip_leading_spaces)
{
    using namespace ccdb::utils;

    if (col <= 0 || line <= 0)
        return {};

    frame_width = std::max(frame_width, 0);
    skip_lines = std::max(skip_lines, 0);
    skip_leading_spaces = std::max(skip_leading_spaces, 0);

    const int view_cols = col - 1;
    const int view_lines = line - 1;
    const int frame_height = static_cast<int>(frame.size());

    // Clamp offsets to the available scrolling range.
    skip_lines = std::min(
        skip_lines, std::max(0, frame_height - view_lines));
    skip_leading_spaces = std::min(
        skip_leading_spaces, std::max(0, frame_width - view_cols));

    const auto vertical = view_lines > 0 && frame_height > 0
        ? utf8_to_u32(generate_linear_handle(
              frame_height, skip_lines,
              skip_lines + view_lines, view_lines))
        : std::u32string(view_lines, U' ');

    const auto horizontal = frame_width > 0
        ? generate_linear_handle(
              frame_width, skip_leading_spaces,
              skip_leading_spaces + view_cols, col)
        : std::string(col, ' ');

    const std::string reset = ccdb::color::no_color();
    std::string result;

    const auto append_char = [&](char32_t ch) {
        result += utf8::utf32to8(std::u32string(1, ch));
    };

    for (int y = 0; y < view_lines; ++y)
    {
        result += reset;
        append_char(static_cast<std::size_t>(y) < vertical.size()
            ? vertical[y] : U' ');

        int printed = 0;
        bool previous_char_visible = false;
        const auto source_row =
            static_cast<std::size_t>(skip_lines) + y;

        if (source_row < frame.size() && view_cols > 0)
        {
            const auto text = utf8_to_u32(frame[source_row]);
            std::int64_t source_col = 0;
            const std::int64_t left = skip_leading_spaces;
            const std::int64_t right = left + view_cols;

            for (std::size_t i = 0; i < text.size();)
            {
                // Preserve SGR color/style sequences, including those
                // before the left edge, without counting their width.
                if (text[i] == U'\033')
                {
                    if (i + 1 < text.size() && text[i + 1] == U'[')
                    {
                        std::size_t end = i + 2;
                        while (end < text.size() &&
                               !(text[end] >= U'@' && text[end] <= U'~'))
                        {
                            ++end;
                        }

                        if (end == text.size())
                            break;

                        if (text[end] == U'm') {
                            result += utf8::utf32to8(
                                text.substr(i, end - i + 1));
                        }

                        i = end + 1;
                        continue;
                    }

                    ++i;
                    continue;
                }

                const char32_t ch = text[i++];

                // Input rows contain printable text and SGR styling.
                if (ch < U' ' || (ch >= U'\x7f' && ch < U'\xa0'))
                    continue;

                const int width = UnicodeDisplayWidth::get_width(ch);
                if (width < 0)
                    continue;

                if (width == 0)
                {
                    if (previous_char_visible)
                        append_char(ch);
                    continue;
                }

                if (source_col >= right)
                    break;

                const auto begin = source_col;
                const auto end = source_col + width;
                source_col = end;
                previous_char_visible = false;

                if (end <= left)
                    continue;

                const int visible = static_cast<int>(
                    std::min(end, right) - std::max(begin, left));

                if (begin < left || end > right) {
                    // A clipped wide character becomes blank cells.
                    result.append(visible, ' ');
                } else {
                    append_char(ch);
                    previous_char_visible = true;
                }

                printed += visible;
            }
        }

        result += reset;
        result.append(view_cols - printed, ' ');
        result += '\n';
    }

    result += reset;
    result += horizontal;
    result += reset;
    return result;
}

void ccdb::ccdb::inferior_less(const std::string & text)
{
    std::vector<std::string> frame_window;
    std::atomic_int leading_spaces, skip_lines, max_leading_spaces, max_skip_lines, mouse_x, mouse_y;
    NotificationType<frame_data_t> frame_stream;
    std::atomic<bool> running(true), sig_window_size_change(false);
    std::vector<std::thread> children;
    int window_width = 0, window_height = 0;
    uint64_t frame_counter = 0;

    {
        std::istringstream iss(text);
        std::string line;
        uint64_t max_line_size = 0;
        while (std::getline(iss, line))
        {
            frame_window.emplace_back(line);
            if (line.length() > max_line_size) max_line_size = line.length();
            if (const auto len = UnicodeDisplayWidth::get_width(strip_color(line));
                window_width < len)
            {
                window_width = len;
            }
        }

        window_height = static_cast<int>(frame_window.size());

        for (auto & str : frame_window)
        {
            if (str.size() < max_line_size) {
                str += std::string(max_line_size - str.size(), ' ');
            }
        }
    }

    children.emplace_back([&frame_stream, &running] {
        display(frame_stream, &running);
    });

    children.emplace_back([&]
    {
        get_conn_input_watcher({
            .running_ptr = &running,
            .leading_spaces_ptr = &leading_spaces,
            .max_leading_spaces_ptr = &max_leading_spaces,
            .current_skip_lines_ptr = &skip_lines,
            .max_skip_lines_ptr = &max_skip_lines,
            .mouse_x = &mouse_x,
            .mouse_y = &mouse_y,
            .space_pressed = nullptr,
            .kill_signal_sent = nullptr,
            .refocus = nullptr,
            .show_detail = nullptr,
            .sort_by_ptr = nullptr,
            .focus_move = nullptr,
            .pause = nullptr,
            .show_search = nullptr,
            .search_content_buffer = nullptr,
            .cursor_position = nullptr,
            .search_focus_move = nullptr,
            .tab_suggestion_requested =nullptr ,
            .reset_completion = nullptr
        });
    });

    auto signal_watcher = watcher.make_status_watcher();
    children.emplace_back([&signal_watcher, &running, &sig_window_size_change]
    {
        while (running) {
            if (signal_watcher.wait() == SIGWINCH) {
                sig_window_size_change = true;
            }
        }
    });

    while (running)
    {
        const auto [line, col] = get_screen_row_col();
        max_leading_spaces = window_width - col + 1 > 0 ? window_width - col + 1 : 0;
        max_skip_lines = window_height - line + 1 > 0 ? window_height - line + 1 : 0;
        if (skip_lines > max_skip_lines) skip_lines = max_skip_lines.load();
        if (leading_spaces > max_leading_spaces) leading_spaces = max_leading_spaces.load();

        if (mouse_y == line && mouse_x >= 1)
        {
            const int ll_max_leading_spaces_ = max_leading_spaces;
            auto new_leading_space = static_cast<int>(std::round(static_cast<double>(mouse_x) /
                static_cast<double>(col) * ll_max_leading_spaces_));
            if (new_leading_space > ll_max_leading_spaces_) new_leading_space = ll_max_leading_spaces_;
            if (new_leading_space < 0) new_leading_space = 0;
            leading_spaces = new_leading_space;
        }

        if (mouse_x == 1)
        {
            const int ll_max_skip_lines = max_skip_lines;
            auto new_skip_lines = mouse_y == 1 ? 0 :
                static_cast<int>(std::round(static_cast<double>(mouse_y) / static_cast<double>(line - 1) * ll_max_skip_lines));
            if (new_skip_lines > ll_max_skip_lines) new_skip_lines = ll_max_skip_lines;
            if (new_skip_lines < 0) new_skip_lines = 0;
            skip_lines = new_skip_lines;
        }

        const auto frame = slide_window(frame_window, window_width, col, line, skip_lines, leading_spaces);
        frame_stream.push({
            .frame_index = ++frame_counter,
            .frame = frame,
            .clear = false,
            .pause = false
        });

        const auto local_leading_spaces = leading_spaces.load();
        const auto local_skip_lines = skip_lines.load();
        const auto local_mouse_x = mouse_x.load();
        const auto local_mouse_y = mouse_y.load();

        for (int i = 0; i < 100; i++)
        {
            if (local_leading_spaces != leading_spaces
                || local_skip_lines != skip_lines
                || local_mouse_x != mouse_x
                || local_mouse_y != mouse_y
                || sig_window_size_change)
            {
                sig_window_size_change = false;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    running = false;
    signal_watcher.stop();
    frame_stream.push({ .frame_index = ++frame_counter, .pause = true });
    std::ranges::for_each(children, [](std::thread &T) { if (T.joinable()) T.join(); });
}

std::string ccdb::ccdb::color_json(const std::string & json)
{
    try {
        const auto format = nlohmann::json::parse(json).dump(2);

        const auto reset = color::no_color();
        const auto key = color::color24(0, 219, 235);
        const auto string = color::color24(0, 209, 1);
        const auto number = color::color24(206, 255, 0);
        const auto boolean = color::color24(255, 0, 247);
        const auto null = color::color24(156, 156, 156);

        std::string result;
        result.reserve(format.size());

        const auto is_space = [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };

        const auto append_colored =
            [&](std::size_t start, std::size_t end, const char * color) {
                result += color;
                result.append(format, start, end - start);
                result += reset;
            };

        for (std::size_t i = 0; i < format.size();) {
            const auto start = i;
            const char c = format[i];

            if (c == '"') {
                ++i; // Opening quote.

                while (i < format.size()) {
                    if (format[i] == '\\') {
                        i += 2; // Skip the escape and its following character.
                    } else if (format[i++] == '"') {
                        break;
                    }
                }

                auto next = i;
                while (next < format.size() && is_space(format[next])) {
                    ++next;
                }

                const bool is_key = next < format.size() && format[next] == ':';
                append_colored(start, i, is_key ? key.c_str() : string.c_str());
            } else if (c == '-' || (c >= '0' && c <= '9')) {
                do {
                    ++i;
                } while (
                    i < format.size() &&
                    ((format[i] >= '0' && format[i] <= '9') ||
                     format[i] == '.' || format[i] == 'e' ||
                     format[i] == 'E' || format[i] == '+' ||
                     format[i] == '-')
                );

                append_colored(start, i, number.c_str());
            } else if (c == 't' || c == 'f' || c == 'n') {
                // Input has already been validated and serialized.
                i += (c == 'f' ? 5 : 4);
                append_colored(start, i, c == 'n' ? null.c_str() : boolean.c_str());
            } else {
                result += c; // Whitespace and punctuation.
                ++i;
            }
        }

        return result;
    } catch (...) {
        return json;
    }
}
// #endif //__WINDOWS_MSVC_BUILD__
