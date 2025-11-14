#include <iostream>
#include <chrono>
#include <thread>
#include "log.h"
#include "httplib.h"
#include "mihomo.h"
#include "ncursesw/ncurses.h"
#include "json.hpp"

Logger::Logger logger;
using json = nlohmann::json;

class general_info_pulling
{
public:
    general_info_pulling() = default;
    ~general_info_pulling() = default;

    void update_from_traffic(std::pair < std::mutex, std::string > & info)
    {
        try {
            std::lock_guard lock(info.first);
            json data = json::parse(info.second);
            current_upload_speed = static_cast<uint64_t>(data["up"]);
            current_download_speed = static_cast<uint64_t>(data["down"]);
            logger.dlog("Upload: ", current_upload_speed, " Download: ", current_download_speed, "\n");
        } catch (std::exception& e) {
            logger.elog("Error when pulling traffic data: ", e.what(), "\n");
        }
    }

private:
    std::atomic < uint64_t > current_upload_speed;
    std::atomic < uint64_t > current_download_speed;
    std::atomic < uint64_t > total_uploaded_bytes;
    std::atomic < uint64_t > total_downloaded_bytes;

    struct connection_t
    {
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
    };
    std::mutex connection_map_mutex;
    std::vector < connection_t > connection_map;

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

public:
    void update_from_connections(std::pair < std::mutex, std::string > & info)
    {
        auto get_time = [](const std::string & time)->unsigned long long
        {
            using namespace std;
            using namespace std::chrono;
            sys_time<nanoseconds> tp;
            istringstream iss {time};
            // Format:
            // %Y-%m-%d      = 2025-11-14
            // T             = literal 'T'
            // %H:%M:%S      = 19:37:18 (seconds may have fractional part)
            // %Ez           = +08:00 (numeric UTC offset with colon)
            iss >> parse("%Y-%m-%dT%H:%M:%S%Ez", tp);
            if (iss.fail()) {
                cerr << "parse failed\n";
                return 1;
            }

            // Convert to Unix timestamp (seconds since 1970-01-01T00:00:00Z)
            const auto ns_since_epoch = tp.time_since_epoch();
            const auto sec_since_epoch = duration_cast<seconds>(ns_since_epoch);

            long long unix_seconds = sec_since_epoch.count();
            long long extra_nanos  = (ns_since_epoch - sec_since_epoch).count();
            return unix_seconds;
        };

        try {
            std::lock_guard lock(info.first);
            json data = json::parse(info.second);
            total_uploaded_bytes = static_cast<uint64_t>(data["downloadTotal"]);
            total_downloaded_bytes = static_cast<uint64_t>(data["uploadTotal"]);
            std::lock_guard map_lock(connection_map_mutex);
            connection_map.clear();
            for (const auto& connection : data["connections"])
            {
                const auto network_type = std::string(connection["metadata"]["network"]);
                connection_t conn = {
                    .host = std::string(connection["metadata"]["host"]) + ":" + std::string(connection["metadata"]["destinationPort"]),
                    .src = std::string(connection["metadata"]["sourceIP"]) + ":" + std::string(connection["metadata"]["sourcePort"]),
                    .destination = connection["metadata"]["destinationIP"],
                    .processName = connection["metadata"]["process"],
                    .uploadSpeed = connection["upload"],
                    .downloadSpeed = connection["download"],
                    .totalUploadedBytes = 0,
                    .totalDownloadedBytes = 0,
                    .chainName = parseChains(connection["chains"]),
                    .ruleName = std::string(connection["rule"]) + "(" + std::string(connection["rulePayload"]) + ")",
                    .networkType = std::string(connection["metadata"]["type"]) +
                        (network_type.empty() ? std::string("") : "(" + network_type + ")"),
                    .timeElapsedSinceConnectionEstablished =
                        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()
                            - get_time(std::string(connection["start"])),
                };

                connection_map.push_back(conn);
            }

            logger.dlog("Active connections: ", connection_map.size(), "\n");
        } catch (std::exception& e) {
            logger.elog("Error when pulling traffic data: ", e.what(), "\n");
        } catch (...) {
            logger.elog("Error when pulling traffic data\n");
        }
    }

    [[nodiscard]] uint64_t get_current_upload_speed() const { return current_upload_speed.load(); }
    [[nodiscard]] uint64_t get_current_download_speed() const { return current_download_speed.load(); }
    [[nodiscard]] uint64_t get_total_uploaded_bytes() const { return total_uploaded_bytes.load(); }
    [[nodiscard]] uint64_t get_total_downloaded_bytes() const { return total_downloaded_bytes.load(); }
    [[nodiscard]] std::vector < connection_t > get_active_connections() { std::lock_guard lock(connection_map_mutex); return connection_map; }
};

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
    std::this_thread::sleep_for(std::chrono::seconds(3l));
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
