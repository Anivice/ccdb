#include <atomic>
#include <string>
#include <csignal>
#include <iostream>
#include <vector>
#include "readline.h"
#include "history.h"

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

void replace_all(
    std::string & original,
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
}

int main()
{
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
        replace_all(cmd, " ", "");
        if (cmd == "quit" || cmd == "exit") {
            free(line);
            break;
        }
        free(line);
    }
    return 0;
}
