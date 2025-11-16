#include "general_info_pulling.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstdint>

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

bool parse_rfc3339_to_unix_ns(const std::string &s, std::int64_t &out_ns)
{
    std::tm tm = {};
    std::int64_t frac_nanos = 0;
    char tz_sign = '+';
    int tz_h = 0, tz_m = 0;

    std::size_t pos = s.find_last_of("+-");
    if (pos == std::string::npos || pos < 10) {
        return false; // no timezone sign, or clearly bogus
    }

    std::string datetime = s.substr(0, pos);
    std::string offset   = s.substr(pos);

    std::string base = datetime;
    std::size_t dot = datetime.find('.');
    if (dot != std::string::npos) {
        base = datetime.substr(0, dot);
        std::string frac = datetime.substr(dot + 1);

        int digits = 0;
        for (std::size_t i = 0;
             i < frac.size() && std::isdigit(static_cast<unsigned char>(frac[i])) && digits < 9;
             ++i) {
            frac_nanos = frac_nanos * 10 + (frac[i] - '0');
            ++digits;
        }

        while (digits < 9) {
            frac_nanos *= 10;
            ++digits;
        }
    }

    std::istringstream iss(base);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        return false;
    }

    tz_sign = offset[0];
    if (std::sscanf(offset.c_str() + 1, "%d:%d", &tz_h, &tz_m) != 2) {
        return false;
    }
    int tz_sec = tz_h * 3600 + tz_m * 60;

    std::time_t t = timegm(&tm);
    if (t == static_cast<std::time_t>(-1)) {
        return false;
    }

    std::int64_t sec = static_cast<std::int64_t>(t);
    if (tz_sign == '+') {
        sec -= tz_sec;
    } else if (tz_sign == '-') {
        sec += tz_sec;
    }

    out_ns = sec * 1000000000LL + frac_nanos;
    return true;
}

void general_info_pulling::update_from_connections(std::string info)
{
    auto get_time = [](const std::string & time)->unsigned long long
    {
#ifdef _FORCE_CPP_23
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
#else
        std::int64_t unix_ns = 0;
        parse_rfc3339_to_unix_ns(time, unix_ns);
        const std::int64_t unix_sec = unix_ns / 1000000000LL;
        return unix_sec;
#endif
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

void general_info_pulling::update_from_logs(std::string info)
{
    json data;
    try {
        data = json::parse(info);
    } catch (const std::exception & e) {
        logger.dlog("Error: Cannot parse json: ", e.what(), "\n");
        return;
    }

    std::string type = data["type"], payload = data["payload"];
    std::ranges::transform(type, type.begin(), ::toupper);

    std::lock_guard lock(logs_mutex);
    if (logs.size() >= 512) {
        logs.erase(logs.begin());
    }
    logs.emplace_back(type, payload);
}

void general_info_pulling::replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement)
{
    if (target.empty()) return; // Avoid infinite loop if target is empty

    if (target == " " && replacement.empty()) {
        std::erase_if(original, [](const char c) { return c == ' '; });
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }
}

void general_info_pulling::pull_continuous_updates()
{
    keep_pull_continuous_updates = true;
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

    // logger.dlog("Stoping...\n");

    for (auto & running : thread_pool | std::views::keys) {
        *running = false;
    }

    for (auto & T : thread_pool | std::views::values) {
        if (T.joinable()) T.join();
    }
}

[[nodiscard]] std::vector < general_info_pulling::connection_t > general_info_pulling::get_active_connections()
{
    std::lock_guard lock(connection_map_mutex);
    const auto copy = connection_map | std::views::values;
    return { copy.begin(), copy.end() };
}

[[nodiscard]] std::vector < std::pair < std::string, std::string > > general_info_pulling::get_logs()
{
    std::lock_guard lock(logs_mutex); return logs;
}

[[nodiscard]] general_info_pulling::proxy_info_summary_t general_info_pulling::get_proxies_and_latencies_as_pair()
{
    std::lock_guard<std::mutex> lock(proxy_list_mtx);
    auto _group = proxy_groups;
    std::map < std::string /* proxy name */, int /* latency in ms */ > _lat;
    for (const auto & [proxy, latency] : proxy_latency)
    {
        _lat[proxy] = latency;
    }

    return { _group, _lat };
}

void general_info_pulling::stop_continuous_updates()
{
    keep_pull_continuous_updates = false;
    backend_client.abort();
    if (pull_continuous_updates_worker.joinable()) pull_continuous_updates_worker.join();
}

void general_info_pulling::change_focus(const std::string & info)
{
    std::lock_guard lock(current_focus_mutex);
    current_focus = info;
}

void general_info_pulling::start_continuous_updates()
{
    change_focus("overview");
    pull_continuous_updates_worker = std::thread(&general_info_pulling::pull_continuous_updates, this);
    std::this_thread::sleep_for(std::chrono::milliseconds(100l));
}

void general_info_pulling::update_proxy_list()
{
    const std::vector<std::string> ignored_proxies = { "COMPATIBLE", "PASS", "REJECT", "REJECT-DROP" };
    backend_client.get_info_no_instance("proxies", [&](std::string proxies)
    {
        try
        {
            std::lock_guard lock(proxy_list_mtx);
            proxy_groups.clear();
            proxy_latency.clear();
            proxy_list.clear();

            json data = json::parse(proxies);
            for (const auto & proxy : data["proxies"])
            {
                std::string string_name(proxy["name"]);
                if (std::ranges::find(ignored_proxies, string_name) != ignored_proxies.end()) {
                    // skip ignored words
                    continue;
                }

                std::vector < std::string > group_members;
                if (proxy.contains("all"))
                {
                    for (const auto & element : proxy["all"]) {
                        group_members.push_back(element);
                    }
                } else {
                    proxy_info_t p_info = {
                        .type = proxy["type"],
                        .udp = proxy["udp"],
                    };
                    proxy_list.emplace(string_name, p_info);
                    continue; // not a group
                }

                proxy_groups[string_name] = { group_members, proxy["now"] };
            }
        }
        catch (const std::exception & e)
        {
            logger.dlog(e.what(), "\n");
        }
    });

    std::ranges::for_each(proxy_list, [&](const std::pair < std::string, proxy_info_t > & proxy_)
    {
        proxy_latency.emplace(proxy_.first, -1);
    });
}

void general_info_pulling::latency_test(const std::string & url)
{
    std::map < std::string, std::atomic_int * > proxy_latency_local;
    {
        std::lock_guard<std::mutex> lock(proxy_list_mtx);
        std::ranges::for_each(proxy_list, [&](const std::pair < std::string, proxy_info_t > & proxy_)
        {
            proxy_latency_local.emplace(proxy_.first, &proxy_latency[proxy_.first]);
        });
    }

    std::vector < std::thread > thread_pool;
    std::ranges::for_each(proxy_latency_local | std::views::keys, [&](const std::string & proxy)
    {
        // logger.dlog("Fuck\n");
        *proxy_latency_local[proxy] = -1;
        auto * ptr = proxy_latency_local[proxy];
        auto worker = [&](std::string proxy_, std::string url_, std::atomic_int * ptr_)->void
        {
            replace_all(proxy_, " ", "%20");
            try
            {
                backend_client.get_info_no_instance("proxies/" + proxy_ + "/delay?url=" + url_ +"&timeout=5000",
                    [&ptr_](std::string result)
                    {
                        if (const json data = json::parse(result);
                            data.contains("delay"))
                        {
                            *ptr_ = data.at("delay");
                        }
                        // else
                        // {
                            // logger.dlog(std::string(data["message"]), "\n");
                        // }
                    });
            } catch (...) {
                // logger.dlog("Error when pulling\n");
                *ptr_ = -1;
            }
        };

        thread_pool.emplace_back(worker, proxy, url, ptr);
    });

    for (auto & thread : thread_pool) {
        if (thread.joinable()) thread.join();
    }
}

bool general_info_pulling::change_proxy_using_backend(const std::string & group_name, const std::string & proxy_name)
{
    if (!backend_client.change_proxy(group_name, proxy_name)) {
        return false;
    }

    update_proxy_list();
    return true;
}
