// ccdb.h
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

#ifndef CCDB_H
#define CCDB_H

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <algorithm>
#include <map>
#include <utility>
#include "config.h"
#include "general_info_pulling.h"
#include "tsl/hopscotch_map.h"
#include "utils.h"
#ifndef __WINDOWS_MSVC_BUILD__
#include "re2/re2.h"
#endif //__WINDOWS_MSVC_BUILD__
#ifndef __WINDOWS_MSVC_BUILD__
#include "re2/set.h"
#endif //__WINDOWS_MSVC_BUILD__

namespace ccdb
{
    using String = std::string;
    using HashType = String;
    using OverrideColorType = tsl::hopscotch_map<uint64_t, std::string>;
    using StringIteratorType = std::vector<String>::const_iterator;
    using StringScopeType = std::pair<StringIteratorType, StringIteratorType>;
    using PrintTableLineType = std::vector<std::vector<std::string>>::const_iterator;
    using PrintTableValScopeType = std::pair < PrintTableLineType, PrintTableLineType >;
    using PrintTableHideIteratorType = std::vector < bool >::const_iterator;
    using PrintTableHideScopeType = std::pair < PrintTableHideIteratorType, PrintTableHideIteratorType >;
    using ColumnAlignmentIteratorType = std::vector < int >::const_iterator;
    using ColumnAlignmentScopeType = std::pair < ColumnAlignmentIteratorType, ColumnAlignmentIteratorType >;

    struct print_table_context_t
    {
        enum class output_mode_t
        {
            frame, // Return a viewport with scrollbars and a search/status line.
            full_table, // Write the complete table, including its bottom border.
            unpaged_table // Write untrimmed lines with the existing no-pager footer behavior.
        };

        PrintTableHideScopeType hidden_columns{};
        int horizontal_offset = 0; // Display columns skipped at the left edge.
        std::atomic_int * max_horizontal_offset = nullptr; // Also enables horizontal clipping in frame mode.
        output_mode_t output_mode = output_mode_t::frame;
        // Borrowed banner overrides; valid for the duration of print_table().
        std::string_view external_banner;
        std::string_view first_banner_override;
        int row_offset = 0; // Data rows skipped at the top.
        std::atomic_int * max_row_offset = nullptr;
        const OverrideColorType * row_colors = nullptr; // Zero-based data row indices.
        std::span<const std::string_view> screen_line_colors; // One-based rendered line indices.
        std::ostream * output_stream = nullptr; // Text output destination; nullptr calls pager().
        std::atomic_bool * show_search = nullptr;
        ccdb_atomic_t < std::u32string > * search_content = nullptr;
        std::atomic_int * search_cursor = nullptr; // Code point offset in search_content.
        std::string_view search_pattern;
        ColumnAlignmentScopeType column_alignment{}; // 0: left, 1: right, 2: center.
        int terminal_height = 0;
        int terminal_width = 0;

        int * status_width = nullptr; // Width before the horizontal scrollbar, used for mouse positioning.
        struct width_cache_t
        {
            std::vector<uint32_t> value_screen_widths;
            std::vector<int> visible_column_widths;
            std::vector<int> title_screen_widths;
            std::vector<uint32_t> column_widths;
            std::vector<int> line_widths;
            std::vector<int> trimmed_line_widths;
        };
        width_cache_t * width_cache = nullptr; // Reuse numeric/clipping buffers; never retain source strings.

        struct {
            const char * vertical;
            const char * horizontal;
        } scroll_lock_labels{};
    };

#ifndef __WINDOWS_MSVC_BUILD__
    template <
        typename... ArgsForFetcherChild, typename... ArgsForFetcherParent,
        typename ChildFunc = std::function<bool(int, ArgsForFetcherChild...)>,
        typename ParentFunc = std::function<bool(int, ArgsForFetcherParent...)>
    >
    [[nodiscard]] bool detach_execute(
        const ChildFunc & child_func, ArgsForFetcherChild... args_for_fetcher_child,
        const ParentFunc & parent_func, ArgsForFetcherParent... args_for_fetcher_parent,
        int timeout_ms);

#endif //__WINDOWS_MSVC_BUILD__
    template <typename T> concept Iterator = std::forward_iterator<T>;
    extern std::atomic<int> g_pid;

    template < typename Itr_ >
    std::pair<Itr_, Itr_> make_screen_vector_frame(const Itr_ begin, const Itr_ end, const uint64_t ScopeSize,
        const int current_skip_lines, const int get_line_size, const int start_line, const uint64_t window_frame_size)
    {
        if (begin == end) return { };
        if (current_skip_lines >= ScopeSize) return { }; // throw std::logic_error("Internal BUG");
        const uint64_t endScope = current_skip_lines +
            std::min(static_cast<long>(get_line_size - start_line),
                    static_cast<long>(ScopeSize) - current_skip_lines);
        const auto offset_end = std::min(
            std::min(endScope, ScopeSize),
            current_skip_lines + window_frame_size);
        return { std::ranges::next(begin, current_skip_lines, end),
                 std::ranges::next(begin, static_cast<std::iter_difference_t<Itr_>>(offset_end), end) };
    }

    /// signal watcher
    extern class signal_watcher_ {
    public:
        std::atomic_bool watcher_clear_disable = false; // disable auto clear
        std::list < NotificationType<int> * > watchers;
        std::mutex watcher_mutex;

        /// Create this instance to skip readline screen clear on SIGINT.
        /// SIGINT watcher will resume clear when this instance is destroyed.
        /// This instance can also be used in if() to check if SIGINT is received.
        class auto_signal_status_t {
        private:
            signal_watcher_ * watcher_;
            NotificationType<int> notification_;
            explicit auto_signal_status_t(signal_watcher_ * _watcher);
            bool stopped_ = false;

        public:
            friend class signal_watcher_;
            ~auto_signal_status_t();
            void stop();
            [[nodiscard]] int wait(); // return -1 as an indication of thread abort. blocked
        };

        auto_signal_status_t make_status_watcher() { return auto_signal_status_t(this); }

    protected:
        std::atomic_bool sigint_watcher_running = true;
        utils::thread_group worker_threads;
        void sigint_watcher();

    private:
        NotificationType<int> SignalWatcher;

    public:
        signal_watcher_();
        ~signal_watcher_();
    } watcher;

    namespace table_detail {
        template<class I, class S> auto range(const std::pair<I, S> & pair)
        { return std::ranges::subrange(pair.first, pair.second); }
        template<std::ranges::forward_range R> auto range(R & value)
        { return std::ranges::ref_view(value); }

        // This deliberately rejects owning string conversions from proxy elements.
        inline std::string_view text(std::string_view value) { return value; }
        inline std::string_view text(const std::string & value) { return value; }
        inline const std::string & text(const std::reference_wrapper<const std::string> value)
        { return value.get(); }
        inline const std::string & text(const std::reference_wrapper<std::string> value)
        { return value.get(); }
        int display_width(std::string_view);
        std::u32string decode(std::string_view);
        std::string make_border(std::span<const int>, const char *);
        void append_cell(std::string &, std::string_view, int, int);
        void print_status(const print_table_context_t &, std::ostream &, int, int, int, uint64_t, int,
                          const std::string &);
        class table_frame_formatter_t {
            const print_table_context_t & context_;
            const std::string cursor_style_;
            const char32_t cursor_;
            const std::string search_style_;
            std::u32string format_search_line(int) const;
        public:
            explicit table_frame_formatter_t(const print_table_context_t &);
            std::string format(std::string_view, int, int, int, int) const;
        };
        struct line_printer_t {
            const print_table_context_t & context;
            print_table_context_t::width_cache_t & width_cache;
            uint64_t horizontal_offset;
            const std::string & border_style;
            std::ostream & frame;
            std::ostream & text_output;
            int & rendered_line_count;
            int & matches;
            int tab_space_size;
            std::u32string search_pattern;
            line_printer_t(const print_table_context_t &, print_table_context_t::width_cache_t &,
                           uint64_t, const std::string &, std::ostream &, std::ostream &, int &, int &);
            void operator()(std::string_view, const std::string &) const;
        };
        std::string row_style(const print_table_context_t &, int, int, const std::string &);
    }

    class ccdb
    {
    private:
        template<typename T> struct function_return_type;

        template<typename R, typename... Args>
        struct function_return_type<std::function<R(Args...)>> {
            using type = R;
        };

        template<typename T> using function_return_type_t = function_return_type<T>::type;

#ifndef __WINDOWS_MSVC_BUILD__
        template <typename handler_t, typename returnType = function_return_type_t<handler_t> >
        class RegexDispatcher
        {
        public:
            RegexDispatcher(): regex_set_(RE2::Options{}, RE2::ANCHOR_BOTH) {}
            bool add(const std::string& pattern, handler_t handler_)
            {
                std::string error;
                if (const int id = regex_set_.Add(pattern, &error); id < 0) return false;
                handlers_.emplace_back(std::move(handler_));
                return true;
            }

            bool compile() {
                return regex_set_.Compile();
            }

            template<typename... Args>
            returnType dispatch(const std::string & input, const Args& ...args)
            {
                std::vector<int> matches;
                if (!regex_set_.Match(input, &matches)) throw std::invalid_argument("Command not found");
                const int id = *std::ranges::min_element(matches);
                return handlers_[id](args...);
            }

        private:
            RE2::Set regex_set_;
            std::vector<handler_t> handlers_;
        };

#else
        template<typename handler_t, typename returnType = function_return_type_t<handler_t>>
        class RegexDispatcher {
            std::vector<std::pair<std::regex, handler_t>> handlers_;
        public:
            bool add(const std::string& pattern, handler_t handler_) {
                handlers_.emplace_back(std::regex(pattern), std::move(handler_));
                return true;
            }
            bool compile() { return true; }
            template<typename... Args>
            returnType dispatch(const std::string& input, const Args&... args) {
                for (const auto& [pattern, handler_] : handlers_)
                    if (std::regex_match(input, pattern)) return handler_(args...);
                throw std::invalid_argument("Command not found");
            }
        };

#endif //__WINDOWS_MSVC_BUILD__
        // const bool experimental_features = utils::getenv("CCDB_ENABLE_EXPERIMENTAL_FEATURES") == "true";
        general_info_pulling backend_instance; // backend instance

        // get connections table: titles
        const std::array<std::string, 12> get_conn_titles = {
            utils::get_text("Host"),         // 0
            utils::get_text("Process"),      // 1
            utils::get_text("DL"),           // 2
            utils::get_text("UP"),           // 3
            utils::get_text("DL Speed"),     // 4
            utils::get_text("UP Speed"),     // 5
            utils::get_text("Rules"),        // 6
            utils::get_text("Time"),         // 7
            utils::get_text("Source IP"),    // 8
            utils::get_text("Destination IP"),   // 9
            utils::get_text("Type"),         // 10
            utils::get_text("Chains"),       // 11
        };

        const std::array<std::string, 3> log_titles = {
            utils::get_text("Time"), utils::get_text("Level"), utils::get_text("Log")
        };

        const std::array<std::string, 3> chat_titles = {
            utils::get_text("Time"), utils::get_text("User"), utils::get_text("Message")
        };

        bool reverse_filter_list = false; // reverse white list
        tsl::hopscotch_map < uint64_t, std::string > filter_patterns; // Regex filter patterns for `get connections`
        ccdb_atomic_t<std::vector<int>> sort_by; // get connections table: sort by which column
        std::atomic_bool sort_reverse = false; // get connections table: if sort is reversed?
        tsl::hopscotch_map < uint64_t, std::string > index_to_proxy_name_list; // vector translation list
        tsl::hopscotch_map < std::string /* groups */, std::vector < std::string > /* endpoint */ > g_proxy_list; // group-proxy list
        const std::string latency_url; // latency URL
        std::unique_ptr<configuration> ccdb_config;
        std::string clash_sublink;
        // std::string jq;
        std::string less;
        tsl::hopscotch_map < std::string, std::string > keyboard_shortcut_map;
        std::mutex keyboard_shortcut_map_mtx;
        std::function<bool(const std::vector<std::string> &)> handler;
        std::function<std::vector<std::string>(const std::vector<std::string> &, const std::string &, int)> auto_completion;
        std::vector<std::string> listed_all_commands_in_path;
        std::atomic_int screen_refresh_interval_in_ms = 500;
        tsl::hopscotch_map < std::string, std::string > alias_list;
        std::atomic_int & max_log_size = backend_instance.max_log_size;
        std::string external_puller_command;
        int external_puller_command_time_out_ms = 10000;
        std::deque < std::vector<std::string> > logPullerNoFilter;
        std::mutex logPullerNoFilter_mtx;
        enum log_level_t : uint8_t { ERROR = 1, DEBUG, WARNING, };
        using handler_t = std::function<bool(const std::vector<std::string> &)>;
        RegexDispatcher<handler_t> commandMatchesRegexCompiled;

        bool execute_and_no_interactive = false;
        std::atomic_bool reverse_mouse;
        std::string banner_command;

        /// Pull groups and proxies from the backend
        void update_providers();

        /// print nload-like status update.
        /// This is supposed to be running in a separated thread
        /// @param total_upload atomic uint64_t of total uploaded bytes
        /// @param total_download atomic uint64_t of total downloaded bytes
        /// @param upload_speed atomic uint64_t of current upload speed
        /// @param download_speed atomic uint64_t of current download speed
        /// @param running atomic bool for thread running flag
        /// @param top_3_connections_using_most_speed // top 3 connections using most bandwidth
        /// @param top_3_connections_using_most_speed_mtx // mutex for top 3 connections using most bandwidth
        /// @returns NONE
        void nload(
            const std::atomic < uint64_t > * total_upload, const std::atomic < uint64_t > * total_download,
            const std::atomic < uint64_t > * upload_speed, const std::atomic < uint64_t > * download_speed,
            std::atomic_bool * running,
            std::vector < std::string > & top_3_connections_using_most_speed,
            std::mutex * top_3_connections_using_most_speed_mtx);

        struct frame_data_t
        {
            uint64_t frame_index = 0;
            std::string frame;
            bool clear = false;
            bool pause = false;
        };

        static void display(NotificationType < frame_data_t > & frame, const std::atomic_bool * running);

        /// show info using pager
        /// @param str content to be shown
        /// @param override_less_check Skip availability test
        /// @param use_pager pager availability flag. If override_less_check is true, then use_pager is dictated here
        /// otherwise, pager availability flag is set automatically.
        /// @returns NONE
        void pager(const std::string & str, bool override_less_check = false, bool use_pager = true);

        /// print table
        // Multipass ranges (or iterator pairs); each row must contain at least
        // as many cells as there are titles. Extra cells are ignored. No input is copied.
        template<class Titles, class Rows, class Banners = std::span<const std::string>>
        std::string print_table(Titles &&, Rows &&, const print_table_context_t &, Banners && = {});

        void simple_print_table(
            std::vector < std::string > const & table_titles,
            std::vector < std::vector < std::string > > const & table_values);

        void simple_print_table_to_ostream(
            std::vector < std::string > const & table_titles,
            std::vector < std::vector < std::string > > const & table_values,
            std::ostream & out_stream);

        std::string simple_print_table_to_std_string(
            std::vector < std::string > const & table_titles,
            std::vector < std::vector < std::string > > const & table_values);

        void simple_print_table_w_pager(
            std::vector < std::string > const & table_titles,
            std::vector < std::vector < std::string > > const & table_values);

        bool is_connection_valid(const general_info_pulling::connection_t & conn);

        /// get proxy groups
        [[nodiscard]] std::vector<std::string> get_groups();
        /// get proxy endpoint by group name
        [[nodiscard]] std::vector<std::string> get_endpoints(const std::string & group);
        /// get vector proxy groups
        [[nodiscard]] std::vector<std::string> get_vgroups();
        /// get vector proxy group endpoints
        [[nodiscard]] std::vector<std::string> get_vendpoints(const std::string & group);
        void interactive_verification() const;

        static int arg_index(const std::string &buffer, int cursor_position);

        enum message_type_t { NORMAL = 0, FOCUSED_ON_NON_PRESENCE, KILL };
        enum search_move_t : int { IDLE_STATE = -1, SEARCH_MOVE_UP = 1, SEARCH_MOVE_DOWN = 2 };

    protected:
        // --- COMMANDS --- //

        void nload(const std::vector<std::string> &);
        void get_connections(const std::vector<std::string>& command_vector);
        void get_latency();
        void get_log();
        void get_logLevel() const;
        void get_proxy();
        void get_rules();
        void get_providerRules();
        void upgrade(const std::vector<std::string> & command_vector);
        void get_latencyHistory(std::vector<std::string> command_vector);
        void get_vecGroupProxy(bool show_vgroups = true);
        void set_mode(const std::vector<std::string> & command_vector) const;
        void __attribute_deprecated__ set_group(const std::vector<std::string> & command_vector);
        void set_vgroup(const std::vector<std::string> & command_vector);
        void set_chain_parser(const std::vector<std::string> & command_vector);
        void set_allowlan(const std::vector<std::string> & command_vector) const;
        void set_log_level(const std::vector<std::string> & command_vector);
        void set_sort_by(const std::vector<std::string> & command_vector);
        void set_sort_reverse(const std::vector<std::string> & command_vector);
        void set_filter_reverse(const std::vector<std::string> & command_vector);
        void set_filter(const std::vector<std::string> & command_vector);
        void clear_filter();
        void get_filter();
#ifndef __WINDOWS_MSVC_BUILD__
        void get_subinfo();
#endif //__WINDOWS_MSVC_BUILD__
        void get_config();
        void get_log_size() const;
        void get_filter_reverse() const;
        void get_sort_reverse() const;
        void get_sort_by();
        void help();
        static void reset_terminal_mode_forcefully();
        void set_port(int port); // Mihomo http proxy port,
        void set_socksport(int port); // Mihomo socks5 proxy port,
        void set_redirport(int port); // Mihomo redirect port,
        void set_tproxyport(int port); // Mihomo transparent proxy port,
        void set_mixedport(int port); // Mihomo mixed proxy port,
        void set_log_size(const std::vector<std::string> & command_vector);
        void apply() const;
#ifndef __WINDOWS_MSVC_BUILD__
        void fork_and_execute(const std::vector<std::string> &, int);
#endif //__WINDOWS_MSVC_BUILD__
        void map_proxy_chain();
        void ccdbrc();
        void reload(const std::vector<std::string> &) const;
#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
        void proxyView();
#endif //__YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
        void changeRuleAffinity(const std::vector<std::string> &);
        void storage(const std::vector<std::string> &);
#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
        void tls_watchdog(const std::vector<std::string> &);
#endif //__YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
        bool commandProcessor(const std::vector<std::string> & command_vector_);
        std::vector<std::string> commandAutoCompletion(const std::vector<std::string> &, const std::string &, int arg_index);

        /// Input watcher that sets running flag when q is pressed
        /// @param name Thread name
        /// @param running Running flag
        void generic_input_watcher(const std::string & name, std::atomic_bool * running) const;

        struct get_conn_input_watcher_context_t {
            std::atomic_bool * running_ptr;
            std::atomic_int * leading_spaces_ptr;
            const std::atomic_int * max_leading_spaces_ptr;
            std::atomic_int * current_skip_lines_ptr;
            const std::atomic_int * max_skip_lines_ptr;
            std::atomic_int * mouse_x;
            std::atomic_int * mouse_y;
            std::atomic_bool * space_pressed;
            std::atomic_bool * kill_signal_sent;
            std::atomic_bool * refocus;
            std::atomic_bool * show_detail;
            std::atomic_int * sort_by_ptr;
            std::atomic_int * focus_move;
            const std::atomic_bool * pause;
            std::atomic_bool * show_search;
            ccdb_atomic_t < std::u32string > * search_content_buffer;
            std::atomic_int * cursor_position;
            std::atomic < search_move_t > * search_focus_move;
            std::atomic_int * tab_suggestion_requested;
            std::atomic_bool * reset_completion;
        };

        /// Input watcher that sets running flag when q is pressed, and changes
        void get_conn_input_watcher(const get_conn_input_watcher_context_t &);
        void inferior_less(const std::string &);
        static std::string color_json(const std::string&);

        void init();

        struct subinfo_ball_t {
            uint64_t total_uploaded { };
            uint64_t total_downloaded { };
            uint64_t quota { };
            uint64_t last_subinfo_pulling_time { };
        };

        using atomic_subinfo_ball_t = std::unique_ptr < ccdb_atomic_t < subinfo_ball_t > >;
        struct subinfo_worker_t
        {
            std::atomic_bool finished { true };
            std::jthread worker;

            void join()
            {
                if (worker.joinable()) worker.join();
            }
        };

#ifndef __WINDOWS_MSVC_BUILD__
        [[nodiscard]] std::string update_subinfo(atomic_subinfo_ball_t &, subinfo_worker_t &) const;
#endif //__WINDOWS_MSVC_BUILD__

        const std::string history_file_loc = utils::getenv("HOME") + "/.cache/ccdb/ccdb_history";

        using CommandVectorType = const std::vector<std::string> &;
        struct session_compliment_data_t
        {
            std::atomic_int * leading_spaces_;
            const std::atomic_int * max_leading_spaces_;
            std::atomic_int * skip_lines_;
            const std::atomic_int * max_skip_lines_;
            std::atomic_int * sort_by_from_watcher;
            bool skip_frame;
        };
        template < typename ContainerType, typename ScopeType >
        using CommandType = tsl::hopscotch_map < std::string, std::function<std::string(const ScopeType &,
            CommandVectorType, const session_compliment_data_t *)>>;
        using CommandAutoCompleteType = tsl::hopscotch_map < std::string, std::vector<std::vector<std::string>>>;
        using SearchMatches = std::vector<std::pair<std::string_view, bool>>;

        struct continuous_table_state_t {
            struct notice_t {
                String text;
                int timeout;
                std::chrono::steady_clock::time_point since;
            };
            bool banner_enabled;
            std::vector<notice_t> notices;
            String displayed_notice;
            String previous_command;
            int previous_cursor = -1;
            std::vector<String> base_args;
            int arg = -1;
            int suggestion = -1;
            std::vector<std::string_view> suggestions;
            std::vector<std::string_view> command_names;
            const String focus_color = "\033[07m";
            const String selected_color = color::color(5,5,0);
            std::map<HashType, String> focused_ids;
            HashType active_id;
            SearchMatches search_matches;
            std::vector<std::string_view> focus_lines;
            explicit continuous_table_state_t(bool enabled) : banner_enabled(enabled) {}
            void show_info(std::string_view, std::string_view, int timeout = -1);
            void clear_completion();
            const std::vector<String> & parse_command(const String &);
            std::string_view notification();
            void set_active_focus(std::string_view);
            void complete_command(String, bool &, const CommandAutoCompleteType &,
                                  std::atomic_int &, std::atomic_int &, ccdb_atomic_t<std::u32string> &);
            static std::pair<int, String> external_banner(const String &);
        };
        // Range callbacks return references/views into callback-owned storage valid
        // until rendering completes. RememberFocus owns any snapshot needed after
        // ReturnContent invalidates its previous range; nullptr requests the initial
        // snapshot. Normal and event banners must not overwrite each other's storage.
        template <typename ContainerType, typename ConstantIteratorType, typename ScopeType,
                  class ContentFn, class BannerFn, class HashFn, class ColorsFn,
                  class TitlesFn, class ValuesFn, class FocusFn, class MatchFn>
        requires (std::is_same_v<ScopeType, std::pair<ConstantIteratorType, ConstantIteratorType>> && Iterator<ConstantIteratorType>)
        void continuous_table(int banner, const std::vector < bool > & do_col_hide,
            const std::vector<int> & alignment,
            const CommandType < ContainerType, ScopeType > & CommandMap, const CommandAutoCompleteType & CommandAutoComplete,
            const ContentFn & ReturnContent,
            const BannerFn & GenerateBanner,
            const HashFn & HashContent,
            const ColorsFn & GenerateOverrideColorInContent,
            const std::function<void(const ContainerType *)> & PressKey_P,
            const std::function<void(const ContainerType *)> & PressKey_K,
            const TitlesFn & GetTitleForCurrentSession,
            const ValuesFn & GetTableValueForCurrentSession,
            const std::function<void(session_compliment_data_t *)> & FrameVisitEach,
            const FocusFn & RememberFocus, const MatchFn & MatchSearch);

        const bool ENABLE_CLEAR_ON_SHRINK = utils::getenv("ENABLE_CLEAR_ON_SHRINK") == "true";

    public:
        ccdb(const std::string & backend, const std::string & token, std::string latency_url_, const std::string&, const std::string&);
        ccdb(const std::string & backend, const std::string & token, std::string latency_url_, const std::vector<std::string> & cmd);

    };

#include "ccdb.print_table.inl"
#include "ccdb.continuous_table.inl"
#ifndef __WINDOWS_MSVC_BUILD__
#include "detach_execute.inl"
#endif //__WINDOWS_MSVC_BUILD__
}

#endif //CCDB_H
