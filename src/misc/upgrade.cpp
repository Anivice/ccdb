#include "httplib.h"
#include "utils.h"
#include "print.h"
#include "json.hpp"
#include <string>
#include <vector>

//Get current architecture, detects nearly every architecture. Coded by Freak
static constexpr const char * getBuild()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "UNKNOWN";
#endif
}

static constexpr int strcmp_constexpr(const char* s1, const char* s2)
{
    while (*s1 && *s2 && (*s1 == *s2)) {
        ++s1;
        ++s2;
    }
    return static_cast<int>(*s1) - static_cast<int>(*s2);
}

static constexpr const char * ArchName = getBuild();
static_assert(strcmp_constexpr(ArchName, "UNKNOWN") != 0, "Unknown arch");

using header_type = std::vector < std::pair < std::string, std::string > >;

namespace
{
    httplib::Result get_from_url(const std::string & url,
        const std::vector<std::pair<std::string, std::string>> & headers,
        const int timeout, const bool resume = false)
    {
        std::string scheme, host, path, proxy_host;
        if (!ccdb::utils::parse_url(url, scheme, host, path)) {
            throw std::invalid_argument("Invalid URL");
        }

        httplib::Client client(scheme + "://" + host);
        httplib::Headers header_for_httpLib;
        std::ranges::for_each(headers, [&](const header_type::value_type & pair) {
            header_for_httpLib.emplace(pair.first, pair.second);
        });

        if (timeout > 0) {
            client.set_read_timeout(timeout, 0);
            client.set_connection_timeout(timeout, 0);
            client.set_write_timeout(timeout, 0);
            client.set_keep_alive(false);
        }

        ccdb::utils::set_ssl_automatically(client, url);
        if (const auto CCDB_POSSIBLE_SSL_CERTIFICATE = ccdb::utils::getenv("CCDB_POSSIBLE_SSL_CERTIFICATE");
            !CCDB_POSSIBLE_SSL_CERTIFICATE.empty())
        {
            client.set_ca_cert_path(CCDB_POSSIBLE_SSL_CERTIFICATE);
            client.enable_server_certificate_verification(true);
        }

        if (int proxy_port = 0;
            ccdb::utils::parse_proxy(ccdb::utils::getenv(scheme + "_proxy"), proxy_host, proxy_port))
        {
            ccdb::utils::print("Using proxy ", proxy_host, ":", proxy_port, "\n");
            client.set_proxy(proxy_host, proxy_port);
        }

        httplib::Result res;
        std::atomic_bool finished = false;
        std::vector<char> content;
        std::atomic_uint64_t progress_in_bytes = 0;
        std::atomic_uint64_t overall_size = 0;
        std::thread T([&]
        {
            if (!resume)
            {
                res = client.Get(path, header_for_httpLib);
            }
            else
            {
                if (!content.empty()) {
                    header_for_httpLib.emplace("Range", "bytes=" + std::to_string(content.size()) + "-");
                }

                auto download = [&]
                {
                    res = client.Head(path, header_for_httpLib);
                    if (!res) {
                        finished = true;
                        return;
                    }

                    try {
                        overall_size = ccdb::utils::convertToNumber<uint64_t>(res->headers.find("Content-Length")->second);
                        content.reserve(overall_size);
                    } catch (...) { finished = true; return; }
                    res = client.Get(path, header_for_httpLib,  [&](const httplib::Response& response)
                    {
                        if (response.status == 206)
                        {
                            // Server accepted resume request.
                            const auto content_range = response.get_header_value("Content-Range");
                            const auto expected_prefix = "bytes " + std::to_string(content.size()) + "-";

                            if (content_range.rfind(expected_prefix, 0) != 0) {
                                ccdb::utils::print<ccdb::utils::is_error>("Unexpected Content-Range: ", content_range, '\n');
                                return false;
                            }

                            return true;
                        }

                        if (response.status == 200 || response.status == 302) {
                            content.clear();
                            return true;
                        }

                        ccdb::utils::print<ccdb::utils::is_error>("Unexpected status: ", response.status, '\n');
                        return false;
                    },
                    [&](const char* data, const std::size_t length)
                    {
                        progress_in_bytes += length;
                        content.insert(content.end(), data, data + length);
                        return true;
                    });
                };

                download();
                while (!res)
                {
                    ccdb::utils::print<ccdb::utils::is_error>("Download failed, retrying...\n");
                    download();
                }

                res->body = { content.begin(), content.end() };
            }

            finished = true;
        });

        bool time_out = false;
        if (timeout > 0)
        {
            for (int i = 0; i < timeout * 100; i++) {
                if (finished) break;
                ccdb::utils::set_progress_bar(ccdb::utils::SET_PROGRESS,
                    overall_size == 0 ? 0 :
                        static_cast<int>(
                            std::round(static_cast<double>(progress_in_bytes) / static_cast<double>(overall_size) * 100)
                        )
                );
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            ccdb::utils::set_progress_bar(ccdb::utils::SET_PROGRESS, 100);
            ccdb::utils::set_progress_bar(ccdb::utils::CLEAR_PROGRESS_BAR, 0);

            if (!finished)
            {
                client.stop();
                time_out = true;
            }
        }

        if (T.joinable()) T.join();

        if (!res || time_out) {
            throw std::runtime_error((res ? std::to_string(res->status) + ": " : "") +
                to_string(res.error()) + (time_out ? " <Timeout>" : ""));
        }

        while (res->status == 302)
        {
            if (std::string redir;
                std::ranges::any_of(res->headers, [&](const std::pair < std::string, std::string > & pair)->bool
                {
                    if (pair.first == "location" || pair.first == "Location") {
                        redir = pair.second;
                        return true;
                    }
                    return false;
                }))
            {
                ccdb::utils::print("Redirect to ", redir, '\n');
                res = get_from_url(redir, headers, timeout, resume);
            }
            else
            {
                throw std::runtime_error(std::to_string(res->status) + ": " + to_string(res.error()));
            }
        }

        return res;
    }

    std::string get_latest_hash(const std::string & url, const int timeout)
    {
        const auto res = get_from_url(url, {}, timeout);
        if (!res || res->status != 200) throw std::runtime_error(std::to_string(res->status) + ": " + to_string(res.error()));
        return res->body;
    }
}

namespace ccdb
{
    std::vector<char> get_content(const std::string & dest_name, const int timeout)
    {
        std::string content;
        for (int i = 0; i < 5 && content.empty(); i++)
        {
            try {
                content = get_latest_hash("https://api.github.com/repos/Anivice/ccdb/commits/HEAD", timeout);
            } catch (const std::exception & e) {
                ccdb::utils::print<utils::is_error>(e.what(), ", retrying...\n");
            }
        }

        const auto json = nlohmann::json::parse(content);
        const auto hash = std::string(json["sha"]);
        if (hash.substr(0, 8) == GIT_HASH) {
#ifndef __DEBUG__
            throw std::runtime_error(ccdb::utils::sprint("Already the latest build (", hash, ")"));
#else
            utils::print("Downloading the build ", hash, ".\n");
#endif
        }

        constexpr const auto * remote_url_prefix =
            strcmp_constexpr(ArchName, "aarch64") == 0 ?
            "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild.aarch64." :
            "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild.";

        const auto url_dest = remote_url_prefix + hash.substr(0, 8) + "/" + dest_name;
        httplib::Result res = get_from_url(url_dest, {}, timeout, true);
        if (!res) {
            throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", httplib::to_string(res.error())));
        }

        if (res->status != 200) {
            throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", std::to_string(res->status)));
        }

        return { res->body.begin(), res->body.end() };
    }
}