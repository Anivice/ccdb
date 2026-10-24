#include "windows_platform.h"
#include <psapi.h>
#include <clocale>
#include <fstream>
#include "Readline.h"
#include "utils.h"
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <system_error>

extern "C" int config_console(void);
extern "C" void unconfig_console(void);

namespace {
    std::atomic_bool interrupted{false};
    BOOL WINAPI console_control(DWORD event) {
        if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
        interrupted.store(true);
        return TRUE;
    }
    int readline_interrupt(int, int) {
        rl_replace_line("", 0);
        rl_crlf();
        rl_done = 1;
        return 0;
    }
    std::string encode_key(const KEY_EVENT_RECORD& key) {
        if (!key.bKeyDown) return {};
        const bool ctrl = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        if (key.uChar.UnicodeChar) {
            const wchar_t c = key.uChar.UnicodeChar;
            if (c == 3) { interrupted = true; return {}; }
            if (c == '\r') return "\n";
            if (c == '\b') return "\x7f";
            // Supplementary Unicode code points arrive as surrogate pairs.
            static wchar_t high = 0;
            wchar_t pair[2] = {c, 0};
            int count = 1;
            if (c >= 0xD800 && c <= 0xDBFF) { high = c; return {}; }
            if (c >= 0xDC00 && c <= 0xDFFF && high) {
                pair[0] = high; pair[1] = c; count = 2;
            }
            high = 0;
            char bytes[8];
            int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pair, count, bytes, 8, nullptr, nullptr);
            return n > 0 ? std::string(bytes, n) : std::string{};
        }
        switch (key.wVirtualKeyCode) {
        case VK_UP: return ctrl ? "\x1b[1;5A" : "\x1b[A";
        case VK_DOWN: return ctrl ? "\x1b[1;5B" : "\x1b[B";
        case VK_RIGHT: return "\x1b[C";
        case VK_LEFT: return "\x1b[D";
        case VK_HOME: return "\x1b[H";
        case VK_END: return "\x1b[F";
        case VK_PRIOR: return "\x1b[5~";
        case VK_NEXT: return "\x1b[6~";
        case VK_DELETE: return "\x1b[3~";
        case VK_F1: return "\x1bOP";
        case VK_F2: return "\x1bOQ";
        case VK_F3: return "\x1bOR";
        case VK_F4: return "\x1bOS";
        case VK_F5: return "\x1b[15~";
        case VK_F6: return "\x1b[17~";
        case VK_F7: return "\x1b[18~";
        case VK_F8: return "\x1b[19~";
        case VK_F9: return "\x1b[20~";
        case VK_F10: return "\x1b[21~";
        case VK_F11: return "\x1b[23~";
        case VK_F12: return "\x1b[24~";
        default: return {};
        }
    }
}

namespace ccdb::windows {
    console_session::console_session() {
        input_cp_ = GetConsoleCP(); output_cp_ = GetConsoleOutputCP();
        input_valid_ = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode_) != 0;
        output_valid_ = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode_) != 0;
        if (output_valid_) SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        SetConsoleCP(CP_UTF8); SetConsoleOutputCP(CP_UTF8);
        std::setlocale(LC_ALL, ".UTF8");
        SetConsoleCtrlHandler(console_control, TRUE);
    }
    console_session::~console_session() {
        SetConsoleCtrlHandler(console_control, FALSE);
        if (input_valid_) SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode_);
        if (output_valid_) SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode_);
        if (input_cp_) SetConsoleCP(input_cp_);
        if (output_cp_) SetConsoleOutputCP(output_cp_);
    }
    bool interactive() {
        DWORD mode;
        return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)
            && GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
    }
    void notify_interrupt() { interrupted = true; }
    bool take_interrupt() { return interrupted.exchange(false); }

    ssize_t read_input(void* buffer, size_t count, int timeout_ms) {
        thread_local std::string pending;
        if (!count) return 0;
        const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(timeout_ms, 0));
        auto handle = GetStdHandle(STD_INPUT_HANDLE);
        while (pending.empty()) {
            const DWORD wait = timeout_ms < 0 ? INFINITE :
                static_cast<DWORD>(deadline > GetTickCount64() ? deadline - GetTickCount64() : 0);
            if (WaitForSingleObject(handle, wait) != WAIT_OBJECT_0) { errno = ETIMEDOUT; return -1; }
            INPUT_RECORD input{}; DWORD got = 0;
            if (!ReadConsoleInputW(handle, &input, 1, &got) || !got) return -1;
            if (input.EventType == KEY_EVENT) {
                auto text = encode_key(input.Event.KeyEvent);
                for (unsigned i = 0; i < input.Event.KeyEvent.wRepeatCount; ++i) pending += text;
            }
            else if (input.EventType == MOUSE_EVENT) {
                auto& m = input.Event.MouseEvent;
                int button = -1;
                char end = 'M';
                if (m.dwEventFlags == MOUSE_WHEELED)
                    button = static_cast<SHORT>(HIWORD(m.dwButtonState)) > 0 ? 64 : 65;
                else if (m.dwEventFlags == 0) {
                    if (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) button = 0;
                    else if (m.dwButtonState & RIGHTMOST_BUTTON_PRESSED) button = 2;
                    else { button = 0; end = 'm'; }
                }
                if (button >= 0) pending = "\x1b[<" + std::to_string(button) + ";" +
                    std::to_string(m.dwMousePosition.X + 1) + ";" +
                    std::to_string(m.dwMousePosition.Y + 1) + end;
            }
            if (pending.empty() && timeout_ms >= 0 && GetTickCount64() >= deadline) {
                errno = ETIMEDOUT; return -1;
            }
        }
        const size_t n = std::min(count, pending.size());
        std::memcpy(buffer, pending.data(), n);
        pending.erase(0, n);
        return static_cast<ssize_t>(n);
    }

    void read_commands(const std::string& prompt) {
        using_history();
        Readline::g_running = 1;
        if (Readline::history_file) {
            std::istringstream history(Readline::blocked_read_file(Readline::history_file));
            for (std::string line; std::getline(history, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) { add_history(line.c_str()); Readline::last_line = line; }
            }
        }
        rl_catch_signals = 0;
        rl_catch_sigwinch = 0;
        rl_attempted_completion_function = Readline::cmd_completion;
        rl_completion_display_matches_hook = Readline::colored_display_hook;
        rl_initialize();
        rl_bind_key(3, readline_interrupt);
        // Readline owns input only while editing. Restore its modes before a shared
        // live view starts its own input worker; the command parser stays unchanged.
        while (Readline::g_running) {
            char* line;
            if (interactive()) {
                struct modes { modes() { config_console(); } ~modes() { unconfig_console(); } } restore;
                line = readline(prompt.c_str());
            }
            else {
                std::string text;
                if (!std::getline(std::cin, text)) break;
                line = _strdup(text.c_str());
            }
            Readline::on_line(line);
        }
    }

    void terminal_write(std::string_view data)
    {
        const HANDLE handle =
            GetStdHandle(STD_OUTPUT_HANDLE);

        if (!handle || handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Invalid STDOUT handle");

        while (!data.empty())
        {
            const DWORD requested =
                static_cast<DWORD>(
                    std::min<std::size_t>(data.size(), 32768)
                    );

            DWORD written = 0;

            if (!WriteFile(
                handle,
                data.data(),
                requested,
                &written,
                nullptr))
            {
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "WriteFile failed"
                );
            }

            if (written == 0)
                throw std::runtime_error(
                    "WriteFile made no progress"
                );

            data.remove_prefix(written);
        }
    }

    void reset_terminal() {
        if (!interactive()) return;
        ::ccdb::windows::terminal_write("\x1b[0m\x1b[?25h\x1b[?1000l\x1b[?1006l\x1b[?1049l");
    }

}

namespace ccdb::utils {
    std::string get_text(const std::string& text) { return text; }
    bool is_less_available() { return false; }
    ssize_t cur_mem_size() {
        PROCESS_MEMORY_COUNTERS info{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) return -1;
        return static_cast<ssize_t>(info.WorkingSetSize);
    }
    std::pair<const int, const int> get_screen_row_col() noexcept {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        // Redirected output has no console buffer; use a sane page so tables and
        // reports remain readable instead of collapsing to "TOO SMALL".
        if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return { 40, 120 };
        const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        const int cols = info.srWindow.Right - info.srWindow.Left + 1;
        if (rows <= 0 || cols <= 0) return { 40, 120 };
        return { rows, cols };
    }
    const char* capstr(const char* name) {
        if (!std::strcmp(name, "smcup")) return "\x1b[?1049h";
        if (!std::strcmp(name, "rmcup")) return "\x1b[?1049l";
        if (!std::strcmp(name, "clear")) return "\x1b[H\x1b[2J";
        if (!std::strcmp(name, "civis")) return "\x1b[?25l";
        if (!std::strcmp(name, "cnorm")) return "\x1b[?25h";
        if (!std::strcmp(name, "ed")) return "\x1b[J";
        return "";
    }
    void put_cap(const char* cap) { if (cap && windows::interactive()) ::ccdb::windows::terminal_write(cap); }
    setup_term::setup_term() {
        if (!windows::interactive()) return;
        set_conio_terminal_mode();
        put_cap(smcup); put_cap(civis); put_cap(clear);
    }
    setup_term::~setup_term() {
        if (terminal_mode_changed) {
            put_cap(cnorm); put_cap(rmcup); reset_terminal_mode();
        }
    }
    void setup_term::set_conio_terminal_mode() {
        if (terminal_mode_changed || !windows::interactive()) return;
        GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &old_input_mode);
        GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &old_output_mode);
        DWORD mode = (old_input_mode | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT)
            & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT);
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        terminal_mode_changed = true;
    }
    void setup_term::reset_terminal_mode() {
        if (!terminal_mode_changed) return;
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), old_input_mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_output_mode);
        terminal_mode_changed = false;
    }
    void setup_term::move_home() const { put_cap("\x1b[H"); }
    void setup_term::ed_clear() const { if (terminal_mode_changed) put_cap(ed); }
}
