// ccdb.continuous_table.cpp
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

#include <algorithm>
#include "ccdb.h"

int ccdb::ccdb::arg_index(const std::string &buffer, const int cursor_position)
{
    const std::size_t cursor = static_cast<std::size_t>(
        std::clamp(cursor_position, 0, static_cast<int>(buffer.size()))
    );

    int arg = 0;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;
    bool in_argument = false;

    for (std::size_t i = 0; i < cursor; ++i)
    {
        const auto uc = static_cast<unsigned char>(buffer[i]);
        const char c = static_cast<char>(uc);

        if (escaped)
        {
            escaped = false;
            in_argument = true;
            continue;
        }

        if (c == '\\' && !in_single_quote)
        {
            escaped = true;
            in_argument = true;
            continue;
        }

        if (c == '\'' && !in_double_quote)
        {
            in_single_quote = !in_single_quote;
            in_argument = true;
            continue;
        }

        if (c == '"' && !in_single_quote)
        {
            in_double_quote = !in_double_quote;
            in_argument = true;
            continue;
        }

        if (!in_single_quote &&
            !in_double_quote &&
            std::isspace(uc))
        {
            if (in_argument)
            {
                ++arg;
                in_argument = false;
            }

            continue;
        }

        in_argument = true;
    }

    return arg;
}

using namespace ccdb::utils;

void ccdb::ccdb::continuous_table_state_t::show_info(std::string_view msg, std::string_view level, int timeout)
{
    if (!banner_enabled) return;
    String text;
    text.reserve(level.size() + msg.size() + 4);
    text += '[';
    text += level;
    text += "]: ";
    text += msg;
    notices.push_back({std::move(text), timeout, std::chrono::steady_clock::now()});
}

void ccdb::ccdb::continuous_table_state_t::clear_completion()
{
    const auto first_non_suggestion = std::find_if(notices.begin(), notices.end(),
        [](const auto & line) { return line.timeout <= 0; });
    notices.erase(notices.begin(), first_non_suggestion);
    suggestions.clear();
    base_args.clear();
    arg = -1;
    previous_command.clear();
    previous_cursor = -1;
    suggestion = -1;
}

const std::vector<std::string> & ccdb::ccdb::continuous_table_state_t::parse_command(const String & command)
{
    base_args = split_via_history(command);
    return base_args;
}

void ccdb::ccdb::continuous_table_state_t::set_active_focus(std::string_view next_focused_id)
{
    if (!active_id.empty() && active_id != next_focused_id)
    {
        if (const auto old_focus = focused_ids.find(active_id);
            old_focus != focused_ids.end())
        {
            if (old_focus->second == focus_color) {
                focused_ids.erase(old_focus);
            }
            else if (old_focus->second.find(focus_color) != std::string::npos) {
                replace_all(old_focus->second, focus_color, "");
            }
        }
    }

    active_id = next_focused_id;
    auto & color = focused_ids[active_id];
    if (color.find(focus_color) == std::string::npos)
        color += focus_color;
}

void ccdb::ccdb::continuous_table_state_t::complete_command(String command_input, bool & on_display,
    const CommandAutoCompleteType & CommandAutoComplete, std::atomic_int & tab_suggestion_requested,
    std::atomic_int & cursor_position, ccdb_atomic_t<std::u32string> & search_content_buffer)
{
    const int tab_request = tab_suggestion_requested.exchange(0);
    if (tab_request <= 0) return;
    command_input.erase(command_input.begin()); // leading ':'
    const int current_cursor = cursor_position.load();
    const bool new_cycle = arg < 0
        || previous_command != command_input
        || previous_cursor != current_cursor;
    if (new_cycle)
    {
        base_args = split_via_history(command_input);
        // Convert the UTF-32 cursor (including ':') to a command byte offset.
        const auto command_u32 = search_content_buffer.get();
        const int cursor_u32 = std::clamp(current_cursor, 1, static_cast<int>(command_u32.size()));
        const auto cursor_bytes = utf8::utf32to8(command_u32.substr(1, cursor_u32 - 1)).size();
        arg = std::clamp(arg_index(command_input, static_cast<int>(cursor_bytes)),
                         0, static_cast<int>(base_args.size()));
        const std::string_view prefix = static_cast<std::size_t>(arg) < base_args.size()
                                ? std::string_view(base_args[arg]) : std::string_view{};
        suggestions.clear();
        suggestion = 0;
        const auto collect = [&](const auto & candidates) {
            for (const auto & candidate : candidates)
                if (std::string_view(candidate).starts_with(prefix)) suggestions.emplace_back(candidate);
        };
        if (arg == 0) collect(command_names);
        else if (const auto it = CommandAutoComplete.find(base_args.front());
                 it != CommandAutoComplete.end() && static_cast<std::size_t>(arg) <= it->second.size())
            collect(it->second[arg - 1]);
        previous_command = command_input;
        previous_cursor = current_cursor;
    }

    if (suggestions.empty() || (suggestions.size() > 1 && new_cycle && tab_request == 1))
        return;

    // Keep the original arguments and target index throughout a Tab cycle.
    String new_cmd;
    const int argument_count = std::max(static_cast<int>(base_args.size()), arg + 1);
    for (int i = 0; i < argument_count; ++i)
    {
        if (i > 0) new_cmd += ' ';
        new_cmd += i == arg ? suggestions[suggestion] : std::string_view(base_args[i]);
    }
    search_content_buffer.set(utf8_to_u32(":" + new_cmd));
    cursor_position = static_cast<int>(search_content_buffer.get().size());
    previous_command = new_cmd;
    previous_cursor = cursor_position.load();

    if (suggestions.size() == 1)
    {
        suggestions.clear();
        arg = -1;
        return;
    }

    notices.clear();
    String suggestion_text;
    for (std::size_t i = 0; i < suggestions.size(); ++i)
    {
        suggestion_text += suggestions[(suggestion + i) % suggestions.size()];
        suggestion_text += ' ';
    }
    show_info(sprint("(Tab suggestion: ") + suggestion_text + ")", "INFO", 60000);
    on_display = false;
    suggestion = (suggestion + 1) % static_cast<int>(suggestions.size());
}


std::string_view ccdb::ccdb::continuous_table_state_t::notification()
{
    while (!notices.empty())
    {
        const auto now = std::chrono::steady_clock::now();
        const auto & notice = notices.front();
        const auto duration = notice.timeout <= 0 ? static_cast<int64_t>(3000 / notices.size()) : notice.timeout;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - notice.since).count() < duration)
            break;
        notices.erase(notices.begin());
        if (!notices.empty()) notices.front().since = now;
    }
    // Preserve this frame's message if commands clear or append notices later.
    if (notices.empty()) displayed_notice.clear();
    else if (displayed_notice != notices.front().text) displayed_notice = notices.front().text;
    return displayed_notice;
}

std::pair<int, std::string> ccdb::ccdb::continuous_table_state_t::external_banner(const String & banner_command)
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
}
