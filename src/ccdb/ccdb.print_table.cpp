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
    bool measure_columns(const ccdb::print_table_context_t & context,
        ccdb::print_table_context_t::width_cache_t & width_cache)
    {
        const auto & column_titles = context.column_titles;
        const auto & rows = context.rows;
        const auto column_count = static_cast<std::size_t>(column_titles.second - column_titles.first);
        const auto row_count = static_cast<std::size_t>(rows.second - rows.first);
        auto & title_screen_widths = width_cache.title_screen_widths;
        auto & column_widths = width_cache.column_widths;
        title_screen_widths.resize(column_count);
        column_widths.resize(column_count);
        std::size_t title_index = 0;
        for (auto title = column_titles.first; title != column_titles.second; ++title, ++title_index)
        {
            const int title_width = UnicodeDisplayWidth::get_width(*title);
            title_screen_widths[title_index] = title_width;
            column_widths[title_index] = static_cast<uint32_t>(title_width);
        }

        const auto value_count = row_count * column_count;
        auto & value_screen_widths = width_cache.value_screen_widths;
        value_screen_widths.resize(value_count);
        auto & cached_values = width_cache.values;
        if (context.width_cache) {
            cached_values.resize(value_count);
        }

        std::size_t value_index = 0;
        for (auto row = rows.first; row < rows.second; ++row)
        {
            if (row->size() != column_count) return false;
            std::size_t index = 0;
            for (const auto & cell : *row)
            {
                uint32_t cell_width = 0;
                if (context.width_cache)
                {
                    auto & cached_value = cached_values[value_index];
                    if (cached_value != cell)
                    {
                        cached_value = cell;
                        value_screen_widths[value_index] =
                            static_cast<uint32_t>(UnicodeDisplayWidth::get_width(cell));
                    }
                    cell_width = value_screen_widths[value_index];
                }
                else
                {
                    cell_width = static_cast<uint32_t>(UnicodeDisplayWidth::get_width(cell));
                    value_screen_widths[value_index] = cell_width;
                }

                if (auto & current_width = column_widths[index]; current_width < cell_width) {
                    current_width = cell_width;
                }
                ++index;
                ++value_index;
            }
        }

        // Preserve the original title-based width semantics (including duplicate
        // table title names), but avoid hashing the title for every rendered cell.
        for (std::size_t i = 0; i < column_count; ++i)
        {
            for (std::size_t j = i + 1; j < column_count; ++j)
            {
                if (*(column_titles.first + static_cast<std::ptrdiff_t>(i)) ==
                    *(column_titles.first + static_cast<std::ptrdiff_t>(j)))
                {
                    const auto width = std::max(column_widths[i], column_widths[j]);
                    column_widths[i] = width;
                    column_widths[j] = width;
                }
            }
        }

        return true;
    }

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

    class table_frame_formatter_t
    {
        const ccdb::print_table_context_t & context_;
        const std::string cursor_style_ = YES_HIGHLIGHTER_LINE_COLOR_CODE ? "\033[01;05;07m" : "";
        const std::u32string::value_type cursor_ =
            ccdb::utils::getenv("CURSOR").empty() ? L' ' :
            utf8_to_u32(ccdb::utils::getenv("CURSOR")).front();
        const std::string search_style_ = ccdb::color::USE_OLD_COLOR_SCHEME ?
            ccdb::color::color(5,5,5,0,0,5) : ccdb::color::color24(255,255,255,120,0,255);

        [[nodiscard]] std::u32string format_search_line(const int matches) const
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
                        utf8_to_u32(ccdb::color::no_color() + search_style_) +
                            std::u32string(context_.terminal_width - 1, ' ') +
                        utf8_to_u32(ccdb::color::no_color());
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
                    ccdb::color::g_color_status_override = 0;
                }

                int rendered_width = before_width + 1;
                before =
                    utf8_to_u32(search_style_) + before +
                    utf8_to_u32(cursor_style_) +
                    std::u32string(1, highlight > 0 ? static_cast<wchar_t>(highlight) : cursor_) +
                    utf8_to_u32(ccdb::color::no_color()) + utf8_to_u32(search_style_);

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

                before += std::u32string(std::max(context_.terminal_width - rendered_width, 0), ' ') + utf8_to_u32(ccdb::color::no_color());
                ccdb::color::g_color_status_override = -1;
                return before;
            }

            if (!context_.search_pattern.empty())
            {
                std::u32string summary = U"/" + utf8_to_u32(context_.search_pattern) + U": " +
                    utf8_to_u32(std::to_string(matches));
                const int width = trim_left_to_width(summary, context_.terminal_width);
                summary.append(std::max(context_.terminal_width - width, 0), U' ');
                return utf8_to_u32(search_style_) + summary + utf8_to_u32(ccdb::color::no_color());
            }

            return { static_cast<std::u32string::size_type>(context_.terminal_width), ' ', std::u32string::allocator_type() };
        }

    public:
        explicit table_frame_formatter_t(const ccdb::print_table_context_t & context)
            : context_(context) {}

        [[nodiscard]] std::string format(const std::ostringstream & frame,
            const int row_count, const int row_offset, const int row_end, const int matches) const
        {
            std::string result;
            if (const std::string frame_text = frame.str(); !frame_text.empty())
            {
                std::vector<std::string> frame_lines;
                frame_lines.reserve(static_cast<std::size_t>(std::ranges::count(frame_text, '\n')) + 1);

                std::string line;
                std::istringstream input(frame_text);
                while (std::getline(input, line)) {
                    frame_lines.emplace_back(line);
                }

                std::stringstream output;
                const std::string scrollbar = generate_linear_handle(
                    row_count, row_offset, row_end, static_cast<int>(frame_lines.size()));
                const std::u32string scrollbar_code_points = utf8_to_u32(scrollbar);

                const auto line_count = std::min(frame_lines.size(), scrollbar_code_points.size());
                for (std::size_t i = 0; i < line_count; ++i)
                {
                    const auto no_color_str = strip_color(frame_lines[i]);
                    const int padding = context_.terminal_width - UnicodeDisplayWidth::get_width(no_color_str) - 1;
                    output << utf8::utf32to8({ scrollbar_code_points[i] })
                        << frame_lines[i]
                        << ccdb::color::no_color()
                        << (padding > 0 ? std::string(padding, ' ') : "")
                        << std::endl;
                }

                result = output.str();
                result += utf8::utf32to8(format_search_line(matches));
            }
            return result;
        }
    };
}

std::string ccdb::ccdb::print_table(const print_table_context_t & context)
{
    using output_mode_t = print_table_context_t::output_mode_t;
    const auto & column_titles = context.column_titles;
    const auto & rows = context.rows;
    const auto & hidden_columns = context.hidden_columns;
    const auto & column_alignment = context.column_alignment;
    uint64_t horizontal_offset = context.horizontal_offset;
    int row_offset = context.row_offset;
    int row_end = 0;
    int matches = 0;
    std::string final_frame;
    std::ostringstream frame;
    std::ostringstream text_output;
    print_table_context_t::width_cache_t local_width_cache;
    auto & width_cache = context.width_cache ? *context.width_cache : local_width_cache;
    static const auto border_style = color::color(5,5,5,0,0,0);
    const table_frame_formatter_t frame_formatter(context);

    const auto finish_output = [&]() noexcept
    {
        if (const auto output = text_output.str(); !output.empty())
        {
            if (context.output_stream) {
                *context.output_stream << output;
            } else {
                pager(output);
            }
            return;
        }
        final_frame = frame_formatter.format(frame,
            static_cast<int>(rows.second - rows.first), row_offset, row_end, matches);
    };

    const auto render_table = [&]
    {
        std::vector<int> visible_column_widths;
        auto make_border = [&visible_column_widths](const char * code)
        {
            std::string separation_line;
            for (auto it = visible_column_widths.begin(); it != visible_column_widths.end(); ++it)
            {
                for (int i = 0; i < *it - 1; ++i) {
                    separation_line.append(unicode_box_line);
                }

                if (it != visible_column_widths.end() - 1) {
                    separation_line.append(code);
                }
            }
            return separation_line;
        };

        const auto viewport_width = context.terminal_width - 1;
        const auto viewport_height = context.terminal_height - 1;
        if (context.terminal_width < 1 || context.terminal_height < 1) return;

        if (viewport_height < 9 || viewport_width < 3) {
            frame << color::color(0,0,0,5,0,0) << sprint("TOO SMALL") << color::no_color() << std::endl;
            return;
        }

        const auto column_count = column_titles.second - column_titles.first;
        const auto row_count = rows.second - rows.first;
        const auto hidden_column_count = hidden_columns.second - hidden_columns.first;
        const bool has_hidden_columns = hidden_column_count != 0 && hidden_column_count == column_count;

        if (!measure_columns(context, width_cache)) return;
        const auto & title_screen_widths = width_cache.title_screen_widths;
        const auto & column_widths = width_cache.column_widths;
        const auto & value_screen_widths = width_cache.value_screen_widths;

        std::string title_line;
        std::string column_index_line;
        int title_line_width = 0;

        {
            int index = 0;
            for (auto title = column_titles.first; title < column_titles.second; ++title)
            {
                if (has_hidden_columns && *(hidden_columns.first + index))
                {
                    ++index;
                    continue;
                }

                {
                    const int paddings = static_cast<int>(column_widths[index]) - title_screen_widths[index] + 2;
                    const int before = std::max(paddings / 2, 1);
                    const int after = std::max(paddings - before, 1);
                    title_line.append(unicode_box_vertical);
                    title_line.append(static_cast<std::size_t>(before), ' ');
                    title_line += *title;
                    title_line.append(static_cast<std::size_t>(after), ' ');
                    const int len = 1 + before + title_screen_widths[index] + after;
                    title_line_width += len;
                    visible_column_widths.push_back(len);
                }

                {
                    const std::string index_str = std::to_string(index);
                    const int paddings = static_cast<int>(column_widths[index]) - static_cast<int>(index_str.size()) + 2;
                    const int before = std::max(paddings / 2, 1);
                    const int after = std::max(paddings - before, 1);
                    column_index_line.append(unicode_box_vertical);
                    column_index_line.append(static_cast<std::size_t>(before), ' ');
                    column_index_line += index_str;
                    column_index_line.append(static_cast<std::size_t>(after), ' ');
                }
                ++index;
            }
        }
        title_line.append(unicode_box_vertical);
        column_index_line.append(unicode_box_vertical);
        ++title_line_width;

        std::string top_border, middle_border;
        if (title_line_width > 2)
        {
            top_border = unicode_box_upper_left + make_border(unicode_box_upper_middle) + unicode_box_upper_right;
            middle_border = unicode_box_left_middle + make_border(unicode_box_middle_middle) + unicode_box_right_middle;
        }

        const int border_width = UnicodeDisplayWidth::get_width(top_border);

        std::vector<int> banner_widths;
        for (const auto & banner : context.banner_lines) {
            banner_widths.emplace_back(UnicodeDisplayWidth::get_width(strip_color(banner)));
        }

        // Banner widths do not contribute to the horizontal scroll limit.
        const int horizontal_limit = std::max(border_width - viewport_width, 0);
        if (context.max_horizontal_offset) *context.max_horizontal_offset = static_cast<int>(horizontal_limit);
        horizontal_offset = std::min(static_cast<decltype(horizontal_limit)>(horizontal_offset), horizontal_limit);
        int rendered_line_count = 0;

        // define Tab size
        const auto tabsz_str = utils::getenv("TABSIZE");
        int tab_space_size = -1;
        try {
            tab_space_size = convertToNumber<int>(tabsz_str);
        } catch (...) { }
        if (tab_space_size <= 0) {
            tab_space_size = 4;
        }
        const auto search_pattern = utf8_to_u32(context.search_pattern);

        auto print_line = [&](std::string text, const std::string & line_style)
        {
            auto line = utf8_to_u32(text);
            replace_all(line, U"\n", U"");
            replace_all(line, U"\r", U"");
            replace_all(line, U"\t", std::u32string(tab_space_size, U' '));
            if (context.max_horizontal_offset && context.output_mode == output_mode_t::frame)
            {
                if (!clip_line(line, horizontal_offset, viewport_width, width_cache, border_style))
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
                const auto line_highlighter = context.screen_line_colors.find(rendered_line_count + 1);
                const bool use_line_highlighter = line_highlighter != context.screen_line_colors.end();
                const std::string line_highlight_color =
                    use_line_highlighter && YES_HIGHLIGHTER_LINE_COLOR_CODE ? line_highlighter->second : "";
                const std::u32string effective_color = utf8_to_u32(line_style + line_highlight_color);

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
        };

        if (!context.banner_lines.empty())
        {
            std::vector<std::string> padded_banner_lines = context.banner_lines;
            for (auto it = padded_banner_lines.begin(); it != padded_banner_lines.end(); ++it)
            {
                *it += std::string(
                        std::max(static_cast<int>(viewport_width + horizontal_offset -
                            banner_widths[it - padded_banner_lines.begin()]), 0),
                    ' ');

                print_line(*it, border_style);
            }
        }

        print_line(top_border, border_style);
        print_line(column_index_line, border_style);
        print_line(middle_border, border_style);
        print_line(title_line, border_style);
        print_line(middle_border, border_style);

        const int row_limit = std::max(static_cast<int>(row_count) - (viewport_height - 2 - rendered_line_count), 0);
        if (context.max_row_offset) *context.max_row_offset = row_limit;
        if (row_offset > row_limit) row_offset = row_limit;

        auto print_status = [&]
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

            const auto scrollbar_width = viewport_width - static_cast<int>(status_text.size());
            frame << color::no_color();

            if (context.show_search && !*context.show_search) {
                frame << generate_linear_handle(horizontal_limit + viewport_width,
                    static_cast<int>(horizontal_offset),
                    static_cast<int>(horizontal_offset) + viewport_width,
                    scrollbar_width);
            } else {
                frame << std::string(scrollbar_width > 0 ? scrollbar_width : 0, ' ');
            }
        };

        // Data rows
        for (auto row = rows.first; row != rows.second; ++row)
        {
            if (context.output_mode != output_mode_t::full_table)
            {
                // skip n elements
                if (row_end < row_offset)
                {
                    row_end++;
                    continue;
                }

                // last element on screen
                if (row_end > row_offset && rendered_line_count >= (viewport_height - 1))
                {
                    print_status();
                    return;
                }
            }

            std::string row_style;
            if (const auto override_it = context.row_colors.find(row_end);
                override_it == context.row_colors.end())
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
                row_style = color::bg_color(0,0,0) + override_it->second;
            }

            int index = 0;
            const auto row_width_offset =
                static_cast<std::size_t>(row - rows.first) * static_cast<std::size_t>(column_count);
            std::string row_line;
            row_line.reserve(title_line.size());

            for (const auto & cell : *row)
            {
                if (has_hidden_columns && *(hidden_columns.first + index))
                {
                    ++index;
                    continue;
                }

                const int current_alignment =
                    column_alignment.second == column_alignment.first ? 0 /* left */ : *(column_alignment.first + index);
                const int cell_width = static_cast<int>(
                    value_screen_widths[row_width_offset + static_cast<std::size_t>(index)]);
                const int column_width = static_cast<int>(column_widths[index]);
                ++index;

                int before = 0;
                int after = 0;
                const int paddings = column_width - cell_width + 2;

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

            row_line.append(unicode_box_vertical);
            print_line(std::move(row_line), row_style);
            row_end++;
        }

        // Bottom border and status line
        if (rendered_line_count <= viewport_height - 2 && border_width > 2)
        {
            if (context.output_mode != output_mode_t::full_table) {
                frame << border_style << (horizontal_offset == 0 ? unicode_box_bottom_left : "<");
                const auto visible_border_width = std::min(viewport_width - 2, border_width - 2);
                std::u32string bottom_border = utf8_to_u32(make_border(unicode_box_bottom_middle));
                bottom_border = bottom_border.substr(horizontal_offset, visible_border_width);
                frame << utf8::utf32to8(bottom_border);
                frame << (horizontal_offset == horizontal_limit ? unicode_box_bottom_right : ">") << std::endl;
            } else {
                text_output << color::bg_color(0,0,0) << unicode_box_bottom_left;
                text_output << make_border(unicode_box_bottom_middle);
                text_output << unicode_box_bottom_right << color::no_color();
            }
        }

        for (int j = rendered_line_count; j < (viewport_height - 2); j++)
            frame << std::endl;
        print_status();
    };

    // Flush partial output on exceptions as the former output guard did.
    try {
        render_table();
    } catch (...) {
        finish_output();
        throw;
    }
    finish_output();
    return final_frame;
}

void ccdb::ccdb::simple_print_table(std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values)
{
    simple_print_table_to_ostream(table_titles, table_values, std::cout);
    std::cout << std::endl;
}

void ccdb::ccdb::simple_print_table_to_ostream(std::vector<std::string> const &table_titles,
    std::vector<std::vector<std::string>> const &table_values, std::ostream &out_stream)
{
    const auto less_bak = less;
    less.clear();
    std::vector < bool > hide;
    std::vector < int > alignment;
    print_table({
        .column_titles = {table_titles.begin(), table_titles.end()},
        .rows = {table_values.begin(), table_values.end()},
        .hidden_columns = {hide.begin(), hide.end()},
        .output_mode = print_table_context_t::output_mode_t::full_table,
        .banner_lines = { },
        .row_colors = {},
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
    print_table({
        .column_titles = {table_titles.begin(), table_titles.end()},
        .rows = {table_values.begin(), table_values.end()},
        .hidden_columns = {hide.begin(), hide.end()},
        .output_mode = less.empty() ? print_table_context_t::output_mode_t::unpaged_table
                                  : print_table_context_t::output_mode_t::full_table,
        .banner_lines = { },
        .row_colors = { },
        .search_pattern = "",
        .column_alignment = {alignment.begin(), alignment.end()},
        .terminal_height = 20,
        .terminal_width = 20
    });
}
