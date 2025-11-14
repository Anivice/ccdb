/* log.h
 *
 * Copyright 2025 Anivice Ives
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>
#include <mutex>
#include <map>
#include <unordered_map>
#include <atomic>
#include <iomanip>
#include <ranges>
#include <vector>
#include <tuple>      // for std::tuple, std::make_tuple
#include <utility>    // for std::forward
#include <any>
#include <cstring>
#include <regex>
#include <chrono>
#include <functional>
#include <cstddef>
#include <type_traits>
#include <source_location>
#include <algorithm>
#include <sys/stat.h>

#define construct_simple_type_compare(type)                             \
    template <typename T>                                               \
    struct is_##type : std::false_type {};                              \
    template <>                                                         \
    struct is_##type<type> : std::true_type { };                        \
    template <typename T>                                               \
    constexpr bool is_##type##_v = is_##type<T>::value;

namespace Logger {
    namespace color {
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

    template <typename T, typename = void>
    struct is_container : std::false_type
    {
    };

    template <typename T>
    struct is_container<T,
        std::void_t<decltype(std::begin(std::declval<T>())),
        decltype(std::end(std::declval<T>()))>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_container_v = is_container<T>::value;

    template <typename T>
    struct is_map : std::false_type
    {
    };

    template <typename Key, typename Value>
    struct is_map<std::map<Key, Value>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_map_v = is_map<T>::value;

    template <typename T>
    struct is_unordered_map : std::false_type
    {
    };

    template <typename Key, typename Value, typename Hash, typename KeyEqual,
        typename Allocator>
    struct is_unordered_map<
        std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>>
        : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_unordered_map_v = is_unordered_map<T>::value;

    template <typename T>
    struct is_string : std::false_type
    {
    };

    template <>
    struct is_string<std::string> : std::true_type
    {
    };

    template <>
    struct is_string<const char*> : std::true_type
    {
    };

    template <>
    struct is_string<std::string_view> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_string_v = is_string<T>::value;

    construct_simple_type_compare(bool);

    inline class move_front_t {} move_front;
    construct_simple_type_compare(move_front_t);

    inline class cursor_off_t {} cursor_off;
    construct_simple_type_compare(cursor_off_t);

    inline class cursor_on_t {} cursor_on;
    construct_simple_type_compare(cursor_on_t);

    inline class debug_log_t {} debug_log;
    construct_simple_type_compare(debug_log_t);

    inline class info_log_t {} info_log;
    construct_simple_type_compare(info_log_t);

    inline class warning_log_t {} warning_log;
    construct_simple_type_compare(warning_log_t);

    inline class error_log_t {} error_log;
    construct_simple_type_compare(error_log_t);

    template <typename T>
    struct is_pair : std::false_type
    {
    };

    template <typename Key, typename Value>
    struct is_pair<std::pair<Key, Value>> : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_pair_v = is_pair<T>::value;

    inline std::string strip_func_name(std::string name)
    {
        if (const auto p = name.find('('); p != std::string::npos) {
            name.erase(p);
        }

        if (const auto p = name.rfind(' '); p != std::string::npos) {
            name.erase(0, p + 1);
        }

        return name;
    }

    template <typename T, typename Tag>
    struct strong_typedef
    {
        T value;
        strong_typedef() = default;
        explicit strong_typedef(T v) : value(std::move(v)) {}
        T& get() noexcept { return value; }
        [[nodiscard]] const T& get() const noexcept { return value; }
        explicit operator T&() noexcept { return value; }
        explicit operator const T&() const noexcept { return value; }
    };

    struct prefix_string_tag {};
    using prefix_string_t = strong_typedef<std::string, prefix_string_tag>;

    class Logger
    {
    protected:
        std::mutex log_mutex;
        unsigned int log_level;
        std::atomic_uint filter_level;
        bool endl_found_in_last_log;
        std::ostream * output;

    public:
        void set_log_level(unsigned int _log_level)
        {
            if (_log_level > 3) _log_level = 3;
            filter_level = _log_level;
        }

        explicit Logger()
        {
            log_level = 1;
            endl_found_in_last_log = true;
            output = nullptr;

            if (const auto log_level_env = color::get_env("LOG_LEVEL"); !log_level_env.empty())
            {
                try {
                    filter_level = std::stoi(log_level_env, nullptr, 10);
                } catch (...) {
#ifdef DEBUG
                    filter_level = !!!DEBUG;
#else
                    filter_level = 2;
#endif
                }

                if (filter_level > 3)
                {
                    filter_level = 3;
                }
            }

            output = &std::cout;
            if (const auto log_level_env = color::get_env("LOG_OUTPUT"); !log_level_env.empty())
            {
                std::string log_output = log_level_env;
                std::ranges::transform(log_output, log_output.begin(), ::tolower);
                if (log_output == "stderr")
                {
                    output = &std::cerr;
                }
                else
                {
                    output = &std::cout;
                }
            }
        }

    private:
        template <typename Container>
        requires (is_container_v<Container> &&
            !is_map_v<Container> &&
            !is_unordered_map_v<Container>)
        void print_container(const Container& container)
        {
            constexpr uint64_t max_elements = 8;
            uint64_t num_elements = 0;
            _log("[");
            for (auto it = std::begin(container); it != std::end(container); ++it)
            {
                if (num_elements == max_elements)
                {
                    _log("\n");
                    _log("    ");
                    num_elements = 0;
                }

                if (sizeof(*it) == 1 /* 8bit data width */)
                {
                    const auto & tmp = *it;
                    _log("0x", std::hex, std::setw(2), std::setfill('0'),
                        static_cast<unsigned>((*(uint8_t *)(&tmp)) & 0xFF)); // ugly workarounds
                }
                else {
                    _log(*it);
                }

                if (std::next(it) != std::end(container)) {
                    _log(", ");
                }

                num_elements++;
            }
            _log("]");
        }

        template <typename Map>
        requires (is_map_v<Map> || is_unordered_map_v<Map>)
        void print_container(const Map & map)
        {
            _log("{");
            for (auto it = std::begin(map); it != std::end(map); ++it)
            {
                _log(it->first);
                _log(": ");
                _log(it->second);
                if (std::next(it) != std::end(map)) {
                    _log(", ");
                }
            }
            _log("}");
        }

        template <typename ParamType> void _log(const ParamType& param)
        {
            // NOLINTBEGIN(clang-diagnostic-repeated-branch-body)
            if constexpr (is_string_v<ParamType>) { // if we don't do it here, it will be assumed as a container
                *output << param;
            }
            else if constexpr (is_container_v<ParamType>) {
                print_container(param);
            }
            else if constexpr (is_bool_v<ParamType>) {
                *output << (param ? "True" : "False");
            }
            else if constexpr (is_pair_v<ParamType>) {
                *output << "<";
                _log(param.first);
                *output << ": ";
                _log(param.second);
                *output << ">";
            }
            else if constexpr (is_move_front_t_v<ParamType>) {
                *output << "\033[F\033[K";
            }
            else if constexpr (is_cursor_off_t_v<ParamType>) {
                *output << "\033[?25l";
            }
            else if constexpr (is_cursor_on_t_v<ParamType>) {
                *output << "\033[?25h";
            }
            else if constexpr (is_debug_log_t_v<ParamType>) {
                log_level = 0;
            }
            else if constexpr (is_info_log_t_v<ParamType>) {
                log_level = 1;
            }
            else if constexpr (is_warning_log_t_v<ParamType>) {
                log_level = 2;
            }
            else if constexpr (is_error_log_t_v<ParamType>) {
                log_level = 3;
            }
            else {
                *output << param;
            }
            // NOLINTEND(clang-diagnostic-repeated-branch-body)
        }

        template <typename T, typename Tag> void _log(const strong_typedef<T, Tag>&) { }

        template <typename ParamType, typename... Args>
        void _log(const ParamType& param, const Args &...args)
        {
            _log(param);
            (_log(args), ...);
        }

        template < typename StringType >
        requires (std::is_same_v<StringType, std::string> || std::is_same_v<StringType, std::string_view>)
        bool _do_i_show_caller_next_time_(const StringType & str)
        {
            return (!str.empty() && str.back() == '\n');
        }

        bool _do_i_show_caller_next_time_(const char * str)
        {
            return (strlen(str) > 0 && str[strlen(str) - 1] == '\n');
        }

        bool _do_i_show_caller_next_time_(const char c)
        {
            return (c == '\n');
        }

        template < typename ClassType >
        requires (!std::is_same_v<ClassType, const char *> && !std::is_same_v<ClassType, const char>
            && !(std::is_same_v<ClassType, std::string> || std::is_same_v<ClassType, std::string_view>))
        bool _do_i_show_caller_next_time_ (const ClassType &)
        {
            return false;
        }

        template<typename T>
        struct is_char_array : std::false_type {};

        template<std::size_t N>
        struct is_char_array<char[N]> : std::true_type {};

        template<std::size_t N>
        struct is_char_array<const char[N]> : std::true_type {};

        template<std::size_t... Is, class Tuple>
        constexpr auto tuple_drop_impl(Tuple&& t, std::index_sequence<Is...>) {
            // shift indices by 1 to skip the head
            return std::forward_as_tuple(std::get<Is + 1>(std::forward<Tuple>(t))...);
        }

        template<class Tuple>
        constexpr auto tuple_drop1(Tuple&& t) {
            constexpr std::size_t N = std::tuple_size_v<std::remove_reference_t<Tuple>>;
            static_assert(N > 0, "cannot drop from empty tuple");
            return tuple_drop_impl(std::forward<Tuple>(t), std::make_index_sequence<N - 1>{});
        }

    public:
        template <typename... Args> void log(const Args &...args)
        {
            std::lock_guard lock(log_mutex);
            static_assert(sizeof...(Args) > 0, "log(...) requires at least one argument");
            auto ref_tuple = std::forward_as_tuple(args...);
            using LastType = std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>;
            using FirstType = std::tuple_element_t<0, std::tuple<Args...>>;
            const std::any last_arg = std::get<sizeof...(Args) - 1>(ref_tuple);
            std::string user_prefix_addon;

            auto call_log = [&]<class... Ts>(Ts&&... xs) -> decltype(auto) {
                return _log(std::forward<Ts>(xs)...);
            };

            if constexpr (std::is_same_v<FirstType, prefix_string_t>) // [PREFIX] [...]
            {
                const std::any first_arg = std::get<0>(ref_tuple);
                user_prefix_addon = std::any_cast<prefix_string_t>(first_arg).value;
            }

            if (endl_found_in_last_log)
            {
                if (!user_prefix_addon.empty()) // Addon and second elements are all present
                {
                    if constexpr (sizeof...(Args) >= 2)
                    {
                        using SecondType = std::tuple_element_t<1, std::tuple<Args...>>;
                        // instead of checking the first, we check the second
                        if constexpr (is_debug_log_t_v<SecondType>) {
                            log_level = 0;
                        }
                        else if constexpr (is_info_log_t_v<SecondType>) {
                            log_level = 1;
                        }
                        else if constexpr (is_warning_log_t_v<SecondType>) {
                            log_level = 2;
                        }
                        else if constexpr (is_error_log_t_v<SecondType>) {
                            log_level = 3;
                        }
                    }
                }
                else // else, we check the first for the presence of log level
                {
                    if constexpr (is_debug_log_t_v<FirstType>) {
                        log_level = 0;
                    }
                    else if constexpr (is_info_log_t_v<FirstType>) {
                        log_level = 1;
                    }
                    else if constexpr (is_warning_log_t_v<FirstType>) {
                        log_level = 2;
                    }
                    else if constexpr (is_error_log_t_v<FirstType>) {
                        log_level = 3;
                    }
                }

                const auto now = std::chrono::system_clock::now();
                std::string prefix;
                switch (log_level)
                {
                case 0: prefix =  color::color(1,1,1) + "[DEBUG]"; break;
                case 1: prefix =  color::color(5,5,5) + "[INFO]"; break;
                case 2: prefix =  color::color(0,5,5) + "[WARNING]"; break;
                case 3: prefix =  color::color(5,0,0) + "[ERROR]"; break;
                default: prefix = color::color(5,5,5) + "[INFO]"; break;
                }

                // now, check if the log level is printable or masked
                if (log_level < filter_level)
                {
                    return;
                }


                _log(color::color(0, 2, 2), std::format(
#ifdef _EU_STYLE_DAY_MONTH_YEAR_
                        "{:%d-%m-%Y %H:%M:%S}",
#else
                        "{:%Y-%m-%d %H:%M:%S}",
#endif //_EU_STYLE_DAY_MONTH_YEAR_
                    now), " ",
                    user_prefix_addon,
                    prefix, ": ", color::no_color());
            }

            if constexpr (!is_char_array<LastType>::value)
            {
                endl_found_in_last_log = _do_i_show_caller_next_time_(std::any_cast<LastType>(last_arg));
            }
            else
            {
                endl_found_in_last_log = _do_i_show_caller_next_time_(std::any_cast<const char*>(last_arg));
            }

            if (user_prefix_addon.empty())
            {
                _log(args...);
            }
            else if constexpr (sizeof...(Args) >= 2)
            {
                std::apply(call_log, tuple_drop1(ref_tuple));
            }
        }
    };
}

#define logPrint(...)  log(Logger::prefix_string_t(Logger::color::color(2,3,4) + "(" + Logger::strip_func_name(std::source_location::current().function_name()) + ") "), __VA_ARGS__)
#define DEBUG_LOG       (Logger::debug_log)
#define INFO_LOG        (Logger::info_log)
#define WARNING_LOG     (Logger::warning_log)
#define ERROR_LOG       (Logger::error_log)

#define debugLogPrint(...)      logPrint(DEBUG_LOG, __VA_ARGS__)
#define infoLogPrint(...)       logPrint(INFO_LOG, __VA_ARGS__)
#define warningLogPrint(...)    logPrint(WARNING_LOG, __VA_ARGS__)
#define errorLogPrint(...)      logPrint(ERROR_LOG, __VA_ARGS__)

#define dlog(...) debugLogPrint(__VA_ARGS__)
#define ilog(...) infoLogPrint(__VA_ARGS__)
#define wlog(...) warningLogPrint(__VA_ARGS__)
#define elog(...) errorLogPrint(__VA_ARGS__)

#endif // LOG_HPP
