#pragma once
#ifdef __WINDOWS_MSVC_BUILD__
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <cstdint>
#include <cstddef>
#include <csignal>
#include <string>
// Win32 defines ERROR as a macro; the shared command code uses it as an enum.
#ifdef ERROR
#undef ERROR
#endif

using ssize_t = std::intptr_t;
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
// Internal resize notification identifier; not installed as a CRT signal.
inline constexpr int SIGWINCH = 28;
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
namespace ccdb::windows {
    // Console session is owned by main; live views save/restore their own modes.
    class console_session {
        DWORD input_mode_ = 0, output_mode_ = 0;
        UINT input_cp_ = 0, output_cp_ = 0;
        bool input_valid_ = false, output_valid_ = false;
    public:
        console_session();
        ~console_session();
    };
    bool interactive();
    ssize_t read_input(void* buffer, size_t count, int timeout_ms);
    void notify_interrupt();
    bool take_interrupt();
    void read_commands(const std::string& prompt);
    void reset_terminal();
}
#endif //__WINDOWS_MSVC_BUILD__
