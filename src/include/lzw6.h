#ifndef LZW_LZW6_H
#define LZW_LZW6_H

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <ranges>
#include <list>
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

    constexpr uint8_t reverse_bits(const uint8_t x)
    {
        constexpr uint8_t lookup[16] = {0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};
        return (lookup[x & 0x0F] << 4) | lookup[x >> 4];
    }

    constexpr uint16_t reverse_bits(const uint16_t x)
    {
        return static_cast<uint16_t>(reverse_bits(static_cast<uint8_t>(x))) << 8 | static_cast<uint16_t>(reverse_bits(static_cast<uint8_t>(x >> 8)));
    }

    constexpr uint32_t reverse_bits(const uint32_t x)
    {
        return static_cast<uint32_t>(reverse_bits(static_cast<uint16_t>(x))) << 16 | static_cast<uint32_t>(reverse_bits(static_cast<uint16_t>(x >> 16)));
    }

    constexpr uint64_t reverse_bits(const uint64_t x)
    {
        return static_cast<uint64_t>(reverse_bits(static_cast<uint32_t>(x))) << 32 | static_cast<uint64_t>(reverse_bits(static_cast<uint32_t>(x >> 32)));
    }

    class Huffman
    {
        static constexpr uint64_t MaxCodexLimit = 256;

        /// bitmap base class
        class bitmap_base
        {
        protected:
            uint8_t * data_array_ = nullptr;
            uint64_t particles_ = 0;
            uint64_t bytes_required_ = 0;

            /// Initialize `data_array_`
            /// @param bytes Bytes required
            /// @return If success, return true, else, false. bitmap_base() will throw an error if false.
            std::function<bool(uint64_t)> init_data_array = [](const uint64_t) -> bool { return false; };

            /// Create a bitmap
            /// @param required_particles Particles
            /// @throws std::runtime_error Init failed
            void init(const uint64_t required_particles)
            {
                bytes_required_ = required_particles / 8 + (required_particles % 8 != 0);
                particles_ = required_particles;
                if (!init_data_array(bytes_required_)) throw std::runtime_error("Cannot init data set");
            }

        public:
            bitmap_base() noexcept = default;
            virtual ~bitmap_base() noexcept = default;

            /// Get bit at the specific location
            /// @param index Bit Index
            /// @return The bit at the specific location
            /// @throws std::out_of_range Out of bounds
            [[nodiscard]] bool get_bit(const uint64_t index) const
            {
                if (index >= particles_) throw std::out_of_range("Index out of range");
                const uint64_t q = index >> 3;
                const uint64_t r = index & 7; // div by 8
                uint8_t c = 0;
                c = data_array_[q];

                c >>= r;
                c &= 0x01;
                return c;
            }

            /// Set the bit at the specific location
            /// @param index Bit Index
            /// @param new_bit The new bit value
            /// @return NONE
            /// @throws std::out_of_range Out of bounds
            void set_bit(const uint64_t index, const bool new_bit)
            {
                if (index >= particles_) throw std::out_of_range("Index out of range");
                const uint64_t q = index >> 3;
                const uint64_t r = index & 7; // div by 8
                uint8_t c = 0x01;
                c <<= r;

                if (new_bit) // set to true
                {
                    data_array_[q] |= c;
                }
                else // clear bit
                {
                    data_array_[q] &= ~c;
                }
            }
        };

        struct HuffmanNode
        {
            uint64_t symbol_;
            uint64_t frequency_;
            explicit HuffmanNode(const uint64_t symbol, const uint64_t frequency)
                : symbol_(symbol), frequency_(frequency) { }

            HuffmanNode * left_ = nullptr;
            HuffmanNode * right_ = nullptr;
            bool connected_to_the_tree = false;

            [[nodiscard]] bool operator >(const HuffmanNode & Node) const { return frequency_ > Node.frequency_; }
            [[nodiscard]] bool operator <(const HuffmanNode & Node) const { return frequency_ < Node.frequency_; }
            [[nodiscard]] bool operator >=(const HuffmanNode & Node) const { return frequency_ >= Node.frequency_; }
            [[nodiscard]] bool operator <=(const HuffmanNode & Node) const { return frequency_ <= Node.frequency_; }
            [[nodiscard]] bool operator ==(const HuffmanNode & Node) const { return frequency_ == Node.frequency_; }
        };

        std::list < HuffmanNode > huffman_list;
        const std::vector <uint8_t> & input_;
        std::vector <uint8_t> & output_;
        HuffmanNode * root = nullptr;
        struct symbol_rep_t
        {
            std::vector<uint8_t> data_buffer{};
            uint64_t data_bits = 0;

            bool operator==(const symbol_rep_t & rep) const {
                return (rep.data_bits == data_bits) && (data_buffer == rep.data_buffer);
            }

            bool operator!=(const symbol_rep_t & rep) const {
                return !((rep.data_bits == data_bits) && (data_buffer == rep.data_buffer));
            }
        };

        lzw_dictionary_t < uint64_t, symbol_rep_t > symbol_map;

        void sort()
        {
            huffman_list.sort([&](const HuffmanNode & a, const HuffmanNode & b)->bool
            {
                if (a.connected_to_the_tree || b.connected_to_the_tree) {
                    return a.connected_to_the_tree < b.connected_to_the_tree;
                }
                return a <= b;
            });
        }

        void load_input_into_huffman_list()
        {
            lzw_dictionary_t < uint8_t, uint64_t > frequency_map;
            for (const auto c : input_) {
                frequency_map[c]++;
            }

            for (const auto [sym, freq] : frequency_map) {
                huffman_list.emplace_back(sym, freq);
            }

            sort();
        }

        void make_huffman_tree_from_huffman_list()
        {
            uint64_t next_code = MaxCodexLimit;
            auto assign = [this, &next_code](decltype(huffman_list)::iterator & it, const decltype(huffman_list)::iterator * cannot = nullptr)->bool
            {
                while ((it)->connected_to_the_tree || (cannot && (it == *cannot)) || ((it)->symbol_ == next_code))
                {
                    ++it;
                    if (it == huffman_list.end())
                    {
                        return false;
                    }
                }

                return true;
            };

            decltype(huffman_list)::iterator left;
            while (true)
            {
                auto it = huffman_list.begin();
                left = it;
                auto right = ++it;
                if (!assign(left)) break;
                if (!assign(right, &left)) break;
                auto * left_ptr = &*left;
                auto * right_ptr = &*right;
                huffman_list.emplace_back(next_code++, (left)->frequency_ + (right)->frequency_);
                auto & newNode = huffman_list.back();

                newNode.left_ = left_ptr;
                newNode.right_ = right_ptr;
                left_ptr->connected_to_the_tree = true;
                right_ptr->connected_to_the_tree = true;
                sort();
            }

            if ((left)->connected_to_the_tree) throw std::runtime_error("Internal BUG");
            root = &*left;
        }

        void walk_huffman_tree(const HuffmanNode * parent, const std::vector<uint8_t> & code_, const uint64_t bpos)
        {
            auto walk_child = [&](
                const HuffmanNode * child,
                const uint64_t p_symbol,
                const std::vector<uint8_t> & p_code,
                const uint64_t p_bpos,
                const bool bit)
            {
                if (child) {
                    std::vector<uint8_t> code = p_code;
                    BitWriterLSB writer(code);
                    writer.bitpos = p_bpos;
                    writer.write(bit, 1);
                    walk_huffman_tree(child, code, writer.bitpos);
                } else {
                    // already the end, push symbol
                    symbol_map[p_symbol] = {
                        .data_buffer = p_code,
                        .data_bits = p_bpos
                    };
                }
            };

            walk_child(parent->left_, parent->symbol_, code_, bpos, false);
            walk_child(parent->right_, parent->symbol_, code_, bpos, true);
        }

        void decode_using_constructed_pairs(const std::vector<uint8_t> & input_stream, const uint64_t bits)
        {
            uint64_t offset = 0;
            uint64_t current_bit_size = 1;
            lzw_dictionary_t < uint64_t, uint8_t > flipped_pairs;

            auto numeric_to_uint64_t = [](const uint64_t & result, const uint64_t & data_bits)->uint64_t
            {
                const uint64_t bits_ = data_bits << 58; // lower 58 bits are available for symbols
                // higher 6 bits can describe how much lower bits are used, which should be more than enough
                return result | bits_;
            };

            auto vec_to_uint64_t = [&numeric_to_uint64_t](const symbol_rep_t & vec)
            {
                uint64_t result = 0;
                std::memcpy(&result, vec.data_buffer.data(), vec.data_buffer.size());
                return numeric_to_uint64_t(result, vec.data_bits);
            };

            for (const auto & [byte, bitStream] : symbol_map) {
                flipped_pairs.emplace(vec_to_uint64_t(bitStream), byte);
            }

            BitReaderLSB reader(input_stream);
            auto can_find_reference = [&](const uint64_t bit_offset, const uint64_t bit_size, uint8_t & decoded)->bool
            {
                reader.bitpos = bit_offset;
                const auto current_reference = reader.read(bit_size);
                const auto key = numeric_to_uint64_t(current_reference, bit_size);
                const auto reference = flipped_pairs.find(key);

                if (reference == flipped_pairs.end()) {
                    return false;
                }

                decoded = (reference->second);
                return true;
            };

            while (offset < bits)
            {
                uint8_t decoded = 0;
                while (!can_find_reference(offset, current_bit_size, decoded)) {
                    current_bit_size++;
                }

                output_.push_back(decoded);
                offset += current_bit_size;
                current_bit_size = 1;
            }
        }

    public:
        Huffman (const std::vector<uint8_t> & input, std::vector <uint8_t> & output) : input_(input), output_(output) { }

        std::vector < uint64_t > export_symbols()
        {
            std::vector < uint64_t > ret;
            std::vector < std::pair <uint64_t, symbol_rep_t > > map;
#if (defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L
            map.insert_range(map.end(), symbol_map);
#else
            map.insert(map.end(), symbol_map.begin(), symbol_map.end());
#endif
            std::ranges::sort(map, [](const auto & a, const auto & b)->bool{ return a.first < b.first; });
            std::ranges::for_each(map | std::views::values, [&](const symbol_rep_t & p) {
                uint64_t val = 0;
                std::memcpy(&val, p.data_buffer.data(), p.data_buffer.size());
                val |= p.data_bits << 58;
                ret.push_back(val);
            });

            return ret;
        }

        void compress()
        {
            // construct Huffman table
            load_input_into_huffman_list();
            make_huffman_tree_from_huffman_list();
            walk_huffman_tree(root, {}, 0);

            // write huffman table
            // [UINT8: SYMBOLS]         0: 256, positive integers: 1 - 255
            // [32 BYTE SYMBOL BITMAP]
            // [UINT8]                  [BIT SIZE], [SYMBOLS] in len
            // [[PACKED BITS]...]
            // [BitSteam]
            std::vector<uint8_t> huffman_table, symbol_bitmap(256 / 8, 0);
            class sym_pos_bitmap_t : public bitmap_base
            {
            public:
                explicit sym_pos_bitmap_t(std::vector < uint8_t > & bitmap_data)
                {
                    init_data_array = [&](const uint64_t)->bool
                    {
                        data_array_ = bitmap_data.data();
                        return true;
                    };

                    init(256);
                }
            } sym_pos_bitmap(symbol_bitmap);

            if (symbol_map.size() > 256) throw std::runtime_error("WTF");
            // 1. record size
            huffman_table.push_back(symbol_map.size() == 256 ? 0 : static_cast<uint8_t>(symbol_map.size()));

            // 2. sort unordered_map
            std::vector < std::pair < uint64_t, symbol_rep_t > > map;
#if (defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L
            map.insert_range(map.end(), symbol_map);
#else
            map.insert(map.end(), symbol_map.begin(), symbol_map.end());
#endif
            std::ranges::sort(map, [](const std::pair < uint64_t, symbol_rep_t > & a, const std::pair < uint64_t, symbol_rep_t > & b)->bool {
                return a.first < b.first;
            });

            // 3. record bitmap and push the bit size
            for (const auto & [sym, rep] : map)
            {
                if (rep.data_bits > 255 || sym > 255) throw std::runtime_error("WTF");
                sym_pos_bitmap.set_bit(sym, true);
                huffman_table.push_back(*reinterpret_cast<const uint8_t *>(&rep.data_bits));
            }
#if (defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L
            huffman_table.insert_range(huffman_table.begin() + 1, symbol_bitmap); // add bitmap after symbol size
#else
            huffman_table.insert(huffman_table.begin() + 1, symbol_bitmap.begin(), symbol_bitmap.end()); // add bitmap after symbol size
#endif

            // 4. write packed bits
            std::vector<uint8_t> huffman_table_val_section; // [[PACKED BITS]...]
            BitWriterLSB table_val_sec_writer(huffman_table_val_section);
            for (const auto &[data_buffer, data_bits] : map | std::views::values)
            {
                uint64_t data = 0;
                std::memcpy(&data, data_buffer.data(), data_buffer.size());
                table_val_sec_writer.write(data, data_bits);
            }
            huffman_table.insert(huffman_table.end(), huffman_table_val_section.begin(), huffman_table_val_section.end());

            // now, compress the whole table
            std::vector<uint8_t> compressed_huffman_table;
            lzw<12> LZW(huffman_table, compressed_huffman_table);
            LZW.compress();
            const auto table_size = static_cast<uint16_t>(compressed_huffman_table.size());

            // write to buffer
#if (defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L
            output_.insert(output_.end(), reinterpret_cast<const uint8_t *>(&table_size),
                    reinterpret_cast<const uint8_t *>(&table_size) + sizeof(table_size));
            output_.insert_range(output_.end(), compressed_huffman_table);
#else
            output_.insert(output_.end(), reinterpret_cast<const uint8_t *>(&table_size),
                    reinterpret_cast<const uint8_t *>(&table_size) + sizeof(table_size));
            output_.insert(output_.end(), compressed_huffman_table.begin(), compressed_huffman_table.end());
#endif

            /// ready to encode actual data
            std::vector<uint8_t> encoded_huffman_stream;
            uint64_t bits_written = 0;
            BitWriterLSB writer(encoded_huffman_stream);

            // encode
            for (const auto & c : input_)
            {
                uint64_t data = 0;
                const auto & [data_buffer, data_bits] = symbol_map.at(c);
                std::memcpy(&data, data_buffer.data(), data_buffer.size());
                writer.write(data, static_cast<int64_t>(data_bits));
                bits_written += data_bits;
            }

#if (defined(__GNUC__) && __GNUC__ >= 15) && __cplusplus >= 202302L
            output_.insert(output_.end(), reinterpret_cast<const uint8_t *>(&bits_written),
                reinterpret_cast<const uint8_t *>(&bits_written) + sizeof(bits_written));
            output_.insert_range(output_.end(), encoded_huffman_stream);
#else
            output_.insert(output_.end(), reinterpret_cast<const uint8_t *>(&bits_written),
                reinterpret_cast<const uint8_t *>(&bits_written) + sizeof(bits_written));
            output_.insert(output_.end(), encoded_huffman_stream.begin(), encoded_huffman_stream.end());
#endif
        }

        void decompress()
        {
            uint16_t table_size = 0; // LZW pack size
            std::memcpy(&table_size, input_.data(), sizeof(table_size));
            std::vector<uint8_t> huffman_table_lzw_compressed { input_.begin() + 2, input_.begin() + 2 + table_size }, huffman_table;
            lzw<12> Decompressor(huffman_table_lzw_compressed, huffman_table);
            Decompressor.decompress();

            /// Read symbol table
            const int64_t symbol_count = huffman_table.front() == 0 ? 256 : huffman_table.front();
            std::vector<uint8_t> symbol_bitmap { huffman_table.begin() + 1, huffman_table.begin() + 1 + 32 };
            const class sym_pos_bitmap_t : public bitmap_base
            {
            public:
                explicit sym_pos_bitmap_t(std::vector < uint8_t > & bitmap_data)
                {
                    init_data_array = [&](const uint64_t)->bool
                    {
                        data_array_ = bitmap_data.data();
                        return true;
                    };

                    init(256);
                }
            } sym_pos_bitmap(symbol_bitmap);

            std::vector < uint8_t > huffman_table_header_section {
                huffman_table.begin() + 1 /* symbol size */ + 32 /* symbol bitmap */,
                huffman_table.begin() + 1 + 32 + symbol_count
            };
            std::ranges::reverse(huffman_table_header_section);

            const std::vector < uint8_t > huffman_table_data_section {
                huffman_table.begin() + 1 + 32 + symbol_count,
                huffman_table.end()
            };

            // read data by info from header section
            BitReaderLSB reader(huffman_table_data_section);
            for (uint64_t i = 0; i < 256; i++)
            {
                if (sym_pos_bitmap.get_bit(i))
                {
                    const auto symbol = static_cast<uint8_t>(i);
                    const uint8_t sym_len = huffman_table_header_section.back();
                    huffman_table_header_section.pop_back();

                    // read symbol definition
                    auto & [data_buffer, data_bits] = symbol_map[symbol];
                    BitWriterLSB writer(data_buffer);
                    writer.write(reader.read(sym_len), sym_len);
                    data_bits = sym_len;
                }
            }

            // and now, we have reconstructed our table
            const std::vector<uint8_t> stream_byte_size { input_.begin() + 2 + table_size,
                input_.begin() + 2 + table_size + sizeof(uint64_t) };
            uint64_t stream_byte_size_lit = 0;
            std::memcpy(&stream_byte_size_lit, stream_byte_size.data(), sizeof(stream_byte_size_lit));
            const std::vector<uint8_t> dataStream { input_.begin() + 2 + table_size + sizeof(uint64_t), input_.end() };
            decode_using_constructed_pairs(dataStream, stream_byte_size_lit);
        }
    };
}

#undef lzw_dictionary_t
#endif //LZW_LZW6_H