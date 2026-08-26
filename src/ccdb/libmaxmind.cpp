#include <cstring>
#include <stdexcept>
#include "libmaxmind.h"
using namespace ccdb;

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
