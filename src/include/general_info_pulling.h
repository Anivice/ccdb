#ifndef SRC_GENERAL_INFO_PULLING_H
#define SRC_GENERAL_INFO_PULLING_H

#include "log.h"
#include "httplib.h"
#include "mihomo.h"
#include "ncursesw/ncurses.h"
#include "json.hpp"
#include "glogger.h"
#include <ranges>
#include <algorithm>
#include <thread>

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
    std::string current_focus;
    std::mutex current_focus_mutex;
    std::atomic_bool keep_pull_continuous_updates;
    std::vector < std::pair < std::string, std::string > > logs;
    std::mutex logs_mutex;
    std::thread pull_continuous_updates_worker;

    void pull_continuous_updates(); // blocked

public:
    general_info_pulling(const std::string & ip, const int port, const std::string& token) : backend_client(ip, port, token) { }
    ~general_info_pulling() = default;

    // need continuous updates
    void update_from_traffic(std::string info);
    void update_from_connections(std::string info);
    void update_from_logs(std::string info);

    [[nodiscard]] uint64_t get_current_upload_speed() const { return current_upload_speed.load(); }
    [[nodiscard]] uint64_t get_current_download_speed() const { return current_download_speed.load(); }
    [[nodiscard]] uint64_t get_total_uploaded_bytes() const { return total_uploaded_bytes.load(); }
    [[nodiscard]] uint64_t get_total_downloaded_bytes() const { return total_downloaded_bytes.load(); }
    [[nodiscard]] std::vector < connection_t > get_active_connections();
    [[nodiscard]] std::vector < std::pair < std::string, std::string > > get_logs();

    void stop_continuous_updates();
    void change_focus(const std::string & info);
    void start_continuous_updates();
};

#endif //SRC_GENERAL_INFO_PULLING_H
