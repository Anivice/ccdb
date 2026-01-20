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

        LZ4F_cctx* cctx = nullptr;
        {
            const size_t rc = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
            throw_if_lz4f_error(rc, "LZ4F_createCompressionContext");
        }

        LZ4F_preferences_t prefs = LZ4F_INIT_PREFERENCES;
        prefs.frameInfo.blockSizeID = LZ4F_max4MB;
        prefs.frameInfo.blockMode = LZ4F_blockLinked;
        prefs.frameInfo.contentChecksumFlag = LZ4F_noContentChecksum;
        prefs.frameInfo.contentSize = static_cast<unsigned long long>(st.st_size);
        prefs.compressionLevel = 0;

        LZ4F_compressOptions_t copt{};
        copt.stableSrc = 1;  // input buffer remains stable during the call

        std::vector<uint8_t> out;
        out.reserve(std::min<size_t>(st.st_size + 64, st.st_size * 2)); // heuristic

        // 1) Write frame header
        {
            uint8_t header[LZ4F_HEADER_SIZE_MAX];
            const size_t headerBytes = LZ4F_compressBegin(cctx, header, sizeof(header), &prefs);
            throw_if_lz4f_error(headerBytes, "LZ4F_compressBegin");
            out.insert(out.end(), header, header + headerBytes);
        }

        // 2) Stream input in chunks
        constexpr size_t kChunk = 4u * 1024u * 1024u; // 4MB
        std::vector<uint8_t> tmp;

        size_t pos = 0;
        while (pos < st.st_size) {
            const size_t inSize = std::min(kChunk, st.st_size - pos);

            const size_t bound = LZ4F_compressBound(inSize, &prefs);
            tmp.resize(bound);

            const size_t written = LZ4F_compressUpdate(
                cctx,
                tmp.data(), tmp.size(),
                data_ + pos, inSize,
                &copt
            );
            throw_if_lz4f_error(written, "LZ4F_compressUpdate");

            out.insert(out.end(), tmp.data(), tmp.data() + written);
            pos += inSize;
        }

        // 3) Finish frame (writes endMark + optional checksum)
        {
            const size_t bound0 = LZ4F_compressBound(0, &prefs);
            tmp.resize(bound0);

            const size_t written = LZ4F_compressEnd(cctx, tmp.data(), tmp.size(), nullptr);
            throw_if_lz4f_error(written, "LZ4F_compressEnd");

            out.insert(out.end(), tmp.data(), tmp.data() + written);
        }

        LZ4F_freeCompressionContext(cctx);

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
