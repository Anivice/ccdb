// config.h
//
// Copyright 2026 Anivice Ives
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY// without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef CCDB_CONFIG_H
#define CCDB_CONFIG_H
#include <string>
#include "tsl/hopscotch_hash.h"
#include "tsl/hopscotch_map.h"

namespace ccdb {
    class configuration {
    public:
        using configuration_map_t = tsl::hopscotch_map < std::string /* Section */, tsl::hopscotch_map < std::string /* Key */, std::string > /* Values */ >;
        using iterator = configuration_map_t::iterator;
        using const_iterator = configuration_map_t::const_iterator;
        iterator begin() { return config_.begin(); }
        iterator end() { return config_.end(); }
        [[nodiscard]] const_iterator begin() const { return config_.begin(); }
        [[nodiscard]] const_iterator end() const { return config_.end(); }

    private:
        configuration_map_t config_;
        tsl::hopscotch_map < std::string, std::string > config_signal_hash_map_;

    public:
        explicit configuration(const std::string & path);
        const configuration_map_t & config = config_;
        const decltype(config_signal_hash_map_) & config_signal_hash_map = config_signal_hash_map_;
    };
} // ccdb

#endif //CCDB_CONFIG_H