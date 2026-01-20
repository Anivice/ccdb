// update-self.cpp
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

#include "update-self.h"
#include "utils.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <sys/wait.h>
#include <sstream>

#define assert_in_update(condition) if (!(condition)) { throw std::runtime_error("Assertion of " #condition " failed"); }

void update_self(const std::string & executable_path)
{
#ifdef __ENABLE_EXPERIMENTAL__
    assert_in_update(access(executable_path.c_str(), W_OK) == 0);
    const int pid = fork();
    if (pid == 0)
    { // child
        const int double_fork = fork();
        if (double_fork == 0)
        { // grandchild
            std::cout << "Update in progress..." << std::endl;
            std::string command =
#ifdef _CCDB_CYGWIN_BUILD_
                R"(curl -fsSL "https://raw.githubusercontent.com/Anivice/ccdb/refs/heads/main/src/script/update_cygwin.sh" | bash -s -- )"
#else
                R"(curl -fsSL "https://raw.githubusercontent.com/Anivice/ccdb/refs/heads/main/src/script/update.sh" | bash -s -- )"
#endif
                + executable_path;
            std::vector<std::string> args;
            args.emplace_back("/bin/sh");
            args.emplace_back("-c");
            args.emplace_back(command);

            auto argv = std::make_unique<char*[]>(args.size() + 1);
            for (long i = 0; i < args.size(); ++i) {
                argv[i] = const_cast<char*>(args[i].c_str());
            }
            argv[args.size()] = nullptr; // null terminated
            char ** cargv = argv.get();
            std::cout << "Update using the command " << command << std::endl;
            if (execv("/bin/sh", cargv)  == -1) {
                std::cerr << "Update failed!" << std::endl;
                exit(1);
            }

            exit(0); // should never reach here
        }

        if (double_fork == -1) {
            std::cerr << "Update failed!" << std::endl;
            exit(1);
        }

        exit(0);
        // child exit
    }

    assert_in_update(pid != -1);
    if (kill(pid, 0) == 0) waitpid(pid, nullptr, 0);
    exit(0);
#endif // __ENABLE_EXPERIMENTAL__
}
