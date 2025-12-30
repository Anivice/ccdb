#ifndef CFS_UTILS_H
#define CFS_UTILS_H

#include <string>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "utf8.h"
#include "colors.h"
#include <atomic>

/// Utilities
namespace ccdb::utils {
    /// Get environment variable (safe)
    /// @param name Name of the environment variable
    /// @return Return the environment variable, or empty string if unset
    std::string getenv(const std::string& name) noexcept;

    std::vector<std::string> splitString(const std::string& s, char delim = ' ');

    /// Replace string inside a string
    /// @param original Original string
    /// @param target String to be replaced
    /// @param replacement Replacement string
    /// @return Replaced string. Original string will be modified as well
    std::string replace_all(
        std::string & original,
        const std::string & target,
        const std::string & replacement) noexcept;

    /// Get Row and Column size from terminal
    /// @return Pair in [Col (x), Row (y)], or 80x25 if all possible attempt failed
    std::pair < const int, const int > get_screen_row_col() noexcept;

    inline int get_col_size() {
        return get_screen_row_col().second;
    }

    inline int get_line_size() {
        return get_screen_row_col().first;
    }

    /// Return current UNIX timestamp
    /// @return Current UNIX timestamp
    uint64_t get_timestamp() noexcept;

    /// Return current timespec
    /// @return Current timespec
    timespec get_timespec() noexcept;

    std::string value_to_human(unsigned long long value,
        const std::string & lv1, const std::string & lv2,
        const std::string & lv3, const std::string & lv4);

    inline std::string value_to_speed(const unsigned long long value) {
        return value_to_human(value, "B/s", "KB/s", "MB/s", "GB/s");
    }

    inline std::string value_to_size(const unsigned long long value) {
        return value_to_human(value, "B", "KB", "MB", "GB");
    }

    std::string second_to_human_readable(unsigned long long value);
    std::u32string utf8_to_u32(const std::string& s);

    constexpr char clear[] = { 0x1b, 0x5b, 0x48, 0x1b, 0x5b, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x4a };

    class UnicodeDisplayWidth {
    public:
        static int get_width_utf8(const std::string& utf8_str);
        static int get_width_utf32(const std::u32string& utf32_str);

    private:
        static int get_char_width(char32_t c);
        static int fallback_char_width(char32_t c);
        static bool is_fullwidth(char32_t c);
    };

    struct cmd_status
    {
        std::string fd_stdout; // normal output
        std::string fd_stderr; // error information
        int exit_status{}; // exit status
    };

    cmd_status exec_command_(const std::string &, const std::vector<std::string> &, const std::string &);

    // execute commands for pager specific programs
    template <typename... Strings>
    cmd_status exec_command(const std::string& cmd, const std::string &input, Strings&&... args)
    {
        const std::vector<std::string> vec{std::forward<Strings>(args)...};
        return exec_command_(cmd, vec, input);
    }

    bool is_less_available();
}

#endif //CFS_UTILS_H