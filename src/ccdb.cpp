#include "ccdb.h"
#include "utils.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <csignal>
#include <utility>

static std::atomic_bool sysint_pressed = false;
void sigint_handler(int) {
    sysint_pressed = true;
}

static std::atomic_bool window_size_change = false;
void window_size_change_handler(int) {
    window_size_change = true;
}

class initialize_locale
{
public:
    initialize_locale() noexcept {
        std::setlocale(LC_ALL, "en_US.UTF-8");
        std::signal(SIGINT, sigint_handler);
        std::signal(SIGPIPE, SIG_IGN);
        std::signal(SIGWINCH, window_size_change_handler);
    }
} initialize_locale_;
using namespace ccdb::utils;

void ccdb::ccdb::update_providers()
{
    backend_instance.update_proxy_list();
    auto proxy_list = backend_instance.get_proxies_and_latencies_as_pair().first;
    std::map <std::string, std::vector < std::string> > groups;
    auto mask_with_latency_when_fit = [&](const std::string & name)->std::string
    {
        if (const auto ptr = latency_backups.find(name);
            ptr != latency_backups.end() && ptr->second != -1)
        {
            return name + " (" + std::to_string(ptr->second) + ")";
        }

        return name;
    };

    auto mask_with_latency_when_fit_vec = [&](const std::vector < std::string > & name)->std::vector < std::string >
    {
        std::vector < std::string > ret;
        std::ranges::for_each(name, [&](const std::string & name_){ ret.push_back(mask_with_latency_when_fit(name_)); });
        return ret;
    };

    if (index_to_proxy_name_list.empty())
    {
        for (const auto & [group, proxy] : proxy_list) {
            groups[mask_with_latency_when_fit(group)] = mask_with_latency_when_fit_vec(proxy.first);
        }
    }
    else
    {
        auto get_index_by_name = [&](const std::string & name)
        {
            std::string suffix;
            if (const auto ptr = latency_backups.find(name);
                ptr != latency_backups.end() && ptr->second != -1)
            {
                suffix = " (" + std::to_string(ptr->second) + ")";
            }

            for (const auto & [index, proxy] : index_to_proxy_name_list) {
                if (proxy == name) return std::to_string(index) + ": " + proxy + suffix;
            }

            throw std::logic_error("Unknown name");
        };

        auto get_index_by_name_vec = [&](const std::vector < std::string > & name_list)
        {
            std::vector < std::string > proxy_list_ret;
            std::ranges::for_each(name_list, [&](const std::string & name) {
                proxy_list_ret.push_back(get_index_by_name(name));
            });
            return proxy_list_ret;
        };

        for (const auto & [group, proxy] : proxy_list) {
            groups[get_index_by_name(group)] = get_index_by_name_vec(proxy.first);
        }
    }

    std::lock_guard lock(arg2_additional_verbs_mutex);
    g_proxy_list = groups;
}

void ccdb::ccdb::nload(
    const std::atomic<uint64_t> *total_upload, const std::atomic<uint64_t> *total_download,
    const std::atomic<uint64_t> *upload_speed, const std::atomic<uint64_t> *download_speed,
    const std::atomic_bool *running,
    std::vector<std::string> &top_3_connections_using_most_speed,
    std::mutex *top_3_connections_using_most_speed_mtx)
{
    pthread_setname_np(pthread_self(), "nload");
    constexpr int reserved_lines = 4 + 3;
    int row = 0, col = 0;
    int window_space = 0;
    auto update_window_spaces = [&row, &col, &window_space]() {
        const auto [ r, c ] = utils::get_screen_row_col();
        row = r;
        col = c;
        window_space = (row - reserved_lines) / 2;
    };

    constexpr char l_1_to_40 = '.';
    constexpr char l_41_to_80 = '|';
    constexpr char l_81_to_100 = '#';

    auto generate_from_metric = [](const std::vector <float> & list, const int height)->std::vector < std::pair < int, int > >
    {
        std::vector <float> image;
        std::ranges::for_each(list, [&](const float f)
        {
            image.push_back(f * static_cast<float>(height));
        });

        std::vector < std::pair < int, int > > meter_list;
        for (const auto meter : image)
        {
            const int full_blocks = static_cast<int>(meter);
            const int partial_block_percentage = static_cast<int>((meter - static_cast<float>(full_blocks)) * 100);
            meter_list.emplace_back(full_blocks, partial_block_percentage);
        }

        return meter_list;
    };

    auto auto_clear = [](std::vector<uint64_t> & list, const uint64_t size)
    {
        std::ranges::reverse(list);
        while (list.size() > size) {
            list.pop_back();
        }
        std::ranges::reverse(list);
    };

    auto max_in_vec = [](const std::vector<uint64_t> & list_)->uint64_t
    {
        if (list_.empty()) return 0;
        std::vector<uint64_t> list = list_;
        std::ranges::sort(list, [](const uint64_t a, const uint64_t b) { return a > b; });
        const uint64_t max_val = list.front();
        return max_val;
    };

    auto min_in_vec = [](const std::vector<uint64_t> & list_)->uint64_t
    {
        if (list_.empty()) return 0;
        std::vector<uint64_t> list = list_;
        std::ranges::sort(list, [](const uint64_t a, const uint64_t b) { return a < b; });
        const uint64_t min_val = list.front();
        return min_val;
    };

    auto avg_in_vec = [](const std::vector<uint64_t> & list)
    {
        uint64_t sum = 0;
        std::ranges::for_each(list, [&](const uint64_t i)
        {
            sum += i;
        });

        return static_cast<double>(sum) / static_cast<double>(list.size());
    };

    int info_space_size = 20;
    auto print_win = [&max_in_vec, &min_in_vec, &avg_in_vec, &info_space_size, &col](
        const std::atomic<uint64_t> * speed,
        const std::atomic<uint64_t> * total,
        const std::vector<uint64_t> & list,
        uint64_t & max_speed_out_of_loop, uint64_t & min_speed_out_of_loop,
        const decltype(generate_from_metric({}, 0)) & metric_list,
        const std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point,
        const uint64_t total_bytes_since_started,
        const uint64_t windows_space_local)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto time_escalated = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_point).count();
        const auto min_speed = min_in_vec(list);
        const auto max_speed = max_in_vec(list);
        max_speed_out_of_loop = std::max(max_speed, max_speed_out_of_loop);
        min_speed_out_of_loop = std::min(min_speed, min_speed_out_of_loop);
        std::vector < std::string > info_list;
        const auto time_escalated_seconds = (static_cast<double>(time_escalated) / 1000.00f);
        const auto avg_speed_overall = time_escalated_seconds > 1.00 ? static_cast<double>(total_bytes_since_started) / time_escalated_seconds : 0.00;
        const auto min_speed_on_page_str = utils::value_to_speed(min_speed);
        const auto max_speed_on_page_str = utils::value_to_speed(max_speed);
        // const auto min_speed_overall_str = value_to_speed(min_speed_out_of_loop);
        const auto max_speed_overall_str = utils::value_to_speed(max_speed_out_of_loop);
        const auto avg_speed_on_page_str = utils::value_to_speed(static_cast<uint64_t>(avg_in_vec(list)));
        const auto avg_speed_overall_str = utils::value_to_speed(static_cast<long>(avg_speed_overall));
        const auto max_pre_slash_content_len = max_in_vec({
            // min_speed_on_page_str.length(),
            max_speed_on_page_str.length(),
            avg_speed_on_page_str.length()
        });

        auto generate_padding = [&max_pre_slash_content_len](const std::string & str)->std::string {
            return str + std::string(max_pre_slash_content_len - str.length(), ' ');
        };

        info_list.push_back(std::string("    Cur (P): ") + utils::value_to_speed(*speed));
        info_list.push_back(std::string("    Min (P): ") + min_speed_on_page_str);
        info_list.push_back(std::string("  Max (P/O): ") + generate_padding(max_speed_on_page_str) + " / " + max_speed_overall_str);
        info_list.push_back(std::string("  Avg (P/O): ") + generate_padding(avg_speed_on_page_str) + " / " + avg_speed_overall_str);
        info_list.push_back(std::string("    Ttl (O): ") + utils::value_to_size(*total));

        std::vector<uint64_t> size_list;
        for (const auto & str : info_list) {
            size_list.push_back(str.size());
        }

        info_space_size = std::max(static_cast<int>(max_in_vec(size_list)), info_space_size);
        if (col < info_space_size) {
            std::cout << color::color(0,0,0,5,0,0) << "TOO SMALL" << std::endl;
            return;
        }

        for (int i = 0; i < windows_space_local; ++i)
        {
            const int start = col - info_space_size - static_cast<int>(metric_list.size());
            const auto current_height_on_screen = windows_space_local - i; // starting from 1

            if (start < 0) {
                std::cout << std::endl; // skip
                continue;
            }

            std::cout << std::string(start, ' ');
            for (auto j = start; j < (col - info_space_size); ++j)
            {
                const auto index = j - start; // starts from 0
                const auto [full_blocks, partial_block_percentage] = metric_list[index];
                const auto actual_content_height = full_blocks + (partial_block_percentage > 0 ? 1 : 0);
                if (actual_content_height == current_height_on_screen) // see partial
                {
                    if (1 <= partial_block_percentage && partial_block_percentage <= 40) {
                        std::cout << l_1_to_40;
                    } else if (41 <= partial_block_percentage && partial_block_percentage <= 80) {
                        std::cout << l_41_to_80;
                    } else if (81 <= partial_block_percentage && partial_block_percentage <= 100) {
                        std::cout << l_81_to_100;
                    } else if (partial_block_percentage == 0 && full_blocks == windows_space_local) {
                        std::cout << l_81_to_100;
                    } else {
                        std::cout << " ";
                    }
                }
                else if (current_height_on_screen < actual_content_height) {
                    std::cout << "#";
                } else {
                    std::cout << " ";
                }

                std::cout << std::flush;
            }

            if (current_height_on_screen <= info_list.size())
            {
                const auto index = info_list.size() - current_height_on_screen;
                std::cout << info_list[index];
            }

            std::cout << std::endl;
        }
    };

    update_window_spaces();
    std::vector < uint64_t > up_speed_list, down_speed_list;
    std::vector<float> up_list, down_list;
    uint64_t max_up_speed = 0, min_up_speed = UINT64_MAX, max_down_speed = 0, min_down_speed = UINT64_MAX;
    for (int i = 0; i < 250; i++) {
        if (*total_upload && *total_download) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10l));
    }
    const uint64_t upload_total_bytes_when_started = *total_upload, download_total_bytes_when_started = *total_download;
    const auto now = std::chrono::high_resolution_clock::now();
    while (*running)
    {
        const int free_space = row - window_space * 2 - reserved_lines;
        if (window_space > reserved_lines && col > info_space_size)
        {
            up_list.clear();
            down_list.clear();

            up_speed_list.push_back(*upload_speed);
            down_speed_list.push_back(*download_speed);

            auto_clear(up_speed_list, col - info_space_size);
            auto_clear(down_speed_list, col - info_space_size);

            std::ranges::for_each(up_speed_list, [&](const uint64_t i) {
                const auto max_num = static_cast<float>(max_in_vec(up_speed_list));
                if (max_num != 0) {
                    const auto val = static_cast<float>(i) / max_num;
                    up_list.push_back(val);
                } else {
                    up_list.push_back(0);
                }
            });

            std::ranges::for_each(down_speed_list, [&](const uint64_t i) {
                const auto max_num = static_cast<float>(max_in_vec(down_speed_list));
                if (max_num != 0) {
                    const auto val = static_cast<float>(i) / max_num;
                    down_list.push_back(val);
                } else {
                    down_list.push_back(0);
                }
            });

            std::cout.write(utils::clear, sizeof(utils::clear)); // clear the screen
            std::string title = "C++ Clash Dashboard:";
            if (title.length() > col) title = title.substr(0, col);
            std::cout << title << std::endl;
            std::cout << color::color(5,3,3) << std::string(col, '=') << color::no_color() << std::endl;
            std::cout << "Incoming:" << std::endl;
            {
                std::cout << color::color(0,5,1);
                const auto metric_list = generate_from_metric(down_list, window_space);
                const auto total_download_since_start = *total_download - download_total_bytes_when_started;
                print_win(download_speed,
                          total_download,
                          down_speed_list,
                          max_down_speed,
                          min_down_speed,
                          metric_list,
                          now,
                          total_download_since_start,
                          window_space);
            }
            std::cout << color::no_color();
            std::cout << "Outgoing:" << std::endl;
            {
                std::cout << color::color(5,1,0);
                const auto height = window_space - (free_space == 0 ? 1 : 0);
                const auto metric_list = generate_from_metric(up_list, height);
                const auto total_upload_since_start = *total_upload - upload_total_bytes_when_started;
                print_win(upload_speed,
                          total_upload,
                          up_speed_list,
                          max_up_speed,
                          min_up_speed,
                          metric_list,
                          now,
                          total_upload_since_start,
                          height);
            }
            std::cout << color::no_color();

            {
                std::lock_guard<std::mutex> lock_gud(*top_3_connections_using_most_speed_mtx);
                std::ranges::for_each(top_3_connections_using_most_speed, [&](const std::string & line)
                {
                    auto new_line = line;
                    if (utils::UnicodeDisplayWidth::get_width_utf8(line) > col)
                    {
                        auto utf32 = utils::utf8_to_u32(line);
                        decltype(utf32) utf32_cut;
                        int len = 0;
                        for (const auto & c : utf32)
                        {
                            len += utils::UnicodeDisplayWidth::get_width_utf32({c});
                            if (len >= (col - 1)) {
                                break;
                            }

                            utf32_cut += c;
                        }

                        new_line = utf8::utf32to8(utf32_cut) + color::color(0,0,0,3,3,3) + ">";
                    }
                    utils::replace_all(new_line, "UP:", color::color(3,3,2) + "UP:");
                    utils::replace_all(new_line, "DL:", color::color(2,3,3) + "DL:");
                    std::cout << color::color(3,3,3) << new_line << color::no_color() << std::endl;
                });
            }

            if (const auto msg = "* P: On this page, O: Overall"; col >= static_cast<int>(strlen(msg)))
            {
                std::cout << color::color(5,5,5, 0,0,5)
                        << msg << std::string(col - strlen(msg), ' ')
                        << color::no_color() << std::flush;
            }
        }
        else
        {
            std::cout.write(utils::clear, sizeof(utils::clear));
            std::cout << color::color(0,0,0,5,0,0) << "TOO SMALL" << color::no_color() << std::endl;
        }

        for (int i = 0; i < 500; i++)
        {
            if (window_size_change) {
                window_size_change = false;
                break;
            }

            if (!*running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1l));
        }

        update_window_spaces();
    }
}

void ccdb::ccdb::pager(const std::string &str, const bool override_less_check, bool use_pager)
{
    if (!override_less_check) {
        use_pager = is_less_available();
    }

    if (use_pager)
    {
        auto pager = utils::getenv("PAGER");
        if (pager.empty()) {
            pager = R"(less -SR -S --rscroll='>')";
        }

        if (const auto [fd_stdout, fd_stderr, exit_status]
                    = exec_command("/bin/sh", str, "-c", pager);
            exit_status != 0)
        {
            std::cerr << fd_stderr << std::endl;
            std::cerr << "(less exited with code " << exit_status << ")" << std::endl;
        }
    }
    else
    {
        std::cout << str << std::flush;
    }
}

void ccdb::ccdb::print_table(
    std::vector<std::string> const &table_keys,
    std::vector<std::vector<std::string>> const &table_values,
    bool muff_non_ascii,
    bool seperator,
    const std::vector<bool> &table_hide,
    uint64_t leading_offset,
    std::atomic_int *max_tailing_size_ptr,
    bool using_less,
    const std::string &additional_info_before_table,
    int skip_lines,
    std::atomic_int *max_skip_lines_ptr,
    const bool enforce_no_pager)
{
    const auto col = utils::get_col_size();

    if (utils::get_line_size() < 9) {
        std::cout << color::color(0,0,0,5,0,0) << "Terminal Size Too Small" << color::no_color() << std::endl;
        return;
    }

    std::map < std::string /* table keys */, uint32_t /* longest value in this column */ > size_map;
    for (const auto & key : table_keys) {
        size_map[key] = key.length();
    }

    auto get_string_screen_length = [](const std::string & str)->int
    {
        const auto u32 = utils::utf8_to_u32(str);
        return utils::UnicodeDisplayWidth::get_width_utf32(u32);
    };

    auto get_string_screen_length_u32 = [](const std::u32string & str)->int {
        return utils::UnicodeDisplayWidth::get_width_utf32(str);
    };

    for (const auto & vals : table_values)
    {
        if (vals.size() != table_keys.size()) return;
        int index = 0;
        for (const auto & val : vals)
        {
            if (const auto & current_key = table_keys[index++];
                size_map[current_key] < get_string_screen_length(val))
            {
                size_map[current_key] = get_string_screen_length(val);
            }
        }
    }

    std::stringstream header;
    std::stringstream ss;
    {
        int index = 0;
        for (const auto & key : table_keys)
        {
            if (!table_hide.empty() && table_hide.size() == table_keys.size() && table_hide[index])
            {
                index++;
                continue;
            }

            {
                const int paddings = static_cast<int>(size_map[key] - get_string_screen_length(key)) + 2;
                const int before = std::max(paddings / 2, 1);
                const int after = std::max(paddings - before, 1);
                ss << "|" << std::string(before, ' ') << key << std::string(after, ' ');
            }

            {
                std::string index_str = std::to_string(index);
                const int paddings = static_cast<int>(size_map[key] - get_string_screen_length(index_str)) + 2;
                const int before = std::max(paddings / 2, 1);
                const int after = std::max(paddings - before, 1);
                header << "|" << std::string(before, ' ') << index_str << std::string(after, ' ');
            }
            index ++;
        }
    }
    ss << "|";
    header << "|";
    const std::string title_line = ss.str();
    const std::string header_line = header.str();
    std::string separation_line;
    if (title_line.size() > 2)
    {
        std::stringstream ss_sep;
        ss_sep << "+" << std::string(title_line.size() - 2, '-') << "+";
        separation_line = ss_sep.str();
    }

    auto max_tailing_size = separation_line.size() > col ? (separation_line.size() - col) : 0;
    if (max_tailing_size_ptr) *max_tailing_size_ptr = static_cast<int>(max_tailing_size);
    leading_offset = std::min(static_cast<decltype(separation_line.size())>(leading_offset), max_tailing_size);
    std::stringstream less_output_redirect;
    int printed_lines = 0;
    auto print_line = [&](const std::string& line_, const std::string & color = "")->void
    {
        auto line = utils::utf8_to_u32(line_);
        if (max_tailing_size_ptr && !using_less && !enforce_no_pager)
        {
            if (leading_offset != 0)
            {
                const auto p_leading_offset = leading_offset + 1;
                int leads = 0;
                while (!line.empty())
                {
                    leads += utils::UnicodeDisplayWidth::get_width_utf32({line.front()});
                    if (leads > p_leading_offset) {
                        break;
                    }

                    line.erase(line.begin());
                }

                // add padding
                if (leads < p_leading_offset) {
                    line = utils::utf8_to_u32(std::string(p_leading_offset - leads, ' ')) + line;
                }

                line = utils::utf8_to_u32("<") + line; // add color code here will mess up formation bc color codes occupies no spaces on screen
            }

            int total_size = get_string_screen_length_u32(line);
            if (total_size > col)
            {
                if (col > 1)
                {
                    int p_size = 0, ap_size = 0;
                    int offset = 0;
                    for (const auto & c : line)
                    {
                        p_size += utils::UnicodeDisplayWidth::get_width_utf32({c});
                        if (p_size > (col - 1)) {
                            break;
                        }

                        offset++;
                        ap_size = p_size;
                    }

                    std::string padding;
                    if (ap_size < (col - 1)) {
                        padding = std::string((col - 1) - ap_size, ' ');
                    }

                    line = line.substr(0, offset) + utils::utf8_to_u32(padding) +
                           utils::utf8_to_u32(color::bg_color(5,5,5) + color::color(0,0,0) + ">" + color::no_color());
                }
                else
                {
                    line = line.substr(0, col);
                }
            }
        }

        if (using_less || enforce_no_pager) {
            less_output_redirect << color << line_ << color::no_color() << std::endl;
        } else {
            std::string utf8_str;
            utf8::utf32to8(line.begin(), line.end(), std::back_inserter(utf8_str));
            if (!utf8_str.empty() && utf8_str.front() == '<') // add color code for '<' at the beginning
            {
                utf8_str.erase(utf8_str.begin());
                utf8_str = color::bg_color(5,5,5) + color::color(0,0,0) + "<" + color::no_color() + color + utf8_str;
            } else {
                utf8_str = color + utf8_str;
            }
            std::cout << utf8_str << color::no_color() << std::endl;
            printed_lines++;
        }
    };

    if (!additional_info_before_table.empty())
    {
        printed_lines++;
        if (using_less) {
            less_output_redirect << additional_info_before_table << std::endl;
        } else {
            std::cout << additional_info_before_table << std::endl;
        }
    }

    print_line(separation_line);
    print_line(header_line);
    print_line(separation_line);
    print_line(title_line, color::color(5,5,5));
    print_line(separation_line);

    const int max_skip_lines = std::max(static_cast<int>(table_values.size()) - (utils::get_line_size() - 2 - printed_lines), 0);
    if (max_skip_lines_ptr) *max_skip_lines_ptr = max_skip_lines;
    if (skip_lines > max_skip_lines) skip_lines = max_skip_lines;
    int i = 0;

    auto print_progress = [&]
    {
        std::cout << color::bg_color(5,5,5) << color::color(5,0,0) << skip_lines
                << color::color(3,3,3) << "/" << color::color(0,0,5) << i
                << color::color(3,3,3) << "/" << color::color(5,0,5) << table_values.size()
                << color::color(3,3,3) << "/" << color::color(0,0,0) << std::fixed << std::setprecision(2)
                << (static_cast<double>(i) / static_cast<double>(table_values.size())) * 100 << "%"
                << color::no_color() << std::endl;
    };

    for (const auto & vals : table_values)
    {
        if (!using_less)
        {
            // skip n elements
            if (i < skip_lines)
            {
                i++;
                continue;
            }

            // last element on screen
            if (i > skip_lines && printed_lines >= (utils::get_line_size() - 2))
            {
                print_progress();
                return;
            }
        }

        i++;
        std::string color_line;
        if (i & 0x01) color_line = color::color(0,5,5);
        int index = 0;
        std::stringstream val_line_stream;
        for (const auto & val : vals)
        {
            if (!table_hide.empty() && table_hide.size() == table_keys.size() && table_hide[index])
            {
                index++;
                continue;
            }

            const auto & current_key = table_keys[index++];
            const int paddings = static_cast<int>(size_map[current_key] - get_string_screen_length(val)) + 2;
            constexpr int before = 1;
            const int after = std::max(paddings - before, 1);
            val_line_stream << (seperator ? "|" : " ") << std::string(before, ' ');
            std::string output;
            output = val;
            if (muff_non_ascii) {
                for (auto & c : output) {
                    if (!std::isprint(c)) c = '#';
                }
            }

            val_line_stream << output << std::string(after, ' ');
        }

        if (seperator) {
            val_line_stream << "|";
        }
        print_line(val_line_stream.str(), color_line);
    }

    if (skip_lines == 0) {
        print_line(separation_line);
    } else {
        print_progress();
    }

    const auto output = less_output_redirect.str();
    pager(output, true, using_less);
}

void ccdb::ccdb::nload()
{
    backend_instance.change_focus("overview");
    std::atomic<uint64_t> total_up = 0, total_down = 0, up_speed = 0, down_speed = 0;
    std::atomic_bool running = true;
    sysint_pressed = false;
    std::mutex lock;
    std::vector<std::string> top_3_conn;

    std::thread Worker([&] {
        nload(
            &total_up,
            &total_down,
            &up_speed,
            &down_speed,
            &running,
            std::ref(top_3_conn),
            &lock);
    });

    std::thread input_watcher([&]
    {
        std::cout << "\033[?25l";
        pthread_setname_np(pthread_self(), "get/nload:input");
        set_conio_terminal_mode();
        int ch;
        while (((ch = getchar()) != EOF) && !sysint_pressed)
        {
            if (ch == 'q' || ch == 'Q')
            {
                running = false;
                break;
            }
        }
        reset_terminal_mode();
        std::cout << "\033[?25h";
    });

    while (running && !sysint_pressed)
    {
        total_up = backend_instance.get_total_uploaded_bytes();
        total_down = backend_instance.get_total_downloaded_bytes();
        up_speed = backend_instance.get_current_upload_speed();
        down_speed = backend_instance.get_current_download_speed();
        auto conn = backend_instance.get_active_connections();
        std::ranges::sort(conn, [](const general_info_pulling::connection_t & a,
            const general_info_pulling::connection_t & b)->bool
        {
            return (a.downloadSpeed + a.uploadSpeed) > (b.downloadSpeed + b.uploadSpeed);
        });

        if (conn.size() > 3) {
            conn.resize(3);
        }

        int max_host_len = 0;
        int max_upload_len = 0;
        std::ranges::for_each(conn, [&](general_info_pulling::connection_t & c)
        {
            c.host = c.host + " " + (c.chainName == "DIRECT" ? "- " : "x ");
            if (max_host_len < UnicodeDisplayWidth::get_width_utf8(c.host)) {
                max_host_len = UnicodeDisplayWidth::get_width_utf8(c.host);
            }

            const auto str = value_to_speed(c.uploadSpeed);
            if (max_upload_len < str.length())
            {
                max_upload_len = static_cast<int>(str.length());
            }

            c.chainName = str; // temp save
        });

        std::vector<std::string> conn_str;
        std::ranges::for_each(conn, [&](const general_info_pulling::connection_t & c)
        {
            const std::string padding(max_host_len - UnicodeDisplayWidth::get_width_utf8(c.host), ' ');
            std::stringstream ss;
            ss  << c.host << padding
                << " UP: " << c.chainName // already up speed by temp save
                << std::string(max_upload_len - c.chainName.length(), ' ')
                << " DL: " << value_to_speed(c.downloadSpeed);
            conn_str.push_back(ss.str());
        });

        {
            std::lock_guard<std::mutex> lock_gud(lock);
            top_3_conn = conn_str;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500l));
    }

    sysint_pressed = true;
    running = false;
    if (Worker.joinable()) Worker.join();
    if (input_watcher.joinable()) input_watcher.join();
    sysint_pressed = false;
}

void ccdb::ccdb::get_connections(const std::vector<std::string>& command_vector)
{
    leading_spaces = 0;
    std::atomic_int max_leading_spaces = get_col_size() / 4;
    backend_instance.change_focus("overview");
    std::atomic_int max_skip_lines = 0;
    std::atomic_int current_skip_lines = 0;
    std::thread input_getc_worker;
    bool use_input = true;
    auto input_worker = [&]
    {
        std::cout << "\033[?25l";
        pthread_setname_np(pthread_self(), "get/conn:input");
        set_conio_terminal_mode();
        std::vector <int> ch_list;
        int ch;
        while (((ch = getchar()) != EOF) && !sysint_pressed)
        {
            const auto [row, col] = get_screen_row_col();
            const auto row_step = std::max(row / 8, 1);
            const auto col_step = std::max(col / 8, 1);
            const auto page_size = std::max(row - 8 /* list headers, etc. */, 1);
            if (ch == 'q' || ch == 'Q')
            {
                sysint_pressed = true;
                break;
            }

            ch_list.push_back(ch);

            if (!ch_list.empty() && ch_list.front() != 27) {
                while (!ch_list.empty() && ch_list.front() != 27) ch_list.erase(ch_list.begin()); // remove wrong paddings
            }

            if (ch_list.size() >= 3 && ch_list[0] == 27 && ch_list[1] == 91)
            {
                if (ch_list.size() == 3)
                {
                    switch (ch_list[2])
                    {
                        case 68: // left arrow
                            if (leading_spaces > 0)
                            {
                                if (leading_spaces > col_step) {
                                    leading_spaces -= col_step;
                                } else {
                                    leading_spaces = 0;
                                }
                            }

                            break;
                        case 67: // right arrow
                            if (leading_spaces < max_leading_spaces) {
                                if ((leading_spaces + col_step) < max_leading_spaces)
                                {
                                    leading_spaces += col_step;
                                } else {
                                    leading_spaces = max_leading_spaces.load();
                                }
                            }

                            break;
                        case 66: // down arrow
                            if (current_skip_lines < max_skip_lines) {
                                if ((current_skip_lines + row_step) < max_skip_lines)
                                {
                                    current_skip_lines += row_step;
                                } else {
                                    current_skip_lines = max_skip_lines.load();
                                }
                            }

                            break;
                        case 65: // up arrow
                            if (current_skip_lines > 0)
                            {
                                if (current_skip_lines > row_step) {
                                    current_skip_lines -= row_step;
                                } else {
                                    current_skip_lines = 0;
                                }
                            }

                            break;
                        case 'H': // Home
                            leading_spaces = 0;
                            ch_list.clear();
                            break;
                        case 'F': // End
                            leading_spaces = max_leading_spaces.load();
                            ch_list.clear();
                            break;

                        default:
                            continue;
                    }

                    ch_list.clear();
                }
                else if (ch_list.size() == 4 && ch_list[3] == 0x7E)
                {
                    switch (ch_list[2])
                    {
                        case '5': // Page up
                            current_skip_lines -= page_size;
                            if (current_skip_lines < 0) {
                                current_skip_lines = 0;
                            }
                            break;
                        case '6': // Page down
                            current_skip_lines += page_size;
                            if (current_skip_lines > max_skip_lines) {
                                current_skip_lines = max_skip_lines.load();
                            }
                            break;
                        default:
                            break;
                    }
                    ch_list.clear();
                }
            }
        }
        reset_terminal_mode();
        std::cout << "\033[?25h";
    };

    std::vector < bool > do_col_hide;
    do_col_hide.resize(titles.size(), false);
    if (command_vector.size() == 4)
    {
        if (command_vector[2] == "hide")
        {
            std::string numeric_expression = command_vector[3], str_num;
            std::vector<int> numeric_values;
            std::istringstream numeric_stream(numeric_expression);
            while (std::getline(numeric_stream, str_num, ','))
            {
                try {
                    if (str_num.find('-') == std::string::npos)
                    {
                        numeric_values.push_back(std::stoi(str_num));
                    }
                    else
                    {
                        std::string start = str_num.substr(0, str_num.find('-'));
                        std::string stop = str_num.substr(str_num.find('-') + 1);
                        int begin = std::stoi(start);
                        int end = std::stoi(stop);
                        for (int i = begin; i <= end; i++) {
                            numeric_values.push_back(i);
                        }
                    }
                } catch (...) {
                }
            }

            for (const auto & i : numeric_values)
            {
                if (do_col_hide.size() > i) {
                    do_col_hide[i] = true;
                }
            }
        }
    } else if (command_vector.size() == 3 && command_vector[2] == "shot") {
        use_input = false;
    }

    if (use_input) {
        input_getc_worker = std::thread(input_worker);
    }

    while (!sysint_pressed)
    {
        auto connections = backend_instance.get_active_connections();
        std::vector<std::vector<std::string>> table_vals;
        std::ranges::sort(connections,
                          [&](const general_info_pulling::connection_t & a, const general_info_pulling::connection_t & b)
                          {
                              switch (sort_by)
                              {
                              case 0:
                                      return a.host > b.host;
                                  case 1:
                                      return a.processName > b.processName;
                                  case 2:
                                      return a.totalDownloadedBytes > b.totalDownloadedBytes;
                                  case 3:
                                      return a.totalUploadedBytes > b.totalUploadedBytes;
                                  case 5:
                                      return a.uploadSpeed > b.uploadSpeed;
                                  case 6:
                                      return a.ruleName > b.ruleName;
                                  case 7:
                                      return a.timeElapsedSinceConnectionEstablished > b.timeElapsedSinceConnectionEstablished;
                                  case 8:
                                      return a.src > b.src;
                                  case 9:
                                      return a.destination > b.destination;
                                  case 10:
                                      return a.networkType > b.networkType;
                                  case 11:
                                      return a.chainName > b.chainName;
                                  case 4:
                                  default:
                                      return a.downloadSpeed > b.downloadSpeed;
                                  }
                              });
        if (reverse) std::ranges::reverse(connections);
        for (const auto & connection : connections)
        {
            table_vals.push_back({
                connection.host,
                connection.processName,
                value_to_size(connection.totalDownloadedBytes),
                value_to_size(connection.totalUploadedBytes),
                value_to_speed(connection.downloadSpeed),
                value_to_speed(connection.uploadSpeed),
                connection.ruleName,
                second_to_human_readable(connection.timeElapsedSinceConnectionEstablished),
                connection.src,
                connection.destination,
                connection.networkType,
                connection.chainName,
            });
        }

        std::stringstream ss;
        int ss_printed_size = 0;
        int skipped_size = 0;
        std::cout.write(clear, sizeof(clear)); // clear the screen
        const int col = get_col_size();
        bool did_i_add_no_color = false;
        auto append_msg = [&](std::string msg,
            const std::string & color = "", const std::string & color_end = "")->void
        {
            if (use_input)
            {
                if (leading_spaces != 0)
                {
                    if ((msg.size() + skipped_size) < leading_spaces) {
                        skipped_size += static_cast<int>(msg.size());
                        return; // skip messages
                    }

                    if (skipped_size < leading_spaces) {
                        msg = msg.substr(leading_spaces - skipped_size);
                        ss << color::color(0,0,0,5,5,5) << "<" << color::no_color();
                        skipped_size = leading_spaces;
                        ss_printed_size = leading_spaces - skipped_size + 1;
                    }
                }

                if (ss_printed_size >= col)
                {
                    if (!did_i_add_no_color)
                    {
                        ss << color::no_color();
                        did_i_add_no_color = true;
                    }

                    return;
                }

                if ((ss_printed_size + static_cast<int>(msg.size())) >= col && !msg.empty())
                {
                    msg = msg.substr(0, std::max(col - ss_printed_size - 1, 0));
                    if (msg.empty()) return;
                    ss_printed_size += static_cast<int>(msg.size()) + 1 /* ">" */;
                    msg += color::color(0,0,0,5,5,5) + ">";
                    ss << color << msg << color::no_color();
                    did_i_add_no_color = true;
                } else {
                    ss_printed_size += static_cast<int>(msg.size());
                    ss << color << msg << color_end;
                    did_i_add_no_color = !color_end.empty();
                }
            }
            else
            {
                ss << color << msg << color_end;
            }
        };

        append_msg("Total uploads: " + value_to_size(backend_instance.get_total_uploaded_bytes()), color::color(5,5,0));
        append_msg("   ");
        append_msg("Upload speed: " + value_to_speed(backend_instance.get_current_upload_speed()),
            color::color(5,5,5,0,0,5), color::no_color());
        append_msg("   ");
        append_msg("Total downloads " + value_to_size(backend_instance.get_total_downloaded_bytes()), color::color(0,5,5));
        append_msg("   ");
        append_msg("Download speed: " + value_to_speed(backend_instance.get_current_download_speed()),
            color::color(5,5,5,0,0,5), color::no_color());

        const auto title_line = ss.str();

        if (use_input)
        {
            print_table(titles,
                table_vals,
                false,
                true,
                do_col_hide,
                leading_spaces,
                &max_leading_spaces,
                false,
                title_line,
                current_skip_lines,
                &max_skip_lines);
            int local_leading_spaces = leading_spaces;
            int local_skip_lines = current_skip_lines;
            for (int i = 0; i < 500; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1l));
                if (local_leading_spaces != leading_spaces
                    || local_skip_lines != current_skip_lines
                    || sysint_pressed || window_size_change)
                {
                    window_size_change = false;
                    break;
                }
            }

            if (leading_spaces > max_leading_spaces) {
                leading_spaces = max_leading_spaces.load();
            }

            if (current_skip_lines > max_skip_lines) {
                current_skip_lines = max_skip_lines.load();
            }
        }
        else
        {
            // print once to the pager, then quit
            print_table(titles,
                table_vals,
                false,
                true,
                { },
                0,
                nullptr,
                is_less_available(),
                ss.str());
            break;
        }
    }

    sysint_pressed = true;
    if (input_getc_worker.joinable()) input_getc_worker.join();
    sysint_pressed = false;
}

void ccdb::ccdb::get_latency()
{
    std::cout << "Testing latency with the url " << latency_url << " ..." << std::endl;
    backend_instance.update_proxy_list(); // update the proxy first
    backend_instance.latency_test(latency_url);
    auto latency_list = backend_instance.get_proxies_and_latencies_as_pair();
    std::vector < std::pair<std::string, int >> list_unordered;
    for (const auto & [proxy, latency] : latency_list.second) {
        list_unordered.emplace_back(proxy, latency);
    }

    std::vector<std::string> titles_lat = { "Latency", "Proxy" };
    std::vector<std::vector<std::string>> table_vals;
    std::vector<std::string> table_line;

    std::ranges::sort(list_unordered,
        [](const std::pair < std::string, int > & a, const std::pair < std::string, int > & b)->bool
        { return a.second < b.second; });

    for (const auto & [proxy, latency] : list_unordered)
    {
        table_line.push_back(std::to_string(latency));
        table_line.push_back(proxy);
        table_vals.emplace_back(table_line);
        table_line.clear();
    }
    latency_backups = latency_list.second;
    update_providers();
    print_table(titles_lat, table_vals, false,
        true, { }, 0, nullptr,
        is_less_available());
}

void ccdb::ccdb::get_log()
{
    backend_instance.change_focus("logs");
    std::thread input_getc_worker([&]
    {
        std::cout << "\033[?25l";
        pthread_setname_np(pthread_self(), "get/log:input");
        set_conio_terminal_mode();
        int ch;
        while (((ch = getchar()) != EOF) && !sysint_pressed)
        {
            if (ch == 'q' || ch == 'Q')
            {
                sysint_pressed = true;
                break;
            }
        }
        reset_terminal_mode();
        std::cout << "\033[?25h";
    });

    while (!sysint_pressed)
    {
        auto current_vector = backend_instance.get_logs();
        uint32_t lines = get_line_size();
        if (lines == 0) lines = 1;
        while (current_vector.size() > (lines - 1)) current_vector.erase(current_vector.begin());
        std::cout.write(clear, sizeof(clear));
        for (const auto & [level, log] : current_vector)
        {
            if (level == "INFO") {
                std::cout << color::color(2,2,2) << level << color::no_color() << ": " << log << std::endl;
            } else {
                std::cout << color::color(5,0,0) << level << color::no_color() << ": " << log << std::endl;
            }
        }

        for (int i = 0; i < 50; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10l));
            if (sysint_pressed) {
                break;
            }
        }
    }

    sysint_pressed = true;
    if (input_getc_worker.joinable()) input_getc_worker.join();
    backend_instance.change_focus("overview");
    sysint_pressed = false;
}

void ccdb::ccdb::get_proxy()
{
    auto [proxy_list, proxy_lat] = backend_instance.get_proxies_and_latencies_as_pair();
    bool is_all_uninited = true;
    for (const auto & lat : proxy_lat | std::views::values)
    {
        if (lat != -1) {
            is_all_uninited = false;
            break;
        }
    }

    // has results, then we update local backups
    if (!is_all_uninited) {
        latency_backups = proxy_lat;
    }
    // mandatory update for each pull
    backend_instance.update_proxy_list();
    update_providers();
    proxy_list = backend_instance.get_proxies_and_latencies_as_pair().first;
    std::vector<std::string> table_titles = { "Group", "Sel", "Proxy Candidates" };
    std::vector<std::vector<std::string>> table_vals;

    auto push_line = [&table_vals](const std::string & s1, const std::string & s2, const std::string & s3)
    {
        std::vector<std::string> table_line;
        table_line.emplace_back(s1);
        table_line.emplace_back(s2);
        table_line.emplace_back(s3);
        table_vals.emplace_back(table_line);
    };

    std::ranges::for_each(proxy_list, [&](const std::pair < std::string, std::pair < std::vector<std::string>, std::string> > & element)
    {
        push_line(element.first, "", "");
        std::ranges::for_each(element.second.first, [&](const std::string & proxy)
        {
            int latency = -1;
            if (latency_backups.contains(proxy)) latency = latency_backups.at(proxy);
            push_line("", proxy == element.second.second ? "*" : "",
                (proxy == element.second.second ? "=> " : "") + proxy +
                (latency == -1 ? "" : " (" + std::to_string(latency) + ")")
            );
        });
    });

    print_table(table_titles,
        table_vals,
        false,
        true,
        { },
        0,
        nullptr,
        is_less_available(),
        "",
        0,
        nullptr,
        true);
}

void ccdb::ccdb::get_vecGroupProxy()
{
    backend_instance.update_proxy_list();
    auto [proxy_list, proxy_lat] = backend_instance.get_proxies_and_latencies_as_pair();
    std::vector<std::string> table_titles = { "Vector", "Group / Endpoint" };
    std::vector<std::vector<std::string>> table_vals;

    uint64_t vector_index = 0;
    std::map < std::string, uint64_t > index_to_name_proxy_endpoint;
    std::map < std::string, uint64_t > index_to_name_group_name;
    auto push_line = [&table_vals](const std::string & s1, const std::string & s2)
    {
        std::vector<std::string> table_line;
        table_line.emplace_back(s1);
        table_line.emplace_back(s2);
        table_vals.emplace_back(table_line);
    };

    std::ranges::for_each(proxy_list, [&](const std::pair < std::string, std::pair < std::vector<std::string>, std::string> > & element)
    {
        // add group
        if (!index_to_name_group_name.contains(element.first) && !index_to_name_proxy_endpoint.contains(element.first)) {
            index_to_name_group_name.emplace(element.first, vector_index++);
        }
        std::ranges::for_each(element.second.first, [&](const std::string & proxy)
        {
            if (!index_to_name_group_name.contains(proxy) && !index_to_name_proxy_endpoint.contains(proxy)) {
                index_to_name_proxy_endpoint.emplace(proxy, vector_index++);
            }
        });
    });

    auto add_pair = [&](const std::pair<std::string, uint64_t> & pair)
    {
        index_to_proxy_name_list.emplace(pair.second, pair.first);
        push_line(std::to_string(pair.second), pair.first);
    };

    index_to_proxy_name_list.clear();
    std::ranges::for_each(index_to_name_proxy_endpoint, add_pair);
    std::ranges::for_each(index_to_name_group_name, add_pair);

    // add my shit in it
    update_providers();

    print_table(table_titles,
        table_vals,
        false,
        true,
        { },
        0,
        nullptr,
        is_less_available(),
        "",
        0,
        nullptr,
        true);
}

void ccdb::ccdb::set_mode(const std::vector<std::string> & command_vector)
{
    if (command_vector[2] != "rule" && command_vector[2] != "global" && command_vector[2] != "direct") {
        std::cerr << "Unknown mode " << command_vector[2] << std::endl;
    } else {
        backend_instance.change_proxy_mode(command_vector[2]);
    }
}

void ccdb::ccdb::set_group(const std::vector<std::string> & command_vector)
{
    const std::string & group = command_vector[2], & proxy = command_vector[3];
    std::cout << "Changing `" << group << "` proxy endpoint to `" << proxy << "`" << std::endl;
    if (!backend_instance.change_proxy_using_backend(group, proxy))
    {
        std::cerr << "Failed to change proxy endpoint to `" << proxy << "`" << std::endl;
    }
}

void ccdb::ccdb::set_vgroup(const std::vector<std::string> & command_vector)
{
    const std::string & group = command_vector[2], & proxy = command_vector[3];
    try {
        if (index_to_proxy_name_list.empty())
        {
            std::cout << "Run `get vecGroupProxy` first!" << std::endl;
            return;
        }
        const uint64_t group_vec = std::strtol(group.c_str(), nullptr, 10);
        const uint64_t proxy_vec = std::strtol(proxy.c_str(), nullptr, 10);
        const auto & group_name = index_to_proxy_name_list.at(group_vec);
        const auto & proxy_name = index_to_proxy_name_list.at(proxy_vec);
        std::cout << "Changing `" << group_name << "` proxy endpoint to `" << proxy_name << "`" << std::endl;
        if (!backend_instance.change_proxy_using_backend(group_name, proxy_name))
        {
            std::cerr << "Failed to change proxy endpoint to `" << proxy_name << "`" << std::endl;
        }
    } catch (...) {
        std::cerr << "Cannot parse vector or vector doesn't exist" << std::endl;
        return;
    }
}

void ccdb::ccdb::set_chain_parser(const std::vector<std::string> & command_vector)
{
    if (command_vector[2] == "on") backend_instance.parse_chains = true;
    else if (command_vector[2] == "off") backend_instance.parse_chains = false;
    else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
}

void ccdb::ccdb::set_sort_by(const std::vector<std::string> &command_vector)
{
    try {
        sort_by = static_cast<int>(std::strtol(command_vector[2].c_str(), nullptr, 10));
        if (sort_by < 0 || sort_by > 11)
        {
            sort_by = 4; // download speed
            throw std::invalid_argument("Invalid sort_by value");
        }
    } catch (...) {
        std::cerr << "Invalid number `" << command_vector[2] << "`" << std::endl;
    }
}

void ccdb::ccdb::set_sort_reverse(const std::vector<std::string> & command_vector)
{
    if (command_vector[2] == "on") reverse = true;
    else if (command_vector[2] == "off") reverse = false;
    else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
}

void ccdb::ccdb::close_connections() {
}

void ccdb::ccdb::reset_terminal_mode()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
}

void ccdb::ccdb::set_conio_terminal_mode()
{
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

ccdb::ccdb::~ccdb()
{
    reset_terminal_mode();
}

ccdb::ccdb::ccdb(const std::string &backend, const int port, const std::string &token, std::string latency_url_)
    : backend_instance(backend, port, token), latency_url(std::move(latency_url_))
{
    try {
        backend_instance.start_continuous_updates();
        update_providers();
        cmdTpTree::read_command([&](const std::vector<std::string> &command_vector)-> bool
        {
            if (sysint_pressed) {
                sysint_pressed = false;
            }

            if (command_vector.front() == "quit" || command_vector.front() == "exit") {
                return false;
            }

            if (command_vector.front() == "nload") {
                nload();
            } else if (command_vector.front() == "help") {
                std::cout << cmdTpTree::command_template_tree.get_help() << std::endl;
            } else if (command_vector.front() == "get" && command_vector.size() >= 2) {
                if (command_vector[1] == "connections") {
                    get_connections(command_vector);
                } else if (command_vector[1] == "latency") {
                    get_latency();
                } else if (command_vector[1] == "log") {
                    get_log();
                } else if (command_vector[1] == "mode") {
                    std::cout << backend_instance.get_current_mode() << std::endl;
                } else if (command_vector[1] == "proxy") {
                    get_proxy();
                } else if (command_vector[1] == "vecGroupProxy") {
                    get_vecGroupProxy();
                } else {
                    std::cerr << "Unknown command `" << command_vector[1] << "`" << std::endl;
                }
            }
            else if (command_vector.front() == "set")
            {
                // set mode [MODE]
                if (command_vector.size() == 3 && command_vector[1] == "mode")  {
                    set_mode(command_vector);
                }
                else if (command_vector.size() == 4 && command_vector[1] == "group") { // set group [PROXY] [ENDPOINT]
                    set_group(command_vector);
                }
                else if (command_vector.size() == 4 && command_vector[1] == "vgroup") { // set vgroup [Vec PROXY] [Vec ENDPOINT]
                    set_vgroup(command_vector);
                }
                else if (command_vector.size() == 3 && command_vector[1] == "chain_parser") { // set chain_parser on/off
                    set_chain_parser(command_vector);
                }
                else if (command_vector.size() == 3 && command_vector[1] == "sort_by") { // set sort_by [num]
                    set_sort_by(command_vector);
                }
                else if (command_vector.size() == 3 && command_vector[1] == "sort_reverse") { // set sort_reverse on/off
                    set_sort_reverse(command_vector);
                }
                else {
                    if (command_vector.size() == 2) {
                        std::cerr << "Unknown command `" << command_vector[1] << "` or invalid syntax" << std::endl;
                    } else {
                        std::cerr << "Empty command vector" << std::endl;
                    }
                }
            }
            else if (command_vector.front() == "close_connections") {
                backend_instance.close_all_connections();
            }
            else {
                std::cerr << "Unknown command `" << command_vector.front() << "` or invalid syntax" << std::endl;
            }

            return true;
        },
        [&](const std::string &)-> std::vector<std::string> { return {}; }, "ccdb> ");

        backend_instance.stop_continuous_updates();
    }
    catch (std::exception & e)
    {
        std::cerr << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception" << std::endl;
    }
}
