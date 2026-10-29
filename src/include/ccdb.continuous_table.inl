#ifndef CCDB_CONTINUOUS_TABLE_INL
#define CCDB_CONTINUOUS_TABLE_INL

template <typename ContainerType, typename ConstantIteratorType, typename ScopeType,
          class ContentFn, class BannerFn, class HashFn, class ColorsFn,
          class TitlesFn, class ValuesFn, class FocusFn, class MatchFn>
requires (std::is_same_v<ScopeType, std::pair<ConstantIteratorType, ConstantIteratorType>> && Iterator<ConstantIteratorType>)
void ccdb::continuous_table(const int banner, const std::vector<bool>& do_col_hide,
        const std::vector<int>& alignment, const CommandType<ContainerType, ScopeType>& CommandMap,
        const CommandAutoCompleteType & CommandAutoComplete,
        const ContentFn & ReturnContent,
        const BannerFn & GenerateBanner,
        const HashFn & HashContent,
        const ColorsFn & GenerateOverrideColorInContent,
        const std::function<void(const ContainerType*)>& PressKey_P,
        const std::function<void(const ContainerType*)>& PressKey_K,
        const TitlesFn & GetTitleForCurrentSession,
        const ValuesFn & GetTableValueForCurrentSession,
        const std::function<void(session_compliment_data_t*)>& FrameVisitEach,
        const FocusFn & RememberFocus, const MatchFn & MatchSearch)
{
    using namespace ::ccdb::utils;
    static_assert(std::is_lvalue_reference_v<std::invoke_result_t<FocusFn, const ContainerType *>>);
    static_assert(std::is_lvalue_reference_v<std::invoke_result_t<ColorsFn, const ScopeType &, uint64_t>>);
    static_assert(std::is_lvalue_reference_v<std::invoke_result_t<HashFn, const ContainerType &>> ||
                  std::is_same_v<std::invoke_result_t<HashFn, const ContainerType &>, std::string_view>,
                  "HashContent must borrow a stable row ID");
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

    continuous_table_state_t state(banner != 0);
    for (const auto & name : CommandMap | std::views::keys) state.command_names.emplace_back(name);
    const auto get_frame = [&] { return continuous_table_state_t::external_banner(banner_command); };
    int start_line = 5 + banner;
    if (!banner_command.empty())
        start_line += get_frame().first;

    auto & focused_id = state.focused_ids;
    auto & active_focused_id = state.active_id;
    auto & search_matches = state.search_matches;
    const auto & selected_line_color = state.selected_color;
    int64_t focused_index = -1;
    thread_group child_workers;
    ccdb_atomic_t < std::u32string > search_content_buffer;
    String search_content;
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

    const auto show_info = [&](std::string_view msg, std::string_view level, int timeout = -1) {
        state.show_info(msg, level, timeout);
    };
    const auto set_active_focus = [&](std::string_view id) { state.set_active_focus(id); };
    const auto * focused_container = &RememberFocus(nullptr);
    const auto show_banner = [&](const message_type_t type) {
        auto && lines = GenerateBanner(type, *focused_container);
        auto range = table_detail::range(lines);
        if (!range.empty()) show_info(table_detail::text(*range.begin()), "INFO");
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
            const OverrideColorType * color_code_overrides = nullptr;
            const auto leading_spaces = leading_spaces_.load();
            auto current_skip_lines = current_skip_lines_.load();
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
            auto && titles = GetTitleForCurrentSession();
            auto && values = GetTableValueForCurrentSession(content);
            auto value_range = table_detail::range(values);
            const auto values_begin = value_range.begin();
            const auto values_end = value_range.end();
            const auto contentSize = std::ranges::distance(value_range);
            search_matches.reserve(contentSize);
            {
                std::size_t index = 0;
                auto value = values_begin;
                for (auto it = content.first; it != content.second; ++it, ++index)
                {
                    const bool matched = value != values_end && MatchSearch(*value, search_content);
                    if (value != values_end) ++value;
                    const std::string_view hash = HashContent(*it);
                    if (index < search_matches.size())
                    {
                        search_matches[index] = {hash, matched};
                    }
                    else
                    {
                        search_matches.emplace_back(hash, matched);
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

            const auto notification = state.notification();
            bool on_display = !state.notices.empty();
            auto && title_line = GenerateBanner(NORMAL, *focused_container);
            if (std::ranges::distance(table_detail::range(title_line)) != banner)
                throw std::logic_error("Expectation disappointed.");
            const auto [banner_lines, banner_content] = get_frame();

            const auto move_focus = [&](const int direction)
            {
                if (active_focused_id.empty()) return;
                const auto it = find_row(content, active_focused_id);
                if (it == content.second)
                {
                    if (focused_index >= 0 && focused_index < static_cast<decltype(focused_index)>(contentSize))
                        set_active_focus(HashContent(*std::ranges::next(content.first, focused_index, content.second)));
                }
                else if (direction > 0 ? std::next(it) != content.second : it != content.first)
                {
                    focus_to_highlight = true;
                    const auto next = direction > 0 ? std::next(it)
                        : std::ranges::next(content.first, std::distance(content.first, it) - 1);
                    set_active_focus(HashContent(*next));
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
                        focused_container = &RememberFocus(&*target);
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
                        focused_container = &RememberFocus(&*it);
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

                color_code_overrides = &GenerateOverrideColorInContent(content_on_cur_page, current_skip_lines);
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
                PressKey_K(focused_container);
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
            if (reset_completion.exchange(false)) state.clear_completion();
            if (!search_content_buffer.get().empty())
            {
                /// is a command
                if (auto command_input = utf8::utf32to8(search_content_buffer.get());
                    !CommandMap.empty() && !command_input.empty() && command_input.front() == ':')
                {
                    state.complete_command(std::move(command_input), on_display, CommandAutoComplete,
                                           tab_suggestion_requested, cursor_position, search_content_buffer);
                }

                if (auto input_buffer_content = utf8::utf32to8(search_content_buffer.get());
                    !input_buffer_content.empty() && input_buffer_content.back() == '\n')
                {
                    // remove suggestion display
                    state.clear_completion();
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
                        if (const auto & vec = state.parse_command(input_buffer_content); !vec.empty())
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
            auto & focus_lines = state.focus_lines;
            focus_lines.assign(static_cast<std::size_t>(std::max(line_size, start_line + window_frame_size + 1)), {});
            {
                const auto content_on_cur_page = page_at(current_skip_lines);
                int index = 1;
                for (auto it = content_on_cur_page.first; it != content_on_cur_page.second; ++it, ++index)
                {
                    if (const auto highlight = focused_id.find(HashContent(*it)); highlight != focused_id.end())
                    {
                        focus_lines[index + start_line] = highlight->second;
                    }
                }
            }

            auto frame_string = print_table(titles, values, print_table_context_t{
                    .hidden_columns = {do_col_hide.begin(), do_col_hide.end()},
                    .horizontal_offset = lock_to_right ? std::numeric_limits<decltype(leading_spaces)>::max() : leading_spaces,
                    .max_horizontal_offset = &max_leading_spaces_,
                    .output_mode = print_table_context_t::output_mode_t::frame,
                    .external_banner = banner_content,
                    .first_banner_override = notification,
                    .row_offset = lock_to_bottom ? std::numeric_limits<decltype(current_skip_lines)>::max() : current_skip_lines,
                    .max_row_offset = &max_skip_lines_,
                    .row_colors = color_code_overrides,
                    .screen_line_colors = focus_lines,
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
                }, title_line);

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
                    .frame = std::move(frame_string),
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
                    || (!on_display && !state.notices.empty()) // not on display, and has notifications
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
