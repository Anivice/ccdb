#include "DNSOverHTTPS.h"
#include "utils.h"
#include "httplib.h"
#include "json.hpp"
#include <regex>
#include <algorithm>

namespace
{
    std::vector<std::string> get(const std::string& url, const std::string& path)
    {
        httplib::Client client(url);
        const httplib::Headers headers = { {"accept", "application/dns-json"} };
        ccdb::utils::set_ssl_automatically(client, url);
        int p_port;
        std::string p_host;
        if (ccdb::utils::parse_proxy(ccdb::utils::getenv("https_proxy"), p_host, p_port)) {
            client.set_proxy(p_host, p_port);
        }

        client.set_read_timeout(10, 0);
        client.set_write_timeout(10, 0);

        const auto result = client.Get(path, headers);
        if (!result) {
            return { };
        }

        std::vector<std::string> ret;
        try {
            for (const auto& entry : nlohmann::json::parse(result->body)["Answer"]) {
                ret.emplace_back(entry["data"].get<std::string>());
            }
        } catch (...) { }
        return ret;
    }
}

// URL is https://[...]/dns-query
std::vector<std::string> ccdb::resolve(const std::string& url, const std::string& host, const std::string& dns_query)
{
    static utils::cache_w_freq_table_t<std::string, std::vector<std::string>> cache;
    if (const auto it = cache.get_cache(host); it) {
        return *it;
    }

    std::vector<std::string> result;
    const auto A = get(url, "/" + dns_query + "?name=" + host + "&type=A");
    const auto AAAA = get(url, "/" + dns_query + "?name=" + host + "&type=AAAA");
    result.insert(result.end(), A.begin(), A.end());
    result.insert(result.end(), AAAA.begin(), AAAA.end());
    // std::erase_if(result, [](const auto& entry) { return entry.empty(); });

    if (!result.empty()) {
        cache.emplace_cache(host, result);
        return result;
    }

    return { };
}
