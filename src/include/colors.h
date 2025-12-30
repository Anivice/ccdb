#ifndef CFS_COLORS_H
#define CFS_COLORS_H

#include <string>

/// ANSI color codes
namespace ccdb::color
{
    /// Is ANSI color code suitable in current environment
    /// Can be overriden by COLOR=always,never
    /// @return true being not suitable, false being suitable
    bool is_no_color() noexcept;

    /// Return ANSI color string that removes coloring
    /// @return ANSI color string that removes coloring
    std::string no_color() noexcept;

    /// Return ANSI color string that sets foreground color
    /// @param r Red
    /// @param g Green
    /// @param b Blue
    /// @return ANSI color string that sets foreground color
    std::string color(int r, int g, int b) noexcept;

    /// Return ANSI color string that sets background color
    /// @param r Red
    /// @param g Green
    /// @param b Blue
    /// @return ANSI color string that sets background color
    std::string bg_color(int r, int g, int b) noexcept;

    /// Return ANSI color string that sets foreground and background color
    /// @param r Foreground Red
    /// @param g Foreground Green
    /// @param b Foreground Blue
    /// @param br Background Red
    /// @param bg Background Green
    /// @param bb Background Blue
    /// @return ANSI color string that sets foreground and background color
    std::string color(int r, int g, int b, int br, int bg, int bb) noexcept;

    /// set by logger to disable color codes when output is not terminal
    extern bool g_color_disabled_since_output_is_not_terminal;
}

#endif //CFS_COLORS_H
