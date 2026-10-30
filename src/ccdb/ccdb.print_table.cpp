// ccdb.print_table.cpp
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
#include <cmath>
#include <string>
#include "print.h"
#include "ccdb.h"

// --------------------------------------------- CCDB --------------------------------------------- //
using namespace ccdb::utils;
const auto YES_HIGHLIGHTER_LINE_COLOR_CODE = ccdb::utils::getenv("NO_HIGHLIGHTER_LINE_COLOR_CODE") != "true";

static std::u32string highlight(const std::u32string & str,
    const std::u32string & pattern,
    const std::u32string & original_color,
    int & matches,
    const std::u32string & highlight_color_str)
{
    if (pattern.empty()) return str;
    std::u32string ret = strip_color(str);
    return original_color + regex_replace_all(ret, pattern,
        [&](const basic_regex_scope_type<char32_t> & mat)->std::u32string
        {
            const auto & mat_str = *mat.first;
            if ((mat_str.size() == 1 && (mat_str.front() >= 0x80 ||
                std::isprint(static_cast<unsigned char>(mat_str.front())))) || mat_str.size() > 1)
            {
                matches += 1;
                return highlight_color_str + mat_str + utf8_to_u32(ccdb::color::no_color()) + original_color;
            }
    
            return mat_str;
        });
}

static bool REVERT_TO_BACKGROUND = ccdb::utils::getenv("CCDB_REVERT_TO_BACKGROUND") == "true";
static std::string color_sim(const int particle, const int overall)
{
    using namespace ccdb::color;
    const double ratio_ref = static_cast<double>(particle) / static_cast<double>(overall);
    const auto [red, green, blue] =
        sim::simulation_rainbow(sim::Span * ratio_ref + sim::Begin);
    auto color_line = REVERT_TO_BACKGROUND ?
        bg_color24(static_cast<int>(std::round(red)), static_cast<int>(std::round(green)), static_cast<int>(std::round(blue))) :
        color24(static_cast<int>(std::round(red)), static_cast<int>(std::round(green)), static_cast<int>(std::round(blue)));

    constexpr double gate = static_cast<double>(255) / 3 * 2;
    if (REVERT_TO_BACKGROUND)
    {
        if (green > gate || (blue > gate && red > gate)) {
            color_line += color(0,0,0);
        } else {
            color_line += color(5,5,5);
        }
    }
    else
    {
        static int SPECIFIED_BACKGROUND_R = -1;
        static int SPECIFIED_BACKGROUND_G = -1;
        static int SPECIFIED_BACKGROUND_B = -1;
        if (SPECIFIED_BACKGROUND_R == -1)
        {
            if (const auto SPECIFIED_BACKGROUND_RGB = ccdb::utils::getenv("SPECIFIED_BACKGROUND_RGB");
                !SPECIFIED_BACKGROUND_RGB.empty())
            {
                try {
                    const auto begin = SPECIFIED_BACKGROUND_RGB.find_first_of(';');
                    const auto end = SPECIFIED_BACKGROUND_RGB.find_last_of(';');
                    const auto SPECIFIED_BACKGROUND_R_ = SPECIFIED_BACKGROUND_RGB.substr(0, begin);
                    const auto SPECIFIED_BACKGROUND_G_ = SPECIFIED_BACKGROUND_RGB.substr(begin + 1, end - (begin + 1));
                    const auto SPECIFIED_BACKGROUND_B_ = SPECIFIED_BACKGROUND_RGB.substr(end + 1);
                    SPECIFIED_BACKGROUND_R = convertToNumber<decltype(SPECIFIED_BACKGROUND_R)>(SPECIFIED_BACKGROUND_R_);
                    SPECIFIED_BACKGROUND_G = convertToNumber<decltype(SPECIFIED_BACKGROUND_G)>(SPECIFIED_BACKGROUND_G_);
                    SPECIFIED_BACKGROUND_B = convertToNumber<decltype(SPECIFIED_BACKGROUND_B)>(SPECIFIED_BACKGROUND_B_);
                } catch (...) {
                    SPECIFIED_BACKGROUND_R = 32;
                    SPECIFIED_BACKGROUND_G = 32;
                    SPECIFIED_BACKGROUND_B = 32;
                }
            }
            else
            {
                SPECIFIED_BACKGROUND_R = 32;
                SPECIFIED_BACKGROUND_G = 32;
                SPECIFIED_BACKGROUND_B = 32;
            }
        }

        color_line += bg_color24(SPECIFIED_BACKGROUND_R,SPECIFIED_BACKGROUND_G,SPECIFIED_BACKGROUND_B);
    }

    return color_line;
}

namespace {
    struct display_width_scan_t
    {
        std::vector<int> widths;
        int total = 0;
    };
}

static int scan_display_widths(
    const std::u32string & str,
    std::vector<int> & widths)
{
    widths.clear();
    widths.reserve(str.size());
    int total = 0;
    for (const auto c : str)
    {
        const auto len = UnicodeDisplayWidth::get_width(c);
        widths.emplace_back(len);
        total += len;
    }
    return total;
}
static display_width_scan_t scan_display_widths(const std::u32string & str)
{
    display_width_scan_t ret;
    ret.total = scan_display_widths(str, ret.widths);
    return ret;
}

static int trim_left_to_width(
    std::u32string & str,
    const int preferred_len,
    const std::vector<int> & width_list,
    int total_len)
{
    if (str.empty()) return 0;
    int offset = 0;
    while (total_len > preferred_len) {
        total_len -= width_list[offset++];
    }

    str = str.substr(offset);
    return total_len;
}

static int trim_left_to_width(std::u32string & str, const int preferred_len)
{
    if (str.empty()) return 0;
    const auto width_scan = scan_display_widths(str);
    return trim_left_to_width(str, preferred_len, width_scan.widths, width_scan.total);
}

namespace {
    // Returns false when the entire line lies to the left of the viewport.
    // viewport_width is at least three; reserve one column for each clipping marker.
    bool clip_line(std::u32string & line, const uint64_t horizontal_offset,
        const int viewport_width, ccdb::print_table_context_t::width_cache_t & width_cache,
        const std::string & border_style)
    {
        auto & line_widths = width_cache.line_widths;
        int display_width = scan_display_widths(line, line_widths);

        // Clip the left edge with a single erase and retain the scanned widths.
        if (horizontal_offset > 0 && display_width >= horizontal_offset)
        {
            const auto left_clip_width = horizontal_offset + 1;
            int removed_width = 0;
            int character_width = 0;
            std::size_t erase_count = 0;

            while (erase_count < line.size())
            {
                character_width = line_widths[erase_count];
                removed_width += character_width;

                if (removed_width > left_clip_width) {
                    removed_width -= character_width;
                    break;
                }

                ++erase_count;
            }

            // Preserve the original partial-wide-character handling, but
            // perform only one front erase instead of shifting the string
            // once per consumed code point.
            std::u32string leading_padding;
            if (removed_width < left_clip_width) {
                ++erase_count;
                leading_padding = utf8_to_u32(
                    std::string(removed_width + character_width - left_clip_width, ' '));
            }

            const auto tail_begin = std::min(erase_count, line_widths.size());
            line.erase(0, erase_count);
            // Style the left marker only after clipping; ANSI bytes have no display width.
            line = utf8_to_u32("<") + leading_padding + line;

            auto & trimmed_widths = width_cache.trimmed_line_widths;
            trimmed_widths.clear();
            trimmed_widths.reserve(
                1 + leading_padding.size() + (line_widths.size() - tail_begin));
            trimmed_widths.emplace_back(1); // '<'
            trimmed_widths.insert(trimmed_widths.end(), leading_padding.size(), 1);

            display_width = 1 + static_cast<int>(leading_padding.size());
            for (std::size_t i = tail_begin; i < line_widths.size(); ++i)
            {
                trimmed_widths.emplace_back(line_widths[i]);
                display_width += line_widths[i];
            }
            line_widths.swap(trimmed_widths);
        }
        else if (horizontal_offset > 0)
        {
            return false;
        }

        if (display_width > viewport_width)
        {
            int scanned_width = 0, retained_width = 0;
            uint64_t i = 0;
            for (;i < line.size(); i++)
            {
                scanned_width += line_widths[i];
                if (scanned_width > (viewport_width - 1)) {
                    break;
                }

                retained_width = scanned_width;
            }

            std::string padding;
            if (retained_width < (viewport_width - 1)) {
                padding = std::string((viewport_width - 1) - retained_width, ' ');
            }

            line = line.substr(0, i) + utf8_to_u32(padding) +
                   utf8_to_u32(border_style + ">" + ccdb::color::no_color());
        }
        return true;
    }

}

ccdb::table_detail::table_frame_formatter_t::table_frame_formatter_t(const print_table_context_t & context)
    : context_(context), cursor_style_(YES_HIGHLIGHTER_LINE_COLOR_CODE ? "\033[01;05;07m" : ""),
      cursor_(utils::getenv("CURSOR").empty() ? U' ' : utf8_to_u32(utils::getenv("CURSOR")).front()),
      search_style_(color::USE_OLD_COLOR_SCHEME ? color::color(5,5,5,0,0,5) : color::color24(255,255,255,120,0,255)) {}

std::u32string ccdb::table_detail::table_frame_formatter_t::format_search_line(const int matches) const
{
    if (context_.show_search && *context_.show_search)
    {
        /// normalized values
        const std::u32string content = context_.search_content->get();
        const int position = *context_.search_cursor;
        if (position < 0) return {};
        auto before = content.substr(0, position);
        if (before.empty() && content.empty())
        {
            return
                utf8_to_u32(cursor_style_ + search_style_) +
                     std::u32string(1, cursor_) +
                utf8_to_u32(::ccdb::color::no_color() + search_style_) +
                    std::u32string(context_.terminal_width - 1, ' ') +
                utf8_to_u32(::ccdb::color::no_color());
        }

        const auto highlight = position < content.size() ? static_cast<signed long long int>(content[position]) : 0;
        const auto content_width_scan = scan_display_widths(content);
        int before_width = 0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            before_width += content_width_scan.widths[i];
        }

        // print the box:
        if (content_width_scan.total > (context_.terminal_width - 1)) {
            before_width = trim_left_to_width(
                before, context_.terminal_width - 1, content_width_scan.widths, before_width);
        }

        if (YES_HIGHLIGHTER_LINE_COLOR_CODE) {
            ::ccdb::color::g_color_status_override = 0;
        }

        int rendered_width = before_width + 1;
        before =
            utf8_to_u32(search_style_) + before +
            utf8_to_u32(cursor_style_) +
            std::u32string(1, highlight > 0 ? static_cast<wchar_t>(highlight) : cursor_) +
            utf8_to_u32(::ccdb::color::no_color()) + utf8_to_u32(search_style_);

        if (highlight > 0)
        {
            for (std::size_t i = static_cast<std::size_t>(position) + 1; i < content.size(); ++i)
            {
                const auto ch = content[i];
                const int ch_width = content_width_scan.widths[i];
                if ((ch_width + rendered_width) > context_.terminal_width) {
                    break;
                }

                before += ch;
                rendered_width += ch_width;
            }
        }

        before += std::u32string(std::max(context_.terminal_width - rendered_width, 0), ' ') + utf8_to_u32(::ccdb::color::no_color());
        ::ccdb::color::g_color_status_override = -1;
        return before;
    }

    if (!context_.search_pattern.empty())
    {
        std::u32string summary = U"/" + decode(context_.search_pattern) + U": " +
            utf8_to_u32(std::to_string(matches));
        const int width = trim_left_to_width(summary, context_.terminal_width);
        summary.append(std::max(context_.terminal_width - width, 0), U' ');
        return utf8_to_u32(search_style_) + summary + utf8_to_u32(::ccdb::color::no_color());
    }

    return { static_cast<std::u32string::size_type>(context_.terminal_width), ' ', std::u32string::allocator_type() };
}

std::string ccdb::table_detail::table_frame_formatter_t::format(std::string_view frame_text,
    const int row_count, const int row_offset, const int row_end, const int matches) const
{
    if (frame_text.empty()) return {};
    const auto count = std::ranges::count(frame_text, '\n') + (frame_text.back() != '\n');
    const auto scrollbar = decode(generate_linear_handle(row_count, row_offset, row_end, static_cast<int>(count)));
    std::ostringstream output;
    std::size_t index = 0;
    while (!frame_text.empty() && index < scrollbar.size())
    {
        const auto end = frame_text.find('\n');
        const auto line = frame_text.substr(0, end);
        const int padding = context_.terminal_width - UnicodeDisplayWidth::get_width(strip_color(std::string(line))) - 1;
        output << utf8::utf32to8({scrollbar[index++]}) << line << color::no_color()
               << std::string(std::max(padding, 0), ' ') << '\n';
        if (end == std::string_view::npos) break;
        frame_text.remove_prefix(end + 1);
    }
    output << utf8::utf32to8(format_search_line(matches));
    return std::move(output).str();
}


void ccdb::ccdb::simple_print_table(std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values)
{
#ifdef __WINDOWS_MSVC_BUILD__
    windows::terminal_write(simple_print_table_to_std_string(table_titles, table_values) + "\n");
#else //__WINDOWS_MSVC_BUILD__
    std::cout << simple_print_table_to_std_string(table_titles, table_values) << std::endl;
#endif //__WINDOWS_MSVC_BUILD__
}

void ccdb::ccdb::simple_print_table_to_ostream(std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values, std::ostream &out_stream)
{
    const auto less_bak = less;
    less.clear();
    std::vector < bool > hide;
    std::vector < int > alignment;
    print_table(table_titles, table_values, {
        .hidden_columns = {hide.begin(), hide.end()},
        .output_mode = print_table_context_t::output_mode_t::full_table,
        .output_stream = &out_stream,
        .search_pattern = "",
        .column_alignment = {alignment.begin(), alignment.end()},
        .terminal_height = 20,
        .terminal_width = 20
    });
    less = less_bak;
}

std::string ccdb::ccdb::simple_print_table_to_std_string(
    std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values)
{
    std::ostringstream out;
    simple_print_table_to_ostream(table_titles, table_values, out);
    return out.str();
}

void ccdb::ccdb::simple_print_table_w_pager(
    std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values)
{
    std::vector < bool > hide;
    std::vector < int > alignment;
    print_table(table_titles, table_values, {
        .hidden_columns = {hide.begin(), hide.end()},
        .output_mode = less.empty() ? print_table_context_t::output_mode_t::unpaged_table
                                  : print_table_context_t::output_mode_t::full_table,
        .search_pattern = "",
        .column_alignment = {alignment.begin(), alignment.end()},
        .terminal_height = 20,
        .terminal_width = 20
    });
}


namespace ccdb::table_detail {
    std::u32string decode(std::string_view text)
    {
        std::u32string result;
        utf8::utf8to32(text.begin(), text.end(), std::back_inserter(result));
        return result;
    }
    int display_width(std::string_view text)
    {
        int width = 0;
        for (auto it = text.begin(); it != text.end();)
            width += UnicodeDisplayWidth::get_width(utf8::next(it, text.end()));
        return width;
    }
    std::string make_border(std::span<const int> widths, const char * code)
    {
        std::string result;
        for (std::size_t column = 0; column < widths.size(); ++column)
        {
            for (int i = 0; i < widths[column] - 1; ++i) result += unicode_box_line;
            if (column + 1 < widths.size()) result += code;
        }
        return result;
    }
    void append_cell(std::string & row_line, std::string_view cell, int paddings, int current_alignment)
    {
        int before = 0;
        int after = 0;

        switch (current_alignment)
        {
        default:
        case 0: // left
            before = 1;
            after = std::max(paddings - before, 1);
            break;
        case 1: // right
            after = 1;
            before = std::max(paddings - after, 1);
            break;
        case 2: // center
            before = paddings / 2;
            after = std::max(paddings - before, 1);
            break;
        }

        row_line.append(unicode_box_vertical);
        row_line.append(static_cast<std::size_t>(before), ' ');
        row_line += cell;
        row_line.append(static_cast<std::size_t>(after), ' ');
    }
    void print_status(const print_table_context_t & context, std::ostream & frame, int row_count,
                      int row_offset, int row_end, uint64_t horizontal_offset, int horizontal_limit,
                      const std::string & border_style)
    {
        std::stringstream status;
        status << row_offset << "/" << row_end << "/" << row_count << "/"
            << std::fixed << std::setprecision(2)
            << (row_count == 0 ? 1 : static_cast<double>(row_end) / static_cast<double>(row_count)) * 100 << "%"
            << (context.scroll_lock_labels.vertical ? context.scroll_lock_labels.vertical : "")
            << "/" << horizontal_offset << "/" << horizontal_limit << "/"
            << (horizontal_limit == 0 ? 1 : static_cast<double>(horizontal_offset) / static_cast<double>(horizontal_limit)) * 100 << "%"
            << (context.scroll_lock_labels.horizontal ? context.scroll_lock_labels.horizontal : "");
        const std::string status_text = status.str();
        if (context.status_width) *context.status_width = static_cast<int>(status_text.size());

        if (color::is_no_color() && YES_HIGHLIGHTER_LINE_COLOR_CODE) {
            color::g_color_status_override = 0;
            frame << border_style;
            color::g_color_status_override = -1;
        }

        frame << color::bg_color(5,5,5) << color::color(0,0,5) << status_text;

        if (color::is_no_color() && YES_HIGHLIGHTER_LINE_COLOR_CODE) {
            color::g_color_status_override = 0;
            frame << color::no_color();
            color::g_color_status_override = -1;
        }

        const auto scrollbar_width = (context.terminal_width - 1) - static_cast<int>(status_text.size());
        frame << color::no_color();

        if (context.show_search && !*context.show_search) {
            frame << generate_linear_handle(horizontal_limit + (context.terminal_width - 1),
                static_cast<int>(horizontal_offset),
                static_cast<int>(horizontal_offset) + (context.terminal_width - 1),
                scrollbar_width);
        } else {
            frame << std::string(scrollbar_width > 0 ? scrollbar_width : 0, ' ');
        }

    }
    std::string row_style(const print_table_context_t & context, int row_end, int row_count, const std::string & border_style)
    {
        std::string row_style;
        if (!context.row_colors || !context.row_colors->contains(row_end))
        {
            // blue and black
            if (color::USE_OLD_COLOR_SCHEME)
            {
                if (row_end & 0x01) row_style = border_style;
                else row_style = color::color(5,5,5,0,0,5);
            }
            else {
                row_style = color_sim(row_end, static_cast<int>(row_count));
            }
        } else {
            row_style = color::bg_color(0,0,0) + context.row_colors->at(row_end);
        }

        return row_style;
    }
    line_printer_t::line_printer_t(const print_table_context_t & options, print_table_context_t::width_cache_t & cache,
                                  uint64_t offset, const std::string & border, std::ostream & frame_stream,
                                  std::ostream & text_stream, int & lines, int & found)
        : context(options), width_cache(cache), horizontal_offset(offset), border_style(border),
          frame(frame_stream), text_output(text_stream), rendered_line_count(lines), matches(found)
    {
        const auto tabsz_str = utils::getenv("TABSIZE");
        tab_space_size = -1;
        try {
            tab_space_size = convertToNumber<int>(tabsz_str);
        } catch (...) { }
        if (tab_space_size <= 0) {
            tab_space_size = 4;
        }
        search_pattern = normalize_search_pattern(decode(context.search_pattern));
    }
    void line_printer_t::operator()(std::string_view text, const std::string & line_style) const
    {
        using output_mode_t = print_table_context_t::output_mode_t;
        auto line = decode(text);
        replace_all(line, U"\n", U"");
        replace_all(line, U"\r", U"");
        replace_all(line, U"\t", std::u32string(tab_space_size, U' '));
        if (context.max_horizontal_offset && context.output_mode == output_mode_t::frame)
        {
            if (!clip_line(line, horizontal_offset, (context.terminal_width - 1), width_cache, border_style))
            {
                frame << std::endl;
                ++rendered_line_count;
                return;
            }
        }

        if (context.output_mode != output_mode_t::frame) {
            text_output << line_style << utf8::utf32to8(line) << color::no_color();
            text_output << std::endl;
        } else {
            const auto index = static_cast<std::size_t>(rendered_line_count + 1);
            const auto line_highlight_color = YES_HIGHLIGHTER_LINE_COLOR_CODE && index < context.screen_line_colors.size()
                ? context.screen_line_colors[index] : std::string_view{};
            const std::u32string effective_color = utf8_to_u32(line_style + std::string(line_highlight_color));

            if (!line.empty() && line.front() == U'<')
            {
                line.erase(line.begin());
                line = utf8_to_u32(border_style) + U"<" + effective_color + line;
            } else {
                line = effective_color + line;
            }

            if (YES_HIGHLIGHTER_LINE_COLOR_CODE) {
                color::g_color_status_override = 0;
            }

            frame << utf8::utf32to8(highlight(
                line,
                search_pattern,
                effective_color,
                matches,
                YES_HIGHLIGHTER_LINE_COLOR_CODE ? U"\033[01;05;07m" : U""))
                  << color::no_color();
            color::g_color_status_override = -1;
            frame << std::endl;
            rendered_line_count++;
        }

    }
}
