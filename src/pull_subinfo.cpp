#include "pull_subinfo.h"
#include <filesystem>
#include "httplib.h"
#include <regex>
#include "print.h"
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

    httplib::Client cli(scheme + "://" + host);
    if (scheme == "https" && utils::getenv("DISABLE_SERVER_CERTIFICATE_VERIFICATION") == "true") {
        cli.enable_server_certificate_verification(false);
    } else {
        std::vector < std::string > ca_paths = {
            utils::getenv("SSL_CERTIFICATE"),
            // possible system CA certificate locations
            utils::getenv("PREFIX") + "/etc/ssl/certs/ca-certificates.crt",
            utils::getenv("PREFIX") + "/etc/ssl/certs/ca-bundle.trust.crt",
            utils::getenv("PREFIX") + "/etc/ssl/cert.pem",
            utils::getenv("PREFIX") + "/etc/tls/cert.pem",
            utils::getenv("PREFIX") + "/etc/pki/ca-trust/extracted/openssl/ca-bundle.trust.crt",
            utils::getenv("PREFIX") + "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        };

        std::ranges::any_of(ca_paths, [&](const std::string& ca_path)->bool
        {
            if (!ca_path.empty() && std::filesystem::exists(ca_path))
            {
                cli.set_ca_cert_path(ca_path);
                cli.enable_server_certificate_verification(true);
                return true;
            }

            return false;
        });
    }

    if (parse_proxy(utils::getenv(scheme + "_proxy"), proxy_host, proxy_port)) {
        cli.set_proxy(proxy_host, proxy_port);
    }

    const httplib::Headers hdrs = {{"User-Agent", "clash-verge/2.1.0"}}; // dummy header
    auto res = cli.Head(path, hdrs);
    if (!res) {
        throw std::runtime_error(utils::sprint("Failed to pull: ", httplib::to_string(res.error())));
    }

    if (res->status != 200) {
        throw std::runtime_error(utils::sprint("Failed to pull: ", std::to_string(res->status)));
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

    throw std::runtime_error(utils::sprint("Failed to parse info"));
}
