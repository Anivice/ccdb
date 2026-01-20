#include "lz4frame.h"
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/mman.h>
#include <vector>
#include <unistd.h>

static void throw_if_lz4f_error(const size_t code, const char* where)
{
    if (LZ4F_isError(code)) {
        throw std::runtime_error(std::string(where) + ": " + LZ4F_getErrorName(code));
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3) {
            return 1;
        }

        const int fd = open(argv[1], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return 1;
        }

        struct stat st = { };
        if (fstat(fd, &st) == -1) {
            perror("fstat");
            return 1;
        }

        // Map the entire file into virtual address space
        auto data_ = static_cast<char*>(mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
        if (data_ == MAP_FAILED) {
            perror("mmap");
            return 1;
        }

        LZ4F_dctx* dctx = nullptr;
        {
            const size_t rc = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
            throw_if_lz4f_error(rc, "LZ4F_createDecompressionContext");
        }

        std::vector<uint8_t> out;

        size_t srcPos = 0;
        {
            LZ4F_frameInfo_t fi{};
            size_t srcSize = st.st_size; // may include more than header; getFrameInfo only consumes header
            const size_t hint = LZ4F_getFrameInfo(dctx, &fi, data_, &srcSize);
            throw_if_lz4f_error(hint, "LZ4F_getFrameInfo");
            srcPos += srcSize;

            if (fi.contentSize != 0 && fi.contentSize <= static_cast<unsigned long long>(out.max_size())) {
                out.reserve(fi.contentSize);
            }
        }

        // Decompress loop
        constexpr size_t kOutChunk = 4u * 1024u * 1024u; // 4MB output buffer
        std::vector<uint8_t> dst(kOutChunk);

        while (true)
        {
            size_t dstSize = dst.size();
            size_t srcSize = st.st_size - srcPos;

            const size_t ret = LZ4F_decompress(
                dctx,
                dst.data(), &dstSize,
                data_ + srcPos, &srcSize,
                nullptr
            );
            throw_if_lz4f_error(ret, "LZ4F_decompress");

            srcPos += srcSize;
            out.insert(out.end(), dst.data(), dst.data() + dstSize);

            if (ret == 0) { // frame fully decoded
                break;
            }
            if (srcPos >= st.st_size) {
                // No more input but decoder expects more => truncated/corrupt stream
                LZ4F_freeDecompressionContext(dctx);
                throw std::runtime_error("LZ4F_decompress: truncated input (more data expected)");
            }
        }

        const size_t freeRc = LZ4F_freeDecompressionContext(dctx);
        // freeRc is 0 when decompression completed cleanly; treat non-zero as suspicious
        throw_if_lz4f_error(freeRc, "LZ4F_freeDecompressionContext");

        const int fd_out = open(argv[2], std::filesystem::exists(argv[2]) ? (O_WRONLY | O_TRUNC) : (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL), 0644);
        if (fd_out == -1) {
            perror("open");
            return 1;
        }

        write(fd_out, out.data(), out.size());
        close(fd_out);

        munmap(data_, st.st_size);
        close(fd);
        return 0;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
