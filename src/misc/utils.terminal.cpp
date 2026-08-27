#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <regex>
#include <iostream>
#include <termios.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include <stdexcept>
#include <poll.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include "json.hpp"
#include "ncursesw/ncurses.h"
#include "ncursesw/term.h"
#include "terminfotar.h"
#include "print.h"
#include "tsl/hopscotch_map.h"
#include "tar.h"
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

#ifdef __USE_IMG__

#include "libtiv.h"

void ccdb::utils::printImg()
{
    std::ios::sync_with_stdio(false);
    show(std::cout);
    std::ios::sync_with_stdio(true);
}

#endif //__USE_IMG__

namespace
{
    int execute_within_page(char** argv, const std::string & to_write,
                            const std::string & dest, const unsigned int len,
                            unsigned char data[], std::string & stdout_str,
                            std::string & stderr_str)
    {
        // stdin pipe
        int stdin_pipe[2];
        if (pipe(stdin_pipe) == -1) {
            throw std::runtime_error("Cannot execute: " + std::string(std::strerror(errno)));
        }

        // stdout and stderr pipes
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
            throw std::runtime_error("Cannot create pipes: " + std::string(std::strerror(errno)));
        }

        const pid_t pid = fork();
        if (pid < 0) {
            throw std::runtime_error("Cannot execute: " + std::string(std::strerror(errno)));
        }

        if (pid == 0) { // child
            // Redirect stdin
            if (dup2(stdin_pipe[0], STDIN_FILENO) == -1) {
                perror("dup2(stdin)");
                _exit(EXIT_FAILURE);
            }
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);

            // Redirect stdout and stderr
            if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) {
                perror("dup2(stdout)");
                _exit(EXIT_FAILURE);
            }
            if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
                perror("dup2(stderr)");
                _exit(EXIT_FAILURE);
            }
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);

            // Memory file creation
            const int fd = static_cast<int>(syscall(__NR_memfd_create, "mem_elf", 0));
            if (fd == -1) {
                perror("syscall");
                _exit(EXIT_FAILURE);
            }

            std::vector<uint8_t> out;
            const std::vector<uint8_t> in{data, data + len};
            out = ccdb::utils::decompress(in);

            if (const ssize_t written = write(fd, out.data(), out.size());
                written != static_cast<ssize_t>(out.size()))
            {
                perror("write");
                close(fd);
                _exit(EXIT_FAILURE);
            }

            if (fchmod(fd, 0700) != 0) {
                perror("fchmod");
                close(fd);
                _exit(EXIT_FAILURE);
            }

            // Try fexecve first
            fexecve(fd, argv, environ);
            perror("fexecve");

            // Fallback: execve via /proc/self/fd
            errno = 0;
            char path[64] = {};
            snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
            execve(path, argv, environ);
            perror("execve");

            // Fallback: write to dest and execv
            errno = 0;
            const int wffd = open(dest.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0700);
            if (wffd == -1) {
                perror("open");
                _exit(EXIT_FAILURE);
            }

            if (write(wffd, out.data(), out.size()) != static_cast<ssize_t>(out.size())) {
                perror("write");
                close(wffd);
                _exit(EXIT_FAILURE);
            }

            close(wffd);

            execv(dest.c_str(), argv);
            perror("execv");

            close(fd);
            _exit(EXIT_FAILURE);
        }

        // Parent process
        // Close unused pipe ends
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Write stdin data to child
        const char *buf = to_write.c_str();
        const auto bytes_to_write = static_cast<ssize_t>(to_write.size());
        ssize_t total_written = 0;
        while (total_written < bytes_to_write)
        {
            const ssize_t written = write(stdin_pipe[1], buf + total_written, bytes_to_write - total_written);
            if (written == -1)
            {
                if (errno == EINTR) continue;
                throw std::runtime_error("Cannot execute: " + std::string(std::strerror(errno)));
            }

            total_written += written;
        }
        close(stdin_pipe[1]);

        // Poll and read stdout/stderr while child runs
        pollfd fds[2] { };
        fds[0].fd = stdout_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = stderr_pipe[0];
        fds[1].events = POLLIN;

        int exit_status = EXIT_FAILURE;
        bool child_exited = false;

        while (true)
        {
            if (const int ret = poll(fds, 2, -1); ret == -1) {
                if (errno == EINTR) continue;
                // On error, break and clean up
                break;
            }

            for (int i = 0; i < 2; ++i)
            {
                if (fds[i].fd == -1) continue;
                if (fds[i].revents & POLLIN) {
                    char buffer[4096];
                    ssize_t n = read(fds[i].fd, buffer, sizeof(buffer));
                    if (n > 0) {
                        if (i == 0) stdout_str.append(buffer, n);
                        else stderr_str.append(buffer, n);
                    } else {
                        // read error
                        close(fds[i].fd);
                        fds[i].fd = -1;
                    }
                }
                if (fds[i].revents & (POLLERR | POLLHUP)) {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                }
            }

            // Check if child has terminated (non‑blocking)
            if (!child_exited)
            {
                int wstatus;
                pid_t wait_result = waitpid(pid, &wstatus, WNOHANG);
                if (wait_result == pid) {
                    child_exited = true;
                    if (WIFEXITED(wstatus)) {
                        exit_status = WEXITSTATUS(wstatus);
                    } else if (WIFSIGNALED(wstatus)) {
                        exit_status = WTERMSIG(wstatus);
                    }
                    // Continue reading any remaining data from pipes
                } else if (wait_result == -1) {
                    // waitpid error
                    break;
                }
            }

            // Break when both pipes are closed and child has exited
            if (fds[0].fd == -1 && fds[1].fd == -1) {
                break;
            }
            // If child exited but pipes still have data, continue polling
            // If child exited and both pipes are closed, break will happen above.
        }

        // Close any remaining pipe fds (should be already closed)
        if (fds[0].fd != -1) close(fds[0].fd);
        if (fds[1].fd != -1) close(fds[1].fd);

        // If child hasn't been reaped yet (e.g., poll error path), wait for it
        if (!child_exited) {
            int wstatus;
            if (waitpid(pid, &wstatus, 0) == pid) {
                if (WIFEXITED(wstatus)) {
                    exit_status = WEXITSTATUS(wstatus);
                } else if (WIFSIGNALED(wstatus)) {
                    exit_status = WTERMSIG(wstatus);
                }
            }
        }

        return exit_status;
    }

    char** to_char_pp(const std::vector<std::string>& args, std::vector<const char*> & cstr_ptrs)
    {
        cstr_ptrs.clear();
        cstr_ptrs.reserve(args.size() + 1);
        for (const auto& s : args) {
            cstr_ptrs.push_back(s.c_str());
        }
        cstr_ptrs.push_back(nullptr);

        return const_cast<char**>(cstr_ptrs.data());
    }
}

ccdb::utils::cmd_status ccdb::utils::tar(const std::vector<std::string> & args, const std::string & to_write)
{
    std::vector<const char*> cstr_ptrs;
    const std::string tar_exec = ccdb::utils::getenv("HOME") + "/.cache/tar";
    class auto_remove {
    public:
        std::string name_;
        explicit auto_remove(std::string  name) : name_(std::move(name)) {
            if (std::filesystem::exists(name_)) {
                std::filesystem::remove_all(name_);
            }
        }

        ~auto_remove() {
            if (std::filesystem::exists(name_)) {
                std::filesystem::remove_all(name_);
            }
        }
    } auto_remove_(tar_exec);

    std::string s_stdout, s_stderr;
    const int result = execute_within_page(to_char_pp(args, cstr_ptrs), to_write,
        tar_exec, tar_exe_len, tar_exe, s_stdout, s_stderr);
    return cmd_status {
        .fd_stdout = s_stdout,
        .fd_stderr = s_stderr,
        .exit_status = result
    };
}

#ifndef __attribute_used__
# if __has_attribute (__used__)
#   define __attribute_used__ __attribute__ ((__used__))
#   define __attribute_noinline__ __attribute__ ((__noinline__))
# else
#   define __attribute_used__ __attribute__ ((__unused__))
#   define __attribute_noinline__ /* Ignore */
# endif
#endif //__attribute_used__
namespace
{
    std::atomic_bool term_inited = false;
    __attribute_used__
    class init_term_t
    {
    public:
        init_term_t()
        {
            try
            {
                // setup terminfo
                const auto cache = ccdb::utils::getenv("HOME") + "/.cache";
                if (!std::filesystem::exists(cache)) {
                    std::filesystem::create_directories(cache);
                }

                if (ccdb::utils::getenv("TERMINFO").empty())
                {
                    const auto target = cache + "/terminfo";
                    if (!std::filesystem::exists(target))
                    {
                        std::filesystem::create_directories(target);
                        std::vector<uint8_t> compressed_terminfo(terminfotar_len);
                        std::memcpy(compressed_terminfo.data(), terminfotar, terminfotar_len);
                        const auto decompressed_terminfo = ccdb::utils::decompress(compressed_terminfo);
                        std::string decompressed_terminfo_string;
                        decompressed_terminfo_string.resize(decompressed_terminfo.size());
                        std::memcpy(decompressed_terminfo_string.data(), decompressed_terminfo.data(), decompressed_terminfo.size());
                        const std::vector<std::string> argv = { "/proc/self/exe", "-x", "--directory=" + cache };
                        const auto [fd_stdout, fd_stderr, status] = ccdb::utils::tar(argv, decompressed_terminfo_string);
                        if (status != 0) {
                            throw std::runtime_error("Failed to uncompress terminfo");
                        }
                    }

                    setenv("TERMINFO", target.c_str(), 1);
                    int err = 0;
                    if (setupterm(nullptr, fileno(stdout), &err) != OK || err <= 0) {
                        ccdb::utils::print<ccdb::utils::is_error>("setupterm failed: ", err, ", ", std::strerror(errno), "\n");
                        return;
                    }
                }
            } catch (std::exception&) { }
            term_inited = true;
        }
    } init_term;
}

ccdb::utils::setup_term::setup_term()
{
    if (!term_inited) {
        return;
    }

    set_conio_terminal_mode();
    put_cap(smcup);
    put_cap(civis);
    put_cap(clear);
    terminal_mode_changed = true;
}

ccdb::utils::setup_term::~setup_term()
{
    if (terminal_mode_changed) {
        // restore
        put_cap(cnorm);
        put_cap(rmcup);
        reset_terminal_mode();
    }
    std::fflush(stdout);
}

void ccdb::utils::setup_term::ed_clear() const
{
    if (terminal_mode_changed && ed) {
        put_cap(ed);
    }
}

void ccdb::utils::setup_term::reset_terminal_mode()
{
    if (terminal_mode_changed) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        fcntl(STDIN_FILENO, F_SETFL, old_flags);
        terminal_mode_changed = false;

        // disable mouse tracking
        const auto * off = "\x1b[?1006l\x1b[?1000l";
        std::cout.write(off, static_cast<std::streamsize>(std::char_traits<char>::length(off)));
        std::cout.flush();
    }
}

void ccdb::utils::setup_term::set_conio_terminal_mode()
{
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    terminal_mode_changed = true;

    // enable mouse tracking + SGR mode
    constexpr auto on = "\x1b[?1000h\x1b[?1006h";
    std::cout.write(on, static_cast<std::streamsize>(std::char_traits<char>::length(on)));
    std::cout.flush();
}

void ccdb::utils::put_cap(const char* cap)
{
    if (!cap || cap == reinterpret_cast<char *>(-1)) return;
    putp(cap);
}

const char* ccdb::utils::capstr(const char* name)
{
    const char* s = tigetstr(name);
    if (s == reinterpret_cast<char *>(-1) || s == nullptr) return nullptr;
    return s;
}

void ccdb::utils::setup_term::move_home() const
{
    if (terminal_mode_changed) {
        const char* cup = capstr("cup"); // cursor position
        if (!cup) return;
        if (const char* seq = tparm(const_cast<char*>(cup), 0, 0)) put_cap(seq);
    } else {
        std::cout << clear_ << std::flush;
    }
}
