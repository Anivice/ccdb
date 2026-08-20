#include <pstl/glue_execution_defs.h>

#include "utils.h"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <regex>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include "json.hpp"
#include "ncursesw/ncurses.h"
#include "tsl/hopscotch_map.h"
#include "readline/history.h"
#include "utils.h"
#include "dump.h"

#ifdef __USE_IMG__
# include "libtiv.h"
#endif

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

#ifdef ENABLE_CRASH_CATCHER
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <csignal>
#include <cstring>
#include <atomic>
#include <dlfcn.h>
#include <elf.h>
#include <cstdint>
#include <link.h>
#include "libunwind.h"

extern "C" __attribute__((visibility("default"))) void landmark() { }

namespace
{
    struct g_info_t
    {
        char name[256] { };
        uint64_t base = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    std::vector <g_info_t> g_info_table_;

    uint64_t async_strlen(const char * str) {
        uint64_t len = 0;
        while (str[len]) len++;
        return len;
    }

    int callback(struct dl_phdr_info *info, size_t size, void *data)
    {
        g_info_table_.emplace_back(g_info_t{
            .base = info->dlpi_addr,
            .offset = info->dlpi_addr - info->dlpi_phdr->p_vaddr,
            .size = info->dlpi_phdr->p_memsz
        });
        std::memcpy(&g_info_table_.back().name, info->dlpi_name, std::min((uint64_t)sizeof(g_info_t::name),
            async_strlen(info->dlpi_name)));
        return 0;
    }

    void print(uint64_t num, const int fd, const int base, const char * dictionary, const char * prefix) noexcept
    {
        char nums[256] { };
        int off = 0;
        while (num) {
            nums[off++] = dictionary[num % base];
            num /= base;
        }

        if (prefix) (void)write(fd, prefix, async_strlen(prefix));
        for (int i = off - 1; i >= 0; i--) {
            (void)write(fd, nums + i, 1);
        }
    }

    void print10(const uint64_t num, const int fd) noexcept {
        print(num, fd, 10, "0123456789", nullptr);
    }

    void print16(const uint64_t num, const int fd) noexcept {
        print(num, fd, 16, "0123456789ABCDEF", "0x");
    }

    constexpr int DUMP_SIGNAL = SIGUSR2;
    constexpr int MAX_FRAMES = 64;

    constexpr uint64_t THREAD_DUMP_TIMEOUT_NS = 100'000'000ULL; // 100 ms

    std::atomic<pid_t> dump_finished_tid { 0 };
    std::atomic_flag fatal_handler_active = ATOMIC_FLAG_INIT;

    static_assert(std::atomic<pid_t>::is_always_lock_free);

    struct linux_dirent64
    {
        uint64_t       d_ino;
        int64_t        d_off;
        unsigned short d_reclen;
        unsigned char  d_type;
        char           d_name[1];
    };

    int out_fd = STDERR_FILENO;

    [[nodiscard]]
    pid_t current_tid() noexcept
    {
        return static_cast<pid_t>(syscall(SYS_gettid));
    }

    void write_literal(const char *str, const size_t len) noexcept
    {
        (void)::write(out_fd, str, len);
    }

    void dump_context(const pid_t tid, void *const context) noexcept
    {
        constexpr char thread_header[] = "\n================ THREAD ";
        constexpr char thread_header_end[] = " ================\n";
        const auto * symbol_table = ccdb::init_crash_report.flatSymbolicTable_literal;
        const auto symbol_table_size = ccdb::init_crash_report.flatSymbolicTable_Size_literal;
        const auto landmark_in_sym = ccdb::init_crash_report.landmark_addr_in_symbol_map;
        const uint64_t offset = (uint64_t)(&landmark) - static_cast<uint64_t>(landmark_in_sym);

        write_literal(thread_header, sizeof(thread_header) - 1);
        print10(static_cast<uint64_t>(tid), out_fd);
        write_literal(thread_header_end, sizeof(thread_header_end) - 1);
        auto *const unwind_context = static_cast<unw_context_t *>(context);

        unw_cursor_t cursor {};

        if (unw_init_local2(&cursor, unwind_context, UNW_INIT_SIGNAL_FRAME) < 0)
        {
            constexpr char msg[] = "<unw_init_local2 failed>\n";
            write_literal(msg, sizeof(msg) - 1);
            return;
        }

        for (int frame = 0; frame < MAX_FRAMES; ++frame)
        {
            unw_word_t ip = 0;

            if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0)
                break;

            print16(static_cast<uint64_t>(ip), out_fd);
            bool found = false;
            bool has_sym = false;
            uint64_t presumed_offset = 0;
            if (ccdb::init_crash_report.flatObjectRuntimeTable_literal)
            {
                if (const auto * literal = ccdb::GetBacktrace(ccdb::init_crash_report.flatObjectRuntimeTable_literal,
                    ccdb::init_crash_report.flatObjectRuntimeTable_literal_size, ip);
                    literal != nullptr)
                {
                    write_literal(" #", 2);
                    write_literal(literal->name, async_strlen(literal->name));
                    presumed_offset = literal->symoff;
                    found = true;
                }
            }

            if (symbol_table)
            {
                if (const auto * literal = ccdb::GetBacktrace(symbol_table, symbol_table_size, ip - offset);
                    literal != nullptr)
                {
                    write_literal(found ? " #" : ": ", 2);
                    write_literal(literal->name, async_strlen(literal->name));
                    has_sym = true;
                }
            }

            if (found && !has_sym)
            {
                write_literal(": ", 2);
                print16(ip - presumed_offset, out_fd);
            }

            write_literal("\n", 1);

            if (const int ret = unw_step(&cursor); ret <= 0)
                break;
        }
    }

    void thread_dump_handler(int, siginfo_t *, void *context) noexcept
    {
        const pid_t tid = current_tid();
        dump_context(tid, context);
        dump_finished_tid.store(tid, std::memory_order_release);
    }

    [[nodiscard]]
    uint64_t monotonic_ns() noexcept
    {
        timespec ts {};
        if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
            return 0;
        return
            static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
            static_cast<uint64_t>(ts.tv_nsec);
    }

    [[nodiscard]]
    bool parse_tid(const char *str, pid_t &result) noexcept
    {
        uint64_t value = 0;
        if (*str == '\0') return false;

        while (*str)
        {
            const char c = *str++;
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<unsigned>(c - '0');
            if (value > 0x7fffffffULL) return false;
        }

        result = static_cast<pid_t>(value);
        return result > 0;
    }


    void dump_one_thread(const pid_t pid,const pid_t tid) noexcept
    {
        dump_finished_tid.store(0, std::memory_order_relaxed);
        if (syscall(SYS_tgkill, pid, tid, DUMP_SIGNAL) != 0) {
            return;
        }

        const uint64_t start = monotonic_ns();
        while (dump_finished_tid.load(std::memory_order_acquire) != tid)
        {
            const uint64_t now = monotonic_ns();
            if (start != 0 && now != 0 && now - start >= THREAD_DUMP_TIMEOUT_NS)
            {
                constexpr char msg[] = "<thread dump timed out: ";
                constexpr char end[] = ">\n";
                write_literal(msg, sizeof(msg) - 1);
                print10(static_cast<uint64_t>(tid), out_fd);
                write_literal(end, sizeof(end) - 1);
                break;
            }
        }
    }

    void dump_other_threads(const pid_t crashing_tid) noexcept
    {
        const auto pid = static_cast<pid_t>(syscall(SYS_getpid));
        const int fd = ::open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) return;
        alignas(8) char buffer[4096];

        while (true)
        {
            const long size = syscall(SYS_getdents64, fd, buffer, sizeof(buffer));
            if (size <= 0) break;
            long offset = 0;
            while (offset < size)
            {
                const auto *entry = reinterpret_cast<const linux_dirent64 *>(buffer + offset);
                if (entry->d_reclen == 0 || offset + entry->d_reclen > size) {
                    break;
                }

                pid_t tid = 0;

                if (parse_tid(entry->d_name, tid) &&tid != crashing_tid) {
                    dump_one_thread(pid, tid);
                }

                offset += entry->d_reclen;
            }
        }

        (void)::close(fd);
    }


    void fatal_signal_handler(const int sig, siginfo_t *, void *context) noexcept
    {
        if (fatal_handler_active.test_and_set(std::memory_order_relaxed)) {
            _exit(128 + sig);
        }

        constexpr char message_head1[] = "\033[H\033[2J\033[3J\n\n\n\n\n\n\n";
        constexpr char message_head2[] =
            " =========================================== FATAL ERROR =========================================== \n"
            "Unfortunately, CCDB had encountered an unexpected error, and there is no known ways to recover.\n"
            "Related trace file and its location will be shown after this general error message.\n"
            "\n"
            "If you don't care about the backtraces, but wish to report this error to the developer, use the command\n"
            "        `ccdb --report-issue` to report the issue. \n"
            "Be sure to attach the trace file if you wish to report.\n"
            "\n"
            "If you do care about backtraces, usually, the trace file would contain the frame addresses and,\n"
            "if a symbol table is attached to CCDB before this incident, then the symbol will be shown as well.\n"
            "If you didn't attach a symbol table, you can still feed the trace file back to ccdb using\n"
            "        `ccdb --backtrace --feedBacktrace < /path/to/trace/file`\n"
            "You can also use `addr2line` to trace the frame, but this better be done on the same build version\n"
            "of the executable of `libccdb.debug_info.so`. The said executable can be obtained on GitHub:\n"
            "        `https://github.com/Anivice/ccdb/releases/tag/ccdb.NightlyBuild." GIT_HASH "`\n\n"
            " Trace file location: ";
        if (ccdb::init_crash_report.crash_log_destination_literal)
        {
            write(STDERR_FILENO, message_head1, sizeof(message_head1) - 1); // clear
            if (ccdb::init_crash_report.additional_prefix_literal)
            {
                write(STDERR_FILENO, ccdb::init_crash_report.additional_prefix_literal,
                    ccdb::init_crash_report.additional_prefix_size);
            }
            write(STDERR_FILENO, message_head2, sizeof(message_head2) - 1);
            write(STDERR_FILENO, ccdb::init_crash_report.crash_log_destination_literal,
                ccdb::init_crash_report.crash_log_destination_literal_size);
            write(STDERR_FILENO, "\n", 1);
        }

        const pid_t crashing_tid = current_tid();
        constexpr char msg[] = "\n\n" "Unexpected signal captured.\n" "SIG NUMBER: ";
        write_literal(msg, sizeof(msg) - 1);
        print10(static_cast<uint64_t>(sig), out_fd);
        write_literal("\n", 1);
        dump_context(crashing_tid, context);
        dump_other_threads(crashing_tid);
        constexpr char landmark_msg[] = "\n================ LANDMARK ================\n" "landmark: ";
        write_literal(landmark_msg, sizeof(landmark_msg) - 1);
        print16(reinterpret_cast<uint64_t>(&landmark), out_fd);
        write_literal("\n", 1);
        close(out_fd);
        _exit(128 + sig);
    }
}

namespace ccdb
{
    namespace
    {
        bool compr(const init_crash_report_t::flatSymbolicTable_t &a,
            const init_crash_report_t::flatSymbolicTable_t &b) {
            return a.symval < b.symval;
        }

    }

    init_crash_report_t::init_crash_report_t()
    {
        {
            dl_iterate_phdr(callback, nullptr);
            flatObjectRuntimeTable.reserve(g_info_table_.size());

            for (const auto & [name, base, offset, size] : g_info_table_)
            {
                if (async_strlen(name) == 0) continue;
                flatObjectRuntimeTable.emplace_back();
                std::memcpy(&flatObjectRuntimeTable.back().name, name, async_strlen(name));
                flatObjectRuntimeTable.back().symval = base;
                flatObjectRuntimeTable.back().symoff = offset;
            }

            std::ranges::sort(flatObjectRuntimeTable, compr);
            flatObjectRuntimeTable_literal = flatObjectRuntimeTable.data();
            flatObjectRuntimeTable_literal_size = flatObjectRuntimeTable.size();
        }

        {
            struct sigaction sa {};
            sa.sa_sigaction = thread_dump_handler;
            sa.sa_flags = SA_SIGINFO | SA_RESTART;
            sigemptyset(&sa.sa_mask);
            sigaction(DUMP_SIGNAL, &sa, nullptr);
        }

        {
            struct sigaction sa {};
            sa.sa_sigaction = fatal_signal_handler;
            sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            constexpr int signals[] = {
                SIGTERM,
                SIGHUP,
                SIGQUIT,
                SIGABRT,
                SIGSEGV,
                SIGFPE
            };

            for (const int sig : signals)
                sigaction(sig, &sa, nullptr);

            {
                const auto crash_dir = utils::getenv("HOME") + "/" + ".cache/ccdb/";
                if (!std::filesystem::exists(crash_dir)) {
                    std::filesystem::create_directories(crash_dir);
                }

                const auto now = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                crash_log_destination = crash_dir + "crash_" + now + ".txt";
                crash_log_destination_literal = crash_log_destination.c_str();
                out_fd = open((const char*)crash_log_destination_literal, O_CREAT | O_TRUNC | O_WRONLY, 0600);
                if (out_fd < 0) {
                    out_fd = STDERR_FILENO;
                    crash_log_destination_literal = nullptr;
                } else {
                    crash_log_destination_literal_size = crash_log_destination.size();
#ifdef __USE_IMG__
                    if (utils::getenv("CCDB_ENABLE_EXPERIMENTAL_FEATURES") == "true")
                    {
                        init_thread = std::thread([this]
                        {
                            std::ostringstream ss;
                            show_img(ss, get_img(), 250, 250);
                            additional_prefix = ss.str();
                            additional_prefix_literal = additional_prefix.c_str();
                            additional_prefix_size = additional_prefix.size();
                        });
                    }
#endif
                }
            }
        }
    }

    init_crash_report_t::~init_crash_report_t()
    {
        if (init_thread.joinable()) init_thread.join();
        if (out_fd != STDERR_FILENO) close(out_fd);
        if (std::filesystem::exists(crash_log_destination)) {
            std::filesystem::remove(crash_log_destination);
        }
    }

    init_crash_report_t init_crash_report;

    const init_crash_report_t::flatSymbolicTable_t* GetBacktrace(const init_crash_report_t::flatSymbolicTable_t* symbolic_table,
        const uint64_t symSize, const uint64_t symbol) noexcept
    {
        if (symbolic_table == nullptr || symSize == 0)
            return nullptr;

        if (symbol < symbolic_table[0].symval || symbol > symbolic_table[symSize - 1].symval)
            return nullptr;

        uint64_t lo = 0;
        uint64_t hi = symSize; // [lo, hi)

        while (lo < hi)
        {
            const uint64_t mid = lo + ((hi - lo) >> 1);

            if (symbolic_table[mid].symval <= symbol)
                lo = mid + 1;
            else
                hi = mid;
        }

        // First element > symbol is lo,
        // therefore lo - 1 is the greatest element <= symbol.
        return &symbolic_table[lo - 1];
    }
}

#endif //ENABLE_CRASH_CATCHER
