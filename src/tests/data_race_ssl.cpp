#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../include/httplib.h"
#include <thread>
#include <vector>

int main()
{
    std::vector < std::thread > thread_pool;
    auto worker = [&]()->void
    {
        httplib::Client cli("https://www.google.com");
        cli.set_ca_cert_path("/etc/ssl/cert.pem");
        cli.enable_server_certificate_verification(true);
        const auto Result = cli.Get("/", [&](const char *data, const size_t len) {
            (void)write(1, data, len);
            return true;
        });

        if (!Result || (Result->status != 200)) {
            std::cerr << "Error: " << Result.error() << std::endl;
        }
    };

    for (int i = 1; i <= 10; ++i) thread_pool.emplace_back(worker);
    std::ranges::for_each(thread_pool, [](std::thread & T) {if (T.joinable()) { T.join(); }});
    return 0;
}
