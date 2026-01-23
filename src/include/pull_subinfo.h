#ifndef CCDB_PULL_SUBINFO_H
#define CCDB_PULL_SUBINFO_H

#include <string>
#include <cstdint>

namespace ccdb {
    struct subinfo_t {
        uint64_t total_uploaded;
        uint64_t total_downloaded;
        uint64_t quota;
        uint64_t expire_unix_timestamp;
    };

    subinfo_t pull_clash_subinfo(const std::string & url);
}

#endif //CCDB_PULL_SUBINFO_H
