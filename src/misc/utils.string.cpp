#ifndef __WINDOWS_MSVC_BUILD__
# include "absl/strings/str_format.h"
#endif //__WINDOWS_MSVC_BUILD__
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#ifndef __WINDOWS_MSVC_BUILD__
# include <unistd.h>
#endif //__WINDOWS_MSVC_BUILD__
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
#ifndef __WINDOWS_MSVC_BUILD__
# include <sys/syscall.h>
#endif //__WINDOWS_MSVC_BUILD__
#include <stdexcept>
#include "json.hpp"
#ifndef __WINDOWS_MSVC_BUILD__
# include "ncursesw/ncurses.h"
#endif //__WINDOWS_MSVC_BUILD__
#ifndef __WINDOWS_MSVC_BUILD__
# include "ncursesw/term.h"
#endif //__WINDOWS_MSVC_BUILD__
#include "tsl/hopscotch_map.h"
#include "readline/history.h"
#include "utils.h"
#include "dump.h"
#include "sort.h"

#ifndef __WINDOWS_MSVC_BUILD__
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

#endif //__WINDOWS_MSVC_BUILD__
#define STRX(x) #x
#define STR(x) JSON_STRX(x)
#define CASSERT(x)  \
if (!(x)) {         \
    std::cout << __FILE__ ":" STR(__LINE__) ": Assertion " #x " Failed!\n"; \
    _exit(EXIT_FAILURE); \
}

bool ccdb::utils::is_highlight_match(const std::vector < std::string > & line, const std::string & search_content)
{
    using namespace utils;
    if (search_content.empty()) return false;
    static cache_w_freq_table_t < std::string, bool > cache;

    std::stringstream joined;
    std::ranges::for_each(line, [&joined](const auto & cell) { joined << cell; });
    const auto text = joined.str();
    const auto cache_key = std::to_string(text.size()) + ":" + text + search_content;
    if (const auto it = cache.get_cache(cache_key); it) {
        return *it;
    }

    std::u32string str = utf8_to_u32(text);
    const std::u32string bak = str;
    const auto result = bak != regex_replace_all(str, utf8_to_u32(search_content),
        [](const basic_regex_scope_type<char32_t> & mat)->std::u32string
        {
            const auto & mat_str = *mat.first;
            if ((mat_str.size() == 1 && (mat_str.front() >= 0x80 ||
                std::isprint(static_cast<unsigned char>(mat_str.front())))) || mat_str.size() > 1) {
                return U"<match>" + mat_str + U"</match>";
            }
            return mat_str;
        });
    cache.emplace_cache(cache_key, result);
    return result;
}

bool ccdb::utils::sort_url_if_fit(const std::string& a, const std::string& b)  {
    return url_sort::sort_url_if_fit(a, b);
}

bool ccdb::utils::parse_url(const std::string& url, std::string& scheme, std::string& host, std::string& path)
{
    const std::regex re(R"(^(\w+)://([^/]+(:\d+)?)(/.*)?$)");
    std::smatch match;
    if (!std::regex_match(url, match, re)) {
        return false;
    }
    scheme = match[1];
    host = match[2];
    path = match[4];
    return true;
}

bool ccdb::utils::parse_proxy(const std::string& url, std::string& host, int & port)
{
    const std::regex re(R"(^[\w]+://([^/]+):([\d]+)(/.*)?$)");
    std::smatch match;
    if (!std::regex_match(url, match, re)) {
        return false;
    }

    host = match[1];
    port = convertToNumber<int>(match[2].str());
    return true;
}

void ccdb::utils::set_ssl_automatically(httplib::Client & client, const std::string & url)
{
    std::string scheme;
    if (std::string host, path;
        !parse_url(url, scheme, host, path))
    {
        throw std::invalid_argument("Invalid URL");
    }

    if (scheme == "https" && getenv("DISABLE_SERVER_CERTIFICATE_VERIFICATION") == "true") {
        client.enable_server_certificate_verification(false);
    } else {
#ifndef __WINDOWS_MSVC_BUILD__
        std::vector ca_paths = {
            getenv("SSL_CERTIFICATE"),
            // possible system CA certificate locations
            getenv("PREFIX") + "/etc/ssl/certs/ca-certificates.crt",
            getenv("PREFIX") + "/etc/ssl/certs/ca-bundle.trust.crt",
            getenv("PREFIX") + "/etc/ssl/cert.pem",
            getenv("PREFIX") + "/etc/tls/cert.pem",
            getenv("PREFIX") + "/etc/pki/ca-trust/extracted/openssl/ca-bundle.trust.crt",
            getenv("PREFIX") + "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        };

        (void)std::ranges::any_of(ca_paths, [&](const std::string& ca_path)->bool
        {
            if (!ca_path.empty() && std::filesystem::exists(ca_path))
            {
                client.set_ca_cert_path(ca_path);
#endif //__WINDOWS_MSVC_BUILD__
                client.enable_server_certificate_verification(true);
#ifndef __WINDOWS_MSVC_BUILD__
                return true;
            }

            return false;
        });
#endif //__WINDOWS_MSVC_BUILD__
    }
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
#ifndef __WINDOWS_MSVC_BUILD__
    const auto var = secure_getenv(name.c_str());
#else
    const auto var = std::getenv(name.c_str());
    if (!var && name == "HOME") {
        const auto profile = std::getenv("USERPROFILE");
        return profile ? profile : "";
    }
#endif //__WINDOWS_MSVC_BUILD__
    if (var == nullptr) {
        return "";
    }

    return var;
}

void ccdb::utils::setenv(const std::string &name, const std::string &value) noexcept
{
#ifndef __WINDOWS_MSVC_BUILD__
    ::setenv(name.c_str(), value.c_str(), 1);
#else
    _putenv_s(name.c_str(), value.c_str());
#endif //__WINDOWS_MSVC_BUILD__
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

bool ccdb::utils::detail::regex_compilation_cache_disabled() noexcept
{
    static const bool disabled = ccdb::utils::getenv("DISABLE_CACHE_BEHAVIOR") == "true";
    return disabled;
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
    thread_local tsl::hopscotch_map <std::string, std::u32string> cache;
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

bool ccdb::utils::NO_0xFE0F_EXPAND_EMOJI = ::ccdb::utils::getenv("NO_0xFE0F_EXPAND_EMOJI") == "true";

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

#ifndef __WINDOWS_MSVC_BUILD__
    const auto wc = static_cast<wchar_t>(c);

    if (const int w = wcwidth(wc); w >= 0) {
        return w;
    }

#endif //__WINDOWS_MSVC_BUILD__
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


void ccdb::utils::set_progress_bar(const progress_bar_state_t state, const int percentages, const std::string & ETA)
{
#ifdef __STATIC_MUSL__
    return;
#endif //__STATIC_MUSL__
    std::stringstream ss;
    ss << "\033]9;4;" << state << ";" << percentages << "\033\\";
    const std::string & str = ss.str();
#ifndef __WINDOWS_MSVC_BUILD__
    if (const int fd = open("/dev/stdout", O_WRONLY); fd > 0)
    {
        (void)write(fd, str.c_str(), str.length());
#else //__WINDOWS_MSVC_BUILD__
        windows::terminal_write(str);
#endif //__WINDOWS_MSVC_BUILD__
        if (state == SET_PROGRESS)
        {
            const auto col_size = get_col_size();
            std::stringstream ss2; ss2 << "] (" << percentages << "%" << (ETA.empty() ? "" : " " + ETA) << ")";
            const auto str_p = ss2.str();
            if (const auto len = col_size - 1 - str_p.length(); len > 0)
            {
                const auto p = static_cast<int>(static_cast<double>(len) * static_cast<double>(percentages) / 100.00);
                const auto l = len > p ? static_cast<int>(len - p) : 0;
                std::stringstream ss3;
                ss3 << "\r" << "[" << std::string(p, '=') << std::string(l, ' ') << str_p;
                const auto str3 = ss3.str();
#ifndef __WINDOWS_MSVC_BUILD__
                (void)write(fd, str3.c_str(), str3.length());
#else //__WINDOWS_MSVC_BUILD__
                windows::terminal_write(str3);
#endif //__WINDOWS_MSVC_BUILD__
            }
        }
#ifndef __WINDOWS_MSVC_BUILD__
        close(fd);
    }
#endif //__WINDOWS_MSVC_BUILD__
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

std::string ccdb::utils::generate_linear_handle(
    const int content_total,
    const int view_start,
    const int view_end,
    const int track_len)
{
    if (track_len <= 0) return {};
    if (content_total <= 0) return { static_cast<std::string::size_type>(track_len), ' ', std::allocator<char>() };

    int viewport = std::max(0, view_end - view_start);
    viewport = std::min(viewport, content_total);

    if (viewport >= content_total) {
        std::string ret;
        ret.reserve(track_len * 3);
        for (int i = 0; i < track_len; ++i) ret += "█";
        return ret;
    }

    const int max_scroll = content_total - viewport;         // >= 1
    const int offset = std::clamp(view_start, 0, max_scroll);

    int thumb_len = static_cast<int>(std::lround(static_cast<double>(track_len) * static_cast<double>(viewport) / static_cast<double>(content_total)));
    thumb_len = std::clamp(thumb_len, 1, track_len);

    const int track_range = track_len - thumb_len;           // >= 0
    int thumb_start = static_cast<int>(std::lround(static_cast<double>(track_range) * static_cast<double>(offset) / static_cast<double>(max_scroll)));
    thumb_start = std::clamp(thumb_start, 0, track_range);
    const int thumb_end = thumb_start + thumb_len - 1;

    std::string ret;
    ret.reserve(track_len * 3);
    for (int i = 0; i < track_len; ++i) {
        ret += (i >= thumb_start && i <= thumb_end) ? "█" : "░";
    }
    return ret;
}
