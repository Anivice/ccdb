#include <chrono>
#include <cstdio>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include "utils.h"

namespace {

[[noreturn]]
void bad_rfc3339(std::string_view s, const char* why)
{
    throw std::invalid_argument(
        std::string("invalid RFC 3339 timestamp (") +
        why + "): " + std::string(s));
}

bool ascii_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

int dec2(std::string_view s, std::size_t p)
{
    if (p + 2 > s.size() ||
        !ascii_digit(s[p]) ||
        !ascii_digit(s[p + 1]))
    {
        bad_rfc3339(s, "expected two decimal digits");
    }

    return (s[p] - '0') * 10 +
           (s[p + 1] - '0');
}

int dec4(std::string_view s, std::size_t p)
{
    if (p + 4 > s.size())
        bad_rfc3339(s, "expected four-digit year");

    int v = 0;

    for (std::size_t i = 0; i < 4; ++i) {
        if (!ascii_digit(s[p + i]))
            bad_rfc3339(s, "expected four-digit year");

        v = v * 10 + (s[p + i] - '0');
    }

    return v;
}


// Local time with numeric RFC 3339 offset:
//
//     2026-09-12T11:23:46.676218741+08:00
//
std::string format_time_rfc3339_local(const std::chrono::system_clock::time_point tp)
{
    using namespace std::chrono;

    /*
     * Work at nanosecond precision explicitly.
     *
     * floor<seconds> is important for timestamps before the epoch:
     * tp_ns - tp_sec then remains in [0, 1s).
     */
    const auto tp_ns  = time_point_cast<nanoseconds>(tp);
    const auto tp_sec = floor<seconds>(tp_ns);

    const auto ns =
        duration_cast<nanoseconds>(tp_ns - tp_sec).count();

    const auto tp_for_time_t =
        time_point_cast<system_clock::duration>(tp_sec);

    const std::time_t t =
        system_clock::to_time_t(tp_for_time_t);

    std::tm tm{};

    if (localtime_r(&t, &tm) == nullptr) {
        throw std::runtime_error(
            "localtime_r failed while formatting RFC 3339 time");
    }

    const int year = tm.tm_year + 1900;

    if (year < 0 || year > 9999) {
        throw std::out_of_range(
            "local time is outside RFC 3339 year range 0000..9999");
    }

    /*
     * POSIX %z normally gives:
     *
     *     +0800
     *     +0530
     *     +0545
     *     -0330
     *
     * RFC 3339 requires the colon:
     *
     *     +08:00
     *     +05:30
     */
    char raw_tz[32]{};

    if (std::strftime(
            raw_tz,
            sizeof raw_tz,
            "%z",
            &tm) == 0)
    {
        throw std::runtime_error(
            "strftime(%z) failed while formatting RFC 3339 time");
    }

    std::string tz{raw_tz};

    if (tz.size() == 5 &&
        (tz[0] == '+' || tz[0] == '-') &&
        ascii_digit(tz[1]) &&
        ascii_digit(tz[2]) &&
        ascii_digit(tz[3]) &&
        ascii_digit(tz[4]))
    {
        // +0530 -> +05:30
        tz.insert(3, 1, ':');
    }
    else if (!(tz.size() == 6 &&
               (tz[0] == '+' || tz[0] == '-') &&
               ascii_digit(tz[1]) &&
               ascii_digit(tz[2]) &&
               tz[3] == ':' &&
               ascii_digit(tz[4]) &&
               ascii_digit(tz[5])))
    {
        /*
         * RFC 3339 cannot represent historical timezone offsets
         * containing a seconds component.
         */
        throw std::runtime_error(
            "local UTC offset is not representable by RFC 3339: " +
            tz);
    }

    const int off_h =
        (tz[1] - '0') * 10 +
        (tz[2] - '0');

    const int off_m =
        (tz[4] - '0') * 10 +
        (tz[5] - '0');

    if (off_h > 23 || off_m > 59) {
        throw std::runtime_error(
            "local UTC offset is outside RFC 3339 range: " +
            tz);
    }

    char buf[80];

    const int n = std::snprintf(
        buf,
        sizeof buf,
        "%04d-%02d-%02dT%02d:%02d:%02d.%09lld%s",
        year,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        static_cast<long long>(ns),
        tz.c_str());

    if (n < 0 ||
        static_cast<std::size_t>(n) >= sizeof buf)
    {
        throw std::runtime_error(
            "failed to format RFC 3339 time");
    }

    return {
        buf,
        static_cast<std::size_t>(n)
    };
}

} // namespace

unsigned long long ccdb::utils::get_time(const std::string& input)
{
    using namespace std::chrono;

    const std::string_view s{input};

    /*
     * Shortest legal RFC 3339 timestamp:
     *
     *     YYYY-MM-DDTHH:MM:SSZ
     *
     *     01234567890123456789
     */
    if (s.size() < 20)
        bad_rfc3339(s, "too short");

    /*
     * RFC 3339 has fixed-width calendar/time fields.
     *
     * The RFC explicitly permits lower-case 't' and 'z' as well,
     * although generators SHOULD use upper case.
     */
    if (s[4] != '-' ||
        s[7] != '-' ||
        (s[10] != 'T' && s[10] != 't') ||
        s[13] != ':' ||
        s[16] != ':')
    {
        bad_rfc3339(s, "bad date/time separators");
    }

    const int y  = dec4(s, 0);
    const int mo = dec2(s, 5);
    const int d  = dec2(s, 8);
    const int h  = dec2(s, 11);
    const int mi = dec2(s, 14);
    const int se = dec2(s, 17);

    if (h > 23)
        bad_rfc3339(s, "hour is outside 00..23");

    if (mi > 59)
        bad_rfc3339(s, "minute is outside 00..59");

    /*
     * 60 is syntactically possible for a positive leap second.
     * See the note below when converting to system_clock.
     */
    if (se > 60)
        bad_rfc3339(s, "second is outside 00..60");

    /*
     * Unlike timegm(), chrono calendar validation does not silently
     * turn nonsense such as February 31 into a different date.
     */
    const year_month_day ymd{
        year{y},
        month{static_cast<unsigned>(mo)},
        day{static_cast<unsigned>(d)}
    };

    if (!ymd.ok())
        bad_rfc3339(s, "invalid Gregorian calendar date");


    /*
     * Fractional seconds.
     *
     * RFC 3339 says:
     *
     *     time-secfrac = "." 1*DIGIT
     *
     * so it is NOT limited to 3, 6 or 9 digits.
     *
     * Our public API returns nanoseconds, however, so precision beyond
     * nine digits cannot be represented. We therefore retain the first
     * nine digits and truncate finer precision.
     */
    std::size_t p = 19;
    unsigned long long frac_ns = 0;

    if (p < s.size() && s[p] == '.') {
        const std::size_t first = ++p;

        while (p < s.size() &&
               ascii_digit(s[p]))
        {
            ++p;
        }

        const std::size_t ndigits =
            p - first;

        if (ndigits == 0) {
            bad_rfc3339(
                s,
                "fractional second has no digits");
        }

        for (std::size_t i = 0; i < 9; ++i) {
            frac_ns *= 10;

            if (i < ndigits) {
                frac_ns +=
                    static_cast<unsigned>(
                        s[first + i] - '0');
            }
        }
    }


    /*
     * RFC 3339 offset.
     *
     * IMPORTANT:
     *
     * RFC 3339 says exactly:
     *
     *     time-numoffset =
     *         ("+" / "-") time-hour ":" time-minute
     *
     * Therefore:
     *
     *     +08:00     valid
     *     +05:30     valid
     *     +05:45     valid
     *     -03:30     valid
     *     +23:59     valid by grammar
     *
     *     +0800      NOT RFC 3339
     *     +8:00      NOT RFC 3339
     *     +08        NOT RFC 3339
     */
    int offset_minutes = 0;  // local time minus UTC

    if (p >= s.size())
        bad_rfc3339(s, "missing UTC offset");

    if (s[p] == 'Z' || s[p] == 'z') {
        ++p;
    }
    else if (s[p] == '+' || s[p] == '-') {
        const char sign = s[p];

        /*
         * Six bytes:
         *
         *     + H H : M M
         */
        if (p + 6 > s.size() ||
            s[p + 3] != ':')
        {
            bad_rfc3339(
                s,
                "UTC offset must be Z or +/-HH:MM");
        }

        const int oh =
            dec2(s, p + 1);

        const int om =
            dec2(s, p + 4);

        /*
         * RFC 3339 uses time-hour/time-minute for offsets too:
         *
         *     hour   00..23
         *     minute 00..59
         */
        if (oh > 23 || om > 59) {
            bad_rfc3339(
                s,
                "UTC offset is outside +/-23:59");
        }

        offset_minutes =
            oh * 60 + om;

        if (sign == '-')
            offset_minutes = -offset_minutes;

        p += 6;
    }
    else {
        bad_rfc3339(
            s,
            "UTC offset must be Z or +/-HH:MM");
    }

    /*
     * Reject garbage such as:
     *
     *     ...+08:00blah
     */
    if (p != s.size())
        bad_rfc3339(s, "trailing characters");


    /*
     * RFC 3339:
     *
     *     offset = local - UTC
     *
     * hence:
     *
     *     UTC = local - offset
     */
    const int ordinary_second =
        (se == 60) ? 59 : se;

    auto utc =
        sys_days{ymd}
        + hours{h}
        + minutes{mi}
        + seconds{ordinary_second}
        - minutes{offset_minutes};

    /*
     * system_clock / Unix timestamps do not have a distinct
     * representation for 23:59:60.
     *
     * Normalize a positive leap-second label onto the following
     * POSIX second.
     */
    if (se == 60)
        utc += seconds{1};

    const auto epoch_s =
        duration_cast<seconds>(
            utc.time_since_epoch()).count();


    /*
     * This API returns an unsigned nanosecond Unix timestamp.
     *
     * That representation is narrower than RFC 3339:
     * RFC 3339 permits years 0000..9999.
     *
     * Do not silently wrap negative timestamps.
     */
    if (epoch_s < 0) {
        throw std::out_of_range(
            "RFC 3339 timestamp is before the Unix epoch");
    }

    constexpr unsigned long long billion =
        1'000'000'000ULL;

    constexpr auto max =
        std::numeric_limits<
            unsigned long long>::max();

    const auto sec_u =
        static_cast<unsigned long long>(
            epoch_s);

    if (sec_u > (max - frac_ns) / billion) {
        throw std::out_of_range(
            "RFC 3339 timestamp does not fit "
            "in uint64 nanoseconds");
    }

    return (sec_u * billion + frac_ns) / 1000000000ULL;
}


std::string ccdb::utils::format_time_local(const std::chrono::system_clock::time_point tp) {
    return format_time_rfc3339_local(tp);
}


std::string ccdb::utils::getTimeNow() {
    return format_time_rfc3339_local(std::chrono::system_clock::now());
}

double ccdb::utils::median_of_sorted(const std::vector<int>& sorted, const size_t start, const size_t end)
{
    if (const size_t count = end - start + 1; /* count % 2 == 1 */ count & 0x01) { // odd
        return sorted[start + count / 2];
    } else {
        const size_t idx = start + count / 2;
        return (sorted[idx - 1] + sorted[idx]) / 2.0;
    }
}

double ccdb::utils::iqr_filtered_latency(const std::vector<std::pair<uint64_t, int>>& data, const bool use_median)
{
    if (data.empty()) {
        throw std::invalid_argument("data vector is empty");
    }

    std::vector<int> latencies;
    latencies.reserve(data.size());
    for (const auto& lat : data | std::views::values) {
        latencies.push_back(lat);
    }

    std::vector<int> sorted_lat = latencies;
    std::ranges::sort(sorted_lat);

    // Determine Q1 (25th percentile) and Q3 (75th percentile)
    // Using the inclusive median method (Tukey's hinges):
    //  - If odd size, the median is included in both halves.
    const size_t n = sorted_lat.size();
    const size_t mid = n / 2;
    double Q1, Q3;

    if (!(n & 0x01) /* n % 2 == 0 */) {
        // Even: lower half [0 .. mid-1], upper half [mid .. n-1]
        Q1 = median_of_sorted(sorted_lat, 0, mid - 1);
        Q3 = median_of_sorted(sorted_lat, mid, n - 1);
    } else {
        // Odd: both halves include the median
        Q1 = median_of_sorted(sorted_lat, 0, mid);      // mid is inclusive
        Q3 = median_of_sorted(sorted_lat, mid, n - 1);
    }

    const double IQR = Q3 - Q1;
    const double lower_bound = Q1 - 1.5 * IQR;
    const double upper_bound = Q3 + 1.5 * IQR;

    // Keep only measurements whose latency lies within [lower_bound, upper_bound]
    std::vector<int> clean_latencies;
    for (const auto& lat : data | std::views::values) {
        if (lat >= lower_bound && lat <= upper_bound) {
            clean_latencies.push_back(lat);
        }
    }

    // If all data were outliers (extremely rare but possible), fall back to original data.
    // Otherwise the cleaned vector would be empty.
    if (clean_latencies.empty()) {
        // Everything was marked as outlier – return the original median/mean.
        clean_latencies = latencies;
    }

    // Compute final summary statistic
    if (use_median) {
        std::ranges::sort(clean_latencies);
        if (const size_t sz = clean_latencies.size(); /* sz % 2 == 1 */ sz & 0x01) { // odd
            return clean_latencies[sz / 2];
        } else {
            return (clean_latencies[sz / 2 - 1] + clean_latencies[sz / 2]) / 2.0;
        }
    } else {
        const double sum = std::accumulate(clean_latencies.begin(), clean_latencies.end(), 0.0);
        return sum / static_cast<double>(clean_latencies.size());
    }
}