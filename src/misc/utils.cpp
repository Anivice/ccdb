#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "utils.h"
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>

std::string ccdb::utils::getenv(const std::string& name) noexcept
{
    const auto var = ::getenv(name.c_str());
    if (var == nullptr) {
        return "";
    }

    return var;
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

std::pair < const int, const int > ccdb::utils::get_screen_row_col() noexcept
{
    constexpr int term_col_size = 80;
    constexpr int term_row_size = 25;
    const auto col_size_from_env = ccdb::utils::getenv("COLUMNS");
    const auto row_size_from_env = ccdb::utils::getenv("LINES");
    long col_env = -1;
    long row_env = -1;

    try
    {
        if (!col_size_from_env.empty() && !row_size_from_env.empty()) {
            col_env = std::strtol(col_size_from_env.c_str(), nullptr, 10);
            row_env = std::strtol(row_size_from_env.c_str(), nullptr, 10);
        }
    } catch (...) {
        col_env = -1;
        row_env = -1;
    }

    auto get_pair = [&]->std::pair < const int, const int >
    {
        if (col_env != -1 && row_env != -1) {
            return {row_env, col_env};
        }

        return {term_row_size, term_col_size};
    };

    bool is_terminal = false;
    struct stat st{};
    if (fstat(STDOUT_FILENO, &st) == -1) {
        return get_pair();
    }

    if (isatty(STDOUT_FILENO)) {
        is_terminal = true;
    }

    if (is_terminal)
    {
        winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || (w.ws_col | w.ws_row) == 0) {
            return get_pair();
        }

        return {w.ws_row, w.ws_col};
    }

    return get_pair();
}

uint64_t ccdb::utils::get_timestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

timespec ccdb::utils::get_timespec() noexcept
{
    timespec ts{};
    timespec_get(&ts, TIME_UTC);
    return ts;
}

std::string ccdb::utils::value_to_human(
    const unsigned long long value,
    const std::string &lv1, const std::string &lv2,
    const std::string &lv3, const std::string &lv4)
{
    std::stringstream ss;
    if (value < 1024ull || value >= 1024ull * 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << value << " " << lv1;
    } else if (value < 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / 1024ull) << " " << lv2;
    } else if (value < 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull)) << " " << lv3;
    } else if (value < 1024ull * 1024ull * 1024ull * 1024ull) {
        ss << std::fixed << std::setprecision(2) << (static_cast<double>(value) / (1024ull * 1024ull * 1024ull)) << " " << lv4;
    }

    return ss.str();
}

std::string ccdb::utils::second_to_human_readable(unsigned long long value)
{
    if (value < 60) {
        return std::to_string(value) + " s";
    }

    if (value < 60 * 60)
    {
        if ((value % 60) >= 30) {
            return "Less than " + std::to_string(value / 60 + 1) + " Min";
        }

        return std::to_string(value / 60) + " Min";
    }

    if (value < 60 * 60 * 24) {
        if ((value % (60 * 60)) >= (30 * 60))
        {
            return "Less than " + std::to_string(value / (60 * 60) + 1) + " H";
        }
        return std::to_string(value / (60 * 60)) + " H";
    }

    const unsigned long long day = value / (60 * 60 * 24);
    value %= (60 * 60 * 24);
    const unsigned long long hour = value / (60 * 60);
    value %= (60 * 60);
    const unsigned long long minute = value / 60;
    const unsigned long long second = value % 60;
    return std::to_string(day) + "d" + std::to_string(hour) + "h" + std::to_string(minute) + "m" + std::to_string(second) + "s";
}

std::u32string ccdb::utils::utf8_to_u32(const std::string &s)
{
    std::u32string result;
    utf8::utf8to32(s.begin(), s.end(), std::back_inserter(result));
    return result;
}

int ccdb::utils::UnicodeDisplayWidth::get_width_utf8(const std::string &utf8_str)
{
    std::u32string utf32_str;
    utf8::utf8to32(utf8_str.begin(), utf8_str.end(),
                   std::back_inserter(utf32_str));

    return get_width_utf32(utf32_str);
}

int ccdb::utils::UnicodeDisplayWidth::get_width_utf32(const std::u32string &utf32_str)
{
    int width = 0;

    for (size_t i = 0; i < utf32_str.length(); i++)
    {
        const char32_t c = utf32_str[i];

        if (c == 0x200D || (c >= 0xFE00 && c < 0xFE0F)) {
            continue;
        }

        if (c == 0xFE0F)
        {
            // when this is printed onto screen, it means an additional color code that expand the emoji
            // this doesn't apply to all the terminals, so fucking headaches
            // you can just disable this by setting the environment variable NO_0xFE0F_EXPAND_EMOJI to true
            // if your terminal doesn't really process this flag
            if (getenv("NO_0xFE0F_EXPAND_EMOJI") == "true") {
                continue;
            }
            width += 1;
            continue;
        }

        if (c >= 0x1F3FB && c <= 0x1F3FF) {
            continue; // These don't add width
        }

        if (c >= 0x1F1E6 && c <= 0x1F1FF) {
            width += 2; // Flags are typically 2 cells
            continue;
        }

        width += get_char_width(c);
    }

    return width;
}

int ccdb::utils::UnicodeDisplayWidth::get_char_width(const char32_t c)
{
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

bool ccdb::utils::is_less_available()
{
    static std::atomic_int pager_is_less_available = -1;

    if (pager_is_less_available != -1) {
        return pager_is_less_available;
    }

    if (const auto nopager = getenv("NOPAGER");
        nopager == "true" || nopager == "1" || nopager == "yes" || nopager == "y")
    {
        pager_is_less_available = false;
        return false;
    }

    if (const auto pager = getenv("PAGER"); pager.empty()) {
        const auto result_which_less = exec_command("/bin/sh", "", "-c", "which less 2>/dev/null >/dev/null");
        const auto result_whereis_less =  exec_command("/bin/sh", "", "-c", "whereis less 2>/dev/null >/dev/null");
        const auto result_less_version =  exec_command("/bin/sh", "", "-c", "less --version 2>/dev/null >/dev/null");
        pager_is_less_available = !result_less_version.exit_status || !result_whereis_less.exit_status || !result_which_less.exit_status;
        return pager_is_less_available;
    }

    pager_is_less_available = true;
    return true; // skip check if you specify a pager. fuck you for providing a faulty one
}
