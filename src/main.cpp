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

const char clear[] = { 0x1b, 0x5b, 0x48, 0x1b, 0x5b, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x4a };

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
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "exit\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "get\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "set\n" << color::no_color()
                << "        " << color::color(0,0,5) << "*" << color::no_color() << " " << color::color(1,4,5) << "close_connections\n" << color::no_color();
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

void print_table(
    std::vector<std::string> const & table_keys,
    std::vector < std::vector<std::string> > const & table_values,
    bool muff_non_ascii = true,
    bool seperator = true)
{
    const decltype(table_keys.size()) max_size = std::strtoll(color::get_env("COLUMNS").c_str(), nullptr, 10) / table_keys.size();
    std::map < std::string /* table keys */, uint32_t /* longest value in this column */ > size_map;
    for (const auto & key : table_keys)
    {
        size_map[key] = key.length();
    }

    for (const auto & vals : table_values)
    {
        if (vals.size() != table_keys.size()) return;
        int index = 0;
        for (const auto & val : vals)
        {
            if (const auto & current_key = table_keys[index++];
                size_map[current_key] < val.size())
            {
                size_map[current_key] = std::min(max_size, val.size());
            }
        }
    }

    std::stringstream ss;
    for (const auto & key : table_keys)
    {
        const int paddings = static_cast<int>(size_map[key] - key.length()) + 2;
        const int before = std::max(paddings / 2, 1);
        const int after = std::max(paddings - before, 1);
        ss << "|" << std::string(before, ' ') << key << std::string(after, ' ');
    }
    ss << "|";
    const std::string title_line = ss.str();
    std::string separation_line;
    if (title_line.size() > 2)
    {
        std::stringstream ss_sep;
        ss_sep << "+" << std::string(title_line.size() - 2, '-') << "+";
        separation_line = ss_sep.str();
    }

    std::cout << separation_line << std::endl;
    std::cout << color::color(5,5,5) << title_line << color::no_color() << std::endl;
    std::cout << separation_line << std::endl;

    for (const auto & vals : table_values)
    {
        int index = 0;
        for (const auto & val : vals)
        {
            const auto & current_key = table_keys[index++];
            const int paddings = static_cast<int>(size_map[current_key] - val.length()) + 2;
            const int before = std::max(paddings / 2, 1);
            const int after = std::max(paddings - before, 1);
            std::cout << (seperator ? "|" : " ") << std::string(before, ' ');
            std::string output;
            if (val.length() > max_size && (muff_non_ascii ? true : index != vals.size()))
            {
                std::string first = val.substr(0, max_size / 3 * 2);
                std::string second = val.substr(val.length() - (max_size - (max_size / 3 * 2 + 3)));
                output += first + "...";
                output += second;
            } else {
                output = val;
            }

            if (muff_non_ascii) {
                for (auto & c : output) {
                    if (!std::isprint(c)) c = '#';
                }
            }

            std::cout << output << std::string(after, ' ');
        }
        std::cout << " " << std::endl;
    }

    std::cout << separation_line << std::endl;
}

std::string value_to_speed(const unsigned long long value)
{
    if (value < 1024) {
        return std::to_string(value) + " B/s";
    } else if (value < 1024 * 1024) {
        return std::to_string(value / 1024) + " KB/s";
    } else if (value < 1024 * 1024 * 1024) {
        return std::to_string(value / (1024 * 1024)) + " MB/s";
    } else if (value < 1024l * 1024 * 1024 * 1024) {
        return std::to_string(value / (1024 * 1024 * 1024)) + " GB/s";
    } else if (value < 1024l * 1024 * 1024 * 1024 * 1024) {
        return std::to_string(value / (1024l * 1024 * 1024 * 1024)) + " TB/s";
    } else {
        return std::to_string(value) + " B/s";
    }
}

std::string value_to_size(const unsigned long long value)
{
    if (value < 1024) {
        return std::to_string(value) + " B";
    } else if (value < 1024 * 1024) {
        return std::to_string(value / 1024) + " KB";
    } else if (value < 1024 * 1024 * 1024) {
        return std::to_string(value / (1024 * 1024)) + " MB";
    } else if (value < 1024l * 1024 * 1024 * 1024) {
        return std::to_string(value / (1024 * 1024 * 1024)) + " GB";
    } else if (value < 1024l * 1024 * 1024 * 1024 * 1024) {
        return std::to_string(value / (1024l * 1024 * 1024 * 1024)) + " TB";
    } else {
        return std::to_string(value) + " B";
    }
}

std::string second_to_human_readable(const unsigned long long value)
{
    if (value < 60) {
        return std::to_string(value) + " s";
    } else if (value < 60 * 60) {
        return std::to_string(value / 60) + " Min";
    } else if (value < 60 * 60 * 24) {
        return std::to_string(value / (60 * 60)) + " H";
    } else {
        return std::to_string(value / (60 * 60 * 24)) + " Day";
    }
}

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
    std::cout << "C++ Clash Dashboard Version 0.0.1" << std::endl;
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

    rl_attempted_completion_function = cmd_completion;
    using_history();
    try
    {
        char * line = nullptr;
        general_info_pulling backend_instance(backend, port, token);

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
            if (sysint_pressed)
            {
                free(line);
                sysint_pressed = false;
                continue;
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
                else if (command_vector.front() == "help")
                {
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
                else if (command_vector.front() == "get")
                {
                    if (command_vector[1] == "connections")
                    {
                        backend_instance.change_focus("connections");
                        while (!sysint_pressed)
                        {
                            auto connections = backend_instance.get_active_connections();
                            std::vector<std::string> titles = {
                                "Host",
                                "Process",
                                "DL",
                                "UP",
                                "DL Speed",
                                "UP Speed",
                                "Rules",
                                "Time",
                                "Source IP",
                                "Destination IP",
                                "Type",
                                "Chains",
                            };
                            std::vector<std::vector<std::string>> table_vals;
                            std::ranges::sort(connections,
                                              [](const general_info_pulling::connection_t & a, const general_info_pulling::connection_t & b)
                                              { return a.downloadSpeed > b.downloadSpeed; });
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

                            std::cout.write(clear, sizeof(clear));
                            print_table(titles, table_vals, false);
                            std::this_thread::sleep_for(std::chrono::seconds(1l));
                        }
                    } else if (command_vector[1] == "latency") {
                        std::cout << "Testing latency with url " << latency_url << " ..." << std::endl;
                        backend_instance.update_proxy_list(); // update the proxy first
                        backend_instance.latency_test(latency_url);
                        auto latency_list = backend_instance.get_proxies_and_latencies_as_pair();
                        std::vector < std::pair<std::string, int >> list_unordered;
                        for (const auto & [proxy, latency] : latency_list.second) {
                            list_unordered.emplace_back(proxy, latency);
                        }

                        std::vector<std::string> titles = { "Latency", "Proxy" };
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
                        print_table(titles, table_vals, false);
                    } else if (command_vector[1] == "log") {
                        backend_instance.change_focus("logs");
                        while (!sysint_pressed)
                        {
                            auto current_vector = backend_instance.get_logs();
                            const uint32_t lines = std::strtol(color::get_env("LINES").c_str(), nullptr, 10);
                            while (current_vector.size() > lines) current_vector.erase(current_vector.begin());
                            std::cout.write(clear, sizeof(clear));
                            for (const auto & [level, log] : current_vector)
                            {
                                if (level == "info") {
                                    std::cout << color::color(2,2,2) << level << color::no_color() << ": " << log << std::endl;
                                } else {
                                    std::cout << color::color(5,0,0) << level << color::no_color() << ": " << log << std::endl;
                                }
                            }

                            std::this_thread::sleep_for(std::chrono::seconds(1l));
                        }
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

                        print_table(table_titles, table_vals, false, false);
                    } else {
                        std::cerr << "Unknown command `" << command_vector[1] << "`" << std::endl;
                    }
                }
                else if (command_vector.front() == "set")
                {
                    if (command_vector[1] == "mode")
                    {
                        if (command_vector[2] != "rule" && command_vector[2] != "global" && command_vector[2] != "direct")
                        {
                            std::cerr << "Unknown mode " << command_vector[2] << std::endl;
                        }
                        else
                        {
                            backend_instance.change_proxy_mode(command_vector[2]);
                        }
                    } else if (command_vector[1] == "group") {
                        const std::string & group = command_vector[2], & proxy = command_vector[3];
                        std::cout << "Changing `" << group << "` proxy endpoint to `" << proxy << "`" << std::endl;
                        if (!backend_instance.change_proxy_using_backend(group, proxy))
                        {
                            std::cerr << "Failed to change proxy endpoint to `" << proxy << "`" << std::endl;
                        }
                    } else {
                        std::cerr << "Unknown command `" << command_vector[1] << "`" << std::endl;
                    }
                }
                else if (command_vector.front() == "close_connections")
                {
                    backend_instance.close_all_connections();
                }
                else
                {
                    std::cerr << "Unknown command `" << command_vector.front() << "`" << std::endl;
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
