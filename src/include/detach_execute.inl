#ifndef CCDB_DETACH_EXECUTE_INL_
#define CCDB_DETACH_EXECUTE_INL_

template <typename... ArgsForFetcherChild, typename... ArgsForFetcherParent, typename ChildFunc, typename ParentFunc>
[[nodiscard]] bool detach_execute(
    const ChildFunc & child_func, ArgsForFetcherChild... args_for_fetcher_child,
    const ParentFunc & parent_func, ArgsForFetcherParent... args_for_fetcher_parent,
    const int timeout_ms)
{
    int pipefd[2] { };

    // Create a pipe
    if (pipe(pipefd) == -1) {
        return false;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        return false;
    }

    if (pid == 0) {  // Child process
        close(pipefd[0]);
        if (!child_func(pipefd[1], args_for_fetcher_child...)) { // child func should fetch info and write to pipe
            _exit(1);
        }

        // Close write end and exit
        close(pipefd[1]);
        _exit(EXIT_SUCCESS);
    }
    // Parent process
    // Close unused write end
    close(pipefd[1]);

    // Set up poll to wait for data on the read end
    pollfd fds { };
    fds.fd = pipefd[0];
    fds.events = POLLIN;
    g_pid = pid;

    if (const int ret = poll(&fds, 1, timeout_ms); ret == -1) {
    } else if (ret == 0) {
        (void)kill(pid, SIGKILL);
    } else {
        // Data is available (or EOF if child closed pipe)
        if (fds.revents & POLLIN) {
            if (!parent_func(pipefd[0], args_for_fetcher_parent...)) {
                return false;
            }
        }
    }

    // Clean up: close pipe and reap child
    close(pipefd[0]);
    // Wait for child to avoid zombie
    int status;
    waitpid(pid, &status, 0);
    g_pid = -1;
    return status == 0;
}
#endif