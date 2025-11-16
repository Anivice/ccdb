#ifndef SRC_TUI_H
#define SRC_TUI_H

#include "ncursesw/ncurses.h"

class TUIScreen
{
public:
    TUIScreen()
    {
        initscr();            // Start curses mode
        cbreak();             // Disable line buffering
        noecho();             // Don't echo typed characters
        keypad(stdscr, TRUE); // Enable special keys
        raw();
        nl();
    }

    ~TUIScreen()
    {
        endwin();
    }

    struct xy_pair_t
    {
        long x;
        long y;
    };

    xy_pair_t get_screen_size()
    {
        int x, y;
        getmaxyx(stdscr, y, x);
        return { y, x };
    }

    void show()
    {
        int top_height   = LINES / 3;
        int lower_height = (LINES - top_height) / 2;
        int left_width   = COLS / 2;
        int right_width  = COLS - left_width;

        WINDOW * win_top     = newwin(get_screen_size().x - 4, get_screen_size().y - 4, 2, 2);
        // WINDOW *win_2_left  = newwin(lower_height, left_width,  top_height, 0);
        // WINDOW *win_2_right = newwin(lower_height, right_width, top_height, left_width);
        // WINDOW *win_3_left  = newwin(lower_height, left_width,
                                     // top_height + lower_height, 0);
        // WINDOW *win_3_right = newwin(lower_height, right_width,
                                     // top_height + lower_height, left_width);

        box(win_top, 0, 0);
        mvwprintw(win_top, 1, 2, "Top window");
        // box(win_2_left, 0, 0);
        // mvwprintw(win_2_left, 1, 2, "Second row left");
        // box(win_2_right, 0, 0);
        // mvwprintw(win_2_right, 1, 2, "Second row right");
        // box(win_3_left, 0, 0);
        // mvwprintw(win_3_left, 1, 2, "Third row left");
        // box(win_3_right, 0, 0);
        // mvwprintw(win_3_right, 1, 2, "Third row right");

        refresh();
        wrefresh(win_top);
        // wrefresh(win_2_left);
        // wrefresh(win_2_right);
        // wrefresh(win_3_left);
        // wrefresh(win_3_right);

        getch();

        delwin(win_top);
        // delwin(win_2_left);
        // delwin(win_2_right);
        // delwin(win_3_left);
        // delwin(win_3_right);
    }
};

#endif //SRC_TUI_H
