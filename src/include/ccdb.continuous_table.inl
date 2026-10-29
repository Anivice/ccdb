#ifndef CCDB_CONTINUOUS_TABLE_INL
#define CCDB_CONTINUOUS_TABLE_INL

template <typename ContainerType, typename ConstantIteratorType, typename ScopeType>
requires (std::is_same_v<ScopeType, std::pair<ConstantIteratorType, ConstantIteratorType>> && Iterator<ConstantIteratorType>)
void ccdb::continuous_table(const int banner, const std::vector<bool>& do_col_hide,
        const std::vector<int>& alignment, const CommandType<ContainerType, ScopeType>& CommandMap,
        const CommandAutoCompleteType & CommandAutoComplete,
        const std::function<ScopeType(session_compliment_data_t*)>& ReturnContent,
        const std::function<void(message_type_t, const ContainerType&, std::vector<std::string> &)>& GenerateBanner,
        const std::function<HashType(const ContainerType&)>& HashContent,
        const std::function<OverrideColorType(const ScopeType&, uint64_t)>& GenerateOverrideColorInContent,
        const std::function<void(const ContainerType*)>& PressKey_P,
        const std::function<void(const ContainerType*)>& PressKey_K,
        const std::function<StringScopeType()>& GetTitleForCurrentSession,
        const std::function<PrintTableValScopeType(const ScopeType&)>& GetTableValueForCurrentSession,
        const std::function<void(session_compliment_data_t*)>& FrameVisitEach) {
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
    std::atomic_bool reset_completion = false;
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

    auto get_frame = [&]->std::pair<int, std::string>
    {
#ifndef __WINDOWS_MSVC_BUILD__
        if (banner_command.empty()) return {0, ""};
        if (const auto & [fs_stdout, fd_stderr, status] =
            exec_command2("/bin/sh", "", "-c", banner_command); !status)
        {
            try {
                if (const auto json = json::parse(fs_stdout); json.contains("banner_lines")) {
                    return { json["banner_lines"].get<int>(), json["banner"].get<std::string>() };
                }
            } catch (...) { }
        }

#endif //__WINDOWS_MSVC_BUILD__
        return {0, ""};
    };

    int start_line = 5 + banner;
    if (!banner_command.empty())
        start_line += get_frame().first;

    const std::string focused_line_color = "\033[07m";
    const std::string selected_line_color = color::color(5,5,0);
    std::map < HashType, std::string > focused_id;
    HashType active_focused_id;
    SearchMatches search_matches;
    int64_t focused_index = -1;
    std::vector < std::pair < String, std::pair < int, std::chrono::time_point<std::chrono::steady_clock> > > > g_title_lines;
    thread_group child_workers;
    ccdb_atomic_t < std::u32string > search_content_buffer;
    String search_content;
    String command_input_prev_cmd;
    int cursor_position_prev = -1;
    std::vector<String> tab_base_args;
    int tab_arg_index = -1;
    int64_t vector_size_last_time = -1;
    uint64_t frame_index = 0;
    NotificationType < frame_data_t > frame_data;
    print_table_context_t::width_cache_t width_cache;
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
                               &tab_suggestion_requested, &reset_completion});

    auto show_info = [&](const String & msg, const String & level, int timeout = -1)
    {
        if (!banner) return;
        g_title_lines.emplace_back("[" + level + "]: " + msg,
                                   std::pair { timeout, std::chrono::steady_clock::now() });
    };

    int in_tab_suggestion = -1;
    std::vector < String > tab_suggestions;
    auto clear_completion = [&]
    {
        const auto first_non_suggestion = std::find_if(g_title_lines.begin(), g_title_lines.end(),
            [](const auto & line) { return line.second.first <= 0; });
        g_title_lines.erase(g_title_lines.begin(), first_non_suggestion);
        tab_suggestions.clear();
        tab_base_args.clear();
        tab_arg_index = -1;
        command_input_prev_cmd.clear();
        cursor_position_prev = -1;
        in_tab_suggestion = -1;
    };
    ContainerType focused_container;

    const auto show_banner = [&](const message_type_t type)
    {
        std::vector<String> lines;
        GenerateBanner(type, focused_container, lines);
        if (!lines.empty()) show_info(lines.front(), "INFO");
    };

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
        auto & color = focused_id[active_focused_id];
        if (color.find(focused_line_color) == std::string::npos)
            color += focused_line_color;
    };

    const auto complete_command = [&](String command_input, bool & on_display)
    {
        const int tab_request = tab_suggestion_requested.exchange(0);
        if (tab_request <= 0) return;
        command_input.erase(command_input.begin()); // leading ':'
        const int current_cursor = cursor_position.load();
        const bool new_cycle = tab_arg_index < 0
            || command_input_prev_cmd != command_input
            || cursor_position_prev != current_cursor;
        if (new_cycle)
        {
            tab_base_args = split_via_history(command_input);
            // Convert the UTF-32 cursor (including ':') to a command byte offset.
            const auto command_u32 = search_content_buffer.get();
            const int cursor_u32 = std::clamp(current_cursor, 1, static_cast<int>(command_u32.size()));
            const auto cursor_bytes = utf8::utf32to8(command_u32.substr(1, cursor_u32 - 1)).size();
            tab_arg_index = std::clamp(arg_index(command_input, static_cast<int>(cursor_bytes)),
                                       0, static_cast<int>(tab_base_args.size()));
            const auto prefix = static_cast<std::size_t>(tab_arg_index) < tab_base_args.size()
                                    ? tab_base_args[tab_arg_index] : String{};
            tab_suggestions.clear();
            in_tab_suggestion = 0;
            if (tab_arg_index == 0)
            {
                const auto possible_args = CommandMap | std::views::keys;
                tab_suggestions = auto_complete(prefix, {possible_args.begin(), possible_args.end()});
            }
            else if (const auto it = CommandAutoComplete.find(tab_base_args.front());
                     it != CommandAutoComplete.end() && static_cast<std::size_t>(tab_arg_index) <= it->second.size())
            {
                tab_suggestions = auto_complete(prefix, it->second[tab_arg_index - 1]);
            }
            command_input_prev_cmd = command_input;
            cursor_position_prev = current_cursor;
        }

        if (tab_suggestions.empty() || (tab_suggestions.size() > 1 && new_cycle && tab_request == 1))
            return;

        // Keep the original arguments and target index throughout a Tab cycle.
        String new_cmd;
        const int argument_count = std::max(static_cast<int>(tab_base_args.size()), tab_arg_index + 1);
        for (int i = 0; i < argument_count; ++i)
        {
            if (i > 0) new_cmd += ' ';
            new_cmd += i == tab_arg_index ? tab_suggestions[in_tab_suggestion] : tab_base_args[i];
        }
        search_content_buffer.set(utf8_to_u32(":" + new_cmd));
        cursor_position = static_cast<int>(search_content_buffer.get().size());
        command_input_prev_cmd = new_cmd;
        cursor_position_prev = cursor_position.load();

        if (tab_suggestions.size() == 1)
        {
            tab_suggestions.clear();
            tab_arg_index = -1;
            return;
        }

        g_title_lines.clear();
        String suggestions;
        for (std::size_t i = 0; i < tab_suggestions.size(); ++i)
            suggestions += tab_suggestions[(in_tab_suggestion + i) % tab_suggestions.size()] + ' ';
        show_info(sprint("(Tab suggestion: ") + suggestions + ")", "INFO", 60000);
        on_display = false;
        in_tab_suggestion = (in_tab_suggestion + 1) % static_cast<int>(tab_suggestions.size());
    };

    const auto too_small = color::color(0,0,0,5,0,0) + "TOO SMALL" + color::no_color();

    while (running) {
        try
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
                    frame_data.push({
                        .frame_index = ++frame_index,
                        .frame = too_small
                    });
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            compliment_data.skip_frame = false;
            const auto content = ReturnContent(&compliment_data);
            OverrideColorType color_code_overrides;
            const auto leading_spaces = leading_spaces_.load();
            auto current_skip_lines = current_skip_lines_.load();
            std::vector<String> title_line;
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
                        search_matches[index] = {std::move(hash), matched};
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

            const auto find_row = [&](const auto & rows, const HashType & id)
            {
                return std::find_if(rows.first, rows.second, [&](const ContainerType & row) {
                    return HashContent(row) == id;
                });
            };
            const auto page_at = [&](const int offset)
            {
                return make_screen_vector_frame(content.first, content.second, contentSize,
                                                offset, line_size, start_line, window_frame_size);
            };

            // If highlighted rows are not present anymore, delete them.
            bool active_focus_removed = false;
            for (auto highlight = focused_id.begin(); highlight != focused_id.end();)
            {
                if (find_row(content, highlight->first) == content.second)
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
                show_banner(FOCUSED_ON_NON_PRESENCE);
                active_focused_id.clear();
            }

            if (search_focus_move != IDLE_STATE)
            {
                const auto it = std::ranges::find(search_matches, active_focused_id, &SearchMatches::value_type::first);
                if (it != search_matches.end())
                {
                    const auto focus_match = [&](const auto begin, const auto end)
                    {
                        const auto match = std::find_if(begin, end, [](const auto & row) { return row.second; });
                        if (match != end) set_active_focus(match->first);
                        focus_to_highlight = true;
                    };
                    switch (search_focus_move)
                    {
                        case SEARCH_MOVE_UP:
                            focus_match(std::make_reverse_iterator(it), search_matches.rend());
                            break;
                        case SEARCH_MOVE_DOWN:
                            focus_match(std::next(it), search_matches.end());
                            break;
                        default: break;
                    }
                }
            }

            bool on_display = false;
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

                GenerateBanner(NORMAL, focused_container, title_line);
                if (title_line.size() != banner)
                    throw std::logic_error("Expectation disappointed.");
                if (g_title_line && !g_title_line->empty()) {
                    title_line.front() = *g_title_line;
                }
                const auto [banner_lines, banner_content] = get_frame();
                std::istringstream ss(banner_content);
                std::string line;
                std::vector<std::string> ext_lines;
                while (std::getline(ss, line)) {
                    ext_lines.push_back(line);
                }
                title_line.insert(title_line.begin(), ext_lines.begin(), ext_lines.end());
            }

            const auto move_focus = [&](const int direction)
            {
                if (active_focused_id.empty()) return;
                const auto it = find_row(content, active_focused_id);
                if (it == content.second)
                {
                    if (focused_index >= 0 && focused_index < static_cast<decltype(focused_index)>(contentSize))
                        set_active_focus(HashContent(*(content.first + focused_index)));
                }
                else if (direction > 0 ? it != content.second - 1 : it != content.first)
                {
                    focus_to_highlight = true;
                    set_active_focus(HashContent(*(it + direction)));
                }
            };

            switch (atm_focus)
            {
                case 1: move_focus(1); break;
                case 2: move_focus(-1); break;
                default: break;
            }

            /// focus
            {
                const auto content_on_cur_page = page_at(current_skip_lines);
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
                        show_banner(FOCUSED_ON_NON_PRESENCE);
                    }
                }
                else if (!active_focused_id.empty())
                {
                    const auto it = find_row(content_on_cur_page, active_focused_id);
                    if (it != content_on_cur_page.second)
                    {
                        focused_container = *it;
                        focused_index = std::distance(content_on_cur_page.first, it) + 1;
                        focus_line = focused_index + start_line;
                    }
                }

                if (space_pressed && !active_focused_id.empty())
                {
                    if (const auto it = focused_id.find(active_focused_id);
                        it != focused_id.end()) // found
                    {
                        if (it->second.find(selected_line_color) != std::string::npos) { // already highlighted
                            focused_id.erase(it);
                        } else {
                            focused_id.insert_or_assign(active_focused_id, selected_line_color);
                        }
                    }
                    move_focus(1);
                    focus_to_highlight = true;
                }

                color_code_overrides = GenerateOverrideColorInContent(content_on_cur_page, current_skip_lines);
            }

            /// refocus
            if ((focus_to_highlight || kill_connection) && !active_focused_id.empty()
                && find_row(content, active_focused_id) != content.second)
            {
                auto focus_on_page = [&](const int i)->bool
                {
                    const auto page = page_at(i);
                    return find_row(page, active_focused_id) != page.second;
                };

                // don't refresh window if this already exists
                if (!focus_on_page(current_skip_lines))
                {
                    for (int i = 0; i <= max_skip_lines; ++i)
                    {
                        if (focus_on_page(i))
                        {
                            current_skip_lines_ = i;
                            current_skip_lines = i;
                            lock_to_bottom = false; // removed lock
                            break;
                        }
                    }
                }
            }

            if (kill_connection && focus_line != -1)
            {
                show_banner(KILL);
                PressKey_K(&focused_container);
            }

            if (conn_show_detail)
            {
                const auto it = focus_line != -1 ? find_row(content, active_focused_id) : content.second;
                const ContainerType * matched = it != content.second ? &*it : nullptr;
                pause_input_watcher = true;
                frame_data.push({ .pause = true });
                PressKey_P(matched);
                pause_input_watcher = false;
            }

            /// commands and search
            bool command_executed = false;
            if (reset_completion.exchange(false)) clear_completion();
            if (!search_content_buffer.get().empty())
            {
                /// is a command
                if (auto command_input = utf8::utf32to8(search_content_buffer.get());
                    !CommandMap.empty() && !command_input.empty() && command_input.front() == ':')
                {
                    complete_command(std::move(command_input), on_display);
                }

                if (auto input_buffer_content = utf8::utf32to8(search_content_buffer.get());
                    !input_buffer_content.empty() && input_buffer_content.back() == '\n')
                {
                    // remove suggestion display
                    clear_completion();
                    input_buffer_content.pop_back(); // pop '\n'
                    search_content_buffer.set({});
                    tab_suggestion_requested = 0;
                    frame_data.push({
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
                                if (const auto msg = cmd->second(content, vec, &compliment_data); !msg.empty())
                                    show_info(msg, "INFO");
                                command_executed = true;
                            } else {
                                show_info(sprint("Unknown command"), "ERROR");
                            }
                        }
                    }
                    else
                    {
                        if (!try_update_search_pattern(search_content, input_buffer_content)) {
                            show_info(sprint("Regex evaluation failed"), "ERROR");
                        }
                    }
                }
            }

            bool skip_due_to_shrink = (vector_size_last_time >= 0 && static_cast<uint64_t>(vector_size_last_time) > contentSize)
                || skip_lines_before < current_skip_lines || command_executed;
            vector_size_last_time = static_cast<int64_t>(contentSize);
            skip_lines_before = current_skip_lines;

            int message_box_width = 0;
            std::map < int, std::string > focus_lines;
            {
                const auto content_on_cur_page = page_at(current_skip_lines);
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
                    .column_titles = {title_begin, title_end},
                    .rows = {values_begin, values_end},
                    .hidden_columns = {do_col_hide.begin(), do_col_hide.end()},
                    .horizontal_offset = lock_to_right ? std::numeric_limits<decltype(leading_spaces)>::max() : leading_spaces,
                    .max_horizontal_offset = &max_leading_spaces_,
                    .output_mode = print_table_context_t::output_mode_t::frame,
                    .banner_lines = title_line,
                    .row_offset = lock_to_bottom ? std::numeric_limits<decltype(current_skip_lines)>::max() : current_skip_lines,
                    .max_row_offset = &max_skip_lines_,
                    .row_colors = color_code_overrides,
                    .screen_line_colors = std::move(focus_lines),
                    .output_stream = nullptr,
                    .show_search = &show_search,
                    .search_content = &search_content_buffer,
                    .search_cursor = &cursor_position,
                    .search_pattern = search_content,
                    .column_alignment = {alignment.begin(), alignment.end()},
                    .terminal_height = line_size,
                    .terminal_width = col_size,
                    .status_width = &message_box_width,
                    .width_cache = &width_cache,
                    .scroll_lock_labels = {
                        .vertical = lock_to_bottom ? "(locked)" : "",
                        .horizontal = lock_to_right ? "(locked)" : "",
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

            if (!skip_due_to_shrink)
            {
                frame_data.push({
                    .frame_index = ++frame_index,
                    .frame = frame_string,
                    .clear = false
                });
            }

            const int local_sort_by_from_watcher = sort_by_from_watcher_;
            const int local_cursor_position = cursor_position;
            const auto local_str_len = search_content_buffer.get().size();
            const bool local_show_search = show_search;
            const int local_tab_suggestion = tab_suggestion_requested;
            const bool local_space_pressed = space_pressed_.load();
            if (lock_to_right) leading_spaces_ = max_leading_spaces_.load();
            if (lock_to_bottom) current_skip_lines_ = max_skip_lines_.load();
            FrameVisitEach(&compliment_data);

            for (int i = 0; i < screen_refresh_interval_in_ms / 10; i++)
            {
                if (compliment_data.skip_frame
                    || leading_spaces != leading_spaces_
                    || current_skip_lines != current_skip_lines_
                    || mouse_y != mouse_y_
                    || mouse_x != mouse_x_
                    || window_size_change
                    || focus_to_highlight != focus_to_highlight_
                    || kill_connection != kill_connection_
                    || conn_show_detail != conn_show_detail_
                    || local_sort_by_from_watcher != sort_by_from_watcher_
                    || local_cursor_position != cursor_position
                    || local_str_len != search_content_buffer.get().size()
                    || local_show_search != show_search
                    || search_focus_move != search_focus_move_
                    || atm_focus != atm_focus_
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
                        frame_data.push({
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
                        current_skip_lines_ = static_cast<int>(std::round(static_cast<double>(mouse_y_ - 1) / static_cast<double>(line_size - 2) * ll_max_skip_lines));
                        if (current_skip_lines_ > ll_max_skip_lines) current_skip_lines_ = ll_max_skip_lines;
                        if (current_skip_lines_ < 0) current_skip_lines_ = 0;
                        mouse_x_ = -1;
                    }

                    if (leading_spaces_ != leading_spaces && leading_spaces_ < max_leading_spaces_) {
                        lock_to_right = false;
                    }

                    if (current_skip_lines_ != current_skip_lines && current_skip_lines_ < max_skip_lines_) {
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
        } catch (std::exception & e) {
            print<is_error>(e.what(), "\n");
            break;
        }
    }

    running = false;
    watcher_.stop();
    frame_data.push({.frame_index = ++frame_index, .pause = true});
    print("\n\n", "Wait...\n", "Press Ctrl+C (^C) to end immediately.\n");
    child_workers.join_all();
}

#endif
