#ifndef CCDB_PRINT_TABLE_INL
#define CCDB_PRINT_TABLE_INL

namespace table_detail {
    template<class Titles, class Rows>
    void measure_columns(Titles & column_titles, Rows & rows,
        print_table_context_t::width_cache_t & width_cache)
    {
        const auto column_count = static_cast<std::size_t>(std::ranges::distance(column_titles));
        const auto row_count = static_cast<std::size_t>(std::ranges::distance(rows));
        auto & title_screen_widths = width_cache.title_screen_widths;
        auto & column_widths = width_cache.column_widths;
        title_screen_widths.resize(column_count);
        column_widths.resize(column_count);
        std::size_t title_index = 0;
        for (auto title = column_titles.begin(); title != column_titles.end(); ++title, ++title_index)
        {
            const int title_width = display_width(text(*title));
            title_screen_widths[title_index] = title_width;
            column_widths[title_index] = static_cast<uint32_t>(title_width);
        }

        const auto value_count = row_count * column_count;
        auto & value_screen_widths = width_cache.value_screen_widths;
        value_screen_widths.resize(value_count);
        std::size_t value_index = 0;
        for (auto row = rows.begin(); row != rows.end(); ++row)
        {
            auto cell = std::ranges::begin(*row);
            for (std::size_t index = 0; index < column_count; ++index, ++cell)
            {
                const auto cell_width = static_cast<uint32_t>(display_width(text(*cell)));
                value_screen_widths[value_index] = cell_width;
                if (auto & current_width = column_widths[index]; current_width < cell_width) {
                    current_width = cell_width;
                }
                ++value_index;
            }
        }

        // Preserve the original title-based width semantics (including duplicate
        // table title names), but avoid hashing the title for every rendered cell.
        std::size_t i = 0;
        for (auto title = column_titles.begin(); title != column_titles.end(); ++title, ++i)
        {
            std::size_t j = i + 1;
            for (auto other = std::next(title); other != column_titles.end(); ++other, ++j)
            {
                if (text(*title) == text(*other))
                {
                    const auto width = std::max(column_widths[i], column_widths[j]);
                    column_widths[i] = column_widths[j] = width;
                }
            }
        }
    }

}

template<class Titles, class Rows, class Banners>
std::string ccdb::print_table(Titles && titles, Rows && data, const print_table_context_t & context, Banners && banners)
{
    using namespace utils;
    using namespace table_detail;
    // A continuous session supplies reusable numeric buffers. One-shot calls own
    // their scratch storage only for this invocation; neither path retains cells.
    if (!context.width_cache)
    {
        print_table_context_t::width_cache_t storage;
        auto options = context;
        options.width_cache = &storage;
        return print_table(titles, data, options, banners);
    }
    auto column_titles = table_detail::range(titles);
    auto rows = table_detail::range(data);
    auto banner_lines = table_detail::range(banners);
    static_assert(std::ranges::forward_range<decltype(column_titles)>);
    static_assert(std::ranges::forward_range<decltype(rows)>);
    const auto column_count = std::ranges::distance(column_titles);
    const auto row_count = std::ranges::distance(rows);
    using output_mode_t = print_table_context_t::output_mode_t;
    const auto & hidden_columns = context.hidden_columns;
    const auto & column_alignment = context.column_alignment;
    uint64_t horizontal_offset = context.horizontal_offset;
    int row_offset = context.row_offset;
    int row_end = 0;
    int matches = 0;
    std::string final_frame;
    std::ostringstream frame;
    std::ostringstream text_output;
    auto & width_cache = *context.width_cache;
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
        final_frame = frame_formatter.format(frame.view(),
            static_cast<int>(row_count), row_offset, row_end, matches);
    };

    const auto render_table = [&]
    {
        auto & visible_column_widths = width_cache.visible_column_widths;
        visible_column_widths.clear();
        const auto make_border = [&](const char * code) {
            return table_detail::make_border(visible_column_widths, code);
        };

        const auto viewport_width = context.terminal_width - 1;
        const auto viewport_height = context.terminal_height - 1;
        if (context.terminal_width < 1 || context.terminal_height < 1) return;

        if (viewport_height < 9 || viewport_width < 3) {
            frame << color::color(0,0,0,5,0,0) << sprint("TOO SMALL") << color::no_color() << std::endl;
            return;
        }

        const auto hidden_column_count = hidden_columns.first == hidden_columns.second
            ? 0 : hidden_columns.second - hidden_columns.first;
        const bool has_hidden_columns = hidden_column_count != 0 && hidden_column_count == column_count;

        measure_columns(column_titles, rows, width_cache);
        const auto & title_screen_widths = width_cache.title_screen_widths;
        const auto & column_widths = width_cache.column_widths;
        const auto & value_screen_widths = width_cache.value_screen_widths;

        std::string title_line;
        std::string column_index_line;
        int title_line_width = 0;

        {
            int index = 0;
            for (auto title = column_titles.begin(); title != column_titles.end(); ++title, ++index)
            {
                if (has_hidden_columns && *(hidden_columns.first + index))
                {
                    continue;
                }

                {
                    const int paddings = static_cast<int>(column_widths[index]) - title_screen_widths[index] + 2;
                    const int before = std::max(paddings / 2, 1);
                    const int after = std::max(paddings - before, 1);
                    title_line.append(unicode_box_vertical);
                    title_line.append(static_cast<std::size_t>(before), ' ');
                    title_line += text(*title);
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

        // Banner widths do not contribute to the horizontal scroll limit.
        const int horizontal_limit = std::max(border_width - viewport_width, 0);
        if (context.max_horizontal_offset) *context.max_horizontal_offset = static_cast<int>(horizontal_limit);
        horizontal_offset = std::min(static_cast<decltype(horizontal_limit)>(horizontal_offset), horizontal_limit);
        int rendered_line_count = 0;

        const line_printer_t print_line(context, width_cache, horizontal_offset, border_style,
                                        frame, text_output, rendered_line_count, matches);
        const auto print_banner = [&](std::string_view banner) {
            std::string padded(banner);
            const int width = display_width(strip_color(padded));
            padded.append(std::max(static_cast<int>(viewport_width + horizontal_offset - width), 0), ' ');
            print_line(padded, border_style);
        };
        auto external = context.external_banner;
        while (!external.empty())
        {
            const auto end = external.find('\n');
            print_banner(external.substr(0, end));
            if (end == std::string_view::npos) break;
            external.remove_prefix(end + 1);
        }
        bool first_banner = true;
        for (auto && banner : banner_lines)
        {
            print_banner(first_banner && !context.first_banner_override.empty()
                             ? context.first_banner_override : std::string_view(text(banner)));
            first_banner = false;
        }

        print_line(top_border, border_style);
        print_line(column_index_line, border_style);
        print_line(middle_border, border_style);
        print_line(title_line, border_style);
        print_line(middle_border, border_style);

        const int row_limit = std::max(static_cast<int>(row_count) - (viewport_height - 2 - rendered_line_count), 0);
        if (context.max_row_offset) *context.max_row_offset = row_limit;
        if (row_offset > row_limit) row_offset = row_limit;

        const auto print_status = [&] {
            table_detail::print_status(context, frame, static_cast<int>(row_count), row_offset,
                                      row_end, horizontal_offset, horizontal_limit, border_style);
        };

        // Data rows
        for (auto row = rows.begin(); row != rows.end(); ++row)
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

            const auto row_style = table_detail::row_style(context, row_end, static_cast<int>(row_count), border_style);

            const auto row_width_offset =
                static_cast<std::size_t>(row_end) * static_cast<std::size_t>(column_count);
            std::string row_line;
            row_line.reserve(title_line.size());

            auto cell = std::ranges::begin(*row);
            for (int index = 0; index < column_count; ++index, ++cell)
            {
                if (has_hidden_columns && *(hidden_columns.first + index))
                {
                    continue;
                }

                const int current_alignment =
                    column_alignment.second == column_alignment.first ? 0 /* left */ : *(column_alignment.first + index);
                const int cell_width = static_cast<int>(
                    value_screen_widths[row_width_offset + static_cast<std::size_t>(index)]);
                const int column_width = static_cast<int>(column_widths[index]);

                append_cell(row_line, text(*cell), column_width - cell_width + 2, current_alignment);
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

#endif
