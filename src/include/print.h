#ifndef CCDB_PRINT_H
#define CCDB_PRINT_H

#include <string>
#include "utils.h"
#include <iostream>
#include <sstream>

namespace ccdb::utils
{
    class is_error {};
    class is_normal {};

    template < typename MsgType > requires (std::is_same_v<MsgType, is_error> || std::is_same_v<MsgType, is_normal>)
    void _print(const char * text)
    {
        if constexpr (std::is_same_v<MsgType, is_error>) {
            std::cerr << get_text(text);
        } else {
            std::cout << get_text(text);
        }
    }

    template < typename MsgType, typename T > requires (std::is_same_v<MsgType, is_error> || std::is_same_v<MsgType, is_normal>)
    void _print(const T & val)
    {
        if constexpr (std::is_same_v<MsgType, is_error>) {
            std::cerr << val;
        } else {
            std::cout << val;
        }
    }

    template < typename MsgType, typename... Args > requires (std::is_same_v<MsgType, is_error> || std::is_same_v<MsgType, is_normal>)
    void print(const Args &...args) {
        (_print<MsgType>(args), ...);
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