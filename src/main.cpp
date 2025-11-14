#include <iostream>
#include "log.h"
#include "httplib.h"
#include "mihomo.h"

Logger::Logger logger;

class dummy
{
public:
    void f(const std::atomic < mihomo::basic_info_overview_t > & info)
    {
        logger.logPrint(INFO_LOG, info.load().download_bytes, "\n");
    }
};

int main()
{
    mihomo sample;
    dummy d;
    sample.get_stream_info(&d, &dummy::f);

    // Logger::Logger logger;
    // logger.logPrint(INFO_LOG, "CCDB Version ", VERSION, "\n");
    // httplib::Client cli("127.0.0.1", 9090);
    // cli.set_decompress(false);
    // httplib::Headers headers = {
    //     {"Authorization", "Bearer "},
    // };
    //
    // std::string buffer;
    // auto res = cli.Get("/traffic", headers,
    //     [&](const char *data, const size_t len)
    //     {
    //         buffer.append(data, len);
    //         if (const auto pos = buffer.find('\n'); pos != std::string::npos) {
    //             const std::string first_line = buffer.substr(0, pos);
    //             std::cout << "First log line: " << first_line << "\n";
    //             return true;
    //         }
    //
    //         return false;
    //     }
    // );
    //
    // if (!res) {
    //     std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
    //     return 1;
    // }
    //
    // std::cout << "HTTP status: " << res->status << "\n";

}
