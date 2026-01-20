// execute.cpp
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

#include <unistd.h>
#include <cstring>
#include <sstream>
#include <sys/wait.h>
#include "utils.h"

inline std::string get_errno_message(const std::string &prefix = "") {
    return prefix + std::strerror(errno);
}

ccdb::utils::cmd_status ccdb::utils::exec_command_(
    const std::string &cmd,
    const std::vector<std::string> &args,
    const std::string &input)
{
    cmd_status status = {"", "", 1};

    int stdin_pipe[2];
    if (pipe(stdin_pipe) == -1) {
        status.fd_stderr += get_errno_message("pipe() failed: ");
        return status;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        status.fd_stderr += get_errno_message("fork() failed: ");
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return status;
    }

    if (pid == 0) {
        // Child process.
        if (dup2(stdin_pipe[0], STDIN_FILENO) == -1) {
            perror("dup2(stdin)");
            _exit(EXIT_FAILURE);
        }
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(cmd.c_str()));
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(cmd.c_str(), argv.data());
        perror("execv");
        _exit(EXIT_FAILURE);
    }

    // Parent process.
    close(stdin_pipe[0]);
    std::string to_write = input;
    if (to_write.empty() || to_write.back() != '\n') {
        to_write.push_back('\n');
    }
    const char *buf = to_write.c_str();
    const auto bytes_to_write = static_cast<ssize_t>(to_write.size());
    ssize_t total_written = 0;
    while (total_written < bytes_to_write)
    {
        const ssize_t written = write(stdin_pipe[1], buf + total_written,
                                bytes_to_write - total_written);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            status.fd_stderr += get_errno_message("write() to child stdin failed: ");
            break;
        }
        total_written += written;
    }
    close(stdin_pipe[1]);

    int wstatus;
    if (waitpid(pid, &wstatus, 0) == -1) {
        status.fd_stderr += get_errno_message("waitpid() failed: ");
        return status;
    }
    if (WIFEXITED(wstatus)) {
        status.exit_status = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        std::ostringstream oss;
        oss << "Child terminated by signal " << WTERMSIG(wstatus) << "\n";
        status.fd_stderr += oss.str();
        status.exit_status = 1;
    } else {
        status.fd_stderr += "Child process ended abnormally.\n";
        status.exit_status = 1;
    }

    return status;
}
