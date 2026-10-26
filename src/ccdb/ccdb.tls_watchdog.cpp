// ccdb.tls_watchdog.cpp
//
// Copyright 2026 Anivice Ives
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY// without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef __WINDOWS_MSVC_BUILD__
#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
#include <regex>
#include "print.h"
#include "ccdb.h"
#include "utils.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace {
    enum class AddressType {
        IPv4,
        IPv6,
        Domain
    };

    AddressType classify_address(const std::string_view address)
    {
        const std::string ip(address);

        in_addr v4{};
        in6_addr v6{};

#ifdef _WIN32
        if (InetPtonA(AF_INET, ip.c_str(), &v4) == 1)
            return AddressType::IPv4;

        if (InetPtonA(AF_INET6, ip.c_str(), &v6) == 1)
            return AddressType::IPv6;
#else
        if (inet_pton(AF_INET, ip.c_str(), &v4) == 1)
            return AddressType::IPv4;

        if (inet_pton(AF_INET6, ip.c_str(), &v6) == 1)
            return AddressType::IPv6;
#endif

        return AddressType::Domain;
    }


    enum class Verdict {
        VALID,
        INVALID_CERTIFICATE,
        INCONCLUSIVE
    };

    struct TLSResult {
        Verdict verdict;
        long verify_code;
        std::string message;
    };

    TLSResult probe_tls(
        SSL_CTX * ctx,
        const std::string& connect_host,
        const std::string& verify_host,
        const std::uint16_t port = 443,
        const bool verify_host_is_ip = false
    ) {
        std::unique_ptr<BIO, decltype(&BIO_free_all)>
            bio(BIO_new_ssl_connect(ctx), &BIO_free_all);

        if (!bio)
            return {Verdict::INCONCLUSIVE, -1,
                    "BIO_new_ssl_connect failed"};

        SSL* ssl = nullptr;  // Owned by BIO
        BIO_get_ssl(bio.get(), &ssl);

        if (!ssl)
            return {Verdict::INCONCLUSIVE, -1,
                    "BIO_get_ssl failed"};

        X509_VERIFY_PARAM* param = SSL_get0_param(ssl);

        if (verify_host_is_ip) {
            if (X509_VERIFY_PARAM_set1_ip_asc(
                    param, verify_host.c_str()) != 1) {
                return {
                    Verdict::INCONCLUSIVE, -1,
                    "Invalid IP address"
                };
                    }
        } else {
            if (X509_VERIFY_PARAM_set1_host(
                    param, verify_host.c_str(), 0) != 1) {
                return {
                    Verdict::INCONCLUSIVE, -1,
                    "Failed to configure hostname verification"
                };
                    }

            // SNI is independent of certificate verification.
            if (SSL_set_tlsext_host_name(
                    ssl, verify_host.c_str()) != 1) {
                return {
                    Verdict::INCONCLUSIVE, -1,
                    "Failed to configure SNI"
                };
                    }
        }

        // Supports IPv4, IPv6 and DNS names.
        const std::string address =
            ((connect_host.find(':') != std::string::npos &&
              connect_host.front() != '[')
                ? "[" + connect_host + "]"
                : connect_host)
            + ":" + std::to_string(port);

        if (BIO_set_conn_hostname(
                bio.get(), address.c_str()) != 1)
            return {Verdict::INCONCLUSIVE, -1,
                    "Connection setup failed"};

        // Connect and negotiate TLS.
        // BIO_do_connect may itself initiate the handshake.
        const int connected = BIO_do_connect(bio.get());
        const int handshaken =
            connected > 0 ? BIO_do_handshake(bio.get()) : 0;

        // Obtain X.509 verification result even on failure.
        const long code = SSL_get_verify_result(ssl);

        if (code != X509_V_OK) {
            return {
                Verdict::INVALID_CERTIFICATE,
                code,
                X509_verify_cert_error_string(code)
            };
        }

        if (connected <= 0 || handshaken <= 0)
            return {Verdict::INCONCLUSIVE, code,
                    "TLS handshake failed"};

        X509* cert = SSL_get1_peer_certificate(ssl);

        if (!cert)
            return {Verdict::INCONCLUSIVE, code,
                    "No peer certificate"};

        X509_free(cert);

        return {Verdict::VALID, X509_V_OK,
                "TLS verification successful"};
    }
}

void ccdb::ccdb::tls_watchdog(const std::vector<std::string> & cmd)
{
    struct info_t {
        tsl::hopscotch_map < std::string, Verdict > destination_ip_map;
        std::string outbound_proxy;
        int error_count = 0;
        std::chrono::time_point<std::chrono::system_clock> last_valid_time { };
    };

    std::atomic_bool finished(false);
    auto watcher_ = watcher.make_status_watcher();
    // std::mutex mutex;
    utils::cache_w_freq_table_t < std::string /* domain */, info_t > local_cache;
    std::vector<std::thread> children;
    auto T0 = std::thread([&]
    {
        int sig = 0;
        while (sig >= 0) {
            if (sig = watcher_.wait(); sig == SIGINT) {
                finished = true;
                utils::print<utils::is_error>("Quitting...\n");
                break;
            }
        }
    });

    const std::vector ca_paths = {
        utils::getenv("SSL_CERTIFICATE"),
        // possible system CA certificate locations
        utils::getenv("PREFIX") + "/etc/ssl/certs/ca-certificates.crt",
        utils::getenv("PREFIX") + "/etc/ssl/certs/ca-bundle.trust.crt",
        utils::getenv("PREFIX") + "/etc/ssl/cert.pem",
        utils::getenv("PREFIX") + "/etc/tls/cert.pem",
        utils::getenv("PREFIX") + "/etc/pki/ca-trust/extracted/openssl/ca-bundle.trust.crt",
        utils::getenv("PREFIX") + "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    };
    std::string certification_path;

    (void)std::ranges::any_of(ca_paths, [&](const std::string& ca_path)->bool
    {
        if (!ca_path.empty() && std::filesystem::exists(ca_path))
        {
            certification_path = ca_path;
            return true;
        }

        return false;
    });

    if (certification_path.empty()) {
        utils::print<utils::is_error>("Empty certificate path\n");
        return;
    }

    while (!finished)
    {
        for (const auto connection_frame = backend_instance.get_active_connections();
            const auto & connection_ : connection_frame)
        {
            if (finished) break;
            children.emplace_back([&local_cache, &certification_path, &cmd, this]
                (general_info_pulling::connection_t connection)
            {
                auto ip_or_domain = connection.host;
                ip_or_domain = ip_or_domain.substr(0, ip_or_domain.find_last_of(':'));
                const auto port = connection.host.substr(connection.host.find_last_of(':') + 1);
                if (const auto type = classify_address(ip_or_domain);
                    (type == AddressType::Domain && port == "443" || port == "8443")
                    && connection.networkType.find("tcp") != std::string::npos) {
                    const auto now = std::chrono::system_clock::now();

                    {
                        if (const auto it = local_cache.get_cache(connection.host); it)
                        {
                            if (const auto endpoint = it->destination_ip_map.find(connection.destination);
                                endpoint != it->destination_ip_map.end())
                            {
                                if (now - it->last_valid_time < std::chrono::seconds(60 * 10))
                                {
                                    if (endpoint->second == Verdict::VALID) return;
                                    if (endpoint->second == Verdict::INCONCLUSIVE && it->error_count > 10) return;
                                    if (endpoint->second == Verdict::INVALID_CERTIFICATE && it->error_count > 5) {
                                        if (cmd.size() == 2 && cmd[1] == "dryRun") utils::print("Killing ", connection.host, "\n");
                                        else if (!backend_instance.close_connection(connection.metadata.connectionID))
                                            utils::print<utils::is_error>("Killing ", connection.host, " failed\n");
                                        return;
                                    }
                                }
                            }
                        }
                    }

                    const std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_client_method()), &SSL_CTX_free);
                    if (!ctx) throw std::runtime_error("SSL_CTX_new failed");
                    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
                    if (SSL_CTX_load_verify_file(ctx.get(), certification_path.c_str()) != 1)
                    {
                        ERR_print_errors_fp(stderr);
                        throw std::runtime_error("Failed to load CA certificates");
                    }

                    const auto result = probe_tls(ctx.get(),
                        ip_or_domain,
                        ip_or_domain,
                        utils::convertToNumber<int>(port));

                    info_t c_ { };
                    if (const auto it = local_cache.get_cache(connection.host); it) {
                        c_ = *it;
                    }
                    c_.last_valid_time = now;
                    c_.destination_ip_map[connection.destination] = result.verdict;
                    if (Verdict::VALID != result.verdict) {
                        ++c_.error_count;
                    }

                    if (result.verify_code == X509_V_ERR_HOSTNAME_MISMATCH
                        || result.verify_code == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) // hostname SNI mismatch
                    {
                        c_.destination_ip_map[connection.destination] = Verdict::INCONCLUSIVE;
                        // SNI match is not a proof of certificate failure
                    }

                    local_cache.emplace_cache(connection.host, c_);

                    switch (result.verdict)
                    {
                    case Verdict::VALID: {
                        utils::print<utils::is_error>("CA OK: ", connection.host, "\n");
                    }
                    break;
                    case Verdict::INVALID_CERTIFICATE: {
                        utils::print<utils::is_error>(connection.host, ": ", result.message, "\n");
                    }
                    break;
                    default: break;
                    }
                }
            }, connection_);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (children.size() > 256) {
            std::ranges::for_each(children, [](auto & T) {
                if (T.joinable()) T.join();
            });
            children.clear();
        }
    }

    if (T0.joinable()) T0.join();
    std::ranges::for_each(children, [](auto & T) { if (T.joinable()) T.join(); });
}

#endif //__YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
#endif //__WINDOWS_MSVC_BUILD__