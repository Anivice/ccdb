// config.cpp
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

#include "config.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include "utils.h"

namespace ccdb {
    std::string clean_line(const std::string& line)
    {
        return line.substr(0, line.find_first_of('#'));
    }

    std::string get_section(const std::string& line)
    {
        const std::regex pattern(R"(\s*\[([^\]]+)\]\s*)");
        if (std::smatch matches; std::regex_match(line, matches, pattern) && matches.size() > 1)
        {
            return matches[1];
        }

        return "";
    }

    std::pair <std::string, std::string> get_pair(const std::string& line)
    {
        std::pair <std::string, std::string> pair;
        const std::regex pattern(R"(\s*([^=]+)\s*=\s*(.*)\s*)");
        if (std::smatch matches; std::regex_match(line, matches, pattern) && matches.size() > 2)
        {
            pair.first = matches[1];
            pair.second = matches[2];
        }

        pair.first = pair.first.substr(0, std::min(pair.first.find_last_not_of(' ') + 1, pair.first.size())); // remove tailing spaces
        pair.second = pair.second.substr(0, std::min(pair.second.find_last_not_of(' ') + 1, pair.second.size()));
        return pair;
    }

    std::string process_value(std::string value)
    {
        return utils::regex_replace_all(value, R"((\%([\w]+)\%))",
            [](const std::smatch& match)->std::string
            {
                if (match.size() == 3) {
                    const std::string result = match[2];
                    return utils::getenv(result);
                }

                return {};
            }
        );
    }

    configuration::configuration(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) {
            throw std::invalid_argument("Cannot open config file " + path);
        }

        std::string line;
        std::string section;
        int line_num = 0;
        while (std::getline(file, line))
        {
            line_num++;
            std::string section_tmp = get_section(clean_line(line));
            const auto [key, value] = get_pair(clean_line(line));
            if (!section_tmp.empty())
            {
                section = section_tmp;
            }
            else
            {
                if (!key.empty())
                {
                    if (section.empty()) {
                        throw std::invalid_argument("Line: " + std::to_string(line_num) + ": section head is empty");
                    }
                    config_[section][key] = process_value(value);
                }
            }
        }

        for (const auto & [ Section, Pairs] : config_)
        {
            for (const auto & [ Key, Value ] : Pairs) {
                config_signal_hash_map_.emplace(Section + "::" + Key, Value);
            }
        }
    }
} // ccdb