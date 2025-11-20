#include <atomic>
#include <string>
#include <csignal>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <ranges>
#include <sys/stat.h>
#include "readline.h"
#include "history.h"
#include "general_info_pulling.h"

volatile std::atomic_bool sysint_pressed = false;
void sigint_handler(int)
{
    const char *msg = "\r[===> KEYBOARD INTERRUPT / PRESS Enter TO CONTINUE <===]";
    write(1, msg, strlen(msg));
    sysint_pressed = true;
}

/* Command vocabulary */
static const char *cmds[] = {
    "help",         // quit, exit, get, set
    "quit", "exit", //
    "get",          // latency, proxy, connections, mode, log
    "set",          // [mode|group]
    "close_connections",
    nullptr
};

static const char *help_voc [] = { "quit", "exit", "get", "set", "close_connections", nullptr };
static const char *get_voc  [] = { "latency", "proxy", "connections", "mode", "log", nullptr };
static const char *set_voc  [] = { "mode", "group", nullptr };

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
    if (!state) { index = 0; len = strlen(text); }
    while ((name = cmds[index++])) {
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }
    return nullptr;
}

arg_generator(help_voc_generator, help_voc);
arg_generator(get_voc_generator, get_voc);
arg_generator(set_voc_generator, set_voc);

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
    std::vector < std::string > arg2_verbs = { "direct", "rule", "global" };
    {
        std::lock_guard lock(arg2_additional_verbs_mutex);
        arg2_verbs.insert(arg2_verbs.end(), arg2_additional_verbs.begin(), arg2_additional_verbs.end());
        arg2_verbs.push_back("");
    }
    static int index, len;
    const char *name;
    if (!state) { index = 0; len = strlen(text); }
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

int main(int argc, char ** argv)
{
    std::string backend;
    int port = 0;
    std::string token;
    std::string latency_url;

    try
    {
        if (argc == 3)
        {
            backend = argv[1];
            port = atoi(argv[2]);
        }
        else if (argc == 4)
        {
            backend = argv[1];
            port = atoi(argv[2]);
            token = argv[3];
        }
        else if (argc == 5)
        {
            backend = argv[1];
            port = atoi(argv[2]);
            token = argv[3];
            latency_url = argv[4];
        }
        else
        {
            std::cout << argv[0] << " [BACKEND] [PORT] <TOKEN> <LATENCY URL>" << std::endl;
            std::cout << " [...] is required, <...> is optional." << std::endl;
            return 1;
        }
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto remove_leading_and_tailing_spaces = [](const std::string & text)->std::string
    {
        std::string middle = text.substr(text.find_first_not_of(' '));
        while (!middle.empty() && middle.back() == ' ') {
            middle.pop_back();
        }
        return middle;
    };
    std::signal(SIGINT, sigint_handler);

    rl_attempted_completion_function = cmd_completion;
    using_history();
    char *line;
    while ((line = readline("ccdb> ")) != nullptr)
    {
        if (sysint_pressed)
        {
            free(line);
            sysint_pressed = false;
            continue;
        }

        if (*line) add_history(line);
        std::string cmd = line;
        cmd = remove_leading_and_tailing_spaces(cmd);
        std::vector < std::string > command_vector;
        {
            std::string buffer;
            for (auto c : cmd)
            {
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
            else if (command_vector.front() == "help")
            {
                auto help_overall = []
                {
                    std::cout   << color::color(0,5,1) << "help" << color::color(5,5,5) << " [COMMAND]" << color::no_color() << std::endl
                                << "    Where " << color::color(5,5,5) << "[COMMAND]" << color::no_color() << " can be:" << std::endl
                                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "quit\n" << color::no_color()
                                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "exit\n" << color::no_color()
                                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "get\n" << color::no_color()
                                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "set\n" << color::no_color()
                                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "close_connections\n" << color::no_color();
                };

                auto help = [](const std::string & cmd_text, const std::string & description)->void
                {
                    std::cout << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
                    std::cout << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << description << color::no_color() << std::endl;
                };

                auto help_sub_cmds = [](const std::string & cmd_text, const std::map <std::string, std::string > & map)->void
                {
                    std::cout << color::color(5,5,5) << "COMMAND     " << color::color(5,3,5) << cmd_text << color::no_color() << std::endl;
                    std::cout << color::color(5,5,5) << "DESCRIPTION " << color::color(2,4,5) << color::no_color() << std::endl;
                    int longest_subcmd_length = 0;
                    for (const auto & s : map | std::views::keys) {
                        if (longest_subcmd_length < s.length()) longest_subcmd_length = s.length();
                    }

                    for (const auto & [sub_cmd_text, des] : map)
                    {
                        std::cout << "            " << color::color(0,0,5) << "*" << color::no_color() << " ";
                        std::cout << color::color(0,5,5) << sub_cmd_text << color::no_color() << ":" << std::string(longest_subcmd_length - sub_cmd_text.length() + 1, ' ') << des;
                        std::cout << std::endl;
                    }
                };

                if (command_vector.size() != 2) {
                    help_overall();
                }
                else
                {
                    if (command_vector[1] == "quit" || command_vector[1] == "exit") {
                        help(command_vector[1], "Exit the program");
                    } else if (command_vector[1] == "close_connections") {
                        help(command_vector[1], "Close all currently active connections");
                    } else if (command_vector[1] == "get") {
                        help_sub_cmds("get", {
                            { "latency", "Get all proxy latencies" },
                            { "proxy", "Get all proxies. Latency will be added if tested before" },
                            { "connections", "Get all active connections" },
                            { "mode", "Get current proxy mode, i.e., direct, rule or global" },
                            { "log", "Watch logs. Use Ctrl+C (^C) to stop watching" },
                        });
                    } else if (command_vector[1] == "set") {
                        help_sub_cmds("set", {
                            { "mode", "set mode " + color::color(5,5,5) + "[MODE]" + color::no_color() + ", where " + color::color(5,5,5) + "[MODE]" + color::no_color() + R"( can be "direct", "rule", or "global")" }, // DO NOT USE "...," use "...", instead. It's confusing
                            { "group", "set group " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " " + color::color(5,5,5) + "[PROXY]" + color::no_color() + ", where " + color::color(5,5,5) + "[GROUP]" + color::no_color() + " is proxy group, and " + color::color(5,5,5) + "[PROXY]" + color::no_color() + " is proxy endpoint" },
                        });
                    } else {
                        help_overall();
                    }
                }
            }
        }

        free(line);
    }
    return 0;
}
