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
#include <fstream>
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
    // const auto msg = "\n\033[F\033[Kccdb> ";
    // (void)write(1, msg, strlen(msg));
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
static const char *get_voc  [] = { "latency", "proxy", "connections", "mode", "log", "vecGroupProxy", nullptr };
static const char *get_sup_voc  [] = { "hide", "shot", nullptr };
static const char *set_voc  [] = { "mode", "group", "chain_parser", "sort_by", "sort_reverse", "vgroup", nullptr };

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
std::map < std::string, std::vector < std::string > > g_proxy_list;

static char * set_arg2_verbs (const char *text, int state)
{
    std::vector < std::string > arg2_verbs;
    {
        std::lock_guard lock(arg2_additional_verbs_mutex);
        arg2_verbs = arg2_additional_verbs;
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
    std::string this_arg = rl_line_buffer;
    while (!this_arg.empty() && this_arg.back() == ' ') this_arg.pop_back(); // remove tailing spaces
    while (!this_arg.empty() && this_arg.front() == ' ') this_arg.erase(this_arg.begin()); // remove leading spaces
    std::vector < std::string > args;
    {
        std::string arg;
        std::ranges::for_each(this_arg, [&](const char c) {
            if (c != ' ') {
                arg += c;
            } else {
                if (!arg.empty()) args.emplace_back(arg);
                arg.clear();
            }
        });

        if (!arg.empty()) args.emplace_back(arg);
    }

    auto sanitize_name = [](const std::string & name)->std::string
    {
        auto str = utf8_to_u32(name);
        if (const auto separator_pos = str.find_first_of(':');
            separator_pos != std::string::npos)
        {
            if (const auto before = str.substr(0, separator_pos);
                std::ranges::all_of(before, [](const int c){ return '0' <= c && c <= '9'; }))
            {
                str = str.substr(separator_pos + 1);
                while (!str.empty() && str.front() == ' ') str.erase(str.begin());
                return utf8::utf32to8(str);
            }

            return name;
        }

        return name;
    };

    auto sanitize_name_list = [&](const std::vector<std::string> & name)->std::vector<std::string>
    {
        std::vector<std::string> result;
        std::ranges::for_each(name, [&](const std::string & name_){ result.push_back(sanitize_name(name_));});
        return result;
    };

    auto get_index_from_pads = [](const std::string& str_)->int
    {
        try {
            auto str = utf8_to_u32(str_);
            const auto pos = str.find_first_of(':');
            if (pos == std::string::npos) return -1;
            str = str.substr(0, pos);
            if (str.empty()) return -1;
            const int ret = static_cast<int>(std::strtol(utf8::utf32to8(str).c_str(), nullptr, 10));
            return ret;
        } catch (...) {
            return -1;
        }
    };

    if (const int arg = argument_index(rl_line_buffer, start); arg == 0) {
        matches = rl_completion_matches(text, cmd_generator);
    }
    else
    {
        if  (const auto & cmd = args.front(); cmd == "help" || cmd == "get")
        {
            if (arg == 1)
            {
                if (cmd == "help") {
                    matches = rl_completion_matches(text, help_voc_generator);
                } else if (cmd == "get") {
                    matches = rl_completion_matches(text, get_voc_generator);
                }
                rl_attempted_completion_over = 1;
            }
            else if (cmd == "get" && arg == 2)
            {
                if (args.size() < 2) {
                    matches = nullptr;
                } else {
                    const auto & second_arg = args[1];
                    if (second_arg == "connections") {
                        matches = rl_completion_matches(text, get_voc_sup_generator);
                    } else {
                        matches = nullptr;
                    }
                }
                rl_attempted_completion_over = 1;
            }
            else if (arg > 1)
            {
                matches = nullptr;
                rl_attempted_completion_over = 1;
            }
        }
        else if (cmd == "set")
        {
            switch (arg)
            {
                case 1:
                    matches = rl_completion_matches(text, set_voc_generator);
                    rl_attempted_completion_over = 1;
                    break;
                case 2:
                    {
                        std::lock_guard lock(arg2_additional_verbs_mutex);
                        arg2_additional_verbs.clear();
                        if (args.size() < 2) {
                            matches = nullptr;
                            rl_attempted_completion_over = 1;
                            break;
                        }
                        const auto & second_arg = args[1];
                        if (second_arg == "mode") {
                            const auto & list = std::vector<std::string>{"rule", "global", "direct"};
                            arg2_additional_verbs.insert(arg2_additional_verbs.begin(), list.begin(), list.end());
                        } else if (second_arg == "sort_reverse") {
                            const auto & list = std::vector<std::string>{"on", "off"};
                            arg2_additional_verbs.insert(arg2_additional_verbs.begin(), list.begin(), list.end());
                        } else if (second_arg == "vgroup") {
                            const auto & list = g_proxy_list | std::views::keys;
                            arg2_additional_verbs.insert(arg2_additional_verbs.begin(), list.begin(), list.end());
                        } else if (second_arg == "group") {
                            const auto & list = g_proxy_list | std::views::keys;
                            const auto & processed_list = sanitize_name_list(std::vector<std::string>(list.begin(), list.end()));
                            arg2_additional_verbs.insert(arg2_additional_verbs.begin(), processed_list.begin(), processed_list.end());
                        }
                    }
                    matches = rl_completion_matches(text, set_arg2_verbs);
                    rl_attempted_completion_over = 1;
                    break;
                case 3:
                    {
                        if (args.size() < 3) {
                            matches = nullptr;
                            rl_attempted_completion_over = 1;
                            break;
                        }
                        const auto & third_arg = args[2];
                        std::lock_guard lock(arg2_additional_verbs_mutex);
                        arg2_additional_verbs.clear();
                        if (std::ranges::all_of(third_arg, [](const char c){ return '0' <= c && c <= '9'; }))
                        {
                            const auto group_index = std::strtol(third_arg.c_str(), nullptr, 10);
                            for (const auto & [group, proxy_candidates] : g_proxy_list)
                            {
                                if (group_index == get_index_from_pads(group)) {
                                    arg2_additional_verbs.clear();
                                    arg2_additional_verbs.insert(arg2_additional_verbs.begin(), proxy_candidates.begin(), proxy_candidates.end());
                                    break;
                                }
                            }
                        } else {
                            // not vector
                            for (const auto & [group, proxy_candidates] : g_proxy_list)
                            {
                                if (third_arg == sanitize_name(group))
                                {
                                    const auto & list = sanitize_name_list(proxy_candidates);
                                    arg2_additional_verbs.clear();
                                    arg2_additional_verbs.insert(arg2_additional_verbs.begin(), list.begin(), list.end());
                                    break;
                                }
                            }
                        }
                    }
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

bool is_less_available();

void pager(const std::string & str, const bool override_less_check = false, bool use_pager = true)
{
    if (!override_less_check) {
        use_pager = is_less_available();
    }

    if (use_pager)
    {
        auto pager = color::get_env("PAGER");
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

void help_overall()
{
    constexpr unsigned char alp_no_expand[] = { 0xe2, 0x9c, 0x88, 0x00 };
    constexpr unsigned char alp_expanded[] = { 0xe2, 0x9c, 0x88, 0xef, 0xb8, 0x8f, 0x00 };
    std::stringstream ss;
    ss   << color::color(0,5,1) << "help" << color::color(5,5,5) << " [COMMAND]" << color::no_color() << std::endl
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
                << " Set NOPAGER to true to disable pagers even if they are available" << std::endl
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(5,5,5) << "NO_0xFE0F_EXPAND_EMOJI" << color::no_color()
                << " Fix Unicode processing issues for emoji space expand code, e.g., \"" << reinterpret_cast<const char*>(alp_no_expand)
                << "\" and \"" << reinterpret_cast<const char*>(alp_expanded) << "\""
                << ".\n" << std::string(33, ' ') << "If you cannot notice any differences of the above emojis, you might want to set this to `true`" << std::endl
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(5,5,5) << "COLOR" << color::no_color()
                << " Set it to `never` to disable color codes" << std::endl;
    pager(ss.str());
}

void help(const std::string & cmd_text, const std::string & description)
{
    std::stringstream ss;
    ss << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
    ss << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << description << color::no_color() << std::endl;
    pager(ss.str());
}

void help_sub_cmds(const std::string & cmd_text, const std::map <std::string, std::string > & map)
{
    std::stringstream ss;
    ss << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
    ss << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << color::no_color() << std::endl;
    int longest_subcmd_length = 0;
    for (const auto & s : map | std::views::keys) {
        if (longest_subcmd_length < s.length()) longest_subcmd_length = static_cast<int>(s.length());
    }

    auto replace_all =
    [](std::string & original,
        const std::string & target,
        const std::string & replacement)
    {
        if (target.empty()) return; // Avoid infinite loop if target is empty

        if (target == " " && replacement.empty()) {
            std::erase_if(original, [](const char c) { return c == ' '; });
        }

        size_t pos = 0;
        while ((pos = original.find(target, pos)) != std::string::npos) {
            original.replace(pos, target.length(), replacement);
            pos += replacement.length(); // Move past the replacement to avoid infinite loop
        }
    };

    for (const auto & [sub_cmd_text, des] : map)
    {
        std::string processed_des = des;
        replace_all(processed_des, ". ", ".\n");
        std::vector<std::string> sentences;
        std::string sentence;
        std::ranges::for_each(processed_des, [&sentences, &sentence](const char & s)
        {
            if (s != '\n') {
                sentence += s;
            } else {
                sentences.push_back(sentence);
                sentence.clear();
            }
        });

        if (!sentence.empty()) sentences.push_back(sentence);
        const auto padding = std::string(longest_subcmd_length - sub_cmd_text.length() + 1, ' ');
        const auto pre_text = 15 + sub_cmd_text.length();
        bool right_after = true;
        ss << "            " << color::color(0,0,5) << "*" << color::no_color() << " ";
        ss  << color::color(0,5,5) << sub_cmd_text << color::no_color() << ":";
        std::ranges::for_each(sentences, [&](const std::string & s)
        {
            if (right_after) {
                ss << padding << s << std::endl;
                right_after = false;
            } else {
                ss << std::string(pre_text, ' ') << padding << s << std::endl;
            }
        });

        if (sentences.size() > 1) ss << std::endl;
    }

    pager(ss.str());
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

class initialize_locale
{
public:
    initialize_locale() noexcept {
        std::setlocale(LC_ALL, "en_US.UTF-8");
    }
} initialize_locale_;

class UnicodeDisplayWidth {
public:
    static int get_width_utf8(const std::string& utf8_str)
    {
        std::u32string utf32_str;
        utf8::utf8to32(utf8_str.begin(), utf8_str.end(), 
                      std::back_inserter(utf32_str));
        
        return get_width_utf32(utf32_str);
    }
    
    static int get_width_utf32(const std::u32string& utf32_str)
    {
        int width = 0;
        
        for (size_t i = 0; i < utf32_str.length(); i++)
        {
            const char32_t c = utf32_str[i];
            
            if (c == 0x200D || (c >= 0xFE00 && c < 0xFE0F)) {
                continue;
            }

            if (c == 0xFE0F)
            {
                // when this is printed onto screen, it means an additional color code that expand the emoji
                // this doesn't apply to all the terminals, so fucking headaches
                // you can just disable this by setting the environment variable NO_0xFE0F_EXPAND_EMOJI to true
                // if your terminal doesn't really process this flag
                if (color::get_env("NO_0xFE0F_EXPAND_EMOJI") == "true") {
                    continue;
                }
                width += 1;
                continue;
            }
            
            if (c >= 0x1F3FB && c <= 0x1F3FF) {
                continue; // These don't add width
            }
            
            if (c >= 0x1F1E6 && c <= 0x1F1FF) {
                width += 2; // Flags are typically 2 cells
                continue;
            }

            width += get_char_width(c);
        }
        
        return width;
    }
    
private:
    static int get_char_width(const char32_t c)
    {
        const auto wc = static_cast<wchar_t>(c);

        if (const int w = wcwidth(wc); w >= 0) {
            return w;
        }
        
        return fallback_char_width(c);
    }
    
    static int fallback_char_width(const char32_t c)
    {
        if (c <= 0x1F || (c >= 0x7F && c <= 0x9F)) {
            return 0;
        }
        
        if (is_fullwidth(c)) {
            return 2;
        }
        
        return 1;
    }
    
    static bool is_fullwidth(const char32_t c)
    {
        if ((c >= 0x4E00 && c <= 0x9FFF) ||
            (c >= 0x3400 && c <= 0x4DBF) ||
            (c >= 0x20000 && c <= 0x2A6DF) ||
            (c >= 0x2A700 && c <= 0x2B73F) ||
            (c >= 0x2B740 && c <= 0x2B81F) ||
            (c >= 0x2B820 && c <= 0x2CEAF) ||
            (c >= 0xF900 && c <= 0xFAFF) ||
            (c >= 0x2F800 && c <= 0x2FA1F)) {
            return true;
        }
        
        if (c >= 0xAC00 && c <= 0xD7AF) {
            return true;
        }
        
        if (c >= 0xFF01 && c <= 0xFF5E) {
            return true;
        }
        
        if ((c >= 0x1F300 && c <= 0x1F5FF) || // Misc symbols and pictographs
            (c >= 0x1F600 && c <= 0x1F64F) || // Emoticons
            (c >= 0x1F680 && c <= 0x1F6FF) || // Transport & map symbols
            (c >= 0x1F900 && c <= 0x1F9FF) || // Supplemental symbols
            (c >= 0x1FA70 && c <= 0x1FAFF)) { // Symbols and pictographs extended
            return true;
        }
        
        if (c == 0x3000 || // Ideographic space
            (c >= 0x3001 && c <= 0x303F) || // CJK symbols and punctuation
            (c >= 0x3099 && c <= 0x30FF) || // Hiragana, Katakana
            (c >= 0x3200 && c <= 0x32FF) || // Enclosed CJK letters and months
            (c >= 0x3300 && c <= 0x33FF)) { // CJK compatibility
            return true;
        }
        
        return false;
    }
};

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
        const auto u32 = utf8_to_u32(str);
        return UnicodeDisplayWidth::get_width_utf32(u32);
    };

    auto get_string_screen_length_u32 = [](const std::u32string & str)->int {
        return UnicodeDisplayWidth::get_width_utf32(str);
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
                    leads += UnicodeDisplayWidth::get_width_utf32({line.front()});
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
                        p_size += UnicodeDisplayWidth::get_width_utf32({c});
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

    const auto output = less_output_redirect.str();
    pager(output, true, using_less);
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
    if (value < 1024ull || value >= 1024ull * 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << value << " " << lv1;
    } else if (value < 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / 1024ull) << " " << lv2;
    } else if (value < 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull)) << " " << lv3;
    } else if (value < 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull * 1024ull)) << " " << lv4;
    } else if (value < 1024ull * 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull * 1024ull * 1024ull)) << " " << lv5;
    }

    return ss.str();
}

inline std::string value_to_speed(const unsigned long long value) {
    return value_to_human(value, "B/s", "KB/s", "MB/s", "GB/s", "TB/s");
}

inline std::string value_to_size(const unsigned long long value) {
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
    return std::to_string(day) + "d" + std::to_string(hour) + "h" + std::to_string(minute) + "m" + std::to_string(second) + "s";
}

void nload(
    const std::atomic < uint64_t > * total_upload,
    const std::atomic < uint64_t > * total_download,
    const std::atomic < uint64_t > * upload_speed,
    const std::atomic < uint64_t > * download_speed,
    const std::atomic_bool * running,
    std::vector < std::string > & top_3_connections_using_most_speed,
    std::mutex * top_3_connections_using_most_speed_mtx)
{
    pthread_setname_np(pthread_self(), "nload");
    constexpr int reserved_lines = 4 + 3;
    int row = 0, col = 0;
    int window_space = 0;
    auto update_window_spaces = [&row, &col, &window_space]() {
        const auto [ r, c ] = get_col_line_size();
        row = r;
        col = c;
        window_space = (row - reserved_lines) / 2;
    };

    constexpr char l_1_to_40 = '.';
    constexpr char l_41_to_80 = '|';
    constexpr char l_81_to_100 = '#';

    auto generate_from_metric = [&](const std::vector <float> & list, int height)->std::vector < std::pair < int, int > >
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
        const std::vector<uint64_t> & list,
        uint64_t & max_speed_out_of_loop, uint64_t & min_speed_out_of_loop,
        const decltype(generate_from_metric({}, 0)) & metric_list,
        const std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point,
        const uint64_t total_bytes_since_started)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto time_escalated = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_point).count();
        const auto min_speed = min(list);
        const auto max_speed = max(list);
        max_speed_out_of_loop = std::max(max_speed, max_speed_out_of_loop);
        min_speed_out_of_loop = std::min(min_speed, min_speed_out_of_loop);
        std::vector < std::string > info_list;
        const auto time_escalated_seconds = (static_cast<double>(time_escalated) / 1000.00f);
        const auto avg_speed_overall = time_escalated_seconds > 1.00 ? static_cast<double>(total_bytes_since_started) / time_escalated_seconds : 0.00;
        const auto min_speed_on_page_str = value_to_speed(min_speed);
        const auto max_speed_on_page_str = value_to_speed(max_speed);
        // const auto min_speed_overall_str = value_to_speed(min_speed_out_of_loop);
        const auto max_speed_overall_str = value_to_speed(max_speed_out_of_loop);
        const auto avg_speed_on_page_str = value_to_speed(static_cast<uint64_t>(avg(list)));
        const auto avg_speed_overall_str = value_to_speed(static_cast<long>(avg_speed_overall));
        const auto max_pre_slash_content_len = max({
            // min_speed_on_page_str.length(),
            max_speed_on_page_str.length(),
            avg_speed_on_page_str.length()
        });

        auto generate_padding = [&max_pre_slash_content_len](const std::string & str)->std::string {
            return str + std::string(max_pre_slash_content_len - str.length(), ' ');
        };

        info_list.push_back(std::string("    Cur (P): ") + value_to_speed(*speed));
        info_list.push_back(std::string("    Min (P): ") + min_speed_on_page_str);
        info_list.push_back(std::string("  Max (P/O): ") + generate_padding(max_speed_on_page_str) + " / " + max_speed_overall_str);
        info_list.push_back(std::string("  Avg (P/O): ") + generate_padding(avg_speed_on_page_str) + " / " + avg_speed_overall_str);
        info_list.push_back(std::string("    Ttl (O): ") + value_to_size(*total));

        std::vector<uint64_t> size_list;
        for (const auto & str : info_list) {
            size_list.push_back(str.size());
        }

        info_space_size = std::max(static_cast<int>(max(size_list)), info_space_size);
        if (col < info_space_size) {
            std::cout << color::color(0,0,0,5,0,0) << "TOO SMALL" << std::endl;
            return;
        }

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
                    } else if (41 <= partial_block_percentage && partial_block_percentage <= 100) {
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
        if (window_space > reserved_lines)
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
                    total_download_since_start);
            }
            std::cout << color::no_color();
            std::cout << "Outgoing:" << std::endl;
            {
                std::cout << color::color(5,1,0);
                const auto metric_list = generate_from_metric(up_list, window_space - (free_space == 0 ? 1 : 0));
                const auto total_upload_since_start = *total_upload - upload_total_bytes_when_started;
                print_win(upload_speed,
                    total_upload,
                    up_speed_list,
                    max_up_speed,
                    min_up_speed,
                    metric_list,
                    now,
                    total_upload_since_start);
            }
            std::cout << color::no_color();

            {
                std::lock_guard<std::mutex> lock_gud(*top_3_connections_using_most_speed_mtx);
                std::ranges::for_each(top_3_connections_using_most_speed, [&](const std::string & line)
                {
                    auto new_line = line;
                    if (UnicodeDisplayWidth::get_width_utf8(line) > col)
                    {
                        auto utf32 = utf8_to_u32(line);
                        decltype(utf32) utf32_cut;
                        int len = 0;
                        for (const auto & c : utf32)
                        {
                            len += UnicodeDisplayWidth::get_width_utf32({c});
                            if (len >= (col - 1)) {
                                break;
                            }

                            utf32_cut += c;
                        }

                        new_line = utf8::utf32to8(utf32_cut) + color::color(0,0,0,3,3,3) + ">";
                    }
                    std::cout   << color::color(3,3,3) << new_line
                                << color::no_color() << std::endl;
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
            std::cout.write(clear, sizeof(clear));
            std::cout << color::color(0,0,0,5,0,0) << "TOO SMALL" << color::no_color() << std::endl;
        }

        for (int i = 0; i < 1000; i++)
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

    const auto home = color::get_env("HOME");
    int sort_by = 4;
    bool reverse = false;

    if (!home.empty())
    {
        if (std::ifstream ifs(home + "/.ccdbrc");
            ifs.is_open())
        {
            try {
                ifs >> sort_by >> reverse;
            } catch (...) {
                sort_by = 4; reverse = false;
            }
        }
    }

    auto save_config = [&]
    {
        if (!home.empty()) {
            if (std::ofstream ofs(home + "/.ccdbrc");
                ofs.is_open())
            {
                try {
                    ofs << sort_by << " " << reverse << std::endl;
                } catch (...) {
                }
            }
        }
    };

    try
    {
        char * line = nullptr;
        general_info_pulling backend_instance(backend, port, token);
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

        std::map < uint64_t, std::string > index_to_proxy_name_list;
        std::map < std::string, int > latency_backups;
        auto update_providers = [&]
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
        };

        backend_instance.start_continuous_updates();
        update_providers();

        std::string last_line;
        // add past history
        if (!home.empty())
        {
            std::ifstream history_file;
            history_file.open(home + "/.ccdb_history", std::ios::in);
            if (history_file.is_open())
            {
                while (!history_file.eof())
                {
                    std::string hline;
                    std::getline(history_file, hline);
                    hline = remove_leading_and_tailing_spaces(hline);
                    if (!hline.empty()) {
                        add_history(hline.c_str());
                        last_line = hline;
                    }
                }
            }
        }

        std::ofstream history_file;
        if (!home.empty()) {
            history_file = std::ofstream(home + "/.ccdb_history", std::ios::out | std::ios::app);
        }

        auto save_history = [&](const std::string & history_line)->void
        {
            add_history(history_line.c_str());
            if (history_file.is_open()) {
                history_file << history_line << std::endl;
            }
        };

        while ((line = readline("ccdb> ")) != nullptr)
        {
            if (sysint_pressed) {
                continue;
                sysint_pressed = false;
            }

            const auto presented_history = remove_leading_and_tailing_spaces(line);
            if (*line && presented_history != last_line) {
                save_history(presented_history);
            }

            if (!presented_history.empty()) last_line = presented_history;
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
                    std::mutex lock;
                    std::vector<std::string> top_3_conn;

                    std::thread Worker(nload, &total_up, &total_down, &up_speed, &down_speed, &running, std::ref(top_3_conn), &lock);
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
                        }
                        else if (command_vector[1] == "nload") {
                            help(command_vector[1], "A nload-like dashboard");
                        }
                        else if (command_vector[1] == "close_connections") {
                            help(command_vector[1], "Close all currently active connections");
                        }
                        else if (command_vector[1] == "get") {
                            help_sub_cmds("get", {
                                { "latency", "Get all proxy latencies" },
                                { "vecGroupProxy", "Get proxy group and endpoint as an index-identifiable list for better console-only experience. You can use `set vgroup [GROUP INDEX] [ENDPOINT INDEX]` to change proxies instead of actual proxy group names" },
                                { "proxy", "Get all proxies. Latency will be added if tested before" },
                                { "connections [hide <0-11>|shot]",
                                    R"(Get all active connections. Using the "hide" to hide columns, which are separated by ',', or use '-' to conjunct numbers. E.g.: 2,4-6,8. Use ^C or 'q' to stop. Use "shot" to use a pager)" },
                                { "mode", "Get current proxy mode, i.e., direct, rule or global" },
                                { "log", "Watch logs. Use Ctrl+C (^C) or press 'q' to stop watching" },
                            });
                        }
                        else if (command_vector[1] == "set") {
                            help_sub_cmds("set", {
                                { "mode", "set mode " + color::color(5,5,5) + "[MODE]" + color::no_color() + ", where " + color::color(5,5,5) + "[MODE]" + color::no_color() + R"( can be "direct", "rule", or "global")" }, // DO NOT USE "...," use "...", instead. It's confusing
                                { "group", "set group " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " " + color::color(5,5,5) + "[PROXY]" + color::no_color() + ", where " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " is proxy group, and " + color::color(5,5,5) + "[PROXY]" + color::no_color() + " is proxy endpoint" },
                                { "vgroup", "set vgroup " + color::color(5,5,5) + "[GROUP VEC]" + color::no_color() + " " + color::color(5,5,5) + "[PROXY VEC]" + color::no_color() + ". You have to run `get vecGroupProxy` first to update the list and see which is which."},
                                { "chain_parser", "set chain_parser " + color::color(5,5,5) + "[on|off]" + color::no_color() + R"(, "on" means parse rule chains, and "off" means show only endpoint)" },
                                { "sort_by", "set sort_by " + color::color(5,5,5) + "[0-11]" + color::no_color() + R"(, used by get connections. Set which column to sort. Default is 4.)" },
                                { "sort_reverse", "set sort_reverse " + color::color(5,5,5) + "[on|off]" + color::no_color() + R"(, "off" means sort by default (highest to lowest), and "on" means sort by reverse (lowest to highest))" },
                            });
                        }
                        else {
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
                            std::cout << "\033[?25l";
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
                            std::cout << "\033[?25h";
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
                        sysint_pressed = false;
                    }
                    else if (command_vector[1] == "latency")
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
                    else if (command_vector[1] == "log")
                    {
                        backend_instance.change_focus("logs");
                        std::thread input_getc_worker([]
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

                        if (input_getc_worker.joinable()) input_getc_worker.join();
                        backend_instance.change_focus("overview");
                        sysint_pressed = false;
                    }
                    else if (command_vector[1] == "mode") {
                        std::cout << backend_instance.get_current_mode() << std::endl;
                    }
                    else if (command_vector[1] == "proxy") {
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
                    else if (command_vector[1] == "vecGroupProxy")
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
                    else {
                        std::cerr << "Unknown command `" << command_vector[1] << "`" << std::endl;
                    }
                }
                else if (command_vector.front() == "set")
                {
                    if (command_vector.size() == 3 && command_vector[1] == "mode") // set mode [MODE]
                    {
                        if (command_vector[2] != "rule" && command_vector[2] != "global" && command_vector[2] != "direct") {
                            std::cerr << "Unknown mode " << command_vector[2] << std::endl;
                        } else {
                            backend_instance.change_proxy_mode(command_vector[2]);
                        }
                    }
                    else if (command_vector.size() == 4 && command_vector[1] == "group") { // set group [PROXY] [ENDPOINT]
                        const std::string & group = command_vector[2], & proxy = command_vector[3];
                        std::cout << "Changing `" << group << "` proxy endpoint to `" << proxy << "`" << std::endl;
                        if (!backend_instance.change_proxy_using_backend(group, proxy))
                        {
                            std::cerr << "Failed to change proxy endpoint to `" << proxy << "`" << std::endl;
                        }
                    }
                    else if (command_vector.size() == 4 && command_vector[1] == "vgroup") { // set vgroup [Vec PROXY] [Vec ENDPOINT]
                        const std::string & group = command_vector[2], & proxy = command_vector[3];
                        try {
                            if (index_to_proxy_name_list.empty())
                            {
                                std::cout << "Run `get vecGroupProxy` first!" << std::endl;
                                continue;
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
                            continue;
                        }
                    }
                    else if (command_vector.size() == 3 && command_vector[1] == "chain_parser") { // set chain_parser on/off
                        if (command_vector[2] == "on") backend_instance.parse_chains = true;
                        else if (command_vector[2] == "off") backend_instance.parse_chains = false;
                        else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
                    }
                    else if (command_vector.size() == 3 && command_vector[1] == "sort_by") { // set sort_by [num]
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
                    else if (command_vector.size() == 3 && command_vector[1] == "sort_reverse") { // set sort_reverse on/off
                        if (command_vector[2] == "on") reverse = true;
                        else if (command_vector[2] == "off") reverse = false;
                        else std::cerr << "Unknown option for parser `" << command_vector[2] << "`" << std::endl;
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
            }

            free(line);
        }

        backend_instance.stop_continuous_updates();
    }
    catch (std::exception & e)
    {
        save_config();
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        save_config();
        std::cerr << "Unknown exception" << std::endl;
        return EXIT_FAILURE;
    }
    save_config();
    return EXIT_SUCCESS;
}
