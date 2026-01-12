#ifndef MIHOMO_H
#define MIHOMO_H

#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include "httplib.h"
#include "json.hpp"

class general_info_pulling;

class mihomo
{
    std::string token_;
    std::string backend_address_;
    int port_ = 0;
    std::atomic_bool info_streaming_pulling_ = true;

public:
    explicit mihomo(std::string  backend, const int port, std::string token_)
    : token_(std::move(token_)), backend_address_(std::move(backend)), port_(port) { }
    ~mihomo() = default;

    bool change_proxy(const std::string & group_name, const std::string & proxy_name);
    void abort() { info_streaming_pulling_ = false; }
    void get_info_no_instance(const std::string & endpoint_name, const std::function < void(std::string) > & method);
    bool change_proxy_mode(const std::string & mode);
    bool close_all_connections();

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
            httplib::Client http_cli(backend_address_, port_);
            http_cli.set_decompress(false);
            http_cli.set_read_timeout(10, 0);
            auto worker = [&]()->void
            {
                if (is_running) return;
                is_running = true;
                std::string buffer;
                std::string first_line;
                std::vector < std::thread > thread_pool;

                auto puller = [&](const char *data, const size_t len)
                {
                    buffer.append(data, len);
                    if (const auto pos = buffer.find('\n'); pos != std::string::npos)
                    {
                        first_line = buffer.substr(0, pos);
                        buffer = buffer.substr(pos + 1);
                        std::thread T([&](std::string _first_line) {
                            pthread_setname_np(pthread_self(), (endpoint_name + " hdlr").c_str());
                            (instance->*method)(_first_line);
                        }, first_line);
                        thread_pool.emplace_back(std::move(T)); // execute handler but doesn't block receive threads

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
                };

                const httplib::Headers headers = {
                    {"Authorization", "Bearer " + token_},
                };

                if (!token_.empty()) {
                    http_cli.Get("/" + endpoint_name, headers, puller);
                } else {
                    http_cli.Get("/" + endpoint_name, puller);
                }

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
                while (*keep_running)
                {
                    if (!is_running)
                    {
                        if (T.joinable()) { T.join(); }
                        T = std::thread([&] {
                            pthread_setname_np(pthread_self(), (endpoint_name + " cont").c_str());
                            try { worker(); } catch (...) { }
                        });
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100l));
                }

                http_cli.stop();
                if (T.joinable()) { T.join(); }
            }
            else
            {
                while (*keep_running) // pull every 300ms
                {
                    stance = true;
                    std::thread T([&] {
                        pthread_setname_np(pthread_self(), (endpoint_name + " rept").c_str());
                        try { worker(); }
                        catch (std::exception & e) { std::cerr << e.what() << std::endl; exit(1); }
                    });
                    std::this_thread::sleep_for(std::chrono::milliseconds(300l));
                    stance = false;
                    if (T.joinable()) { T.join(); }
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
