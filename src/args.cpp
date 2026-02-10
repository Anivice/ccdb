#include "args.h"
#include "utils.h"
#include <sstream>

std::string ccdb::utils::PreDefinedArgumentType::print_help() const
{
    struct arg_show_t {
        std::string name;
        std::string description;
    };
    std::vector < arg_show_t > list;

    for (const auto & [ short_name,
        long_name,
        argument_required,
        description ] : arguments_)
    {
        arg_show_t show;
        std::ostringstream oss;
        if (short_name > 0) {
            oss << "-" << short_name << (long_name.empty() ? "" : ",");
        }

        if (!long_name.empty()) {
            oss << "--" << long_name;
        }

        if (argument_required) {
            oss << " [ARG]";
        }

        show.name = oss.str();
        show.description = description;
        list.push_back(show);
    }

    uint64_t max_name_len = 0;
    std::ranges::for_each(list, [&max_name_len](const arg_show_t & show)
    {
        if (max_name_len < show.name.length()) {
            max_name_len = show.name.length();
        }
    });

    std::ostringstream os;
    std::ranges::for_each(list, [&](arg_show_t show)
    {
        const std::string padding(max_name_len + 1 - show.name.length(), ' ');
        const std::string new_line(4 + max_name_len + 1 + 2, ' ');
        replace_all(show.description, "\n", "\n" + new_line);
        os << "    " << show.name << padding << "  " << show.description << std::endl;
    });
    return os.str();
}

bool ccdb::utils::ParsedArgumentType::contains(const signed char c) const noexcept
{
    return std::ranges::any_of(arguments_, [&](const args_parsed_t & arg) ->bool {
        return arg.short_name == c;
    });
}

bool ccdb::utils::ParsedArgumentType::contains(const std::string &str) const noexcept
{
    return std::ranges::any_of(arguments_, [&](const args_parsed_t & arg) ->bool {
        return arg.long_name == str;
    });
}

std::string ccdb::utils::ParsedArgumentType::at(const char c) const
{
    for (const auto & arg : arguments_) {
        if (arg.short_name == c) {
            return arg.parameter;
        }
    }

    throw ccdb::error::no_such_argument("Argument '" + std::string(1, c) + "' not found.");
}

std::string ccdb::utils::ParsedArgumentType::at(const std::string &str) const
{
    for (const auto & arg : arguments_) {
        if (arg.long_name == str) {
            return arg.parameter;
        }
    }

    throw ccdb::error::no_such_argument("Argument '" + str + "' not found.");
}

ccdb::utils::ArgumentParser::ArgumentParser(const int argc, char **argv, const PreDefinedArgumentType & PreDefinedArgs)
{
    auto clean_arg = [](std::string str)->std::string
    {
        std::ranges::reverse(str);
        while (!str.empty() && str.back() == '-') str.pop_back();
        std::ranges::reverse(str);
        return str;
    };

    auto fill_missing = [&](args_parsed_t & arg)->bool // return if arg required
    {
        for (const auto & pd_arg : PreDefinedArgs)
        {
            if (arg.short_name > 0) {
                if (pd_arg.short_name == arg.short_name) {
                    arg.long_name = pd_arg.long_name;
                    return pd_arg.argument_required;
                }
            } else if (!arg.long_name.empty()) {
                if (pd_arg.long_name == arg.long_name) {
                    arg.short_name = pd_arg.short_name;
                    return pd_arg.argument_required;
                }
            }
        }

        throw error::no_such_argument("Unknown argument '" + (arg.short_name > 0 ? std::string(1, arg.short_name) : arg.long_name) + "'.");
    };

    for (int i = 1; i < argc; i++)
    {
        args_parsed_t arg_parsed { };
        const std::string arg = clean_arg(argv[i]);
        if (arg.size() == 1) {
            arg_parsed.short_name = arg[0];
        } else if (!arg.empty()) {
            arg_parsed.long_name = arg;
        }

        if (fill_missing(arg_parsed))
        {
            i++;
            if (i >= argc) throw error::argument_parser_exception("Argument '" + arg + "' requires a parameter but was never provided");
            const std::string param = argv[i];
            arg_parsed.parameter = param;
        }

        parsedArgument_.arguments_.push_back(arg_parsed);
    }

    parsedArgument_.executable_name_ = argv[0];
}
