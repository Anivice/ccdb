#include <atomic>
#include <string>
#include <csignal>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <ranges>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "readline.h"
#include "history.h"
#include "general_info_pulling.h"
#include "exec.h"
#include "utf8.h"

std::u32string utf8_to_u32(const std::string& s)
{
    std::u32string result;
    utf8::utf8to32(s.begin(), s.end(), std::back_inserter(result));
    return result;
}

const char clear[] = { 0x1b, 0x5b, 0x48, 0x1b, 0x5b, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x4a };

volatile std::atomic_bool sysint_pressed = false;
void sigint_handler(int)
{
    const char *msg = "\r[===> KEYBOARD INTERRUPT / PRESS Enter TO CONTINUE <===]\n";
    (void)write(1, msg, strlen(msg));
    sysint_pressed = true;
}

volatile std::atomic_bool window_size_change = false;
void window_size_change_handler(int) {
    window_size_change = true;
}

/* Command vocabulary */
static const char *cmds[] = {
    "help",         // quit, exit, get, set
    "quit", "exit", //
    "get",          // latency, proxy, connections, mode, log
    "set",          // [mode|group]
    "close_connections",
    "nload",
    nullptr
};

static const char *help_voc [] = { "quit", "exit", "get", "set", "close_connections", "nload", nullptr };
static const char *get_voc  [] = { "latency", "proxy", "connections", "mode", "log", nullptr };
static const char *get_sup_voc  [] = { "hide", "shot", nullptr };
static const char *set_voc  [] = { "mode", "group", "chain_parser", "sort_by", "sort_reverse", nullptr };

#define arg_generator(name, vector)                                             \
static char * name (const char *text, int state) {                              \
    static int index, len;                                                      \
    const char *fmt;                                                            \
    if (!state) { index = 0; len = strlen(text); }                              \
    while ((fmt = (vector)[index++])) {                                         \
        if (strncmp(fmt, text, len) == 0)                                       \
            return strdup(fmt);                                                 \
    }                                                                           \
    return nullptr;                                                             \
}

/* Generator for command names */
static char *cmd_generator(const char *text, int state) {
    static int index, len;
    const char *name;
    if (!state) { index = 0; len = static_cast<int>(strlen(text)); }
    while ((name = cmds[index++])) {
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }
    return nullptr;
}

arg_generator(help_voc_generator, help_voc);
arg_generator(get_voc_generator, get_voc);
arg_generator(set_voc_generator, set_voc);
arg_generator(get_voc_sup_generator, get_sup_voc);

static int argument_index(const char *buffer, int start)
{
    int arg = 0;
    int i = 0;
    while (i < start) {
        while (buffer[i] && buffer[i] == ' ') i++; /* skip spaces */
        if (i >= start || buffer[i] == '\0') break;
        arg++;
        while (buffer[i] && buffer[i] != ' ') i++; /* skip over word */
    }
    return arg;
}

std::mutex arg2_additional_verbs_mutex;
std::vector < std::string > arg2_additional_verbs = { "" };

static char * set_arg2_verbs (const char *text, int state)
{
    std::vector < std::string > arg2_verbs = { "direct", "rule", "global", "on", "off" };
    {
        std::lock_guard lock(arg2_additional_verbs_mutex);
        arg2_verbs.insert(arg2_verbs.end(), arg2_additional_verbs.begin(), arg2_additional_verbs.end());
        arg2_verbs.emplace_back("");
    }
    static int index, len;
    const char *name;
    if (!state) { index = 0; len = static_cast<int>(strlen(text)); }
    while (((name = arg2_verbs[index++].c_str())) && strlen(name) > 0) {
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }
    return nullptr;
}

static char ** cmd_completion(const char *text, int start, int end) {
    (void)end;
    char **matches = nullptr;
    int arg = argument_index(rl_line_buffer, start);
    if (arg == 0) {
        matches = rl_completion_matches(text, cmd_generator);
    } else {
        char *buf = strdup(rl_line_buffer);
        char *cmd = strtok(buf, " ");
        if  (cmd && (strcmp(cmd, "help") == 0 || strcmp(cmd, "get") == 0))
        {
            if (arg == 1)
            {
                if (std::string(cmd) == "help") {
                    matches = rl_completion_matches(text, help_voc_generator);
                } else if (std::string(cmd) == "get") {
                    matches = rl_completion_matches(text, get_voc_generator);
                }
                rl_attempted_completion_over = 1;
            }
            else if (std::string(cmd) == "get" && arg == 2)
            {
                matches = rl_completion_matches(text, get_voc_sup_generator);
            }
            else if (arg > 1)
            {
                matches = nullptr;
                rl_attempted_completion_over = 1;
            }
        }
        else if (cmd && (strcmp(cmd, "set") == 0))
        {
            switch (arg)
            {
                case 1:
                    matches = rl_completion_matches(text, set_voc_generator);
                    rl_attempted_completion_over = 1;
                    break;
                case 2:
                case 3:
                    matches = rl_completion_matches(text, set_arg2_verbs);
                    rl_attempted_completion_over = 1;
                    break;
                default:
                    matches = nullptr;
                    rl_attempted_completion_over = 1;
                    break;
            }
        }
        else
        {
            matches = nullptr;
            rl_attempted_completion_over = 1;
        }

        free(buf);
    }
    return matches;
}

namespace color
{
    inline std::string get_env(const std::string & name)
    {
        const char * ptr = std::getenv(name.c_str());
        if (ptr == nullptr) {
            return "";
        }
        return ptr;
    }

    inline bool is_no_color()
    {
        static std::mutex mtx;
        static int is_no_color_cache = -1;

        std::lock_guard lock(mtx);
        if (is_no_color_cache != -1) {
            return is_no_color_cache;
        }

        auto color_env = get_env("COLOR");
        std::ranges::transform(color_env, color_env.begin(), ::tolower);

        if (color_env == "always")
        {
            is_no_color_cache = 0;
            return false;
        }

        const bool no_color_from_env = color_env == "never" || color_env == "none" || color_env == "off"
                            || color_env == "no" || color_env == "n" || color_env == "0" || color_env == "false";
        bool is_terminal = false;
        struct stat st{};
        if (fstat(STDOUT_FILENO, &st) == -1)
        {
            is_no_color_cache = true;
        }

        if (isatty(STDOUT_FILENO))
        {
            is_terminal = true;
        }

        is_no_color_cache = no_color_from_env || !is_terminal;
        return is_no_color_cache;
    }

    inline std::string no_color()
    {
        if (!is_no_color()) {
            return "\033[0m";
        }

        return "";
    }

    inline std::string color(int r, int g, int b)
    {
        if (is_no_color()) {
            return "";
        }

        auto constrain = [](int var, const int min, const int max)->int
        {
            var = std::max(var, min);
            var = std::min(var, max);
            return var;
        };

        r = constrain(r, 0, 5);
        g = constrain(g, 0, 5);
        b = constrain(b, 0, 5);

        const int scale = 16 + 36 * r + 6 * g + b;
        return "\033[38;5;" + std::to_string(scale) + "m";
    }

    inline std::string bg_color(int r, int g, int b)
    {
        if (is_no_color()) {
            return "";
        }

        auto constrain = [](int var, const int min, const int max)->int
        {
            var = std::max(var, min);
            var = std::min(var, max);
            return var;
        };

        r = constrain(r, 0, 5);
        g = constrain(g, 0, 5);
        b = constrain(b, 0, 5);

        const int scale = 16 + 36 * r + 6 * g + b;
        return "\033[48;5;" + std::to_string(scale) + "m";
    }

    inline std::string color(const int r, const int g, const int b, const int br, const int bg, const int bb)
    {
        if (is_no_color()) {
            return "";
        }

        return color(r, g, b) + bg_color(br, bg, bb);
    }
}

void help_overall()
{
    std::cout   << color::color(0,5,1) << "help" << color::color(5,5,5) << " [COMMAND]" << color::no_color() << std::endl
                << "    Where " << color::color(5,5,5) << "[COMMAND]" << color::no_color() << " can be:" << std::endl
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "quit\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "nload\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "exit\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "get\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "set\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "close_connections\n"
                << "  " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "ENVIRONMENT" << color::no_color() << ":\n"
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(5,5,5) << "PAGER" << color::no_color()
                << " Specify a pager command for `get proxy/latency`" << std::endl
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(5,5,5) << "NOPAGER" << color::no_color()
                << " Set NOPAGER to true to disable pagers even if they are available" << std::endl;
}

void help(const std::string & cmd_text, const std::string & description)
{
    std::cout << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
    std::cout << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << description << color::no_color() << std::endl;
}

void help_sub_cmds(const std::string & cmd_text, const std::map <std::string, std::string > & map)
{
    std::cout << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
    std::cout << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << color::no_color() << std::endl;
    int longest_subcmd_length = 0;
    for (const auto & s : map | std::views::keys) {
        if (longest_subcmd_length < s.length()) longest_subcmd_length = static_cast<int>(s.length());
    }

    for (const auto & [sub_cmd_text, des] : map)
    {
        std::cout << "            " << color::color(0,0,5) << "*" << color::no_color() << " ";
        std::cout << color::color(0,5,5) << sub_cmd_text << color::no_color() << ":" << std::string(longest_subcmd_length - sub_cmd_text.length() + 1, ' ') << des;
        std::cout << std::endl;
    }
}

std::pair < int, int > get_col_line_size()
{
    constexpr int term_col_size = 80;
    constexpr int term_row_size = 25;
    bool is_terminal = false;
    struct stat st{};
    if (fstat(STDOUT_FILENO, &st) == -1)
    {
        return {term_row_size, term_col_size};
    }

    if (isatty(STDOUT_FILENO))
    {
        is_terminal = true;
    }

    if (is_terminal)
    {
        winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || (w.ws_col | w.ws_row) == 0)
        {
            std::cerr << "Warning: failed to determine a reasonable terminal size: " << strerror(errno) << std::endl;
            return {term_row_size, term_col_size};;
        }

        return {w.ws_row, w.ws_col};
    }

    return {term_row_size, term_col_size};;
}

inline int get_col_size()
{
    return get_col_line_size().second;
}

inline int get_line_size()
{
    return get_col_line_size().first;
}

void print_table(
    std::vector<std::string> const & table_keys,
    std::vector < std::vector<std::string> > const & table_values,
    bool muff_non_ascii = true,
    bool seperator = true,
    const std::vector < bool > & table_hide = { },
    uint64_t leading_offset = 0,
    std::atomic_int * max_tailing_size_ptr = nullptr,
    bool using_less = false,
    const std::string & additional_info_before_table = "",
    int skip_lines = 0,
    std::atomic_int * max_skip_lines_ptr = nullptr,
    const bool enforce_no_pager = false // disable line shrinking, used when NOPAGER=y or pager is not available
)
{
    const auto col = get_col_size();

    if (get_line_size() < 9) {
        std::cout << color::color(0,0,0,5,0,0) << "Terminal Size Too Small" << color::no_color() << std::endl;
        return;
    }

    std::map < std::string /* table keys */, uint32_t /* longest value in this column */ > size_map;
    for (const auto & key : table_keys) {
        size_map[key] = key.length();
    }

    auto get_string_screen_length = [](const std::string & str)->int
    {
        int len = 0;
        const auto u32 = utf8_to_u32(str);
        for (const auto c : u32)
        {
            if (c <= 0xFF) {
                len++;
            } else {
                len += 2;
            }
        }

        return len;
    };

    auto get_string_screen_length_u32 = [](const std::u32string & str)->int
    {
        int len = 0;
        for (const auto c : str)
        {
            if (c <= 0xFF) {
                len++;
            } else {
                len += 2;
            }
        }

        return len;
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
        auto line = utf8_to_u32(line_);
        if (max_tailing_size_ptr && !using_less && !enforce_no_pager)
        {
            if (leading_offset != 0)
            {
                const auto p_leading_offset = leading_offset + 1;
                int leads = 0;
                while (!line.empty())
                {
                    if (line.front() <= 0xFF) {
                        leads++;
                    } else {
                        leads += 2;
                    }

                    if (leads > p_leading_offset) {
                        break;
                    }

                    line.erase(line.begin());
                }

                // add padding
                if (leads < p_leading_offset) {
                    line = utf8_to_u32(std::string(p_leading_offset - leads, ' ')) + line;
                }

                line = utf8_to_u32("<") + line; // add color code here will mess up formation bc color codes occupies no spaces on screen
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
                        if (c <= 0xFF) {
                            p_size++;
                        } else {
                            p_size += 2;
                        }

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

                    line = line.substr(0, offset) + utf8_to_u32(padding) +
                        utf8_to_u32(color::bg_color(5,5,5) + color::color(0,0,0) + ">" + color::no_color());
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

    const int max_skip_lines = std::max(static_cast<int>(table_values.size()) - (get_line_size() - 2 - printed_lines), 0);
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
            if (i > skip_lines && printed_lines >= (get_line_size() - 2))
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

    if (using_less)
    {
        const auto output = less_output_redirect.str();
        auto pager = color::get_env("PAGER");
        if (pager.empty()) {
            pager = R"(less -SR -S --rscroll='>')";
        }
        if (const auto [fd_stdout, fd_stderr, exit_status]
            = exec_command("/bin/sh", output, "-c", pager);
            exit_status != 0)
        {
            std::cerr << fd_stderr << std::endl;
            std::cerr << "(less exited with code " << exit_status << ")" << std::endl;
        }
    }
    else if (enforce_no_pager)
    {
        // pager unavailable, just print
        std::cout << less_output_redirect.str() << std::flush;
    }
}

bool is_less_available()
{
    static std::atomic_int pager_is_less_available = -1;

    if (pager_is_less_available != -1) {
        return pager_is_less_available;
    }

    if (const auto nopager = color::get_env("NOPAGER");
        nopager == "true" || nopager == "1" || nopager == "yes" || nopager == "y")
    {
        pager_is_less_available = false;
        return false;
    }

    if (const auto pager = color::get_env("PAGER"); pager.empty()) {
        const auto result_which_less = exec_command("/bin/sh", "", "-c", "which less 2>/dev/null >/dev/null");
        const auto result_whereis_less =  exec_command("/bin/sh", "", "-c", "whereis less 2>/dev/null >/dev/null");
        const auto result_less_version =  exec_command("/bin/sh", "", "-c", "less --version 2>/dev/null >/dev/null");
        pager_is_less_available = !result_less_version.exit_status || !result_whereis_less.exit_status || !result_which_less.exit_status;
        return pager_is_less_available;
    }

    pager_is_less_available = true;
    return true; // skip check if you specify a pager. fuck you for providing a faulty one
}

std::string value_to_human(const unsigned long long value,
    const std::string & lv1,
    const std::string & lv2,
    const std::string & lv3,
    const std::string & lv4,
    const std::string & lv5)
{
    std::stringstream ss;
    if (value < 1024ull) {
        ss << value << " " << lv1;
    } else if (value < 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / 1024ull) << " " << lv2;
    } else if (value < 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull)) << " " << lv3;
    } else if (value < 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull * 1024ull)) << " " << lv4;
    } else if (value < 1024ull * 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull * 1024ull * 1024ull)) << " " << lv5;
    } else {
        ss << value << " " << lv1;
    }

    return ss.str();
}

std::string value_to_speed(const unsigned long long value)
{
    return value_to_human(value, "B/s", "KB/s", "MB/s", "GB/s", "TB/s");
}

std::string value_to_size(const unsigned long long value)
{
    return value_to_human(value, "B", "KB", "MB", "GB", "TB");
}

std::string second_to_human_readable(unsigned long long value)
{
    if (value < 60) {
        return std::to_string(value) + " s";
    }

    if (value < 60 * 60)
    {
        if ((value % 60) >= 30) {
            return "Less than " + std::to_string(value / 60 + 1) + " Min";
        }

        return std::to_string(value / 60) + " Min";
    }

    if (value < 60 * 60 * 24) {
        if ((value % (60 * 60)) >= (30 * 60))
        {
            return "Less than " + std::to_string(value / (60 * 60) + 1) + " H";
        }
        return std::to_string(value / (60 * 60)) + " H";
    }

    const auto day = value / (60 * 60 * 24);
    value %= (60 * 60 * 24);
    const auto hour = value / (60 * 60);
    value %= (60 * 60);
    const auto minute = value / 60;
    const auto second = value % 60;
    return std::to_string(day) + "d" + std::to_string(minute) + "m" + std::to_string(second) + "s";
}

void nload(
    const std::atomic < uint64_t > * total_upload,
    const std::atomic < uint64_t > * total_download,
    const std::atomic < uint64_t > * upload_speed,
    const std::atomic < uint64_t > * download_speed,
    const std::atomic_bool * running)
{
    pthread_setname_np(pthread_self(), "nload");
    auto [ row, col ] = get_col_line_size();
    auto window_space = (row - 5) / 2;
    constexpr char l_1_to_40 = '.';
    constexpr char l_41_to_80 = '|';
    constexpr char l_81_to_100 = '#';

    auto generate_from_metric = [&](const std::vector <float> & list)->std::vector < std::pair < int, int > >
    {
        std::vector <float> image;
        std::ranges::for_each(list, [&](const float f)
        {
            image.push_back(f * static_cast<float>(row));
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
        while (list.size() > size) {
            list.erase(list.begin());
        }
    };

    auto max = [](const std::vector<uint64_t> & list)
    {
        uint64_t max_val = 0;
        std::ranges::for_each(list, [&](const uint64_t i)
        {
            if (i > max_val) {
                max_val = i;
            }
        });

        return max_val;
    };

    auto min = [](const std::vector<uint64_t> & list)
    {
        uint64_t min_val = UINT64_MAX;
        std::ranges::for_each(list, [&](const uint64_t i)
        {
            if (i < min_val) {
                min_val = i;
            }
        });

        return min_val;
    };

    auto avg = [](const std::vector<uint64_t> & list)
    {
        uint64_t sum = 0;
        std::ranges::for_each(list, [&](const uint64_t i)
        {
            sum += i;
        });

        return static_cast<double>(sum) / static_cast<double>(list.size());
    };

    int info_space_size = 20;
    auto print_win = [&max, &min, &avg, &info_space_size, &window_space, &col](
        const std::atomic<uint64_t> * speed,
        const std::atomic<uint64_t> * total,
        std::vector<uint64_t> & list,
        const decltype(generate_from_metric({})) & metric_list)
    {
        const auto min_speed = min(list);
        const auto max_speed = max(list);
        std::vector < std::string > info_list;
        info_list.push_back(std::string("  Cur: ") + value_to_speed(*speed));
        info_list.push_back(std::string("  Min: ") + value_to_speed(min_speed));
        info_list.push_back(std::string("  Max: ") + value_to_speed(max_speed));
        info_list.push_back(std::string("  Avg: ") + value_to_speed(static_cast<uint64_t>(avg(list))));
        info_list.push_back(std::string("  Ttl: ") + value_to_size(*total));

        std::vector<uint64_t> size_list;
        for (const auto & str : info_list) {
            size_list.push_back(str.size());
        }

        info_space_size = std::max(static_cast<int>(max(size_list)), info_space_size);

        for (int i = 0; i < window_space; ++i)
        {
            const int start = col - info_space_size - static_cast<int>(metric_list.size());
            const auto current_height_on_screen = window_space - i; // starting from 1

            if (start < 0) {
                std::cout << std::endl; // skip
                continue;
            }

            std::cout << std::string(start, ' ');
            for (auto j = start; j < (col - info_space_size); ++j)
            {
                const auto index = j - start;
                const auto [full_blocks, partial_block_percentage] = metric_list[index];
                const auto actual_content_height = full_blocks + (partial_block_percentage == 0 ? 0 : 1);
                if (actual_content_height == current_height_on_screen) // see partial
                {
                    if (1 <= partial_block_percentage && partial_block_percentage <= 40) {
                        std::cout << l_1_to_40;
                    } else if (41 <= partial_block_percentage && partial_block_percentage <= 80) {
                        std::cout << l_41_to_80;
                    } else if (81 <= partial_block_percentage && partial_block_percentage <= 100) {
                        std::cout << l_81_to_100;
                    } else {
                        std::cout << " ";
                    }
                }
                else if (actual_content_height >= 2 && (current_height_on_screen == actual_content_height - 1))
                {
                    if (1 <= partial_block_percentage && partial_block_percentage <= 40) {
                        std::cout << l_41_to_80;
                    } else if (41 <= partial_block_percentage && partial_block_percentage <= 80) {
                        std::cout << l_81_to_100;
                    } else if (81 <= partial_block_percentage && partial_block_percentage <= 100) {
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

    std::vector < uint64_t > up_speed_list, down_speed_list;
    std::vector<float> up_list, down_list;

    while (*running)
    {
        if (window_space > 5)
        {
            up_list.clear();
            down_list.clear();

            up_speed_list.push_back(*upload_speed);
            down_speed_list.push_back(*download_speed);

            auto_clear(up_speed_list, col - info_space_size);
            auto_clear(down_speed_list, col - info_space_size);

            std::ranges::for_each(up_speed_list, [&](const uint64_t i) {
                up_list.push_back(static_cast<float>(i) / static_cast<float>(max(up_speed_list)));
            });

            std::ranges::for_each(down_speed_list, [&](const uint64_t i) {
                down_list.push_back(static_cast<float>(i) / static_cast<float>(max(down_speed_list)));
            });

            std::cout.write(clear, sizeof(clear)); // clear the screen
            std::cout << "C++ Clash Dashboard:" << std::endl;
            std::cout << std::string(col, '=') << std::endl;
            std::cout << "Incoming" << std::endl;
            {
                const auto metric_list = generate_from_metric(down_list);
                print_win(download_speed, total_download, down_speed_list, metric_list);
            }
            std::cout << "Outgoing" << std::endl;
            {
                const auto metric_list = generate_from_metric(up_list);
                print_win(upload_speed, total_upload, up_speed_list, metric_list);
            }
        }

        row = get_line_size();
        col = get_col_size();
        window_space = (row - 5) / 2;
        for (int i = 0; i < 100; i++)
        {
            if (window_size_change) {
                window_size_change = false;
                break;
            }

            if (!*running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10l));
        }
    }
}

void reset_terminal_mode();
void set_conio_terminal_mode();

int main(int argc, char ** argv)
{
    std::string backend;
    int port = 0;
    std::string token;
    std::string latency_url = "https://api.epicgames.dev/";

    try
    {
        if (argc >= 3)
        {
            backend = argv[1];
            port = static_cast<int>(std::strtol(argv[2], nullptr, 10));
        }

        if (argc >= 4) {
            token = argv[3];
        }

        if (argc == 5) {
            latency_url = argv[4];
        }

        if (argc < 3 || argc > 5)
        {
            std::cout << argv[0] << " [BACKEND] [PORT] <TOKEN> <LATENCY URL>" << std::endl;
            std::cout << " [...] is required, <...> is optional." << std::endl;
            return EXIT_FAILURE;
        }
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    std::cout << "Connecting to http://" << backend << ":" << port << std::endl;
    std::cout << "C++ Clash Dashboard Version " << CCDB_VERSION << std::endl;
    ////////////////////////////////////////////////////////////////////////////////////////

    auto remove_leading_and_tailing_spaces = [](const std::string & text)->std::string
    {
        if (text.empty()) return text;
        const auto pos = text.find_first_not_of(' ');
        if (pos == std::string::npos) return text;
        std::string middle = text.substr(pos);
        while (!middle.empty() && middle.back() == ' ') {
            middle.pop_back();
        }
        return middle;
    };

    std::signal(SIGINT, sigint_handler);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGWINCH, window_size_change_handler);
    pthread_setname_np(pthread_self(), "main");

    rl_attempted_completion_function = cmd_completion;
    using_history();
    try
    {
        char * line = nullptr;
        general_info_pulling backend_instance(backend, port, token);
        int sort_by = 4;
        bool reverse = false;
        std::atomic_int leading_spaces = 0;
        const std::vector<std::string> titles = {
            "Host",         // 0
            "Process",      // 1
            "DL",           // 2
            "UP",           // 3
            "DL Speed",     // 4
            "UP Speed",     // 5
            "Rules",        // 6
            "Time",         // 7
            "Source IP",    // 8
            "Destination IP",   // 9
            "Type",         // 10
            "Chains",       // 11
        };

        auto update_providers = [&]
        {
            backend_instance.update_proxy_list();
            auto proxy_list = backend_instance.get_proxies_and_latencies_as_pair().first;
            std::vector<std::string> groups;
            std::vector<std::string> proxies;

            for (const auto & [group, proxy] : proxy_list)
            {
                if (std::ranges::find(groups, group) == groups.end())
                {
                    groups.push_back("\"" + group + "\"");
                }

                std::ranges::for_each(proxy.first, [&](const std::string & _p)
                {
                    if (std::ranges::find(proxies, _p) == proxies.end()) proxies.push_back("\"" + _p + "\"");
                });
            }

            std::lock_guard lock(arg2_additional_verbs_mutex);
            arg2_additional_verbs.clear();
            arg2_additional_verbs.insert(arg2_additional_verbs.end(), proxies.begin(), proxies.end());
            arg2_additional_verbs.insert(arg2_additional_verbs.end(), groups.begin(), groups.end());
        };

        backend_instance.start_continuous_updates();
        update_providers();

        while ((line = readline("ccdb> ")) != nullptr)
        {
            if (sysint_pressed) {
                sysint_pressed = false;
            }

            if (*line) add_history(line);
            std::vector < std::string > command_vector;
            {
                std::string cmd = line;
                cmd = remove_leading_and_tailing_spaces(cmd);
                std::string buffer;
                bool override = false;
                for (auto c : cmd)
                {
                    if (c == '"') {
                        override = !override;
                        continue;
                    }

                    if (override)
                    {
                        buffer += c;
                        continue;
                    }

                    if (c != ' ') {
                        buffer.push_back(c);
                    }
                    else if (!buffer.empty())
                    {
                        command_vector.push_back(buffer);
                        buffer.clear();
                    }
                }

                if (!buffer.empty()) {
                    command_vector.push_back(buffer);
                    buffer.clear();
                }
            }

            if (!command_vector.empty())
            {
                if (command_vector.front() == "quit" || command_vector.front() == "exit") {
                    free(line);
                    break;
                }
                else if (command_vector.front() == "nload")
                {
                    backend_instance.change_focus("overview");
                    std::atomic<uint64_t> total_up = 0, total_down = 0, up_speed = 0, down_speed = 0;
                    std::atomic_bool running = true;
                    sysint_pressed = false;

                    std::thread Worker(nload, &total_up, &total_down, &up_speed, &down_speed, &running);
                    std::thread input_watcher([&]
                    {
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
                    });

                    while (running && !sysint_pressed)
                    {
                        total_up = backend_instance.get_total_uploaded_bytes();
                        total_down = backend_instance.get_total_downloaded_bytes();
                        up_speed = backend_instance.get_current_upload_speed();
                        down_speed = backend_instance.get_current_download_speed();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500l));
                    }

                    sysint_pressed = false;
                    running = false;
                    if (Worker.joinable()) Worker.join();
                    if (input_watcher.joinable()) input_watcher.join();
                }
                else if (command_vector.front() == "help")
                {
                    if (command_vector.size() != 2) {
                        help_overall();
                    }
                    else
                    {
                        if (command_vector[1] == "quit" || command_vector[1] == "exit") {
                            help(command_vector[1], "Exit the program");
                        } else if (command_vector[1] == "nload") {
                            help(command_vector[1], "A nload-like dashboard");
                        } else if (command_vector[1] == "close_connections") {
                            help(command_vector[1], "Close all currently active connections");
                        } else if (command_vector[1] == "get") {
                            help_sub_cmds("get", {
                                { "latency", "Get all proxy latencies" },
                                { "proxy", "Get all proxies. Latency will be added if tested before" },
                                { "connections [hide <0-11>|shot]",
                                    R"(Get all active connections. Using the "hide" to hide columns, which are separated by ',', or use '-' to conjunct numbers. E.g.: 2,4-6,8. Use ^C or 'q' to stop. Use "shot" to use a pager)" },
                                { "mode", "Get current proxy mode, i.e., direct, rule or global" },
                                { "log", "Watch logs. Use Ctrl+C (^C) or press 'q' to stop watching" },
                            });
                        } else if (command_vector[1] == "set") {
                            help_sub_cmds("set", {
                                { "mode", "set mode " + color::color(5,5,5) + "[MODE]" + color::no_color() + ", where " + color::color(5,5,5) + "[MODE]" + color::no_color() + R"( can be "direct", "rule", or "global")" }, // DO NOT USE "...," use "...", instead. It's confusing
                                { "group", "set group " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " " + color::color(5,5,5) + "[PROXY]" + color::no_color() + ", where " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " is proxy group, and " + color::color(5,5,5) + "[PROXY]" + color::no_color() + " is proxy endpoint" },
                                { "chain_parser", "set chain_parser " + color::color(5,5,5) + "[on|off]" + color::no_color() + R"(, "on" means parse rule chains, and "off" means show only endpoint)" },
                                { "sort_by", "set sort_by " + color::color(5,5,5) + "[0-11]" + color::no_color() + R"(, used by get connections. Set which column to sort. Default is 4.)" },
                                { "sort_reverse", "set sort_reverse " + color::color(5,5,5) + "[on|off]" + color::no_color() + R"(, "off" means sort by default (highest to lowest), and "on" means sort by reverse (lowest to highest))" },
                            });
                        } else {
                            help_overall();
                        }
                    }
                }
                else if (command_vector.front() == "get" && command_vector.size() >= 2)
                {
                    if (command_vector[1] == "connections")
                    {
                        leading_spaces = 0;
                        std::atomic_int max_leading_spaces = get_col_size() / 4;
                        backend_instance.change_focus("overview");
                        std::atomic_int max_skip_lines = 0;
                        std::atomic_int current_skip_lines = 0;
                        std::thread input_getc_worker;
                        bool use_input = true;
                        auto input_worker = [&leading_spaces, &max_leading_spaces, &current_skip_lines, &max_skip_lines]
                        {
                            pthread_setname_np(pthread_self(), "get/conn:input");
                            set_conio_terminal_mode();
                            std::vector <int> ch_list;
                            int ch;
                            while (((ch = getchar()) != EOF) && !sysint_pressed)
                            {
                                const auto [row, col] = get_col_line_size();
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
                        };

                        std::vector < bool > do_col_hide;
                        do_col_hide.resize(titles.size(), false);
                        if (command_vector.size() == 4)
                        {
                            if (command_vector[2] == "hide")
                            {
                                std::string & numeric_expression = command_vector[3], str_num;
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
                                              [&sort_by](const general_info_pulling::connection_t & a, const general_info_pulling::connection_t & b)
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
                            bool should_i_add_leading_pager_marker = false;
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
                                            if (!msg.empty())
                                            {
                                                msg.erase(msg.begin());
                                                should_i_add_leading_pager_marker = true;
                                            }
                                            skipped_size = leading_spaces;
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

                            auto title_line = ss.str();
                            if (should_i_add_leading_pager_marker) {
                                title_line = color::color(0,0,0,5,5,5) += "<" + color::no_color() += title_line;
                            }

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
                                for (int i = 0; i < 100; i++)
                                {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(10l));
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

                        if (input_getc_worker.joinable()) input_getc_worker.join();
                    }
                    else if (command_vector[1] == "latency")
                    {
                        std::cout << "Testing latency with url " << latency_url << " ..." << std::endl;
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
                        print_table(titles_lat, table_vals, false,
                            true, { }, 0, nullptr,
                            is_less_available());
                    }
                    else if (command_vector[1] == "log")
                    {
                        backend_instance.change_focus("logs");
                        std::thread input_getc_worker([]
                        {
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

                        if (input_getc_worker.joinable()) input_getc_worker.join();
                    } else if (command_vector[1] == "mode") {
                        std::cout << backend_instance.get_current_mode() << std::endl;
                    } else if (command_vector[1] == "proxy") {
                        backend_instance.update_proxy_list();
                        update_providers();
                        auto [proxy_list, proxy_lat] = backend_instance.get_proxies_and_latencies_as_pair();
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
                                push_line("", proxy == element.second.second ? "*" : "",
                                    (proxy == element.second.second ? color::bg_color(0,0,4) + color::color(5,5,5) :
                                        color::bg_color(2,2,2) + color::color(5,5,5))
                                    + proxy + color::no_color());
                            });
                        });

                        print_table(table_titles,
                            table_vals,
                            false,
                            false,
                            { },
                            0,
                            nullptr,
                            is_less_available(),
                            "",
                            0,
                            nullptr,
                            true);
                    } else {
                        std::cerr << "Unknown command `" << command_vector[1] << "`" << std::endl;
                    }
                }
                else if (command_vector.front() == "set")
                {
                    if (command_vector[1] == "mode" && command_vector.size() == 3) // set mode [MODE]
                    {
                        if (command_vector[2] != "rule" && command_vector[2] != "global" && command_vector[2] != "direct")
                        {
                            std::cerr << "Unknown mode " << command_vector[2] << std::endl;
                        }
                        else
                        {
                            backend_instance.change_proxy_mode(command_vector[2]);
                        }
                    } else if (command_vector[1] == "group" && command_vector.size() == 4) { // set group [PROXY] [ENDPOINT]
                        const std::string & group = command_vector[2], & proxy = command_vector[3];
                        std::cout << "Changing `" << group << "` proxy endpoint to `" << proxy << "`" << std::endl;
                        if (!backend_instance.change_proxy_using_backend(group, proxy))
                        {
                            std::cerr << "Failed to change proxy endpoint to `" << proxy << "`" << std::endl;
                        }
                    } else if (command_vector[1] == "chain_parser" && command_vector.size() == 3) { // set chain_parser on/off
                        if (command_vector[2] == "on") backend_instance.parse_chains = true;
                        else if (command_vector[2] == "off") backend_instance.parse_chains = false;
                        else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
                    } else if (command_vector[1] == "sort_by" && command_vector.size() == 3) { // set sort_by [num]
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
                    } else if (command_vector[1] == "sort_reverse" && command_vector.size() == 3) { // set sort_reverse on/off
                        if (command_vector[2] == "on") reverse = true;
                        else if (command_vector[2] == "off") reverse = false;
                        else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
                    } else {
                        std::cerr << "Unknown command `" << command_vector[1] << "` or invalid syntax" << std::endl;
                    }
                }
                else if (command_vector.front() == "close_connections")
                {
                    backend_instance.close_all_connections();
                }
                else
                {
                    std::cerr << "Unknown command `" << command_vector.front() << "` or invalid syntax" << std::endl;
                }
            }

            free(line);
        }

        backend_instance.stop_continuous_updates();
    }
    catch (std::exception & e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
