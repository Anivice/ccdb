// Native resource generator. Shares the existing LZW format with the Linux build.
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include "lzw6.h"

int main(int argc, char** argv)
{
    try {
        if (argc != 4) throw std::runtime_error("embed INPUT OUTPUT_BASE SYMBOL");
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open resource");
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
        std::vector<uint8_t> encoded;
        lzw::lzw<12> codec(bytes, encoded);
        codec.compress();
        std::ofstream cpp(std::string(argv[2]) + ".cpp"), h(std::string(argv[2]) + ".h");
        if (!cpp || !h) throw std::runtime_error("Cannot create embedded resource");
        h << "#pragma once\nextern unsigned char " << argv[3] << "[];\nextern unsigned int "
          << argv[3] << "_len;\n";
        cpp << "unsigned char " << argv[3] << "[] = {";
        for (auto byte : encoded) cpp << unsigned(byte) << ',';
        cpp << "};\nunsigned int " << argv[3] << "_len = " << encoded.size() << ";\n";
        if (!cpp || !h) throw std::runtime_error("Cannot write embedded resource");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
