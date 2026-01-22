#ifndef LZW_LZW6_H
#define LZW_LZW6_H

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#ifdef USE_TSL_HOPSCOTCH_MAP
# include "tsl/hopscotch_map.h"
# define lzw_dictionary_t tsl::hopscotch_map
#else
# include <unordered_map>
# define lzw_dictionary_t std::unordered_map
#endif

namespace lzw
{
    /// Bit IO steam
    struct BitWriterLSB
    {
        std::vector<uint8_t>& out;
        uint64_t bitpos = 0;

        explicit BitWriterLSB(std::vector<uint8_t>& o) : out(o) { }

        void write(const uint64_t code, const uint64_t width) {
            for (uint64_t i = 0; i < width; ++i, ++bitpos) {
                const uint64_t bytepos = bitpos >> 3;
                const uint64_t bitoff  = bitpos & 7;
                if (bytepos >= out.size()) out.push_back(0);
                const uint8_t bit = (code >> i) & 1;
                out[bytepos] |= (bit << bitoff);
            }
        }
    };

    struct BitReaderLSB
    {
        const std::vector<uint8_t>& in;
        uint64_t bitpos = 0;

        explicit BitReaderLSB(const std::vector<uint8_t>& i) : in(i) {}

        uint64_t read(const uint64_t width)
        {
            const uint64_t total_bits = in.size() * 8;
            if (bitpos + width > total_bits) throw std::out_of_range("EOF");

            uint64_t v = 0;
            for (uint64_t i = 0; i < width; ++i, ++bitpos) {
                const uint64_t bytepos = bitpos >> 3;
                const uint64_t bitoff  = bitpos & 7;
                const uint8_t bit = (in[bytepos] >> bitoff) & 1;
                v |= (static_cast<uint64_t>(bit) << i);
            }
            return v;
        }
    };

    template <typename Type>
    constexpr Type const_two_power(const Type n)
    {
        static_assert(std::is_integral_v<Type>, "Type must be an integral type");
        return static_cast<Type>(0x01) << n;
    }

    template <
        uint64_t LZWMaxBitSize,
        const bool EarlyChange = false,
        const uint64_t MinimumCodeSize = 8,
        const uint64_t MaxDictionarySize = const_two_power(LZWMaxBitSize) - 1,
        const uint64_t ClearCode = 1 << MinimumCodeSize,
        const uint64_t EOICode = ClearCode + 1,
        const uint64_t FirstFreeCode = ClearCode + 2,
        const uint64_t MaxCode  = (1 << LZWMaxBitSize) - 1
    >
    requires (LZWMaxBitSize <= 63)
    class lzw {
        const std::vector<uint8_t> & input_;
        std::vector<uint8_t> & output_;

    public:
        lzw(const std::vector<uint8_t> & input, std::vector<uint8_t> & output)
            : input_(input), output_(output) { }

        void compress()
        {
            output_.clear();
            BitWriterLSB BitStream(output_);
            lzw_dictionary_t < std::string, uint64_t > dictionary;
            dictionary.reserve(MaxDictionarySize);
            auto init_dict = [&dictionary]
            {
                for (uint64_t i = 0; i < ClearCode; ++i) {
                    dictionary[{static_cast<char>(i)}] = i;
                }
            };

            init_dict();

            uint64_t code_width = MinimumCodeSize + 1;
            uint64_t next_code = FirstFreeCode;
            BitStream.write(ClearCode, code_width);

            std::string w;
            for (const auto k : input_)
            {
                std::string wk = w;
                wk.push_back(*reinterpret_cast<const char *>(&k));

                if (dictionary.contains(wk)) {
                    w = wk;
                    continue;
                }

                // Output current longest string
                BitStream.write(dictionary[w], code_width);

                // Add new entry
                if (next_code <= MaxCode) {
                    dictionary[wk] = next_code;
                    ++next_code;

                    // Code width increase: encoder and decoder must follow same rule
                    const uint64_t threshold = (1ULL << code_width) - (EarlyChange ? 1 : 0);
                    if (next_code > threshold && code_width < LZWMaxBitSize) {
                        ++code_width;
                    }
                } else {
                    // Dictionary full: write Clear and reset
                    BitStream.write(ClearCode, code_width);

                    dictionary.clear();
                    init_dict();

                    code_width = MinimumCodeSize + 1;
                    next_code = FirstFreeCode;
                }

                w = {*reinterpret_cast<const char *>(&k)};
            }

            if (!w.empty()) {
                BitStream.write(dictionary[w], code_width);
            }

            BitStream.write(EOICode, code_width);
        }

        void decompress()
        {
            output_.clear();
            output_.reserve(input_.size());
            BitReaderLSB BitStream(input_);
            uint64_t code_width = MinimumCodeSize + 1;
            uint64_t next_code = FirstFreeCode;
            int64_t prev = -1;  // Using -1 as sentinel for "no previous"
            lzw_dictionary_t < uint64_t, std::string > dictionary;

            dictionary.reserve(MaxDictionarySize);
            auto init_dict = [&dictionary]
            {
                dictionary.clear();
                for (uint64_t i = 0; i < ClearCode; ++i) {
                    dictionary[i] = std::string {static_cast<char>(i)};
                }
            };

            init_dict();

            // Read first code (should be clear)
            uint64_t code = BitStream.read(code_width);
            if (code != ClearCode) {
                throw std::invalid_argument("Invalid LZW format!");
            }

            while (true)
            {
                code = BitStream.read(code_width);
                if (code == EOICode) {
                    break;
                }

                if (code == ClearCode) {
                    init_dict();
                    code_width = MinimumCodeSize + 1;
                    next_code = FirstFreeCode;
                    prev = -1;
                    continue;
                }

                std::string entry;
                if (auto it = dictionary.find(code); it != dictionary.end()) {
                    entry = it->second;
                } else {
                    if (prev == -1 || code != next_code) {
                        throw std::invalid_argument("Corrupted LZW stream (invalid code)");
                    }
                    const std::string& prev_entry = dictionary[static_cast<uint64_t>(prev)];
                    entry = prev_entry;
                    entry.push_back(prev_entry[0]);
                }

                if (prev != -1 && next_code <= MaxCode)
                {
                    const std::string prev_entry = dictionary[prev];
                    std::string new_entry = prev_entry;
                    new_entry.push_back(entry[0]);
                    dictionary[next_code] = new_entry;
                    ++next_code;

                    const uint64_t threshold = (1ULL << code_width) - (EarlyChange ? 1 : 0);
                    if (next_code >= threshold && code_width < LZWMaxBitSize) {
                        ++code_width;
                    }
                }

                // Add entry to output
                if (output_.capacity() < entry.size() + output_.size()) {
                    output_.reserve(std::max(output_.size() * 2, entry.size() + output_.size()));
                }
                output_.insert(output_.end(), entry.begin(), entry.end());
                prev = static_cast<int64_t>(code);
            }
        }
    };
}

#undef lzw_dictionary_t
#endif //LZW_LZW6_H