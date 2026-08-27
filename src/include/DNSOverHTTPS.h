#ifndef CCDB_DNSOVERHTTPS_H
#define CCDB_DNSOVERHTTPS_H

#include <string>
#include <vector>

namespace ccdb
{
    std::vector<std::string> resolve(const std::string& url, const std::string& host, const std::string& dns_query = "dns-query");
}

#endif //CCDB_DNSOVERHTTPS_H
