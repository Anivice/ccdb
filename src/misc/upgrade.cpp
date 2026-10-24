#ifndef __WINDOWS_MSVC_BUILD__
#ifndef APPIMAGE_BUILD
#include "httplib.h"
#include "utils.h"
#include "print.h"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include <vector>

//Get current architecture, detects nearly every architecture. Coded by Freak
static constexpr const char * getBuild()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i586";
#elif (defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7__)) && defined(__ARM_PCS_VFP)
    return "armv7hf";
#else
    return "UNKNOWN";
#endif
}

static constexpr char user_agent_key[] = "User-Agent";
static constexpr char user_agent[] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36";

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
    httplib::Result download(
        httplib::Client & client,
        const std::string & path,
        const httplib::Headers & headers,
        std::string & content,
        std::atomic_uint64_t * size,
        std::atomic_uint64_t * cur_size)
    {
        auto result = client.Get(path, headers,  [&](const httplib::Response& response)
        {
            if (response.status == 206)
            {
                // Server accepted resume request.
                const auto content_range = response.get_header_value("Content-Range");
                const auto expected_prefix = "bytes " + std::to_string(content.size()) + "-";

                if (content.empty() || content_range.rfind(expected_prefix, 0) != 0) {
                    ccdb::utils::print<ccdb::utils::is_error>("Unexpected Content-Range: ", content_range, '\n');
                    return false;
                }

                // An open-ended Range request must supply the rest of the file.
                const auto slash = content_range.find('/', expected_prefix.size());
                if (slash == std::string::npos) return false;
                const auto end = ccdb::utils::convertToNumber<uint64_t>(
                    std::string_view(content_range).substr(expected_prefix.size(), slash - expected_prefix.size()));
                const auto total = ccdb::utils::convertToNumber<uint64_t>(
                    std::string_view(content_range).substr(slash + 1));
                if (total <= content.size() || end != total - 1) return false;
                *size = total;
                return true;
            }

            if (response.status == 200 || response.status == 302) {
                // A server may ignore Range and send the complete file again.
                content.clear();
                *cur_size = 0;
                *size = response.has_header("Content-Length")
                    ? response.get_header_value_u64("Content-Length") : 0;
                ccdb::utils::print("Content-Length: ", *size, "\n");
                return true;
            }

            ccdb::utils::print<ccdb::utils::is_error>("Unexpected status: ", response.status, '\n');
            return false;
        },
        [&](const char* data, const std::size_t length)
        {
            content.append(data, length);
            *cur_size = content.size();
            return true;
        });

        if (result && result->status == 206 && content.size() != size->load()) {
            throw std::runtime_error("Incomplete resumed download");
        }
        return result;
    }

    httplib::Result get_from_url(const std::string & url,
        const std::vector<std::pair<std::string, std::string>> & headers,
        const int timeout, const bool resume = false,
        const int redirects_remaining = CPPHTTPLIB_REDIRECT_MAX_COUNT)
    {
        std::string scheme, host, path, proxy_host;
        if (!ccdb::utils::parse_url(url, scheme, host, path)) {
            throw std::invalid_argument("Invalid URL");
        }

        if (path.empty()) path = "/";
        httplib::Client client(scheme + "://" + host);
        httplib::Headers header_for_httpLib;
        std::ranges::for_each(headers, [&](const header_type::value_type & pair) {
            header_for_httpLib.emplace(pair.first, pair.second);
        });
        header_for_httpLib.emplace(user_agent_key, user_agent);

        if (timeout > 0) {
            client.set_read_timeout(timeout, 0);
            client.set_connection_timeout(timeout, 0);
            client.set_write_timeout(timeout, 0);
            client.set_keep_alive(true);
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

        std::atomic_bool finished = false;
        std::atomic_bool cancelled = false;
        std::exception_ptr worker_error;
        std::atomic_uint64_t progress_in_bytes = 0;
        std::atomic_uint64_t overall_size = 0;
        std::string body;
        httplib::Result result;
        std::thread T([&]
        {
            try
            {
                if (!resume) {
                    result = client.Get(path, header_for_httpLib);
                }
                else
                {
                    constexpr int max_attempts = 5;
                    for (int attempt = 0; attempt < max_attempts && !cancelled; ++attempt)
                    {
                        header_for_httpLib.erase("Range");
                        if (!body.empty()) {
                            header_for_httpLib.emplace("Range", "bytes=" + std::to_string(body.size()) + "-");
                        }

                        result = download(client, path, header_for_httpLib, body, &overall_size, &progress_in_bytes);
                        if (result || cancelled || result.error() == httplib::Error::Canceled) break;
                        if (attempt + 1 < max_attempts) {
                            ccdb::utils::print<ccdb::utils::is_error>("Download failed: ",
                                httplib::to_string(result.error()), ", retrying...\n");
                        }
                    }

                    if (result) result->body = std::move(body);
                }
            }
            catch (...) {
                worker_error = std::current_exception();
            }
            finished = true;
        });

        bool time_out = false;
        if (timeout > 0)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
            while (!finished)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    cancelled = true;
                    time_out = true;
                    // Repeat until the worker exits: stop() can race with a request starting.
                    client.stop();
                }
                else
                {
                    const auto total = overall_size.load();
                    const auto current = progress_in_bytes.load();
                    ccdb::utils::set_progress_bar(ccdb::utils::SET_PROGRESS,
                        total == 0 ? 0 : static_cast<int>(std::min(100.0,
                            std::round(static_cast<double>(current) / static_cast<double>(total) * 100))));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            ccdb::utils::set_progress_bar(ccdb::utils::CLEAR_PROGRESS_BAR, 0);
        }

        T.join();
        if (worker_error) std::rethrow_exception(worker_error);

        if (!result || time_out) {
            throw std::runtime_error((result ? std::to_string(result->status) + ": " : "") +
                to_string(result.error()) + (time_out ? " <Timeout>" : ""));
        }

        if (result->status == 302)
        {
            const auto redir = result->get_header_value("Location");
            if (redir.empty()) throw std::runtime_error("Redirect has no Location header");
            if (redirects_remaining == 0) throw std::runtime_error("Too many redirects");
            ccdb::utils::print("Redirect to ", redir, '\n');
            return get_from_url(redir, headers, timeout, resume, redirects_remaining - 1);
        }

        return result;
    }

    std::string get_latest_hash(const std::string & url, const int timeout)
    {
        const auto res = get_from_url(url, {}, timeout);
        if (!res) throw std::runtime_error(httplib::to_string(res.error()));
        if (res->status != 200) throw std::runtime_error("Unexpected HTTP status: " + std::to_string(res->status));
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

        if (content.empty()) throw std::runtime_error("Cannot establish hash");

        const auto json = nlohmann::json::parse(content);
        const auto hash = std::string(json["sha"]);
        if (hash.substr(0, 8) == GIT_HASH) {
#ifndef __DEBUG__
            throw std::runtime_error(ccdb::utils::sprint("Already the latest build (", hash, ")"));
#else
            utils::print("Downloading the build ", hash, ".\n");
#endif
        }

        const char * remote_url_prefix = nullptr;
        if constexpr (strcmp_constexpr(ArchName, "aarch64") == 0) {
#ifdef __STATIC_MUSL__
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild_aarch64_static.";
#else
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild.aarch64.";
#endif
        } else if constexpr (strcmp_constexpr(ArchName, "x86_64") == 0) {
#ifdef __STATIC_MUSL__
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild_x86_64_static.";
#else
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild.";
#endif
        } else if constexpr (strcmp_constexpr(ArchName, "i586") == 0) {
#ifdef __STATIC_MUSL__
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild_i586_static.";
#endif //__STATIC_MUSL__
        } else if constexpr (strcmp_constexpr(ArchName, "armv7hf") == 0) {
#ifdef __STATIC_MUSL__
            remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild_armv7hf_static.";
#endif //__STATIC_MUSL__
        }

        if (!remote_url_prefix) throw std::runtime_error("ENOSYS");
        const auto url_dest = remote_url_prefix + hash.substr(0, 8) + "/" + dest_name;
        httplib::Result res = get_from_url(url_dest, {}, timeout, true);
        if (!res) {
            throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", httplib::to_string(res.error())));
        }

        if (res->status != 200 && res->status != 206) {
            throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", std::to_string(res->status)));
        }

        return { res->body.begin(), res->body.end() };
    }
}
#endif // APPIMAGE_BUILD
#endif //__WINDOWS_MSVC_BUILD__
