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
    auto run = [&]()->void
    {
        {
            std::lock_guard lock(d.current_focus_mutex);
            d.current_focus = "overview";
            d.keep_pull_continuous_updates = true;
        }
        d.pull_continuous_updates();
    };

    std::thread T(run);

    for (int i = 0; i < 10; i++)
    {
        logger.ilog("UploadSpeed: ", d.get_current_upload_speed(), ", DownloadSpeed: ", d.get_current_download_speed(), "\n");
        logger.ilog("TotalUploadedBytes: ", d.get_total_uploaded_bytes(), ", TotalDownloadedBytes: ", d.get_total_downloaded_bytes(), "\n");
        logger.ilog("ActiveConnections: ", d.get_active_connections().size(), "\n");
        logger.ilog("\n");
        std::this_thread::sleep_for(std::chrono::seconds(1l));
    }

    {
        std::lock_guard lock(d.current_focus_mutex);
        d.current_focus = "logs";
    }

    d.keep_pull_continuous_updates = false;

    if (T.joinable())
    {
        T.join();
    }

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
