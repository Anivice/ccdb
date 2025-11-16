#ifndef SRC_TUI_H
#define SRC_TUI_H

#include <vector>
#include <atomic>
#include "ncursesw/ncurses.h"
#include "log.h"
#include <iostream>
#include <csignal>
#include <sys/ioctl.h>
#include <sys/stat.h>

extern volatile std::atomic_bool resized;
void _sighandler(int);

class TUIScreen
{
public:
    struct xy_pair_t
    {
        long x;
        long y;
    };

private:
    xy_pair_t get_screen_size()
    {
        int x = 0, y = 0;
        winsize w{};
        // If redirecting STDOUT to one file ( col or row == 0, or the previous
        // ioctl call's failed )
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 ||
            (w.ws_col | w.ws_row) == 0)
        {
            std::cerr << "Warning: failed to determine a reasonable terminal size: " << strerror(errno) << std::endl;
        } else {
            x = w.ws_col;
            y = w.ws_row;
        }
        return { x, y };
    }

    WINDOW * window_side_panel = nullptr;
    WINDOW * window_overview_bar = nullptr;
    WINDOW * window_proxy_bar = nullptr;
    WINDOW * window_connection_bar = nullptr;
    WINDOW * window_logs_bar = nullptr;
    WINDOW * window_main = nullptr;
    std::vector < WINDOW * > stray_windows_within_main;

public:
    ~TUIScreen() {
        auto autodel = [](WINDOW * win)
        {
            if (win != nullptr) {
                delwin(win);
            }
        };

        autodel(window_side_panel);
        autodel(window_overview_bar);
        autodel(window_proxy_bar);
        autodel(window_connection_bar);
        autodel(window_logs_bar);
        autodel(window_main);

        for (WINDOW * win : stray_windows_within_main) {
            autodel(win);
        }
        endwin();
    }

    TUIScreen()
    {
        initscr();            // Start curses mode
        cbreak();             // Disable line buffering
        noecho();             // Don't echo typed characters
        keypad(stdscr, TRUE); // Enable special keys
        raw();
        nl();

        // [PANEL] (1/8) [MAIN] (7/8)
        long panel_width = (get_screen_size().x - 2) / 8;
        long main_window_width = (get_screen_size().x - 2) /* available */ - 1 /* padding between two windows */ - panel_width;
        long height = get_screen_size().y - 2;
        window_side_panel = newwin(height, panel_width, 1, 1);
        window_main = newwin(height, main_window_width, 1, 1 + panel_width + 1);

        box(window_side_panel, 0, 0);
        box(window_main, 0, 0);
        mvwprintw(window_side_panel, 1, 2, "Side Panel");
        mvwprintw(window_main, 1, 2, "Main Window");
        refresh();

        wrefresh(window_main);
        wrefresh(window_side_panel);
        refresh();

        std::signal(SIGWINCH, _sighandler);
    }

    void terminal_resize_request_handler()
    {
        const auto [ x, y ] = get_screen_size();
        // update curses’ internal structures
        if (is_term_resized(y, x)) {
            resizeterm(y, x);
        }
        // [PANEL] (1/8) [MAIN] (7/8)
        long panel_width = (x - 2) / 8;
        long main_window_width = (x - 2) /* available */ - 1 /* padding between two windows */ - panel_width;
        long height = y - 2;
        delwin(window_main);
        delwin(window_side_panel);
        window_side_panel = newwin(height, panel_width, 1, 1);
        window_main = newwin(height, main_window_width, 1, 1 + panel_width + 1);

        box(window_side_panel, 0, 0);
        box(window_main, 0, 0);
        mvwprintw(window_side_panel, 1, 2, "Side Panel");
        mvwprintw(window_main, 1, 2, "Main Window");

        box(window_side_panel, 0, 0);
        box(window_main, 0, 0);
        refresh();

        wrefresh(window_main);
        wrefresh(window_side_panel);
        refresh();
        // std::cerr << get_screen_size().x << "x" << get_screen_size().y << std::endl;
    }
};

#endif //SRC_TUI_H
