#include "update.h"
#include "httplib.h"
#include "utils.h"
#include "print.h"
#include <string>
#include <vector>

#include "GIT_HASH.h"

//Get current architecture, detects nearly every architecture. Coded by Freak
static constexpr const char * getBuild()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
    return "i586";
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) \
    || defined(__ARM_ARCH_7S__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) \
    || defined(__ARM_ARCH_7S__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__) \
    || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
    return "armv7";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "UNKNOWN";
#endif
}

/*
 * curl -fsSL   -H 'Accept: application/vnd.github.sha' \
 *              -H 'X-GitHub-Api-Version: 2022-11-28' \
 *                  'https://api.github.com/repos/Anivice/ccdb/commits/HEAD'
 */

static constexpr int strcmp_constexpr(const char* s1, const char* s2)
{
    while (*s1 && *s2 && (*s1 == *s2)) {
        ++s1;
        ++s2;
    }
    return static_cast<int>(*s1) - static_cast<int>(*s2);
}

static constexpr char remote_repo_url[] = "https://api.github.com/repos/Anivice/ccdb/commits/HEAD";
static constexpr char header_1_name[] = "Accept";
static constexpr char header_1_value[] = "application/vnd.github.sha";
static constexpr char header_2_name[] = "X-GitHub-Api-Version";
static constexpr char header_2_value[] = "2022-11-28";
static constexpr const char * ArchName = getBuild();
static_assert(strcmp_constexpr(ArchName, "UNKNOWN") != 0, "Unknown arch");

using header_type = std::vector < std::pair < std::string, std::string > >;

namespace
{
    struct remote_repo
    {
        const std::string url;
        const header_type headers;
    };

    remote_repo get_remote_from_env()
    {
        const auto env_remote_repo_url = ccdb::utils::getenv("REMOTE_UPDATE_REPO_URL");
        const auto env_headers = ccdb::utils::getenv("REMOTE_UPDATE_REPO_HEADERS");
        header_type env_headers_vec;
        std::stringstream ss(env_headers); std::string s;
        while (std::getline(ss, s, ';')) {
            const auto name = s.substr(0, s.find_first_of(':'));
            const auto value = s.substr(s.find_first_of(':') + 1);
            env_headers_vec.emplace_back(name,value);
        }

        return {
            .url = env_remote_repo_url,
            .headers = env_headers_vec
        };
    }

    remote_repo get_remote()
    {
        const auto env_remote = get_remote_from_env();
        if (env_remote.url.empty())
        {
            return {
                .url = remote_repo_url,
                .headers = {
                        { header_1_name, header_1_value },
                        { header_2_name, header_2_value }
                }
            };
        }

        return env_remote;
    }

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
                finished = true;
            }
        });

        if (timeout > 0)
        {
            for (int i = 0; i < timeout * 100; i++) {
                if (finished) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            client.stop();
        }

        if (T.joinable()) T.join();

        if (!res) {
            throw std::runtime_error(std::to_string(res->status) + ": " + to_string(res.error()));
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

    std::string get_latest_hash(const int timeout)
    {
        const auto [url, headers] = get_remote();
        const auto res = get_from_url(url, headers, timeout);
        if (!res || res->status != 200) throw std::runtime_error(std::to_string(res->status) + ": " + to_string(res.error()));
        return res->body;
    }
}

std::vector<char> get_content(const int timeout)
{
    const auto hash = get_latest_hash(timeout);
    const auto current_hash = ccdb_utils_unpack_string(GIT_HASH);
    if (hash.substr(0, 8) == current_hash)
    {
        throw std::runtime_error(ccdb::utils::sprint("Already the latest build (", hash, ")"));
    }

    // wget https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild."$VER"/ccdb."$ARCH" -O "$DEST"
    const auto url_dest = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild."
        + hash.substr(0, 8) + "/ccdb." + ArchName;
    httplib::Result res = get_from_url(url_dest, {}, timeout, true);
    if (!res) {
        throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", httplib::to_string(res.error())));
    }

    if (res->status != 200) {
        throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", std::to_string(res->status)));
    }

    return { res->body.begin(), res->body.end() };
}
