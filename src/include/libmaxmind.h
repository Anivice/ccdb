#ifndef CCDB_LIBMAXMIND_H
#define CCDB_LIBMAXMIND_H

#include <optional>
#include <string>
#include <cstring>
#include "maxminddb.h"

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
    public:
        explicit maxmindDB(const std::string &);
        void open(const std::string &);
        ~maxmindDB();

        template <StringArgs... T>
        [[nodiscard]] std::optional<std::string> find(const std::string & ip, const T & ... modes) const {
            MMDB_entry_data_s entry_data;
            int gai_error = -1, mmdb_error = -1;
            const std::array<std::string, sizeof...(T)> strings{ std::string(modes)... };

            auto result = MMDB_lookup_string(&mmdb_, ip.c_str(), &gai_error, &mmdb_error);
            if (gai_error != 0 || mmdb_error != MMDB_SUCCESS)
            {
                return std::nullopt;
            }

            if (!result.found_entry) {
                return std::nullopt;
            }

            auto call_mmdb = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
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
                return std::nullopt;
            }

            if (entry_data.has_data)
            {
                std::string str;
                str.resize(entry_data.data_size);
                std::memcpy(str.data(), entry_data.utf8_string, entry_data.data_size);
                return str;
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

#endif //CCDB_LIBMAXMIND_H
