#ifndef CCDB_LIBMAXMIND_H
#define CCDB_LIBMAXMIND_H

#ifdef __YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__

#include <optional>
#include <string>
#include <cstring>
#include "maxminddb.h"
#include "utils.h"

namespace ccdb
{
    template<typename T>
    concept StringArgType = std::constructible_from<std::string, T>;

    template<typename... T>
    concept StringArgs = (StringArgType<T> && ...);

    class maxmindDB {
    private:
        MMDB_s mmdb_ { };
        bool opened_ { false };
        const bool MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT = utils::getenv("CCDB_MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT") == "true";
        utils::cache_w_freq_table_t<std::string, std::string> online_search_cache;
        std::string online_geoip_search(const std::string & ip);

    public:
        explicit maxmindDB(const std::string &);
        void open(const std::string &);
        ~maxmindDB();

        template <StringArgs... T>
        [[nodiscard]] std::optional<std::string> find(const std::string & ip, const T & ... modes)
        {
            if (ip.empty()) return { };
            MMDB_entry_data_s entry_data;
            int gai_error = -1, mmdb_error = -1;
            const std::array<std::string, sizeof...(T)> strings{ std::string(modes)... };
            std::string online_result;

            auto result = MMDB_lookup_string(&mmdb_, ip.c_str(), &gai_error, &mmdb_error);
            if (gai_error != 0 || mmdb_error != MMDB_SUCCESS)
            {
                if (MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT) {
                    if (online_result.empty()) online_result = online_geoip_search(ip);
                    if (!online_result.empty()) return online_result;
                }
                return std::nullopt;
            }

            if (!result.found_entry)
            {
                if (MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT) {
                    if (online_result.empty()) online_result = online_geoip_search(ip);
                    if (!online_result.empty()) return online_result;
                }
                return std::nullopt;
            }

            auto call_mmdb = [&]<std::size_t... Is>(std::index_sequence<Is...>)
            {
                return MMDB_get_value(
                    &result.entry,
                    &entry_data,
                    strings[Is].c_str()...,
                    nullptr
                );
            };

            if (const auto status = call_mmdb(std::index_sequence_for<T...>{});
                status != MMDB_SUCCESS)
            {
                if (MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT) {
                    if (online_result.empty()) online_result = online_geoip_search(ip);
                    if (!online_result.empty()) return online_result;
                }
                return std::nullopt;
            }

            if (entry_data.has_data)
            {
                std::string str;
                str.resize(entry_data.data_size);
                std::memcpy(str.data(), entry_data.utf8_string, entry_data.data_size);
                return str;
            }

            if (MAXMIND_DB_USE_USE_ONLINE_COMPLIMENT) {
                if (online_result.empty()) online_result = online_geoip_search(ip);
                if (!online_result.empty()) return online_result;
            }
            return std::nullopt;
        }

        maxmindDB() = default;
        maxmindDB(const maxmindDB&) = delete;
        maxmindDB& operator=(const maxmindDB&) = delete;
        maxmindDB(maxmindDB&&) = delete;
        maxmindDB& operator=(maxmindDB&&) = delete;

    };
}

#endif //__YES_ENABLE_THE_CCDB_FUCK_AROUND_FEATURES__

#endif //CCDB_LIBMAXMIND_H
