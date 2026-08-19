#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fcntl.h>
#include <cstdio>
#include <sys/syscall.h>
#include "lzw6.h"
#include "json.hpp"
#include "ncursesw/ncurses.h"
#include "tsl/hopscotch_map.h"
#include "readline/history.h"
#include "utils.h"
#include "dump.h"

#ifndef __NR_memfd_create
# if defined(__x86_64__)
#  define __NR_memfd_create 319
# elif defined(__i386__)
#  define __NR_memfd_create 356
# elif defined(__aarch64__)
#  define __NR_memfd_create 279
# else
#  error "Unknown architecture"
# endif
#endif

#define STRX(x) #x
#define STR(x) JSON_STRX(x)
#define CASSERT(x)  \
if (!(x)) {         \
    std::cout << __FILE__ ":" STR(__LINE__) ": Assertion " #x " Failed!\n"; \
    _exit(EXIT_FAILURE); \
}

uint64_t ccdb::utils::get_timestamp() noexcept
{
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::system_clock::to_time_t(now);
    return ts;
}

timespec ccdb::utils::get_timespec() noexcept
{
    timespec ts{};
    timespec_get(&ts, TIME_UTC);
    return ts;
}

#if !((defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L)
static bool parse_rfc3339_to_unix_ns(const std::string &s, std::int64_t &out_ns)
{
    std::tm tm = {};
    std::int64_t frac_nanos = 0;
    int tz_h = 0, tz_m = 0;

    const std::size_t pos = s.find_last_of("+-");
    if (pos == std::string::npos || pos < 10) {
        return false; // no timezone sign, or clearly bogus
    }

    std::string datetime = s.substr(0, pos);
    const std::string offset   = s.substr(pos);

    std::string base = datetime;
    if (const std::size_t dot = datetime.find('.'); dot != std::string::npos)
    {
        base = datetime.substr(0, dot);
        const std::string frac = datetime.substr(dot + 1);

        int digits = 0;
        for (std::size_t i = 0;
             i < frac.size() && std::isdigit(static_cast<unsigned char>(frac[i])) && digits < 9;
             ++i) {
            frac_nanos = frac_nanos * 10 + (frac[i] - '0');
            ++digits;
             }

        while (digits < 9) {
            frac_nanos *= 10;
            ++digits;
        }
    }

    std::stringstream iss(base);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        return false;
    }

    const char tz_sign = offset[0];
    if (std::sscanf(offset.c_str() + 1, "%d:%d", &tz_h, &tz_m) != 2) {
        return false;
    }
    const int tz_sec = tz_h * 3600 + tz_m * 60;

    const std::time_t t = timegm(&tm);
    if (t == static_cast<std::time_t>(-1)) {
        return false;
    }

    auto sec = static_cast<std::int64_t>(t);
    if (tz_sign == '+') {
        sec -= tz_sec;
    } else if (tz_sign == '-') {
        sec += tz_sec;
    }

    out_ns = sec * 1000000000LL + frac_nanos;
    return true;
}
#endif

unsigned long long ccdb::utils::get_time(std::string time)
{
    if (!time.empty() && (time.back() == 'Z' || time.back() == 'z')) {
        time.pop_back();
        time += "+00:00";
    }

#if ((defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L)
    // #ifdef _FORCE_CPP_23
    using namespace std;
    using namespace std::chrono;
    sys_time<nanoseconds> tp;
    istringstream iss {time};
    // Format:
    // %Y-%m-%d
    // T
    // %H:%M:%S
    // %Ez
    iss >> std::chrono::parse("%Y-%m-%dT%H:%M:%S%Ez", tp);
    if (iss.fail()) {
        return 0;
    }

    const auto ns_since_epoch = tp.time_since_epoch();
    const auto sec_since_epoch = duration_cast<seconds>(ns_since_epoch);

    const long long unix_seconds = sec_since_epoch.count();
    // long long extra_nanos  = (ns_since_epoch - sec_since_epoch).count();
    return unix_seconds;
#else
    std::int64_t unix_ns = 0;
    parse_rfc3339_to_unix_ns(time, unix_ns);
    const std::int64_t unix_sec = unix_ns / 1000000000LL;
    return unix_sec;
#endif
}

static std::tm to_local_tm(std::time_t t)
{
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string ccdb::utils::format_time_local(const std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    const std::tm tm = to_local_tm(t);

    char buf[128] { };
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return { buf };
}

std::string ccdb::utils::getTimeNow()
{
#if !((defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L)
    const auto now = std::chrono::high_resolution_clock::now();
    const std::time_t now_c = std::chrono::high_resolution_clock::to_time_t(now);
    const std::tm now_tm = *std::localtime(&now_c); // potential thread-safety issue
    const auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()) % 1000000000ull;
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(9) << ms.count();
    return oss.str();
#else
    return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::high_resolution_clock::now());
#endif
}
