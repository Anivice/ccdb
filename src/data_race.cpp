#include "httplib.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

int main(int argc, char ** argv)
{
    std::vector < std::thread > thread_pool;
    std::atomic_bool running = true;
    auto worker = [&]()->void
    {
        httplib::Client cli("127.0.0.1", 9090);
        httplib::Headers headers = {
            {"Authorization", "Bearer "}
        };
        cli.Get("/traffic", headers,  [&](const char *data, const size_t len) { return running.load(); });
    };

    thread_pool.emplace_back(worker);
    if (argc == 1) thread_pool.emplace_back(worker);

    std::this_thread::sleep_for(std::chrono::seconds(5));
    running = false;

    for (auto & T : thread_pool)
    {
        if (T.joinable())
        {
            T.join();
        }
    }

    return 0;
}
