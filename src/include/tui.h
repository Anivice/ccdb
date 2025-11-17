#ifndef SRC_TUI_H
#define SRC_TUI_H

#include <atomic>
#include <mutex>
#include <thread>
#include "general_info_pulling.h"
#include "ncursesw/ncurses.h"

extern volatile std::atomic_bool resized;

class TUIScreen
{
public:
    struct xy_pair_t
    {
        long x;
        long y;
    };

private:
    xy_pair_t get_screen_size();
    WINDOW * window_side_panel = nullptr;
    WINDOW * window_main = nullptr;
    std::mutex screen_mutex;
    general_info_pulling info_puller;
    void draw_info();

public:
    ~TUIScreen();
    TUIScreen(std::string url, int port, std::string token = "");
    void terminal_resize_request_handler();

    void worker()
    {
        std::atomic_bool stop_watcher = false;
        std::thread Watcher([&stop_watcher, this]()
        {
            while (!stop_watcher)
            {
                if (resized)
                {
                    terminal_resize_request_handler();
                    resized = false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100l));
            }
        });


        int _c;
        while (true)
        {
            switch (_c = getch())
            {
            case 'q':
            case 'Q':
            case 27: // ESC
                stop_watcher = true;
                if (Watcher.joinable()) Watcher.join();
                return;
            case 259: // up
            case 258: // down
            case 260: // left
            case 261: // right
            case 10: // Enter
            default:
                    std::cerr << "Unknown key code: " << _c << std::endl;
            }
        }
    }
};

#endif //SRC_TUI_H
