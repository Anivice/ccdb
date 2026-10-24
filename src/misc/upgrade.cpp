#ifndef APPIMAGE_BUILD
#include "httplib.h"
#include "utils.h"
#include "print.h"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
    constexpr uint64_t download_slice_size = 1024 * 1024;

    httplib::Result download_slice(
        httplib::Client & client,
        const std::string & path,
        httplib::Headers headers,
        const uint64_t offset,
        const uint64_t total,
        std::string & cache,
        std::atomic_uint64_t & overall_size,
        std::atomic_uint64_t & progress,
        const std::atomic_bool & cancelled)
    {
        const auto end = total == 0 ? download_slice_size - 1
            : offset + std::min(download_slice_size, total - offset) - 1;
        headers.erase("Range");
        headers.emplace("Range", "bytes=" + std::to_string(offset) + "-" + std::to_string(end));
        cache.reserve(static_cast<std::size_t>(end - offset + 1));

        constexpr int max_attempts = 5;
        for (int attempt = 0; attempt < max_attempts && !cancelled; ++attempt)
        {
            cache.clear();
            uint64_t expected = 0;
            int status = 0;
            httplib::Result result;
            try
            {
                result = client.Get(path, headers, [&](const httplib::Response & response)
                {
                    if (cancelled) return false;
                    status = response.status;
                    const auto encoding = response.get_header_value("Content-Encoding");
                    if (!encoding.empty() && encoding != "identity") {
                        throw std::runtime_error("Unexpected Content-Encoding in download");
                    }
                    if (status == 206)
                    {
                        const auto range = response.get_header_value("Content-Range");
                        const auto slash = range.find('/');
                        if (slash == std::string::npos) throw std::runtime_error("Missing Content-Range total");
                        const auto response_total = ccdb::utils::convertToNumber<uint64_t>(
                            std::string_view(range).substr(slash + 1));
                        if (response_total <= offset || (total != 0 && response_total != total)
                            || response_total > cache.max_size()) {
                            throw std::runtime_error("Unexpected download size");
                        }
                        const auto response_end = std::min(end, response_total - 1);
                        if (range != "bytes " + std::to_string(offset) + "-"
                            + std::to_string(response_end) + "/" + std::to_string(response_total)) {
                            throw std::runtime_error("Unexpected Content-Range: " + range);
                        }
                        expected = response_end - offset + 1;
                        if (response.has_header("Content-Length")
                            && response.get_header_value_u64("Content-Length") != expected) {
                            throw std::runtime_error("Unexpected slice Content-Length");
                        }
                        if (total == 0) overall_size = response_total;
                        return true;
                    }
                    // Only the initial request may fall back to a full response or redirect.
                    if (total == 0 && (status == 200 || status == 302)) {
                        overall_size = response.has_header("Content-Length")
                            ? response.get_header_value_u64("Content-Length") : 0;
                        return true;
                    }
                    if (total == 0 && status == 416
                        && response.get_header_value("Content-Range") == "bytes */0") {
                        overall_size = 0;
                        return true;
                    }
                    throw std::runtime_error("Unexpected download status: " + std::to_string(status));
                }, [&](const char * data, const std::size_t length)
                {
                    if (cancelled) return false;
                    if (status == 416) return true;
                    if (status == 206 && length > expected - cache.size()) {
                        throw std::runtime_error("Slice exceeds its requested range");
                    }
                    cache.append(data, length);
                    progress.fetch_add(length, std::memory_order_relaxed);
                    return true;
                });
                if (result && (status != 206 || cache.size() == expected)) return result;
            }
            catch (...) {
                progress.fetch_sub(cache.size(), std::memory_order_relaxed);
                cache.clear();
                throw;
            }

            progress.fetch_sub(cache.size(), std::memory_order_relaxed);
            cache.clear();
            if (cancelled) break;
            const auto error = result ? "Incomplete slice" : httplib::to_string(result.error());
            if (attempt + 1 == max_attempts) {
                throw std::runtime_error("Download failed at byte " + std::to_string(offset) + ": " + error);
            }
            ccdb::utils::print<ccdb::utils::is_error>("Download failed at byte ", offset,
                ": ", error, ", retrying...\n");
        }
        throw std::runtime_error("Download cancelled");
    }

    httplib::Result download_batch(
        std::vector<std::unique_ptr<httplib::Client>> & clients,
        const std::string & path,
        httplib::Headers headers,
        std::atomic_uint64_t & overall_size,
        std::atomic_uint64_t & progress,
        std::atomic_bool & cancelled)
    {
        headers.erase("Accept-Encoding");
        headers.emplace("Accept-Encoding", "identity");
        std::string first;
        auto result = download_slice(*clients.front(), path, headers, 0, 0,
            first, overall_size, progress, cancelled);
        if (result->status != 206) {
            if (result->status == 200 && result->has_header("Content-Length")) {
                ccdb::utils::print("Content-Length: ", overall_size.load(), "\n");
            }
            if (result->status == 416) {
                result->status = 200;
                result->reason = "OK";
                result->headers.erase("Content-Range");
                result->headers.erase("Content-Length");
                result->headers.emplace("Content-Length", "0");
            }
            result->body = std::move(first);
            return result;
        }

        const auto total = overall_size.load();
        ccdb::utils::print("Content-Length: ", total, "\n");
        // If available, prevent slices from different versions being combined.
        const auto etag = result->get_header_value("ETag");
        const auto modified = result->get_header_value("Last-Modified");
        headers.erase("If-Range");
        if (!etag.empty() && !etag.starts_with("W/")) headers.emplace("If-Range", etag);
        else if (!modified.empty()) headers.emplace("If-Range", modified);

        std::map<uint64_t, std::string> slices;
        slices.emplace(0, std::move(first));
        std::mutex slices_mutex;
        std::mutex error_mutex;
        std::exception_ptr worker_error;
        std::atomic_uint64_t next_offset = download_slice_size;
        const auto remaining = (total - 1) / download_slice_size;
        const auto worker_count = std::min<uint64_t>(clients.size(), remaining);
        const auto worker = [&](const std::size_t index)
        {
            // Map nodes and payloads are allocated locally, never under the shared lock.
            std::map<uint64_t, std::string> pending;
            try
            {
                while (!cancelled)
                {
                    const auto offset = next_offset.fetch_add(download_slice_size, std::memory_order_relaxed);
                    if (offset >= total) break;
                    std::string cache;
                    download_slice(*clients[index], path, headers, offset, total,
                        cache, overall_size, progress, cancelled);
                    pending.emplace(offset, std::move(cache));
                    std::unique_lock lock(slices_mutex, std::try_to_lock);
                    if (lock.owns_lock()) slices.merge(pending);
                    // A busy map never prevents fetching the next slice.
                }
                if (!cancelled && !pending.empty()) {
                    // This worker has finished all its network work before waiting here.
                    std::lock_guard lock(slices_mutex);
                    slices.merge(pending);
                }
            }
            catch (...) {
                std::lock_guard lock(error_mutex);
                if (!worker_error) worker_error = std::current_exception();
                cancelled = true;
            }
        };

        {
            std::vector<std::jthread> workers;
            try {
                for (std::size_t index = 1; index < worker_count; ++index) {
                    workers.emplace_back(worker, index);
                }
                if (worker_count != 0) worker(0);
            }
            catch (...) {
                cancelled = true;
                throw; // jthread joins already-started workers before leaving this scope.
            }
        }
        if (worker_error) std::rethrow_exception(worker_error);
        if (cancelled) throw std::runtime_error("Download cancelled");

        // Every worker is joined; concatenate in byte-offset order and release each cache.
        result->body.reserve(static_cast<std::size_t>(total));
        while (!slices.empty()) {
            auto slice = slices.extract(slices.begin());
            if (slice.key() != result->body.size()) throw std::runtime_error("Missing download slice");
            result->body.append(slice.mapped());
        }
        if (result->body.size() != total) throw std::runtime_error("Incomplete download");
        result->status = 200;
        result->reason = "OK";
        result->headers.erase("Content-Range");
        result->headers.erase("Content-Length");
        result->headers.emplace("Content-Length", std::to_string(total));
        return result;
    }

    httplib::Result get_from_url(const std::string & url,
        const std::vector<std::pair<std::string, std::string>> & headers,
        const int timeout, const bool batch_download = false,
        const int redirects_remaining = CPPHTTPLIB_REDIRECT_MAX_COUNT)
    {
        std::string scheme, host, path, proxy_host;
        if (!ccdb::utils::parse_url(url, scheme, host, path)) {
            throw std::invalid_argument("Invalid URL");
        }

        if (path.empty()) path = "/";
        httplib::Headers header_for_httpLib;
        std::ranges::for_each(headers, [&](const header_type::value_type & pair) {
            header_for_httpLib.emplace(pair.first, pair.second);
        });
        header_for_httpLib.emplace(user_agent_key, user_agent);

        int proxy_port = 0;
        const bool use_proxy = ccdb::utils::parse_proxy(
            ccdb::utils::getenv(scheme + "_proxy"), proxy_host, proxy_port);
        if (use_proxy) ccdb::utils::print("Using proxy ", proxy_host, ":", proxy_port, "\n");
        const auto ca_certificate = ccdb::utils::getenv("CCDB_POSSIBLE_SSL_CERTIFICATE");
        const auto client_count = batch_download
            ? std::clamp(std::thread::hardware_concurrency(), 1u, 16u) : 1u;
        // Each worker owns a client; sharing one would serialize requests inside httplib.
        // Build the pool before starting so the monitor can safely stop every client.
        std::vector<std::unique_ptr<httplib::Client>> clients;
        clients.reserve(client_count);
        for (unsigned int index = 0; index < client_count; ++index)
        {
            auto client = std::make_unique<httplib::Client>(scheme + "://" + host);
            if (timeout > 0) {
                client->set_read_timeout(timeout, 0);
                client->set_connection_timeout(timeout, 0);
                client->set_write_timeout(timeout, 0);
                client->set_keep_alive(true);
            }
            ccdb::utils::set_ssl_automatically(*client, url);
            if (!ca_certificate.empty()) {
                client->set_ca_cert_path(ca_certificate);
                client->enable_server_certificate_verification(true);
            }
            if (use_proxy) client->set_proxy(proxy_host, proxy_port);
            if (batch_download) client->set_decompress(false);
            clients.push_back(std::move(client));
        }

        std::atomic_bool finished = false;
        std::atomic_bool cancelled = false;
        std::exception_ptr worker_error;
        std::atomic_uint64_t progress_in_bytes = 0;
        std::atomic_uint64_t overall_size = 0;
        httplib::Result result;
        const auto begin = std::chrono::steady_clock::now();
        std::thread T([&]
        {
            try
            {
                if (!batch_download) {
                    result = clients.front()->Get(path, header_for_httpLib);
                }
                else {
                    result = download_batch(clients, path, header_for_httpLib,
                        overall_size, progress_in_bytes, cancelled);
                }
            }
            catch (...) {
                worker_error = std::current_exception();
            }
            finished = true;
        });

        bool time_out = false;
        const auto deadline = timeout > 0 ? begin + std::chrono::seconds(timeout)
            : std::chrono::steady_clock::time_point::max();
        auto next_progress = begin;
        while (!finished)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                cancelled = true;
                time_out = true;
            }
            if (cancelled) {
                // Repeat until all workers exit: stop() can race with a request starting.
                for (const auto & client : clients) client->stop();
            }
            else if (timeout > 0 && now >= next_progress)
            {
                const auto total = overall_size.load();
                const auto current = progress_in_bytes.load();
                const auto duration = std::chrono::duration<double>(now - begin).count();
                ccdb::utils::set_progress_bar(ccdb::utils::SET_PROGRESS,
                    total == 0 ? 0 : static_cast<int>(std::min(100.0,
                        std::round(static_cast<double>(current) / static_cast<double>(total) * 100))),
                    ccdb::utils::value_to_speed(duration > 0
                        ? static_cast<uint64_t>(std::round(static_cast<double>(current) / duration)) : 0));
                next_progress = now + std::chrono::milliseconds(800);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        T.join();
        if (timeout > 0) ccdb::utils::set_progress_bar(ccdb::utils::CLEAR_PROGRESS_BAR, 0);
        if (time_out) throw std::runtime_error("Download timed out");
        if (worker_error) std::rethrow_exception(worker_error);

        if (!result) throw std::runtime_error(httplib::to_string(result.error()));

        if (result->status == 302)
        {
            const auto redir = result->get_header_value("Location");
            if (redir.empty()) throw std::runtime_error("Redirect has no Location header");
            if (redirects_remaining == 0) throw std::runtime_error("Too many redirects");
            ccdb::utils::print("Redirect to ", redir, '\n');
            return get_from_url(redir, headers, timeout, batch_download, redirects_remaining - 1);
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
                ccdb::utils::print<utils::is_error>(e.what(), ": ", strerror(errno), " ", ", retrying...\n");
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

#ifndef __WINDOWS_MSVC_BUILD__
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
#else //__WINDOWS_MSVC_BUILD__
        const char* remote_url_prefix = "https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild.windows.x64.";
#endif //__WINDOWS_MSVC_BUILD__

        if (!remote_url_prefix) throw std::runtime_error("ENOSYS");
        const auto url_dest = remote_url_prefix + hash.substr(0, 8) + "/" + dest_name;
        try {
            httplib::Result res = get_from_url(url_dest, {}, timeout, true);

            if (!res) {
                throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", httplib::to_string(res.error())));
            }

            if (res->status != 200 && res->status != 206) {
                throw std::runtime_error(ccdb::utils::sprint("Failed to pull: ", std::to_string(res->status)));
            }

            return { res->body.begin(), res->body.end() };
        }
        catch (std::exception& e)
        {
            ::ccdb::utils::print(e.what(), "\n");
            return { };
        }
    }
}
#endif // APPIMAGE_BUILD