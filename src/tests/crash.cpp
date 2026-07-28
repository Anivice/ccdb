// File: crash.c
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <libunwind.h>
#include <cstring>
static int pipe_fds[2];

// Worker thread: read addresses from pipe and resolve them
static void *worker_thread(void *arg)
{
    (void)arg;
    while (true)
    {
        int count = 0;
        // Read count of addresses (blocking)
        if (read(pipe_fds[0], &count, sizeof(count)) <= 0)
            break;
        void *addrs[count];
        read(pipe_fds[0], addrs, count * sizeof(void*));

        fprintf(stderr, "\n=== Worker: Stack trace ===\n");
        for (int i = 0; i < count; i++)
        {
            Dl_info info;
            char buf[512];
            if (dladdr(addrs[i], &info) && info.dli_sname) {
                int status = 0;
                char *dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                const char *func = (status == 0 && dem) ? dem : info.dli_sname;
                // Calculate offset from symbol base
                unsigned long offset = (unsigned long)addrs[i] - (unsigned long)info.dli_saddr;
                snprintf(buf, sizeof(buf), "%s+0x%lx (%s:%s)", func, offset,
                         info.dli_fname ? info.dli_fname : "?", "?");
                write(STDERR_FILENO, buf, strlen(buf));
                write(STDERR_FILENO, "\n", 1);
                free(dem);
            } else {
                // Fallback: just print address
                snprintf(buf, sizeof(buf), "%p\n", addrs[i]);
                write(STDERR_FILENO, buf, strlen(buf));
            }
        }
    }
    return nullptr;
}

// Signal handler: capture addresses via libunwind and write to pipe
static void signal_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si; (void)uc;
    unw_context_t ctx;
    unw_cursor_t cursor;

    unw_getcontext(&ctx);
    unw_init_local(&cursor, &ctx);

    void *trace[64];
    int idx = 0;
    while (unw_step(&cursor) > 0 && idx < 64) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        trace[idx++] = (void *)ip;
    }

    // Write count and addresses to pipe (async-signal-safe writes)
    write(pipe_fds[1], &idx, sizeof(idx));
    write(pipe_fds[1], trace, idx * sizeof(void*));

    // _exit(128 + sig);
}

int main() {
    // Create a pipe for handler->worker communication
    if (pipe(pipe_fds) != 0) { perror("pipe"); exit(1); }

    // Start worker thread
    pthread_t thr;
    if (pthread_create(&thr, nullptr, worker_thread, nullptr) != 0) {
        perror("pthread_create"); exit(1);
    }

    // Alternate signal stack
    stack_t sigstk;
    sigstk.ss_sp = malloc(SIGSTKSZ);
    if (!sigstk.ss_sp) { perror("malloc"); exit(1); }
    sigstk.ss_size = SIGSTKSZ;
    sigstk.ss_flags = 0;
    if (sigaltstack(&sigstk, nullptr) != 0) { perror("sigaltstack"); exit(1); }

    // Install signal handler
    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGABRT, &sa, nullptr);
    // sigaction(SIGINT, &sa, nullptr);

    // Trigger signals
    printf("Main thread raising SIGTRAP\n");
    kill(getpid(), SIGABRT);
    fgetc(stdin);
    // printf("Main thread raising SIGABRT\n");
    // abort();

    while (true);
    // Join worker (though program exits in handler via _exit)
    pthread_join(thr, nullptr);
    return 0;
}