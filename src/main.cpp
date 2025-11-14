#include <iostream>
#include <chrono>
#include <thread>
#include "log.h"
#include "httplib.h"
#include "mihomo.h"
#include "ncursesw/ncurses.h"
#include "json.hpp"

Logger::Logger logger;

class dummy
{
public:
    using json = nlohmann::json;

    void f(std::pair < std::mutex, std::string > & info)
    {
        std::lock_guard lock(info.first);
        json data = json::parse(info.second);
        logger.logPrint(INFO_LOG, static_cast<uint64_t>(data["up"]), "\n");
    }
};

int main()
{
    mihomo sample;
    dummy d;
    std::atomic_bool running = true;
    auto run = [&]()->void
    {
        sample.get_stream_info("traffic", &running, &d, &dummy::f);
    };
    std::thread T(run);
    std::this_thread::sleep_for(std::chrono::seconds(3l));
    running = false;

    if (T.joinable())
    {
        T.join();
    }

    initscr();            // Start curses mode
    cbreak();             // Disable line buffering
    noecho();             // Don't echo typed characters
    keypad(stdscr, TRUE); // Enable special keys
    raw();
    nl();
    std::this_thread::sleep_for(std::chrono::seconds(3l));
    endwin();
}
