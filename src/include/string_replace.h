#ifndef CCDB_STRING_REPLACE_H
#define CCDB_STRING_REPLACE_H

#include <algorithm>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "srell/srell.hpp"

namespace ccdb::utils
{
    template<class Char, class Traits = std::char_traits<Char>>
    using basic_regex_scope_type = std::pair<
        typename std::vector<std::basic_string<Char, Traits>>::const_iterator,
        typename std::vector<std::basic_string<Char, Traits>>::const_iterator>;

    using regex_scope_type = basic_regex_scope_type<char>;

    // Deduce character type/traits from original; literals can still be passed
    // for target and replacement, as with the former std::string overload.
    template<class Char, class Traits>
    std::basic_string<Char, Traits> replace_all(
        std::basic_string<Char, Traits> & original,
        const std::type_identity_t<std::basic_string<Char, Traits>> & target,
        const std::type_identity_t<std::basic_string<Char, Traits>> & replacement) noexcept
    {
        if (target.empty()) return original;
        if (target.size() == 1 && replacement.empty()) {
            std::erase_if(original, [&](const Char c) { return Traits::eq(c, target.front()); });
            return original;
        }
        typename std::basic_string<Char, Traits>::size_type position = 0;
        while ((position = original.find(target, position)) != original.npos) {
            original.replace(position, target.size(), replacement);
            position += replacement.size();
        }
        return original;
    }

    namespace detail
    {
        bool regex_compilation_cache_disabled() noexcept;

        template<class String> struct replacement_string_hash
        {
            std::size_t operator()(const String & value) const noexcept
            {
                if constexpr (requires { std::hash<String>{}(value); }) {
                    return std::hash<String>{}(value);
                } else {
                    // std::hash does not cover basic_string with custom traits.
                    std::size_t hash = 0;
                    for (const auto c : value) hash = hash * 131 + static_cast<std::size_t>(c);
                    return hash;
                }
            }
        };

        template<class String> struct replacement_string_equal
        {
            bool operator()(const String & a, const String & b) const noexcept
            {
                // Cache actual code units, even when custom traits treat
                // different characters as equal during literal replacement.
                return std::equal(a.begin(), a.end(), b.begin(), b.end());
            }
        };

        template<class String> struct regex_replacement_cache
        {
            using char_type = String::value_type;
            // Preserve existing narrow-string regex behavior. Unicode regexes
            // must consume code points even on platforms with 16-bit wchar_t.
            using regex_type = std::conditional_t<std::is_same_v<char_type, char>,
                std::regex, srell::basic_regex<char_type>>;
            using iterator_type = std::conditional_t<std::is_same_v<char_type, char>,
                std::regex_iterator<typename String::const_iterator>,
                srell::regex_iterator<typename String::const_iterator>>;

            struct fragment
            {
                std::vector<String> captures; // Full match first, then capture groups.
                bool is_match;
            };
            using fragments = std::vector<fragment>;
            struct key
            {
                String input;
                String pattern;
                bool operator==(const key & other) const noexcept {
                    const replacement_string_equal<String> equal;
                    return equal(input, other.input) && equal(pattern, other.pattern);
                }
            };
            struct key_hash
            {
                std::size_t operator()(const key & value) const noexcept
                {
                    const replacement_string_hash<String> hash;
                    return hash(value.input) ^ (hash(value.pattern) << 1);
                }
            };

            // Shared by all callbacks for a string type; cache matches, never
            // replacement results, so callback side effects run on every call.
            inline static thread_local std::unordered_map<String, regex_type,
                replacement_string_hash<String>, replacement_string_equal<String>> patterns;
            inline static thread_local std::unordered_map<key, fragments, key_hash> matches;
        };
    }

    // Modify original and return it. Callback captures have the same string type
    // and traits as original, including unmatched/empty capture groups.
    template<class Char, class Traits, class Replacement>
    std::basic_string<Char, Traits> regex_replace_all(
        std::basic_string<Char, Traits> & original,
        const std::type_identity_t<std::basic_string<Char, Traits>> & pattern,
        Replacement && replacement)
    {
        using string_type = std::basic_string<Char, Traits>;
        using cache = detail::regex_replacement_cache<string_type>;
        const typename cache::key key{original, pattern};
        string_type result;
        if (const auto found = cache::matches.find(key); found != cache::matches.end())
        {
            for (const auto & fragment : found->second) {
                if (fragment.is_match)
                    result.append(replacement(basic_regex_scope_type<Char, Traits>{
                        fragment.captures.begin(), fragment.captures.end()}));
                else
                    result.append(fragment.captures.front());
            }
            original = std::move(result);
            return original;
        }

        const typename cache::regex_type * regex;
        std::unique_ptr<typename cache::regex_type> uncached_regex;
        if (detail::regex_compilation_cache_disabled()) {
            uncached_regex = std::make_unique<typename cache::regex_type>(pattern.begin(), pattern.end());
            regex = uncached_regex.get();
        } else {
            regex = &cache::patterns.try_emplace(pattern, pattern.begin(), pattern.end()).first->second;
        }

        typename cache::fragments fragments;
        typename string_type::size_type last_position = 0;
        const auto append_literal = [&](const auto position, const auto length)
        {
            if (const auto literal = original.substr(position, length); !literal.empty()) {
                result.append(literal);
                fragments.push_back({{std::move(literal)}, false});
            }
        };
        using iterator = cache::iterator_type;
        for (iterator match(original.begin(), original.end(), *regex), end; match != end; ++match)
        {
            append_literal(last_position, match->position() - last_position);
            typename cache::fragment fragment{{}, true};
            fragment.captures.reserve(match->size());
            for (const auto & capture : *match)
                fragment.captures.emplace_back(capture.first, capture.second);
            result.append(replacement(basic_regex_scope_type<Char, Traits>{
                fragment.captures.begin(), fragment.captures.end()}));
            fragments.push_back(std::move(fragment));
            last_position = match->position() + match->length();
        }
        append_literal(last_position, string_type::npos);
        if (const auto DISABLE_CACHE_BEHAVIOR = ::getenv("DISABLE_CACHE_BEHAVIOR");
            DISABLE_CACHE_BEHAVIOR && std::string(DISABLE_CACHE_BEHAVIOR) == "true")
        {
            cache::matches.clear();
        } else if (cache::matches.size() > 8192) {
            cache::matches.clear();
        }

        cache::matches.emplace(key, std::move(fragments));
        original = std::move(result);
        return original;
    }

    template<class Char, class Traits>
    std::basic_string<Char, Traits> strip_color(std::basic_string<Char, Traits> text)
    {
        constexpr char expression[] = R"(\x1B\[(?:\d*(?:;\d*)*)?m)";
        static const std::basic_string<Char, Traits> pattern(expression, expression + sizeof(expression) - 1);
        return regex_replace_all(text, pattern, [](const auto &) {
            return std::basic_string<Char, Traits>{};
        });
    }
}

#endif
