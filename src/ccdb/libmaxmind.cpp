#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__
#include <cstring>
#include <stdexcept>
#include "libmaxmind.h"
#include "httplib.h"
#include "utils.h"
#include "json.hpp"
using namespace ccdb;
static const auto TIMEOUT_RESOLVED = utils::convertToNumber<int>(utils::getenv("CCDB_RESOLVED_TIMEOUT").empty() ? "0"
    : utils::getenv("CCDB_RESOLVED_TIMEOUT"));
std::string maxmindDB::online_geoip_search(const std::string &ip)
{
    if (const auto it = online_search_cache.get_cache(ip); it) {
        return *it;
    }

    httplib::Client client("http://ip-api.com");
    int p_port;
    std::string p_host;
    if (utils::parse_proxy(ccdb::utils::getenv("http_proxy"), p_host, p_port)) {
        client.set_proxy(p_host, p_port);
    } else {
        throw std::invalid_argument("You requested online compliment."
            "Built-in http://ip-api.com doesn't offer https for free, and you have to set an `http_proxy`.");
    }

    client.set_read_timeout(TIMEOUT_RESOLVED, 0);
    client.set_write_timeout(TIMEOUT_RESOLVED, 0);

    const auto result = client.Get("/json/" + ip);
    if (!result) {
        return { };
    }

    try {
        const auto geoCountryCode = nlohmann::json::parse(result->body)["countryCode"].get<std::string>();
        online_search_cache.emplace_cache(ip, geoCountryCode);
        return geoCountryCode;
    } catch (...) {
        return { };
    }
}

maxmindDB::maxmindDB(const std::string & path) {
    this->open(path);
}

void maxmindDB::open(const std::string & path)
{
    if (opened_) throw std::runtime_error("maxmindDB already opened");
    if (const int status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, &mmdb_); status != MMDB_SUCCESS) {
        throw std::runtime_error(MMDB_strerror(status));
    }
    opened_ = true;
}

maxmindDB::~maxmindDB()
{
    if (opened_) MMDB_close(&mmdb_);
}

#endif // __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__