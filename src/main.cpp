#include <iostream>
#include <chrono>
#include <thread>
#include "log.h"
#include "httplib.h"
#include "mihomo.h"
#include "ncursesw/ncurses.h"
#include "json.hpp"
#include "general_info_pulling.h"

Logger::Logger logger;
using json = nlohmann::json;


int main()
{
    mihomo sample("127.0.0.1", 9090, "");
    general_info_pulling d;
    std::atomic_bool running = true;
    auto run = [&]()->void
    {
        sample.get_stream_info("traffic", &running, &d, &general_info_pulling::update_from_traffic);
    };

    auto run2 = [&]()->void
    {
        while (running)
        {
            sample.get_info("connections", &d, &general_info_pulling::update_from_connections);
            std::this_thread::sleep_for(std::chrono::seconds(1l));
        }
    };
    std::thread T(run);
    std::thread T2(run2);
    std::this_thread::sleep_for(std::chrono::seconds(30l));
    running = false;

    if (T.joinable())
    {
        T.join();
    }

    if (T2.joinable())
    {
        T2.join();
    }

    for (const auto & conn : d.get_active_connections())
    {
        logger.ilog("host: ", conn.host, "\n");
        logger.ilog("src: ", conn.src, "\n");
        logger.ilog("destination: ", conn.destination, "\n");
        logger.ilog("processName: ", conn.processName, "\n");
        logger.ilog("uploadSpeed: ", conn.uploadSpeed, "\n");
        logger.ilog("downloadSpeed: ", conn.downloadSpeed, "\n");
        logger.ilog("totalUploadedBytes: ", conn.totalUploadedBytes, "\n");
        logger.ilog("totalDownloadedBytes: ", conn.totalDownloadedBytes, "\n");
        logger.ilog("chainName: ", conn.chainName, "\n");
        logger.ilog("ruleName: ", conn.ruleName, "\n");
        logger.ilog("networkType: ", conn.networkType, "\n");
        logger.ilog("timeElapsedSinceConnectionEstablished: ", conn.timeElapsedSinceConnectionEstablished, "\n");
        logger.ilog("\n");
    }

    // initscr();            // Start curses mode
    // cbreak();             // Disable line buffering
    // noecho();             // Don't echo typed characters
    // keypad(stdscr, TRUE); // Enable special keys
    // raw();
    // nl();
    // std::this_thread::sleep_for(std::chrono::seconds(3l));
    // endwin();
}
