#ifndef CCDB_PRINT_H
#define CCDB_PRINT_H

#include <string>
#include "utils.h"
#include <iostream>
#include <sstream>

namespace ccdb::utils
{
    inline void _print(const char * text) {
        std::cout << get_text(text);
    }

    template <typename T>
    void _print(const T& val) {
        std::cout << val;
    }

    template <typename... Args>
    void print(const Args &...args) {
        (_print(args), ...);
    }

    inline void _sprint(std::ostringstream & oss, const char * text) {
        oss << get_text(text);
    }

    template <typename T>
    void _sprint(std::ostringstream & oss, const T& val) {
        oss << val;
    }

    template <typename... Args>
    std::string sprint(const Args &...args)
    {
        std::ostringstream oss;
        (_sprint(oss, args), ...);
        return oss.str();
    }
}

#endif //CCDB_PRINT_H