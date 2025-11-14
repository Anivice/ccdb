#include <iostream>
#include <chrono>
#include <thread>
#include "log.h"
#include "httplib.h"
#include "mihomo.h"

Logger::Logger logger;

class dummy
{
public:
    void f(std::pair < std::mutex, std::string > & info)
    {
        std::lock_guard lock(info.first);
        logger.logPrint(INFO_LOG, info.second, "\n");
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
    std::this_thread::sleep_for(std::chrono::seconds(300l));
    running = false;

    if (T.joinable())
    {
        T.join();
    }

    // Logger::Logger logger;
    // logger.logPrint(INFO_LOG, "CCDB Version ", VERSION, "\n");

    //
    // if (!res) {
    //     std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
    //     return 1;
    // }
    //
    // std::cout << "HTTP status: " << res->status << "\n";

}
