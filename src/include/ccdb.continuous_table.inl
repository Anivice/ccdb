#ifndef CCDB_CONTINUOUS_TABLE_INL
#define CCDB_CONTINUOUS_TABLE_INL

template <typename ContainerType, typename ConstantIteratorType, typename ScopeType> requires (std::is_same_v<
        ScopeType, std::pair<ConstantIteratorType, ConstantIteratorType>> && Iterator<ConstantIteratorType>)
void ccdb::continuous_table(const bool banner, const std::vector<bool>& do_col_hide,
        const std::vector<int>& alignment, const CommandType<ContainerType, ScopeType>& CommandMap,
        const CommandAutoCompleteType & CommandAutoComplete,
        const std::function<ScopeType(session_compliment_data_t*)>& ReturnContent,
        const std::function<String(message_type_t, const ContainerType& current_focus)>& GenerateBanner,
        const std::function<HashType(const ContainerType&)>& HashContent,
        const std::function<OverrideColorType(const ScopeType&, uint64_t)>& GenerateOverrideColorInContent,
        const std::function<void(const ContainerType*)>& PressKey_P,
        const std::function<void(const ContainerType*)>& PressKey_K,
        const std::function<StringScopeType()>& GetTitleForCurrentSession,
        const std::function<PrintTableValScopeType(const ScopeType&)>& GetTableValueForCurrentSession,
        const std::function<void(session_compliment_data_t*)>& FrameVisitEach)
{
    using namespace ::ccdb::utils;
    bool lock_to_right = false;
    bool lock_to_bottom = false;
    std::atomic_int leading_spaces_ = 0;
    std::atomic_int max_leading_spaces_ = get_col_size() / 4;
    std::atomic_int max_skip_lines_ = 0;
    std::atomic_int current_skip_lines_ = 0;
    std::atomic_int mouse_x_ = -1;
    std::atomic_int mouse_y_ = -1;
    std::atomic_bool space_pressed_ = false;
    std::atomic_bool kill_connection_ = false;
    std::atomic_bool focus_to_highlight_ = false;
    std::atomic_bool conn_show_detail_ = false;
    std::atomic_int sort_by_from_watcher_ = -1;
    std::atomic_int tab_suggestion_requested = 0; // 0, no, 1, fill, 2, show possible candidates
    std::atomic_int atm_focus_ = -1;
    std::atomic_bool pause_input_watcher = false;
    std::atomic_int cursor_position = 0;
    std::atomic_bool show_search = false;
    std::atomic < search_move_t > search_focus_move_ = IDLE_STATE;
    std::atomic_bool running = true;
    std::atomic_bool window_size_change = false;
    session_compliment_data_t compliment_data = {
        .leading_spaces_ = &leading_spaces_,
        .max_leading_spaces_ = &max_leading_spaces_,
        .skip_lines_ = &current_skip_lines_,
        .max_skip_lines_ = &max_skip_lines_,
        .sort_by_from_watcher = &sort_by_from_watcher_,
        .skip_frame = false
    };

    const std::string focused_line_color = "\033[07m";
    const std::string selected_line_color = color::color(5,5,0);
    std::map < HashType, std::string > focused_id;
    HashType active_focused_id;
    SearchMatches search_matches;
    int64_t focused_index = -1;
    std::vector < std::pair < String, std::pair < int, std::chrono::time_point<std::chrono::steady_clock> > > > g_title_lines;
    utils::thread_group child_workers;
    ccdb_atomic_t < std::u32string > search_content_buffer;
    String search_content;
    String command_input_prev_cmd;
    int cursor_position_prev = -1;
    std::vector<String> tab_base_args;
    int tab_arg_index = -1;
    const int start_line = banner ? 6 : 5;
    int64_t vector_size_last_time = -1;
    uint64_t frame_index = 0;
    ccdb_atomic_t<frame_data_t> frame_data;
    frame_data.set({});
    print_table_context_t::width_context_t width_context;
    int skip_lines_before = current_skip_lines_;
    auto watcher_ = watcher.make_status_watcher();

    child_workers.emplace_back([&]{display(frame_data, &running);});
    child_workers.emplace_back([&]
    {
        int sig = 0;
        while (sig >= 0 || running)
        {
            if (sig = watcher_.wait(); sig == SIGWINCH)
                window_size_change = true;
        }
    });
    child_workers.emplace_back(&ccdb::get_conn_input_watcher, this,
                               get_conn_input_watcher_context_t{&running, &leading_spaces_, &max_leading_spaces_, &current_skip_lines_, &max_skip_lines_,
                               &mouse_x_, &mouse_y_, &space_pressed_, &kill_connection_, &focus_to_highlight_, &conn_show_detail_, &sort_by_from_watcher_, &atm_focus_,
                               &pause_input_watcher, &show_search, &search_content_buffer, &cursor_position, &search_focus_move_,
                               &tab_suggestion_requested});

    auto show_info = [&](const String & msg, const String & level, int timeout = -1)
    {
        if (!banner) return;
        g_title_lines.emplace_back("[" + level + "]: " + msg,
                                   std::pair { timeout, std::chrono::steady_clock::now() });
    };

    int in_tab_suggestion = -1;
    std::vector < String > tab_suggestions;
    bool on_display = false; // if banner has msg
    ScopeType content;
    ContainerType focused_container;

    const auto unstable_terminal = color::color(0,0,0,5,0,0) + "UNSTABLE TERMINAL" + color::no_color();
    const auto too_small = color::color(0,0,0,5,0,0) + "TOO SMALL" + color::no_color();

    while (running)
    {
        const auto [line_size, col_size] = get_screen_row_col();
        if (const auto window_size_change_ = window_size_change.load();
            line_size <= 7 || window_size_change_)
        {
            if (window_size_change_) {
                window_size_change = false;
            }
            else
            {
                frame_data.set({
                    .frame_index = ++frame_index,
                    .frame = too_small
                });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        compliment_data.skip_frame = false;
        content = ReturnContent(&compliment_data);
        OverrideColorType color_code_overrides;
        const auto leading_spaces = leading_spaces_.load();
        auto current_skip_lines = current_skip_lines_.load();
        String title_line;
        bool skip_due_to_shrink = false;
        int focus_line = -1;
        const auto mouse_y = mouse_y_.load();
        const auto mouse_x = mouse_x_.load();
        const auto space_pressed = space_pressed_.load();
        const auto kill_connection = kill_connection_.load();
        auto focus_to_highlight = focus_to_highlight_.load();
        const auto conn_show_detail = conn_show_detail_.load();
        const auto atm_focus = atm_focus_.load();
        const auto search_focus_move = search_focus_move_.load();
        const auto max_skip_lines = max_skip_lines_.load();
        const auto max_leading_spaces = max_leading_spaces_.load();

        mouse_y_ = -1;
        mouse_x_ = -1;
        space_pressed_ = false;
        kill_connection_ = false;
        focus_to_highlight_ = false;
        conn_show_detail_ = false;
        atm_focus_ = -1;
        search_focus_move_ = IDLE_STATE;
        const auto [title_begin, title_end] = GetTitleForCurrentSession();
        const auto [values_begin, values_end] = GetTableValueForCurrentSession(content);

        const auto contentSize = values_end - values_begin;
        search_matches.reserve(contentSize);
        {
            std::size_t index = 0;
            for (auto it = content.first; it != content.second; ++it, ++index)
            {
                const bool matched = index < contentSize
                                         ? is_highlight_match(*(values_begin + index), search_content)
                                         : false;
                auto hash = HashContent(*it);
                if (index < search_matches.size())
                {
                    auto & entry = search_matches[index];
                    entry.first = std::move(hash);
                    entry.second = matched;
                }
                else
                {
                    search_matches.emplace_back(std::move(hash), matched);
                }
            }
            search_matches.resize(index);
        }

        const int fr = line_size - start_line - 1 /* print_table do not use the last line */; // space without heads
        const int window_frame_size = std::max(0, std::min(static_cast<int>(contentSize), // list size
                                                           fr - (contentSize > static_cast<std::size_t>(std::max(fr, 0)) ? 1 : 0)
                                                           - (current_skip_lines == max_skip_lines ? 1 : 0)));

        auto set_active_focus = [&](HashType next_focused_id)
        {
            if (!active_focused_id.empty() && active_focused_id != next_focused_id)
            {
                if (const auto old_focus = focused_id.find(active_focused_id);
                    old_focus != focused_id.end())
                {
                    if (old_focus->second == focused_line_color) {
                        focused_id.erase(old_focus);
                    }
                    else if (old_focus->second.find(focused_line_color) != std::string::npos) {
                        replace_all(old_focus->second, focused_line_color, "");
                    }
                }
            }

            active_focused_id = std::move(next_focused_id);
            if (const auto it = focused_id.find(active_focused_id);
                it != focused_id.end())
            {
                if (it->second.find(focused_line_color) == std::string::npos) { // no focus
                    it->second = it->second + focused_line_color; // highlight AND focus
                }
            } else {
                focused_id.emplace(active_focused_id, focused_line_color);
            }
        };

        // If highlighted rows are not present anymore, delete them.
        bool active_focus_removed = false;
        for (auto highlight = focused_id.begin(); highlight != focused_id.end();)
        {
            if (!std::any_of(content.first, content.second, [&](const ContainerType & conn) {
                return HashContent(conn) == highlight->first;
            }))
            {
                active_focus_removed = active_focus_removed || highlight->first == active_focused_id;
                highlight = focused_id.erase(highlight);
            }
            else
            {
                ++highlight;
            }
        }
        if (active_focus_removed)
        {
            show_info(GenerateBanner(FOCUSED_ON_NON_PRESENCE, focused_container), "INFO");
            active_focused_id.clear();
        }

        if (search_focus_move != IDLE_STATE)
        {
            if (const auto it = std::ranges::find_if(search_matches,
                                                     [&](const std::pair < std::string, bool > & conn)->bool { return conn.first == active_focused_id; });
                it != search_matches.end())
            {
                switch (search_focus_move)
                {
                case SEARCH_MOVE_UP:
                    {
                        if (it != search_matches.begin())
                        {
                            const auto match = std::find_if(
                                std::make_reverse_iterator(it), search_matches.rend(),
                                [](const auto & conn) { return conn.second; });
                            if (match != search_matches.rend()) set_active_focus(match->first);
                        }

                        focus_to_highlight = true;
                    }
                break;
                case SEARCH_MOVE_DOWN:
                    {
                        const auto next = std::next(it);
                        if (next != search_matches.end())
                        {
                            const auto match = std::find_if(next, search_matches.end(),
                                                            [](const auto & conn) { return conn.second; });
                            if (match != search_matches.end()) set_active_focus(match->first);
                        }

                        focus_to_highlight = true;
                    }
                break;
                default: break;
                }
            }
        }

        if (banner)
        {
            std::string * g_title_line = nullptr;
            while (!g_title_lines.empty())
            {
                const auto now = std::chrono::steady_clock::now();
                const auto & [timeout, time] = g_title_lines.front().second;
                const auto display_time_ms = timeout <= 0
                                                 ? static_cast<int64_t>(3000 / g_title_lines.size())
                                                 : static_cast<int64_t>(timeout);
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - time).count() >= display_time_ms) // transcendental display time
                {
                    g_title_lines.erase(g_title_lines.begin());
                    if (!g_title_lines.empty()) g_title_lines.front().second.second = now;
                    continue; // get next
                }

                g_title_line = &g_title_lines.front().first;
                break; // stop here
            }

            on_display = g_title_line != nullptr;

            if (!g_title_line)
            {
                title_line = GenerateBanner(NORMAL, focused_container);
                if (title_line.empty()) title_line = " ";
            }
            else
            {
                title_line = g_title_line->empty() ? " " : *g_title_line;
            }
        }

        auto move = [&](auto && do_i_process, auto && how_do_i_process)
        {
            if (!active_focused_id.empty())
            {
                bool found = false;
                for (auto it_ = content.first; it_ != content.second; ++it_)
                {
                    if (HashContent(*it_) == active_focused_id)
                    {
                        if (do_i_process(it_, content.first, content.second))
                        {
                            focus_to_highlight = true;
                            set_active_focus(how_do_i_process(it_));
                        }

                        found = true;
                        break;
                    }
                }

                if (!found && focused_index >= 0
                    && focused_index < static_cast<decltype(focused_index)>(contentSize)) {
                    set_active_focus(HashContent(*(content.first + focused_index)));
                    }
            }
        };

        auto highlight_move_down = [&]{
            move([&](auto it_, auto, auto end)->bool {
                     return it_ != (end - 1);
                 },
                 [&](auto it_)->std::string {
                     return HashContent(*(it_ + 1));
                 });
        };

        auto highlight_move_up = [&]{
            move([&](auto it_, auto begin, auto)->bool {
                     return it_ != begin;
                 },
                 [&](auto it_)->std::string {
                     return HashContent(*(it_ - 1));
                 });
        };

        /// move
        switch (atm_focus)
        {
            // move down
        case 1: highlight_move_down();
        break;
            // move up
        case 2: highlight_move_up();
        break;
        default: break;
        }

        /// focus
        {
            auto content_on_cur_page = make_screen_vector_frame(content.first, content.second, contentSize,
                                                                current_skip_lines, line_size, start_line, window_frame_size);
            if ((mouse_x > 1) && (mouse_y > start_line && (mouse_y - start_line) <= window_frame_size))
            {
                // refocus
                auto target = content_on_cur_page.first;
                const auto offset = mouse_y - start_line - 1;
                std::ranges::advance(target, offset, content_on_cur_page.second);
                if (target != content_on_cur_page.second)
                {
                    set_active_focus(HashContent(*target));
                    focused_container = *target;
                    focus_line = mouse_y;
                }
                else
                {
                    show_info(GenerateBanner(FOCUSED_ON_NON_PRESENCE, focused_container), "INFO");
                }
            }
            else if (!active_focused_id.empty())
            {
                // find the focused line on page
                if (int index = 0;
                    std::any_of(content_on_cur_page.first, content_on_cur_page.second, [&](const ContainerType & line)->bool
                    {
                        index++;
                        if (const auto & line_hash = HashContent(line); line_hash == active_focused_id)
                        {
                            focused_container = line;
                            return true;
                        }

                        return false;
                    })
                )
                {
                    focus_line = index + start_line;
                    focused_index = index;
                }
            }

            if (space_pressed && !active_focused_id.empty())
            {
                // focused_id.insert_or_assign(active_focused_id, selected_line_color);
                if (const auto it = focused_id.find(active_focused_id);
                    it != focused_id.end()) // found
                {
                    if (it->second.find(selected_line_color) != std::string::npos) { // already highlighted
                        focused_id.erase(it);
                    } else {
                        focused_id.insert_or_assign(active_focused_id, selected_line_color);
                    }
                }
                highlight_move_down();
                focus_to_highlight = true;
            }

            color_code_overrides = GenerateOverrideColorInContent(content_on_cur_page, current_skip_lines);
        }

        /// refocus
        if ((focus_to_highlight || kill_connection) && !active_focused_id.empty()
            && std::any_of(content.first, content.second, [&](const ContainerType & c_)->bool {
                return HashContent(c_) == active_focused_id;
            }))
        {
            auto can_i_find_in_this_index = [&](const int i)->bool
            {
                auto connections_current_page = make_screen_vector_frame(content.first, content.second, contentSize,
                                                                         i, line_size, start_line, window_frame_size);
                return std::any_of(connections_current_page.first, connections_current_page.second, [&](const ContainerType & conn)->bool {
                    return (HashContent(conn) == active_focused_id);
                });
            };

            // don't refresh window if this already exists
            if (!can_i_find_in_this_index(current_skip_lines))
            {
                for (int i = 0; i <= max_skip_lines; ++i)
                {
                    if (can_i_find_in_this_index(i))
                    {
                        current_skip_lines_ = i;
                        current_skip_lines = i;
                        break;
                    }
                }
            }
        }

        if (kill_connection)
        {
            if (focus_line != -1) {
                show_info(GenerateBanner(KILL, focused_container), "INFO");
                PressKey_K(&focused_container);
            }
        }

        if (conn_show_detail)
        {
            const ContainerType * matched = nullptr;
            if (focus_line != -1)
            {
                (void)std::any_of(content.first, content.second, [&](const ContainerType & conn)->bool
                {
                    if (HashContent(conn) == active_focused_id) {
                        matched = &conn;
                        return true;
                    }

                    return false;
                });
            }

            pause_input_watcher = true;
            frame_data.set({ .pause = true });
            PressKey_P(matched);
            pause_input_watcher = false;
        }

        /// commands and search
        bool command_executed = false;
        if (!search_content_buffer.get().empty())
        {
            /// is a command
            if (auto command_input = utf8::utf32to8(search_content_buffer.get());
                !CommandMap.empty() && !command_input.empty() && command_input.front() == ':')
            {
                command_input.erase(command_input.begin());
                const auto vec = split_via_history(command_input);
                const auto & possible_args = CommandMap | std::views::keys;
                std::vector < std::vector<std::string> > completion_list;
                completion_list.push_back({possible_args.begin(), possible_args.end()});
                if (!vec.empty())
                {
                    if (const auto it = CommandAutoComplete.find(vec.front());
                        it != CommandAutoComplete.end())
                    {
                        completion_list.insert(completion_list.end(), it->second.begin(), it->second.end());
                    }
                }

                auto get_suggestive_content = [&](const String & completion, const int index) -> String
                {
                    std::stringstream ss;
                    for (int i = 0; i < index; ++i)
                        ss << tab_base_args[i] << " ";
                    ss << completion << " ";
                    for (int i = index + 1; i < static_cast<int>(tab_base_args.size()); ++i)
                        ss << tab_base_args[i] << " ";
                    std::string ret = ss.str();
                    if (!ret.empty() && ret.back() == ' ') ret.pop_back();
                    return ret;
                };

                // Keep the original arguments and target index throughout a Tab cycle.
                // A candidate ends in a space, which would otherwise move arg_index
                // to the next argument and append the next candidate there.
                if (const int tab_request = tab_suggestion_requested.exchange(0); tab_request > 0)
                {
                    const int current_cursor = cursor_position.load();
                    const bool new_cycle = tab_arg_index < 0
                        || command_input_prev_cmd != command_input
                        || cursor_position_prev != current_cursor;
                    if (new_cycle)
                    {
                        tab_base_args = vec;
                        // cursor_position includes the leading ':', command_input does not.
                        // Convert the UTF-32 cursor to a byte offset for command_input.
                        const auto command_u32 = search_content_buffer.get();
                        const int cursor_u32 = std::clamp(current_cursor, 1, static_cast<int>(command_u32.size()));
                        const auto cursor_bytes = utf8::utf32to8(command_u32.substr(1, cursor_u32 - 1)).size();
                        tab_arg_index = std::clamp(arg_index(command_input, static_cast<int>(cursor_bytes)),
                                                   0, static_cast<int>(vec.size()));
                        tab_suggestions.clear();
                        in_tab_suggestion = 0;
                        if (static_cast<std::size_t>(tab_arg_index) < completion_list.size())
                        {
                            const auto prefix = static_cast<std::size_t>(tab_arg_index) < vec.size()
                                                    ? vec[tab_arg_index] : String{};
                            tab_suggestions = auto_complete(prefix, completion_list[tab_arg_index]);
                        }
                        command_input_prev_cmd = command_input;
                        cursor_position_prev = current_cursor;
                    }

                    if (tab_suggestions.size() == 1 || (tab_suggestions.size() > 1 && (!new_cycle || tab_request > 1)))
                    {
                        const auto new_cmd = get_suggestive_content(tab_suggestions[in_tab_suggestion], tab_arg_index);
                        search_content_buffer.set(utf8_to_u32(":" + new_cmd));
                        cursor_position = static_cast<int>(search_content_buffer.get().size());
                        command_input_prev_cmd = new_cmd;
                        cursor_position_prev = cursor_position.load();

                        if (tab_suggestions.size() == 1)
                        {
                            // A unique match is complete; the next Tab targets the next argument.
                            tab_suggestions.clear();
                            tab_arg_index = -1;
                        }
                        else
                        {
                            g_title_lines.clear();
                            std::string sug_str;
                            for (auto it = tab_suggestions.begin() + in_tab_suggestion; it != tab_suggestions.end(); ++it)
                            {
                                sug_str += *it;
                                sug_str += ' ';
                            }
                            for (auto it = tab_suggestions.begin(); it != tab_suggestions.begin() + in_tab_suggestion; ++it)
                            {
                                sug_str += *it;
                                sug_str += ' ';
                            }
                            show_info(sprint("(Tab suggestion: ") + sug_str + ")", "INFO", 60000);
                            on_display = false;
                            in_tab_suggestion = (in_tab_suggestion + 1) % static_cast<int>(tab_suggestions.size());
                        }
                    }
                }
            }

            if (auto input_buffer_content = utf8::utf32to8(search_content_buffer.get());
                !input_buffer_content.empty() && input_buffer_content.back() == '\n')
            {
                // remove suggestion display
                const auto first_non_suggestion = std::find_if(g_title_lines.begin(), g_title_lines.end(),
                                                               [](const auto & line) { return line.second.first <= 0; });
                g_title_lines.erase(g_title_lines.begin(), first_non_suggestion);
                input_buffer_content.pop_back(); // pop '\n'
                search_content_buffer.set({});
                tab_suggestions.clear();
                tab_base_args.clear();
                tab_arg_index = -1;
                command_input_prev_cmd.clear();
                cursor_position_prev = -1;
                in_tab_suggestion = -1;
                tab_suggestion_requested = 0;
                frame_data.set({
                    .frame_index = ++frame_index,
                    .clear = true,
                });

                // command block
                if (!input_buffer_content.empty() && input_buffer_content.front() == ':')
                {
                    input_buffer_content.erase(input_buffer_content.begin());
                    if (const auto vec = split_via_history(input_buffer_content); !vec.empty())
                    {
                        if (const auto cmd = CommandMap.find(vec.front());cmd != CommandMap.end())
                        {
                            if (const auto msg = cmd->second(content, vec); !msg.empty()) show_info(msg, "INFO");
                            command_executed = true;
                        } else {
                            show_info(sprint("Unknown command"), "ERROR");
                        }
                    }
                }
                else
                {
                    search_content = input_buffer_content;
                }
            }
        }

        skip_due_to_shrink = (vector_size_last_time >= 0 && static_cast<uint64_t>(vector_size_last_time) > contentSize)
            || skip_lines_before < current_skip_lines || command_executed;
        vector_size_last_time = static_cast<int64_t>(contentSize);
        skip_lines_before = current_skip_lines;

        int message_box_width = 0;
        std::map < int, std::string > focus_lines;
        {
            auto content_on_cur_page = make_screen_vector_frame(content.first, content.second, contentSize,
                                                                current_skip_lines, line_size, start_line, window_frame_size);
            int index = 1;
            for (auto it = content_on_cur_page.first; it != content_on_cur_page.second; ++it, ++index)
            {
                if (const auto highlight = focused_id.find(HashContent(*it)); highlight != focused_id.end())
                {
                    focus_lines.emplace(index + start_line, highlight->second);
                }
            }
        }

        const auto frame_string = print_table(print_table_context_t{
                .table_keys = {title_begin, title_end},
                .table_values = {values_begin, values_end},
                .table_hide = {do_col_hide.begin(), do_col_hide.end()},
                .leading_offset = lock_to_right ? std::numeric_limits<decltype(leading_spaces)>::max() : leading_spaces,
                .max_leading_offset_ptr = &max_leading_spaces_,
                .using_pager = false,
                .additional_info_before_table = title_line,
                .skip_lines = lock_to_bottom ? std::numeric_limits<decltype(current_skip_lines)>::max() : current_skip_lines,
                .max_skip_lines_ptr = &max_skip_lines_,
                .enforce_no_pager = false,
                .color_code_overrides = color_code_overrides,
                .highlight_screen_line = std::move(focus_lines),
                .out = nullptr,
                .show_search = &show_search,
                .search_line_boxContent = &search_content_buffer,
                .cursor_position_in_search_box = &cursor_position,
                .highlight_str = search_content,
                .column_alignment = {alignment.begin(), alignment.end()},
                .line_size = line_size,
                .col_size = col_size,
                .message_box_width_ = &message_box_width,
                .width_context_ = &width_context,
                .locked_info = {
                    .locked_1 = lock_to_bottom ? "(locked)" : "",
                    .locked_2 = lock_to_right ? "(locked)" : "",
                }
            });

        if (max_leading_spaces_ > 0 && leading_spaces_ == max_leading_spaces_) {
            if (!lock_to_right) skip_due_to_shrink = true;
            lock_to_right = true;
        }

        if (max_skip_lines_ > 0 && current_skip_lines_ == max_skip_lines_) {
            if (!lock_to_bottom) skip_due_to_shrink = true;
            lock_to_bottom = true;
        }

        if (const bool i_dont_print = skip_due_to_shrink; !i_dont_print)
        {
            frame_data.set({
                .frame_index = ++frame_index,
                .frame = frame_string,
                .clear = false
            });
        }

        int local_leading_spaces = leading_spaces;
        int local_skip_lines = current_skip_lines;
        const int local_mouse_y = mouse_y;
        const int local_mouse_x = mouse_x;
        const bool local_focus_status = focus_to_highlight;
        const bool local_kill_status = kill_connection;
        const bool local_show_detail = conn_show_detail;
        const int local_sort_by_from_watcher = sort_by_from_watcher_;
        const int local_cursor_position = cursor_position;
        const auto local_str_len = search_content_buffer.get().size();
        const bool local_show_search = show_search;
        const int local_search_focus_move = search_focus_move;
        const int local_atm_focus = atm_focus;
        const int local_tab_suggestion = tab_suggestion_requested;
        const bool local_space_pressed = space_pressed_.load();
        if (lock_to_right) leading_spaces_ = max_leading_spaces_.load();
        if (lock_to_bottom) current_skip_lines_ = max_skip_lines_.load();
        FrameVisitEach(&compliment_data);

        for (int i = 0; i < screen_refresh_interval_in_ms / 10; i++)
        {
            if (compliment_data.skip_frame
                || local_leading_spaces != leading_spaces_
                || local_skip_lines != current_skip_lines_
                || local_mouse_y != mouse_y_
                || local_mouse_x != mouse_x_
                || window_size_change
                || local_focus_status != focus_to_highlight_
                || local_kill_status != kill_connection_
                || local_show_detail != conn_show_detail_
                || local_sort_by_from_watcher != sort_by_from_watcher_
                || local_cursor_position != cursor_position
                || local_str_len != search_content_buffer.get().size()
                || local_show_search != show_search
                || local_search_focus_move != search_focus_move_
                || local_atm_focus != atm_focus_
                || !running
                || skip_due_to_shrink
                || local_space_pressed != space_pressed_
                || local_tab_suggestion != tab_suggestion_requested
                || (!on_display && !g_title_lines.empty()) // not on display, and has notifications
            )
            {
                if (window_size_change || (ENABLE_CLEAR_ON_SHRINK && skip_due_to_shrink
                        && contentSize <= static_cast<std::size_t>(window_frame_size)))
                {
                    frame_data.set({
                        .frame_index = ++frame_index,
                        .clear = true,
                    });
                    window_size_change = false;
                }

                if (mouse_y_ == line_size - 1 && mouse_x_ > 1) {
                    const int ll_max_leading_spaces_ = max_leading_spaces_;
                    leading_spaces_ = static_cast<int>(std::round(static_cast<double>(mouse_x_ - message_box_width) /
                        static_cast<double>(col_size - message_box_width) * ll_max_leading_spaces_));
                    if (leading_spaces_ > ll_max_leading_spaces_) leading_spaces_ = ll_max_leading_spaces_;
                    if (leading_spaces_ < 0) leading_spaces_ = 0;
                    mouse_y_ = -1;
                }

                if (mouse_x_ == 1) {
                    const int ll_max_skip_lines = max_skip_lines_;
                    current_skip_lines_ = mouse_y_ == 1 ? 0 :
                        static_cast<int>(std::round(static_cast<double>(mouse_y_) / static_cast<double>(line_size - 1) * ll_max_skip_lines));
                    if (current_skip_lines_ > ll_max_skip_lines) current_skip_lines_ = ll_max_skip_lines;
                    if (current_skip_lines_ < 0) current_skip_lines_ = 0;
                    mouse_x_ = -1;
                }

                if (leading_spaces_ != local_leading_spaces && leading_spaces_ < max_leading_spaces_) {
                    lock_to_right = false;
                }

                if (current_skip_lines_ != local_skip_lines && current_skip_lines_ < max_skip_lines_) {
                    lock_to_bottom = false;
                }

                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10l));
        }

        if (leading_spaces_ > max_leading_spaces_) {
            leading_spaces_ = max_leading_spaces_.load();
        }

        if (current_skip_lines_ > max_skip_lines_) {
            current_skip_lines_ = max_skip_lines_.load();
        }
    }

    running = false;
    watcher_.stop();
    print("\n\n", "Wait...\n", "Press Ctrl+C (^C) to end immediately.\n");
    child_workers.join_all();
    // if (const char* clear = capstr("clear")) {
        // std::cout.write(clear, static_cast<std::streamsize>(strlen(clear)));
        // std::cout.flush();
    // }
}

#endif
