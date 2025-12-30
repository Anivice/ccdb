#include <atomic>
#include <ranges>
#include <string>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include "colors.h"
#include "utils.h"

bool ccdb::color::g_color_disabled_since_output_is_not_terminal = false;

bool ccdb::color::is_no_color() noexcept
{
    static std::atomic_int is_no_color_cache = -1;
    if (is_no_color_cache != -1) {
        return is_no_color_cache;
    }

    if (g_color_disabled_since_output_is_not_terminal) {
        is_no_color_cache = true;
        return true;
    }

    auto color_env = ccdb::utils::getenv("COLOR");
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

    if (isatty(STDOUT_FILENO)) {
        is_terminal = true;
    }

    is_no_color_cache = no_color_from_env || !is_terminal;
    return is_no_color_cache;
}

std::string ccdb::color::no_color() noexcept
{
    if (!is_no_color()) {
        return "\033[0m";
    }

    return "";
}

std::string ccdb::color::color(int r, int g, int b) noexcept
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

std::string ccdb::color::bg_color(int r, int g, int b) noexcept
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

std::string ccdb::color::color(const int r, const int g, const int b, const int br, const int bg, const int bb) noexcept
{
    if (is_no_color()) {
        return "";
    }

    return color(r, g, b) + bg_color(br, bg, bb);
}