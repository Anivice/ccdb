#include "utils.h"
#include "windows_platform.h"

namespace ccdb::color {
bool USE_OLD_COLOR_SCHEME = false;
std::atomic_int g_color_status_override{-1};
bool is_no_color() noexcept {
    if (g_color_status_override != -1) return g_color_status_override;
    const auto mode = utils::getenv("COLOR");
    if (mode == "always" || mode == "y") return false;
    if (mode == "never" || mode == "n" || !utils::getenv("NO_COLOR").empty()) return true;
    DWORD flags;
    return !GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &flags);
}
std::string no_color() noexcept { return is_no_color() ? "" : "\x1b[0m"; }
std::string color24(int r, int g, int b) noexcept {
    return is_no_color() ? "" : "\x1b[38;2;" + std::to_string(std::clamp(r,0,255)) + ";" +
        std::to_string(std::clamp(g,0,255)) + ";" + std::to_string(std::clamp(b,0,255)) + "m";
}
std::string bg_color24(int r, int g, int b) noexcept {
    return is_no_color() ? "" : "\x1b[48;2;" + std::to_string(std::clamp(r,0,255)) + ";" +
        std::to_string(std::clamp(g,0,255)) + ";" + std::to_string(std::clamp(b,0,255)) + "m";
}
std::string color24(int r,int g,int b,int br,int bg,int bb) noexcept { return color24(r,g,b)+bg_color24(br,bg,bb); }
std::string color(int r,int g,int b) noexcept { return color24(r*51,g*51,b*51); }
std::string bg_color(int r,int g,int b) noexcept { return bg_color24(r*51,g*51,b*51); }
std::string color(int r,int g,int b,int br,int bg,int bb) noexcept { return color(r,g,b)+bg_color(br,bg,bb); }
}
// Fixed graph gradient only; custom scheme evaluation/export is disabled.
namespace sim {
const Num Begin = 0;
const Num Span = 1;
NumPack_t simulation_rainbow(Num x) {
    x = std::clamp(x, Num(0), Num(1));
    return {255*x, 220*(1-x), 80};
}
}
