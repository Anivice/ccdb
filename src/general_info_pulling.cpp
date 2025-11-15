#include "general_info_pulling.h"

void general_info_pulling::update_from_traffic(std::string info)
{
    try {
        json data = json::parse(info);
        current_upload_speed = static_cast<uint64_t>(data["up"]);
        current_download_speed = static_cast<uint64_t>(data["down"]);
        // logger.dlog("Upload: ", current_upload_speed, " Download: ", current_download_speed, "\n");
    } catch (std::exception& e) {
        logger.elog("Error when pulling traffic data: ", e.what(), "\n");
    }
}

void general_info_pulling::update_from_connections(std::string info)
{
    auto get_time = [](const std::string & time)->unsigned long long
    {
        using namespace std;
        using namespace std::chrono;
        sys_time<nanoseconds> tp;
        istringstream iss {time};
        // Format:
        // %Y-%m-%d
        // T
        // %H:%M:%S
        // %Ez
        iss >> parse("%Y-%m-%dT%H:%M:%S%Ez", tp);
        if (iss.fail()) {
            cerr << "parse failed\n";
            return 1;
        }

        const auto ns_since_epoch = tp.time_since_epoch();
        const auto sec_since_epoch = duration_cast<seconds>(ns_since_epoch);

        const long long unix_seconds = sec_since_epoch.count();
        long long extra_nanos  = (ns_since_epoch - sec_since_epoch).count();
        return unix_seconds;
    };

    try {
        json data;
        data = json::parse(info);
        total_uploaded_bytes = static_cast<uint64_t>(data["downloadTotal"]);
        total_downloaded_bytes = static_cast<uint64_t>(data["uploadTotal"]);
        std::lock_guard map_lock(connection_map_mutex);
        std::map < std::string, connection_t > new_connection_map;
        for (const auto& connection : data["connections"])
        {
            std::string id = connection["id"];
            const auto network_type = std::string(connection["metadata"]["network"]);
            connection_t conn = { };
            conn.host = std::string(connection["metadata"]["host"]) + ":" + std::string(connection["metadata"]["destinationPort"]);
            conn.src = std::string(connection["metadata"]["sourceIP"]) + ":" + std::string(connection["metadata"]["sourcePort"]);
            conn.destination = connection["metadata"]["destinationIP"];
            conn.processName = connection["metadata"]["process"];
            conn.uploadSpeed = 0;
            conn.downloadSpeed = 0;
            conn.totalUploadedBytes = connection["upload"];
            conn.totalDownloadedBytes = connection["download"];
            conn.chainName = parseChains(connection["chains"]);
            conn.ruleName = std::string(connection["rule"]) + "(" + std::string(connection["rulePayload"]) + ")";
            conn.networkType = std::string(connection["metadata"]["type"]) +
                (network_type.empty() ? std::string("") : "(" + network_type + ")");
            conn.timeElapsedSinceConnectionEstablished =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()
                    - get_time(std::string(connection["start"]));
            const auto now = std::chrono::high_resolution_clock::now();
            conn.timeLastPulled = now;

            if (auto previous = connection_map.find(id); previous != connection_map.end())
            {
                const auto last_pull = previous->second.timeLastPulled;
                const auto duration = now - last_pull;
                const auto duration_in_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                const auto uploaded_during_pull = conn.totalUploadedBytes - previous->second.totalUploadedBytes;
                const auto download_during_pull = conn.totalDownloadedBytes - previous->second.totalDownloadedBytes;
                const auto uploaded_bytes_per_second = (long)((double)uploaded_during_pull / ((double)duration_in_milliseconds / 1000));
                const auto downloaded_bytes_per_second = (long)((double)download_during_pull / ((double)duration_in_milliseconds / 1000));

                // logger.dlog("====> Download Speed ", downloaded_bytes_per_second, "\n");

                conn.uploadSpeed = uploaded_bytes_per_second;
                conn.downloadSpeed = downloaded_bytes_per_second;
            }

            new_connection_map[id] = conn;
        }

        connection_map = new_connection_map; // update and discard previous

        // logger.dlog("Active connections: ", connection_map.size(), "\n");
    } catch (std::exception& e) {
        logger.elog("Error when pulling traffic data: ", e.what(), "\n");
    } catch (...) {
        logger.elog("Error when pulling traffic data\n");
    }
}

void general_info_pulling::pull_continuous_updates()
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
            auto log_running = std::make_shared<std::atomic_bool>(true);
            auto run_logs = [&](const std::atomic_bool * _log_running)
            {
                backend_client.get_stream_info("logs",
                    _log_running,
                    this,
                    &general_info_pulling::update_from_logs,
                    true);
            };
            std::atomic_bool * ptr = log_running.get();
            thread_pool.emplace_back(std::move(log_running), std::thread(run_logs, ptr));
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
