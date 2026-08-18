#ifndef CCDB_JSON_H
#define CCDB_JSON_H

#include "utils.h"

#define JSON_STRX(x) #x
#define JSON_STR(x) JSON_STRX(x)
#define JSON_ASSERT(x)                                                                                                  \
    if (!(x)) {                                                                                                         \
        std::cerr << __FILE__ ":" JSON_STR(__LINE__) ": Assertion " #x " Failed!\n";                                    \
        std::abort();                                                                                                   \
    }
#include "nlohmann/json.hpp"

#endif //CCDB_JSON_H
