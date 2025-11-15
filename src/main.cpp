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
    general_info_pulling d("127.0.0.1", 9090, "");
    d.update_proxy_list();
    // d.latency_test();
    std::thread T([&]{ d.latency_test(); });

    for (int i = 0; i < 30; i++)
    {
        {
            logger.dlog(d.get_proxies_and_latencies_as_pair(), "\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1l));
    }

    if (T.joinable()) T.join();
    logger.dlog(d.get_proxies_and_latencies_as_pair(), "\n");

    // for (const auto & conn : d.get_active_connections())
    // {
    //     logger.ilog("host: ", conn.host, "\n");
    //     logger.ilog("src: ", conn.src, "\n");
    //     logger.ilog("destination: ", conn.destination, "\n");
    //     logger.ilog("processName: ", conn.processName, "\n");
    //     logger.ilog("uploadSpeed: ", conn.uploadSpeed, "\n");
    //     logger.ilog("downloadSpeed: ", conn.downloadSpeed, "\n");
    //     logger.ilog("totalUploadedBytes: ", conn.totalUploadedBytes, "\n");
    //     logger.ilog("totalDownloadedBytes: ", conn.totalDownloadedBytes, "\n");
    //     logger.ilog("chainName: ", conn.chainName, "\n");
    //     logger.ilog("ruleName: ", conn.ruleName, "\n");
    //     logger.ilog("networkType: ", conn.networkType, "\n");
    //     logger.ilog("timeElapsedSinceConnectionEstablished: ", conn.timeElapsedSinceConnectionEstablished, "\n");
    //     logger.ilog("\n");
    // }

    // initscr();            // Start curses mode
    // cbreak();             // Disable line buffering
    // noecho();             // Don't echo typed characters
    // keypad(stdscr, TRUE); // Enable special keys
    // raw();
    // nl();
    // std::this_thread::sleep_for(std::chrono::seconds(3l));
    // endwin();
}
