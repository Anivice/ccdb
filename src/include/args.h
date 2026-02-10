#ifndef CCDB_ARGS_H
#define CCDB_ARGS_H

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ccdb::error {
    class no_such_argument final : public std::invalid_argument {
    public: explicit no_such_argument(const std::string & msg) : std::invalid_argument(msg) { }
    };

    class argument_parser_exception final : public std::runtime_error {
    public: explicit argument_parser_exception(const std::string & msg) : std::runtime_error(msg) { }
    };
}

namespace ccdb::utils
{
    struct PreDefinedArgumentType_Single {
        signed char short_name; // single char, set to -1 to indicate no short name
        std::string long_name;  // full name, set to "" to indicate no long name
        bool argument_required; // require an argument?
        std::string description; // description for this argument
    };

    /// predefined argument
    class PreDefinedArgumentType {
    private:
        std::vector < PreDefinedArgumentType_Single > arguments_;

    public:
        explicit PreDefinedArgumentType(decltype(arguments_)  arguments) : arguments_(std::move(arguments)) { }

        using PreDefinedArgument = decltype(arguments_);

        [[nodiscard]] decltype(arguments_)::const_iterator begin() const { return arguments_.begin(); }
        [[nodiscard]] decltype(arguments_)::const_iterator end() const { return arguments_.end(); }
        decltype(arguments_)::iterator begin() { return arguments_.begin(); }
        decltype(arguments_)::iterator end() { return arguments_.end(); }

        /// print help message from pre-defined arguments
        [[nodiscard]] std::string print_help() const;
    };

    struct args_parsed_t {
        signed char short_name;
        std::string long_name;
        std::string parameter;
    };

    class ArgumentParser;

    class ParsedArgumentType {
    private:
        std::vector < args_parsed_t > arguments_;
        std::string executable_name_;

    public:
        ParsedArgumentType() = default;
        [[nodiscard]] decltype(arguments_)::const_iterator begin() const { return arguments_.begin(); }
        [[nodiscard]] decltype(arguments_)::const_iterator end() const { return arguments_.end(); }
        decltype(arguments_)::iterator begin() { return arguments_.begin(); }
        decltype(arguments_)::iterator end() { return arguments_.end(); }

        /// check if this argument is present
        /// @param c argument short name
        /// @return if the parsed argument exist, it returns true, otherwise false
        [[nodiscard]] bool contains(signed char c) const noexcept;

        /// check if this argument is present
        /// @param str argument short name
        /// @return if the parsed argument exist, it returns true, otherwise false
        [[nodiscard]] bool contains(const std::string & str) const noexcept;

        /// get argument list
        /// @param c argument short name
        /// @return corresponding argument list
        /// @throws ccdb::error::no_such_argument argument not found in parsed list
        [[nodiscard]] std::string at(char c) const;

        /// get argument list
        /// @param c argument short name
        /// @return corresponding argument list
        /// @throws ccdb::error::no_such_argument argument not found in parsed list
        [[nodiscard]] std::string operator[](const char c) const { return at(c); }

        /// get argument list
        /// @param str argument short name
        /// @return corresponding argument list
        /// @throws ccdb::error::no_such_argument argument not found in parsed list
        [[nodiscard]] std::string at(const std::string & str) const;

        /// get argument list
        /// @param str argument short name
        /// @return corresponding argument list
        /// @throws ccdb::error::no_such_argument argument not found in parsed list
        [[nodiscard]] std::string operator[](const std::string & str) const { return at(str); }

        friend class ArgumentParser;
    };

    class ArgumentParser {
        ParsedArgumentType parsedArgument_;

    public:
        /// parse argument
        /// @param argc argc
        /// @param argv argv
        /// @param PreDefinedArgs Pre-defined arguments
        ArgumentParser(int argc, char ** argv, const PreDefinedArgumentType & PreDefinedArgs);

        /// get parsed arguments
        /// @return Parsed argument (ParsedArgumentType)
        ParsedArgumentType parse() { return parsedArgument_; }
    };
}

#endif //CCDB_ARGS_H