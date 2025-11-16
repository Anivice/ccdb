#ifndef MIHOMO_H
#define MIHOMO_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include "httplib.h"
#include "json.hpp"
#include "glogger.h"

class general_info_pulling;

class mihomo
{
    std::string token;
    httplib::Client http_cli;
    // std::mutex http_cli_mutex;

public:
    explicit mihomo(const std::string& backend, int port, std::string token_) : token(std::move(token_)), http_cli(backend, port)
    {
        // std::lock_guard lock(http_cli_mutex);
        http_cli.set_decompress(false);
        http_cli.set_read_timeout(1, 0);
    }
    ~mihomo() = default;

    bool change_proxy(const std::string & group_name, const std::string & proxy_name);
    void abort() { http_cli.stop(); }
    void get_info_no_instance(const std::string & endpoint_name, const std::function < void(std::string) > & method);

    template < typename InstanceType >
    void get_info(const std::string & endpoint_name, InstanceType* instance, void (InstanceType::*method)(std::string))
    {
        try {
            get_info_no_instance(endpoint_name, [&](std::string buff) { (instance->*method)(buff); });
        } catch (const std::exception& e) {
            throw std::runtime_error(e.what());
        }
    }

    template < typename InstanceType >
    void get_stream_info(
        const std::string & endpoint_name,
        const std::atomic_bool * keep_running,
        InstanceType* instance,
        void (InstanceType::*method)(std::string),
        const std::atomic_bool is_continuous = false)
    {
        try
        {
            std::atomic_bool stance(true);
            std::atomic_bool is_running(false);
            auto worker = [&]()->void
            {
                if (is_running) return;
                is_running = true;
                std::string buffer;
                std::string first_line;
                std::vector < std::thread > thread_pool;
                const httplib::Headers headers = {
                    {"Authorization", "Bearer " + token},
                };

                // std::lock_guard lock(http_cli_mutex);
                http_cli.Get("/" + endpoint_name, headers,
                    [&](const char *data, const size_t len)
                {
                    buffer.append(data, len);
                    if (const auto pos = buffer.find('\n'); pos != std::string::npos)
                    {
                        first_line = buffer.substr(0, pos);
                        buffer = buffer.substr(pos + 1);
                        std::thread T([&](std::string _first_line) {
                            (instance->*method)(_first_line);
                        }, first_line);
                        thread_pool.emplace_back(std::move(T));

                        if (thread_pool.size() > 32) // oversized pool cleanup
                        {
                            for (auto & thread : thread_pool)
                            {
                                if (thread.joinable()) {
                                    thread.join();
                                }
                            }

                            thread_pool.clear();
                        }

                        if (is_continuous) return keep_running->load();
                        return stance.load();
                    }

                    return true;
                });

                for (auto & thread : thread_pool)
                {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }

                is_running = false;
            };

            if (is_continuous)
            {
                std::thread T;
                while (*keep_running) {
                    if (!is_running) {
                        if (T.joinable()) { T.join(); }
                        T = std::thread([&] {try { worker(); } catch (...) { } });
                        // logger.dlog("Flag: ", *keep_running, ", Spawning new thread...\n");
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100l));
                }

                http_cli.stop();
                if (T.joinable()) { T.join(); }
            }
            else
            {
                while (*keep_running) // pull every 1s for
                {
                    stance = true;
                    std::thread T(worker);
                    std::this_thread::sleep_for(std::chrono::seconds(1l));
                    stance = false;
                    if (T.joinable()) {
                        T.join();
                    }
                }
            }
        } catch (std::exception & e) {
            throw std::runtime_error(e.what());
        } catch (...) {
            throw std::runtime_error("Unknown error");
        }
    }

    friend class general_info_pulling;
};

#endif //MIHOMO_H
