#include "DNSOverHTTPS.h"
#include "utils.h"
#include "httplib.h"
#include "json.hpp"
#include <regex>
#include <algorithm>

namespace
{
    std::vector<std::string> get(const std::string& url, const std::string& path, const int timeout_sec)
    {
        const thread_local std::regex ipv4_pattern(R"(^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.){3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])$)");
        const thread_local std::regex ipv6_pattern(R"(^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:))$)");

        httplib::Client client(url);
        const httplib::Headers headers = { {"accept", "application/dns-json"} };
        ccdb::utils::set_ssl_automatically(client, url);
        int p_port;
        std::string p_host;
        if (ccdb::utils::parse_proxy(ccdb::utils::getenv("https_proxy"), p_host, p_port)) {
            client.set_proxy(p_host, p_port);
        }

        client.set_read_timeout(timeout_sec, 0);
        client.set_write_timeout(timeout_sec, 0);

        const auto result = client.Get(path, headers);
        if (!result) {
            return { };
        }

        std::vector<std::string> ret;
        try {
            for (const auto& entry : nlohmann::json::parse(result->body)["Answer"]) {
                const auto & ip = entry["data"].get<std::string>();
                if (std::regex_match(ip, ipv4_pattern) || std::regex_match(ip, ipv6_pattern)) {
                    ret.emplace_back(ip);
                }
            }
        } catch (...) { }
        return ret;
    }
}

// URL is https://[...]/dns-query
std::vector<std::string> ccdb::resolve(
    const std::string& url,
    const std::string& host,
    const std::string& dns_query,
    const int timeout_sec)
{
    std::vector<std::string> result;
    const auto A = get(url, "/" + dns_query + "?name=" + host + "&type=A", timeout_sec);
    const auto AAAA = get(url, "/" + dns_query + "?name=" + host + "&type=AAAA", timeout_sec);
    result.insert(result.end(), A.begin(), A.end());
    result.insert(result.end(), AAAA.begin(), AAAA.end());
    return result;
}
