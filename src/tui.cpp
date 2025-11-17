#include "tui.h"
#include "log.h"
#include <iostream>
#include <csignal>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <vector>

volatile std::atomic_bool resized = false;
void sigwitch_sighandler(int)
{
    resized = true;
}

TUIScreen::xy_pair_t TUIScreen::get_screen_size()
{
    int x = 0, y = 0;
    winsize w{};
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

TUIScreen::~TUIScreen()
{
    auto autodel = [](WINDOW * win)
    {
        if (win != nullptr) {
            delwin(win);
        }
    };

    autodel(window_side_panel);
    autodel(window_main);
    endwin();
}

TUIScreen::TUIScreen(std::string url, int port, std::string token) : info_puller(url, port, token)
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

    std::signal(SIGWINCH, sigwitch_sighandler);
}

void TUIScreen::terminal_resize_request_handler()
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

    // clear windows
    delwin(window_main);
    delwin(window_side_panel);

    // clear screen
    const char clear[] = { 0x1b, 0x5b, 0x48, 0x1b, 0x5b, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x4a };
    write(STDOUT_FILENO, clear, sizeof(clear));

    // reinitialize
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
}
