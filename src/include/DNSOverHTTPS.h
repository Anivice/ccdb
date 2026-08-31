#ifndef CCDB_DNSOVERHTTPS_H
#define CCDB_DNSOVERHTTPS_H

#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__

#include <string>
#include <vector>

namespace ccdb
{
    std::vector<std::string> resolve(const std::string& url,
        const std::string& host,
        const std::string& dns_query,
        int timeout_sec = 5);
}

#endif

#endif //CCDB_DNSOVERHTTPS_H
