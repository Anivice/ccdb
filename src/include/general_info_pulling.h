#ifndef SRC_GENERAL_INFO_PULLING_H
#define SRC_GENERAL_INFO_PULLING_H

#include "log.h"
#include "httplib.h"
#include "mihomo.h"
#include "ncursesw/ncurses.h"
#include "json.hpp"
#include "glogger.h"

using json = nlohmann::json;

class general_info_pulling
{
private:
    std::atomic < uint64_t > current_upload_speed;
    std::atomic < uint64_t > current_download_speed;
    std::atomic < uint64_t > total_uploaded_bytes;
    std::atomic < uint64_t > total_downloaded_bytes;

    struct connection_t
    {
    public:
        std::string host; // IP+Port
        std::string src;
        std::string destination;
        std::string processName;
        uint64_t uploadSpeed;
        uint64_t downloadSpeed;
        uint64_t totalUploadedBytes; // total up bytes in this connection
        uint64_t totalDownloadedBytes; // total download bytes in this connection
        std::string chainName;
        std::string ruleName;
        std::string networkType; // Tun, socks5, etc.
        uint64_t timeElapsedSinceConnectionEstablished; // in seconds

        friend class general_info_pulling;

    private:
        std::chrono::high_resolution_clock::time_point timeLastPulled;
    };
    std::mutex connection_map_mutex;
    std::map < std::string, connection_t > connection_map;

    template <typename T, typename = void> struct is_container : std::false_type{};
    template <typename T>
        struct is_container<T,
            std::void_t<decltype(std::begin(std::declval<T>())), decltype(std::end(std::declval<T>()))>>
        : std::true_type { };

    template <typename Type> requires is_container<Type>::value
    std::string parseChains(const Type & chain)
    {
        std::string ret;
        std::vector<std::string> chains;
        for (auto it = begin(chain); it != end(chain); ++it)
        {
            chains.push_back(*it);
        }

        std::ranges::reverse(chains);
        for (auto it = begin(chains); it != end(chains); ++it) {
            std::string final_str = *it;
            // remove tailing and leading spaces
            while (!final_str.empty() && final_str.front() == ' ') final_str.erase(final_str.begin());
            final_str = final_str.substr(0, final_str.find_last_not_of(' ') + 1);
            ret += final_str + (it == (end(chains) - 1) /* last element? */ ? "" : " => ");
        }

        return ret;
    }

    mihomo backend_client;

public:
    general_info_pulling(const std::string & ip, const int port, const std::string& token) : backend_client(ip, port, token) { }
    ~general_info_pulling() = default;

    // need continuous updates
    void update_from_traffic(std::pair < std::mutex, std::string > & info);
    void update_from_connections(std::pair < std::mutex, std::string > & info);
    void update_from_logs(std::pair < std::mutex, std::string > & info)
    {
        logger.dlog(info.second, "\n");
    }

    // no continuous updates
    void update_from_configs(std::pair < std::mutex, std::string > & info) { logger.dlog(info.second, "\n"); }
    void update_from_proxies(std::pair < std::mutex, std::string > & info) { logger.dlog(info.second, "\n"); }

    [[nodiscard]] uint64_t get_current_upload_speed() const { return current_upload_speed.load(); }
    [[nodiscard]] uint64_t get_current_download_speed() const { return current_download_speed.load(); }
    [[nodiscard]] uint64_t get_total_uploaded_bytes() const { return total_uploaded_bytes.load(); }
    [[nodiscard]] uint64_t get_total_downloaded_bytes() const { return total_downloaded_bytes.load(); }
    [[nodiscard]] std::vector < connection_t > get_active_connections()
    {
        std::lock_guard lock(connection_map_mutex);
        const auto copy = connection_map | std::views::values;
        return { copy.begin(), copy.end() };
    }

    std::string current_focus;
    std::mutex current_focus_mutex;
    std::atomic_bool keep_pull_continuous_updates;

    void pull_continuous_updates()
    {
        std::vector < std::pair < std::shared_ptr < std::atomic_bool >, std::thread > > thread_pool;
        std::string last_update;
        while (keep_pull_continuous_updates.load())
        {
            std::string local_copy;
            {
                std::lock_guard lock(current_focus_mutex);
                local_copy = current_focus;
            }

            if (last_update != local_copy) // status changed
            {
                last_update = local_copy; // update status
                for (auto & running : thread_pool | std::views::keys) {
                    *running = false;
                }

                for (auto & T : thread_pool | std::views::values) {
                    if (T.joinable()) T.join();
                }

                // clear pool
                thread_pool.clear();
            }
            else // status unchanged
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100l));
                continue; // skip the rest
            }

            auto make_traffic = [&]
            {
                // /traffic puller
                auto traffic_running = std::make_shared<std::atomic_bool>(true);
                auto run_traffic = [this](const std::atomic_bool * _traffic_running)
                {
                    backend_client.get_stream_info("traffic",
                        _traffic_running,
                        this,
                        &general_info_pulling::update_from_traffic);
                };
                std::atomic_bool * ptr = traffic_running.get();
                thread_pool.emplace_back(std::move(traffic_running), std::thread(run_traffic, ptr));
            };

            auto make_connections = [&]
            {
                // /connections puller
                auto connection_running = std::make_shared<std::atomic_bool>(true);
                auto run_connections = [this](const std::atomic_bool * _connection_running)
                {
                    while (*_connection_running)
                    {
                        backend_client.get_info("connections",
                            this,
                            &general_info_pulling::update_from_connections);
                        std::this_thread::sleep_for(std::chrono::seconds(1l));
                    }
                };
                std::atomic_bool * ptr = connection_running.get();
                thread_pool.emplace_back(std::move(connection_running), std::thread(run_connections, ptr));
            };

            if (local_copy == "overview")
            {
                make_traffic();
                make_connections();
            }
            else if (local_copy == "connections")
            {
                make_connections();
            }
            else if (local_copy == "logs")
            {
                // /logs puller
                auto * log_running = new std::atomic_bool(true);
                auto run_logs = [&]
                {
                    backend_client.get_stream_info("logs",
                        log_running,
                        this,
                        &general_info_pulling::update_from_logs);
                };
                thread_pool.emplace_back(log_running, run_logs);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100l));
            }
        }

        for (auto & running : thread_pool | std::views::keys) {
            *running = false;
        }

        for (auto & T : thread_pool | std::views::values) {
            if (T.joinable()) T.join();
        }
    }
};

#endif //SRC_GENERAL_INFO_PULLING_H