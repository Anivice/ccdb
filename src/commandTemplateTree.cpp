#include "commandTemplateTree.h"
#include "readline.h"
#include <sstream>
#include <cstring>
#include <algorithm>
#include "command.readline.h"

namespace cmdTpTree
{
    std::string gen_cmd(const unsigned char *src, const unsigned int len)
    {
        std::vector<char> data;
        data.resize(len + 1, 0);
        std::memcpy(data.data(), src, len);
        std::string ret = data.data();
        return ret;
    }

    const NodeType * find(const NodeType &entry, const std::string &name)
    {
        for (const auto & v : entry.children_)
        {
            if (v->name_ == name) {
                return v.get();
            }
        }

        return nullptr;
    }

    const NodeType * find(const NodeType &root,
        const std::vector<std::string> &command_string)
    {
        auto * entry = &root;
        for (const auto & verb : command_string)
        {
            entry = find(*entry, verb);
            if (!entry) {
                throw std::invalid_argument("Command not found");
            }
        }

        return entry;
    }

    void commandTemplateTree_t::construct(const std::string &command_description)
    {
        /*
             * { < command: < subcommand1: verb1, <subcommand1.1, verb1 > >, verb2, verb3, < subcommand2: verb1> >, # ignored util '\n'
             *   < command2: [HSP], [CFSP] > }
             */

        auto remove_comments = [](const std::string & text)->std::string
        {
            std::stringstream ss(text), output;
            std::string line;
            while (std::getline(ss, line))
            {
                const auto pos = line.find_first_of('#');
                if (pos != std::string::npos) {
                    line = line.substr(0, pos);
                }

                output << line << std::endl;
            }

            return output.str();
        };

        std::stringstream ss(remove_comments(command_description));
        std::vector < frame_t > stack;
        stack.emplace_back();
        stack.back().entry_ = &root;
        bool help_text_override = false;

        char c;
        while (ss && ((  c = static_cast<char>( ss.get())  )) )
        {
            if (c < 0x20) continue; // ignore '\n' and all
            std::string & command_ = stack.back().command_;
            std::string & verb_ = stack.back().verb_;
            std::vector < std::string > & verbs_ = stack.back().verbs_;
            std::string & help_text_ = stack.back().help_text_;
            auto & help_map_ = stack.back().help_map_;
            NodeType * entry = stack.back().entry_;
            CurrentStatusType & status = stack.back().status_;

            if (status == EndLoop) break;

            if (help_text_override)
            {
                if (c != ')') {
                    help_text_ += c;
                } else {
                    help_text_override = false;
                }

                continue;
            }

            if (c == '(') {
                help_text_override = true;
                continue;
            }

            switch (c)
            {
                case '{':
                    continue;
                case '}': {
                    status = EndLoop;
                    continue;
                }
                case '<': {
                    const auto parent = entry;
                    parent->children_.emplace_back(std::make_unique<NodeType>());
                    const auto child = entry->children_.back().get();
                    child->parent_ = parent;
                    if (status == NoOperation) status = ReadingCommand;

                    stack.emplace_back();
                    stack.back().entry_ = child;
                    stack.back().status_ = ReadingCommand;
                    continue;
                }
                case ':': {
                    if (!help_text_.empty()) {
                        help_map_.emplace(command_, help_text_);
                        help_text_.clear();
                    }
                    status = ReadingVerbs;
                    continue;
                }
                case '>': {
                    if (!verb_.empty()) {
                        verbs_.push_back(verb_);
                        help_map_.emplace(verb_, help_text_);
                    }

                    entry->help_text_ = help_map_[command_];
                    entry->name_ = command_;
                    for (const auto & v : verbs_)
                    {
                        entry->children_.emplace_back(std::make_unique< NodeType >(NodeType{
                            .name_ = v,
                            .help_text_ = help_map_[v],
                            // .children_ = {},
                            .parent_ = entry,
                        }));
                    }

                    stack.pop_back();
                    continue;
                }
                default: break;
            }

            switch (status)
            {
                case ReadingCommand: {
                    if (c == ' ') continue;
                    command_ += c;
                    break;
                }

                case ReadingVerbs:
                {
                    if (c == ' ') continue;
                    if (c != ',') {
                        verb_ += c;
                    } else if (!verb_.empty()) {
                        verbs_.push_back(verb_);
                        help_map_.emplace(verb_, help_text_);
                        help_text_.clear();
                        verb_.clear();
                    }
                    break;
                }

                case NoOperation: {
                    continue;
                }

                default: std::cerr << "Unparsed character: `" << std::dec << c << "'\n";
            }
        }
    }

    const NodeType * commandTemplateTree_t::find(const std::vector<std::string> &command_string) const
    {
        return cmdTpTree::find(root, command_string);
    }

    std::vector<std::string> commandTemplateTree_t::find_sub_commands(const std::vector<std::string> &command_string) const
    {
        const auto * node = cmdTpTree::find(root, command_string);
        std::vector < std::string > result;
        for (const auto & v : node->children_) {
            result.push_back(v->name_);
        }

        return result;
    }

    std::string commandTemplateTree_t::get_help(const std::vector<std::string> &command_string) const
    {
        const auto * node = cmdTpTree::find(root, command_string);
        return node->help_text_;
    }

    std::string commandTemplateTree_t::get_help()
    {
        std::vector<std::pair<std::string, std::string>> command_help_text;
        uint64_t max_command_length = 0;
        for_each([&](const NodeType& node, const int depth)
        {
            if (!node.help_text_.empty())
            {
                std::ostringstream oss;
                oss << std::string(depth * 2, ' ') << node.name_;
                const auto str = oss.str();
                command_help_text.emplace_back(str, node.help_text_);
                if (max_command_length < str.length()) {
                    max_command_length = str.length();
                }
            }
        });

        std::ostringstream oss;
        for (const auto & [command, help] : command_help_text) {
            oss << command << std::string(max_command_length - command.length(), ' ');
            oss << ": " << help << std::endl;
        }

        return oss.str();
    }

    static std::string convert_from_raw()
    {
        std::vector<char> raw(command_readline_len + 1, 0);
        std::memcpy(raw.data(), command_readline, command_readline_len);
        return { raw.data() };
    }

    commandTemplateTree_t command_template_tree = convert_from_raw();

    std::vector < std::string > current_verbs;
    static char * arg_generator(const char *text, const int state)
    {
        std::vector<std::string> arg2_verbs = current_verbs;
        arg2_verbs.emplace_back("");
        static int index, len;
        const char *name;
        if (!state) { index = 0; len = static_cast<int>(strlen(text)); }
        while (((name = arg2_verbs[index++].c_str())) && strlen(name) > 0)
        {
            if (strncmp(name, text, len) == 0)
                return strdup(name);
        }
        return nullptr;
    }

    static int argument_index(const char *buffer, const int start)
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

    SpecialArgumentCandidates SpecialArgumentCandidatesGenerator = nullptr;

    static std::vector < std::string > args_completion_list;
    static int special_index = 0;

    char ** cmd_completion(const char *text, const int start, const int end)
    {
        (void)end;
        char **matches = nullptr;
        std::string this_arg = rl_line_buffer;
        while (!this_arg.empty() && this_arg.back() == ' ') this_arg.pop_back(); // remove tailing spaces
        while (!this_arg.empty() && this_arg.front() == ' ') this_arg.erase(this_arg.begin()); // remove leading spaces
        const int arg_index = argument_index(rl_line_buffer, start);
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
            if (args.size() > arg_index) args.pop_back();
        }

        auto can_find_special_args = [](const std::vector<std::string> & pargs) {
            return std::ranges::any_of(pargs, [](const std::string & arg) {
                return arg.find('[') != std::string::npos;
            });
        };

        auto special_handler = [&](const std::string & current_special)
        {
            if (current_special == arbitrary_length) {
                matches = rl_completion_matches(text, rl_filename_completion_function);
            }
            else if (current_special == no_subcommands) {
                matches = nullptr;
            }
            else
            {
                if (SpecialArgumentCandidatesGenerator) {
                    current_verbs = SpecialArgumentCandidatesGenerator(current_special);
                    matches = rl_completion_matches(text, arg_generator);
                }
                else {
                    std::cout << "Function not implemented\n";
                    matches = nullptr;
                }
            }
        };

        try
        {
            if (const auto sub_commands = command_template_tree.find_sub_commands(args);
                can_find_special_args(sub_commands))
            {
                args_completion_list.clear();
                special_index = arg_index;
                args_completion_list = sub_commands;
                const auto & current_special = args_completion_list.front();
                special_handler(current_special);
            }
            else {
                args_completion_list.clear();
                current_verbs = sub_commands;
                matches = rl_completion_matches(text, arg_generator);
            }
        } catch (std::invalid_argument &) {
            auto complete = [&]
            {
                if (args_completion_list.size() == 1 && args_completion_list.front() == arbitrary_length) {
                    special_handler(arbitrary_length);
                } else {
                    if (const auto current_index = arg_index - special_index;
                        current_index < args_completion_list.size())
                    {
                        special_handler(args_completion_list[current_index]);
                    } else {
                        matches = nullptr;
                    }
                }
            };

            if (!args_completion_list.empty()) {
                complete();
            }
            else {
                std::vector<std::string> list = args, commands;
                while (!list.empty())
                {
                    try {
                        commands = command_template_tree.find_sub_commands(list);
                    } catch (std::invalid_argument &) {
                        list.pop_back();
                        continue;
                    }

                    break;
                }

                if (commands.empty()) {
                    matches = nullptr;
                }
                else {
                    special_index = list.size();
                    args_completion_list = commands;
                    complete();
                }
            }
        }

        rl_attempted_completion_over = 1;
        return matches;
    }
} // cmdTpTree
