#include "pull_subinfo.h"
#include "httplib.h"
#include <regex>
#include "utils.h"

static bool parse_url(const std::string& url, std::string& scheme, std::string& host, std::string& path)
{
    const std::regex re(R"(^(\w+)://([^/]+)(/.*)$)");
    std::smatch match;
    if (!std::regex_match(url, match, re)) {
        return false;
    }
    scheme = match[1];
    host = match[2];
    path = match[3];
    return true;
}

static bool parse_proxy(const std::string& url, std::string& host, int & port)
{
    const std::regex re(R"(^[\w]+://([^/]+):([\d]+)$)");
    std::smatch match;
    if (!std::regex_match(url, match, re)) {
        return false;
    }
    host = match[1];
    port = std::stoi(match[2]);
    return true;
}

ccdb::subinfo_t ccdb::pull_clash_subinfo(const std::string &url)
{
    std::string scheme, host, path, proxy_host;
    int proxy_port = 0;
    if (!parse_url(url, scheme, host, path)) {
        throw std::invalid_argument("Invalid URL");
    }

    httplib::Client cli("http://" + host);
    if (parse_proxy(utils::getenv(scheme + "_proxy"), proxy_host, proxy_port)) {
        cli.set_proxy(proxy_host, proxy_port);
    }

    const httplib::Headers hdrs = {{"User-Agent", "clash-verge/2.1.0"}}; // dummy header
    auto res = cli.Get(path, hdrs);
    if (!res) {
        throw std::runtime_error(httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        throw std::runtime_error("Failed to pull: " + std::to_string(res->status));
    }

    // Get Subscription‑Userinfo
    const auto it = res->headers.find("Subscription-Userinfo");
    if (it == res->headers.end()) {
        return { };
    }

    const std::string info = it->second;
    const std::regex pattern(R"(upload=(\d+).*?download=(\d+).*?total=(\d+).*?expire=(\d+))");
    if (std::smatch match; std::regex_search(info, match, pattern)) {
        return {
            .total_uploaded = std::stoull(match[1].str()),
            .total_downloaded = std::stoull(match[2].str()),
            .quota = std::stoull(match[3].str()),
            .expire_unix_timestamp = std::stoull(match[4].str())
        };
    }

    throw std::runtime_error("Failed to parse info");
}
