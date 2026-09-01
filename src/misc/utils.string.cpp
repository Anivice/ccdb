#include "absl/strings/str_format.h"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <regex>
#include <iostream>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include <stdexcept>
#include "json.hpp"
#include "ncursesw/ncurses.h"
#include "ncursesw/term.h"
#include "tsl/hopscotch_map.h"
#include "readline/history.h"
#include "utils.h"
#include "dump.h"

#ifndef __NR_memfd_create
# if defined(__x86_64__)
#  define __NR_memfd_create 319
# elif defined(__i386__)
#  define __NR_memfd_create 356
# elif defined(__aarch64__)
#  define __NR_memfd_create 279
# else
#  error "Unknown architecture"
# endif
#endif

#define STRX(x) #x
#define STR(x) JSON_STRX(x)
#define CASSERT(x)  \
if (!(x)) {         \
    std::cout << __FILE__ ":" STR(__LINE__) ": Assertion " #x " Failed!\n"; \
    _exit(EXIT_FAILURE); \
}

std::string ccdb::utils::unpack_string(const unsigned char str[], const unsigned int len)
{
    std::vector<uint8_t> data(len);
    std::memcpy(data.data(), str, len);
    const auto ret = decompress(data);
    std::vector<char> char_str(ret.size());
    std::memcpy(char_str.data(), ret.data(), char_str.size());
    return { char_str.begin(), char_str.end() };
}

std::string ccdb::utils::getenv(const std::string& name) noexcept
{
    const auto var = secure_getenv(name.c_str());
    if (var == nullptr) {
        return "";
    }

    return var;
}

void ccdb::utils::setenv(const std::string &name, const std::string &value) noexcept
{
    ::setenv(name.c_str(), value.c_str(), 1);
}

std::vector<std::string> ccdb::utils::splitString(const std::string& s, const char delim)
{
    std::vector<std::string> parts;
    std::string token;
    std::stringstream ss(s);

    while (std::getline(ss, token, delim)) {
        parts.push_back(token);
    }

    return parts;
}

std::string ccdb::utils::replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement) noexcept
{
    if (target.empty()) return original; // Avoid infinite loop if target is empty

    if (target.size() == 1 && replacement.empty()) {
        std::erase_if(original, [&target](const char c) { return c == target[0]; });
        return original;
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }

    return original;
}

namespace {
    struct regex_replace_callback_cache_t
    {
        std::vector<std::string> matched_string;
        bool is_regex_replace;
    };

    using regex_replace_callback_cache_array_t = std::vector<regex_replace_callback_cache_t>;
    using ScopeType = std::vector<std::string>;
    using ReplacerFuncType = std::function<std::string(const std::pair<ScopeType::const_iterator, ScopeType::const_iterator> &)>;
    bool DISABLE_CACHE_BEHAVIOR = ccdb::utils::getenv("DISABLE_CACHE_BEHAVIOR") == "true";

    std::string regex_replace_callback(
        const std::string& input,
        const std::string& pattern,
        const ReplacerFuncType & replacer,
        regex_replace_callback_cache_array_t & cache_array)
    {
        std::string result;
        if (cache_array.empty())
        {
            thread_local static tsl::hopscotch_map<std::string, std::regex> reg_cache;
            const std::regex * reg = nullptr;
            std::unique_ptr<std::regex> regex_ptr;
            if (const auto it = reg_cache.find(pattern); it != reg_cache.end()) {
                reg = &it->second;
            } else {
                regex_ptr = std::make_unique<std::regex>(pattern);
                reg = regex_ptr.get();
                if (!DISABLE_CACHE_BEHAVIOR) {
                    reg_cache.emplace(pattern, *reg);
                }
            }

            std::sregex_iterator it(input.begin(), input.end(), *reg);
            std::size_t last_pos = 0;

            for (const std::sregex_iterator end; it != end; ++it)
            {
                // Append text before match
                result.append(input, last_pos, it->position() - last_pos);
                cache_array.emplace_back(regex_replace_callback_cache_t{
                    .matched_string = { input.substr(last_pos, it->position() - last_pos) },
                    .is_regex_replace = false
                });

                // Call user function to generate replacement
                cache_array.emplace_back(regex_replace_callback_cache_t{
                    .matched_string = { it->begin(), it->end() },
                    .is_regex_replace = true
                });
                result.append(replacer({cache_array.back().matched_string.begin(), cache_array.back().matched_string.end()}));
                last_pos = it->position() + it->length();
            }

            result.append(input, last_pos, std::string::npos);
            cache_array.emplace_back(regex_replace_callback_cache_t{
                .matched_string = { input.substr(last_pos, std::string::npos) },
                .is_regex_replace = false
            });

            std::erase_if(cache_array, [](const regex_replace_callback_cache_t & list)->bool {
                return list.matched_string.empty() || std::ranges::all_of(list.matched_string,
                    [](const auto & matched_string) { return matched_string.empty(); });
            });
        }
        else
        {
            for (const auto & [str, rcpr] : cache_array) {
                if (rcpr)
                    result.append(replacer({ str.begin(), str.end() }));
                else
                    result.append(str.front());
            }
        }

        return result;
    }
}

std::string ccdb::utils::regex_replace_all(
    std::string & original, const std::string &pattern,
    const std::function <std::string(const regex_scope_type &) > & replacement)
{
    thread_local static std::unordered_map < std::string, regex_replace_callback_cache_array_t > result_cache;
    const auto hash = original + pattern;
    if (const auto it = result_cache.find(hash); it != result_cache.end() && !it->second.empty()) {
        original = regex_replace_callback(original, pattern, replacement, it->second);
        return original;
    }

    if (result_cache.size() > 8192) result_cache.clear();
    regex_replace_callback_cache_array_t cache_array;
    original = regex_replace_callback(original, pattern, replacement, cache_array);
    result_cache.emplace(hash, cache_array);
    return original;
}

std::string ccdb::utils::value_to_human(
    const uint64_t value,
    const uint64_t p,
    const std::vector<std::string>& lvs)
{
    if (lvs.empty()) {
        throw std::runtime_error("lvs is empty");
    }

    if (p < 2) {
        throw std::invalid_argument("p must be greater than 1");
    }

    std::stringstream ss;

    if (value == 0) {
        ss << "0 " << lvs.front();
        return ss.str();
    }

    std::size_t level = 0;
    uint64_t scale = 1;

    while (level + 1 < lvs.size() && value / scale >= p) {
        scale *= p;
        ++level;
    }

    const long double human_value =
        static_cast<long double>(value) /
        static_cast<long double>(scale);

    ss << std::fixed
       << std::setprecision(2)
       << human_value
       << " "
       << lvs[level];

    return ss.str();
}

std::vector<std::string> ccdb::utils::split_via_history(const std::string &line, const std::string& delims)
{
    static std::mutex readline_mutex;
    std::lock_guard<std::mutex> lock(readline_mutex);
    char * before = history_word_delimiters;
    history_word_delimiters = const_cast<char *>(delims.c_str());

    char** toks = history_tokenize(line.c_str());
    std::vector<std::string> out;
    if (!toks) return out;

    for (char** p = toks; *p; ++p) {
        out.emplace_back(*p);
        std::free(*p);
    }
    std::free(toks);

    history_word_delimiters = before;
    return out;
}

std::string ccdb::utils::second_to_human_readable(unsigned long long value)
{
    if (value < 60) {
        return std::to_string(value) + "s";
    }

    if (value < 60 * 60)
    {
        return std::to_string(value / 60) + "m " + second_to_human_readable(value % 60);
    }

    if (value < 60 * 60 * 24) {
        return std::to_string(value / (60 * 60)) + "h " + second_to_human_readable(value % (60 * 60));
    }

    const unsigned long long day = value / (60 * 60 * 24);
    value %= (60 * 60 * 24);
    return std::to_string(day) + "d " + second_to_human_readable(value);
}

std::u32string ccdb::utils::utf8_to_u32(const std::string &s)
{
    static thread_local tsl::hopscotch_map <std::string, std::u32string> cache;
    if (const auto it = cache.find(s); it != cache.end()) {
        return it->second;
    }

    if (cache.size() > 8192) cache.clear();
    std::u32string result;
    utf8::utf8to32(s.begin(), s.end(), std::back_inserter(result));
    cache.emplace(s, result);
    return result;
}

int ccdb::utils::UnicodeDisplayWidth::get_width_utf8(const std::string &utf8_str) {
    return get_width_utf32(utf8_to_u32(utf8_str));
}

int ccdb::utils::UnicodeDisplayWidth::get_width_utf32(const std::u32string &utf32_str)
{
    int width = 0;
    for (const char32_t c : utf32_str) {
        width += get_char_width(c);
    }

    return width;
}

bool ccdb::utils::NO_0xFE0F_EXPAND_EMOJI = ccdb::utils::getenv("NO_0xFE0F_EXPAND_EMOJI") == "true";

int ccdb::utils::UnicodeDisplayWidth::get_char_width(const char32_t c)
{
    if (c == 0x200D || (c >= 0xFE00 && c < 0xFE0F)) {
        return 0;
    }

    if (c == 0xFE0F)
    {
        // when this is printed onto screen, it means an additional color code that expand the emoji
        // this doesn't apply to all the terminals, so fucking headaches
        // you can just disable this by setting the environment variable NO_0xFE0F_EXPAND_EMOJI to true
        // if your terminal doesn't really process this flag
        if (NO_0xFE0F_EXPAND_EMOJI) {
            return 0;
        }
        return 1;
    }

    if (c >= 0x1F3FB && c <= 0x1F3FF) {
        return 0; // These don't add width
    }

    if (c >= 0x1F1E6 && c <= 0x1F1FF) {
        return 2; // Flags are typically 2 cells
    }

    const auto wc = static_cast<wchar_t>(c);

    if (const int w = wcwidth(wc); w >= 0) {
        return w;
    }

    return fallback_char_width(c);
}

int ccdb::utils::UnicodeDisplayWidth::fallback_char_width(const char32_t c)
{
    if (c <= 0x1F || (c >= 0x7F && c <= 0x9F)) {
        return 0;
    }

    if (is_fullwidth(c)) {
        return 2;
    }

    return 1;
}

bool ccdb::utils::UnicodeDisplayWidth::is_fullwidth(const char32_t c)
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


std::string ccdb::utils::strip_color(std::string str_)
{
    constexpr auto color_pattern = R"(\x1B\[(?:\d*(?:;\d*)*)?m)";
    return regex_replace_all(str_, color_pattern, [](const auto &)->std::string {
        return "";
    });
}

void ccdb::utils::set_progress_bar(const progress_bar_state_t state, const int percentages)
{
    std::stringstream ss;
    ss << "\033]9;4;" << state << ";" << percentages << "\033\\";
    const std::string & str = ss.str();
    if (const int fd = open("/dev/stdout", O_WRONLY); fd > 0)
    {
        (void)write(fd, str.c_str(), str.length());

        if (state == SET_PROGRESS)
        {
            const auto col_size = get_col_size();
            std::stringstream ss2; ss2 << "] (" << percentages << "%)";
            const auto str_p = ss2.str();
            if (const auto len = col_size - 1 - str_p.length(); len > 0)
            {
                const auto p = static_cast<int>(static_cast<double>(len) * static_cast<double>(percentages) / 100.00);
                const auto l = len > p ? static_cast<int>(len - p) : 0;
                std::stringstream ss3;
                ss3 << "\r" << "[" << std::string(p, '=') << std::string(l, ' ') << str_p;
                const auto str3 = ss3.str();
                (void)write(fd, str3.c_str(), str3.length());
            }
        }

        close(fd);
    }
}

static void encode_dump94(input_stream_t & in, output_stream_t & out)
{
    using namespace ccdb::utils;
	try {
        CRC64 crc64;
        std::stringstream oss;
        const auto [len, hash] = encode<CharacterDictionary.size()>(in, oss, CharacterDictionary, [&crc64](const char * data, const uint64_t len, uint64_t &){
            crc64.update(reinterpret_cast<const uint8_t*>(data), len);
        });
		out << dump_start_signature << std::endl;
		out << header << len << std::endl;
        out << hash_header << crc64.get_checksum_str() << std::endl;
		const auto line_size = std::strlen(dump_start_signature);
		std::vector < char > buff(line_size, 0);
		while (oss)
		{
			oss.read(buff.data(), static_cast<std::streamsize>(line_size));
			const std::streamsize bytes_read = oss.gcount();
			if (bytes_read <= 0) break;
			out.write(buff.data(), bytes_read);
			out << std::endl;
		}
		out << dump_end_signature << std::endl;
	} catch (std::exception & e) {
		throw std::runtime_error(e.what());
	}
}

static void decode_dump94(input_stream_t & in, output_stream_t & out)
{
    using namespace ccdb::utils;
	std::stringstream iss;
	std::string line;
	while (std::getline(in, line)) {
		while (!line.empty() && (line.front() <= 20 || line.front() >= 0x7F)) line.erase(line.begin());
		while (!line.empty() && (line.back() <= 20 || line.back() >= 0x7F)) line.pop_back();
		if (line == dump_start_signature) break;
	}

	uint64_t size = 0;
	bool first_line = true;
    std::string crc64_value;
	while (std::getline(in, line))
	{
		while (!line.empty() && (line.front() <= 20 || line.front() >= 0x7F)) line.erase(line.begin());
		while (!line.empty() && (line.back() <= 20 || line.back() >= 0x7F)) line.pop_back();
		if (line == dump_end_signature) break;
		if (first_line)
		{
			first_line = false;
            auto read_header = [](const char * header_, std::string & line_)
            {
                if (line_.size() <= std::strlen(header_) || line_.substr(0, std::strlen(header_)) != header_) {
                    throw std::runtime_error("Invalid encoded data format: missing header");
                }

                line_ = line_.substr(std::strlen(header_));
            };

            read_header(header, line);
			size = convertToNumber<decltype(size)>(line);
            std::getline(in, line);
            read_header(hash_header, line);
            crc64_value = line;
            while (!crc64_value.empty() && crc64_value.front() <= ' ') crc64_value.erase(crc64_value.begin());
            while (!crc64_value.empty() && crc64_value.back() <= ' ') crc64_value.pop_back();
			continue;
		}

		iss << line;
	}

    std::stringstream out_;
	decode<CharacterDictionary.size()>(iss, out_, CharacterDictionary, size);
    const auto & str = out_.str();
    CRC64 crc64;
    crc64.update(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    if (crc64.get_checksum_str() != crc64_value) throw std::runtime_error("Corrupted stream!");
    out.write(str.data(), static_cast<std::streamsize>(str.size()));
}

void ccdb::utils::exportBinary(const std::vector<uint8_t>& data, std::basic_ostream<char>& out)
{
    const std::string binaryStream { reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<const char*>(data.data()) + data.size() };
    std::istringstream iss { binaryStream };
    encode_dump94(iss, out);
}

std::vector<uint8_t> ccdb::utils::importBinary(std::basic_istream<char>& in)
{
    std::ostringstream oss;
    decode_dump94(in, oss);
    const auto & data = oss.str();
    return { reinterpret_cast<const uint8_t*>(data.data()),
        reinterpret_cast<const uint8_t*>(data.data()) + data.size() };
}