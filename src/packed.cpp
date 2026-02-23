#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "elf_file.h"
#include <vector>
#include "include/lzw6.h"

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

int main(int argc, char** argv)
{
    const int fd = static_cast<int>(syscall(__NR_memfd_create, "mem_elf", 0));
    if (fd == -1) {
        perror("memfd_create");
        return 1;
    }

    std::vector<uint8_t> out;
    const std::vector<uint8_t> in{elf_file, elf_file + elf_file_len};
    lzw::lzw<12> LZW(in, out);
    LZW.decompress();

    const ssize_t written = write(fd, out.data(), out.size());
    if (written != static_cast<ssize_t>(out.size()))
    {
        perror("write");
        close(fd);
        return 1;
    }

    fchmod(fd, 0700);
    fexecve(fd, argv, environ);

    char path[64] { };
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    execve(path, argv, environ);

    perror("fexecve/execve");
    close(fd);
    return 1;
}