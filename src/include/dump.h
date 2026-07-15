#ifndef CCDB_DUMP_H
#define CCDB_DUMP_H

#include <string>
#include <vector>
#include <stdexcept>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <algorithm>
#include <sstream>
#include <bit>

using input_stream_t = std::basic_istream<char>;
using output_stream_t = std::basic_ostream<char>;

#ifndef _WIN32
using pathCharType = char;
using pathStringType = std::string;
#else
using pathCharType = wchar_t;
using pathStringType = std::wstring;
#endif

[[nodiscard]]
constexpr static bool integer_is_power_of_two(const std::size_t value) noexcept {
	return (value != 0) && ((value & (value - 1)) == 0);
}

template < typename T, std::size_t SetSize, typename ArrayUnit = char >
concept CharacterSetType = (std::is_same_v<std::array<ArrayUnit, SetSize>, T> && (SetSize >= 2));

template <unsigned FractionBits = 32>
[[nodiscard]]
constexpr long double approximate_log2(const std::uint64_t value) noexcept
{
	if (value == 0) {
		return -std::numeric_limits<long double>::infinity();
	}

	const auto exponent = static_cast<unsigned>(std::bit_width(value) - 1);
	const std::uint64_t power_of_two = std::uint64_t{ 1 } << exponent;

	long double normalized =
		static_cast<long double>(value) /
		static_cast<long double>(power_of_two);

	auto result = static_cast<long double>(exponent);
	long double fractional_bit = 0.5L;

	for (unsigned i = 0; i < FractionBits; ++i)
	{
		normalized *= normalized;

		if (normalized >= 2.0L)
		{
			normalized *= 0.5L;
			result += fractional_bit;
		}

		fractional_bit *= 0.5L;
	}

	return result;
}

static constexpr uint64_t static_pow(const uint64_t base, const std::size_t exponent) noexcept
{
	uint64_t result = 1;
	for (std::size_t i = 0; i < exponent; ++i) {
		result *= base;
	}
	return result;
}

template <typename T, std::size_t N>
[[nodiscard]]
constexpr bool contains_duplicates(
	const std::array<T, N>& values)
{
	for (std::size_t i = 0; i < N; ++i)
		for (std::size_t j = i + 1; j < N; ++j)
			if (values[i] == values[j])
				return true;

	return false;
}

[[nodiscard]]
static constexpr std::pair<int, int> find_encoding_block(
	const std::uint64_t alphabet_size,
	const long double minimum_efficiency = 0.95L,
	const std::size_t maximum_bytes = 4096)
{
	if (alphabet_size < 2)
	{
		throw std::invalid_argument(
			"Alphabet size must be at least 2.");
	}

	if (!(minimum_efficiency > 0.0L &&
		minimum_efficiency <= 1.0L))
	{
		throw std::invalid_argument(
			"Efficiency must be in the range (0, 1].");
	}

	if (maximum_bytes == 0)
		return { -1, -1 };

	const long double bits_per_symbol =
		approximate_log2(alphabet_size);

	if (!(bits_per_symbol > 0.0L))
		return { -1, -1 };

	for (std::size_t bytes = 1;
		bytes <= maximum_bytes;
		++bytes)
	{
		const long double required_symbols =
			(8.0L * static_cast<long double>(bytes))
			/ bits_per_symbol;

		// constexpr replacement for ceil().
		auto symbols = static_cast<std::size_t>(required_symbols);

		if (static_cast<long double>(symbols)
			< required_symbols)
		{
			++symbols;
		}

		if (symbols == 0)
			continue;

		const long double efficiency =
			(8.0L * static_cast<long double>(bytes))
			/
			(static_cast<long double>(symbols)
				* bits_per_symbol);

		if (efficiency >= minimum_efficiency)
		{
			if (bytes >
				static_cast<std::size_t>(
					std::numeric_limits<int>::max()) ||
				symbols >
				static_cast<std::size_t>(
					std::numeric_limits<int>::max()))
			{
				return { -1, -1 };
			}

			return {
				static_cast<int>(bytes),
				static_cast<int>(symbols)
			};
		}
	}

	return { -1, -1 };
}

constexpr auto size_metadata =
#ifdef CMAKE_METADATA_BLOCK_MINIMUM_EFFICIENCY
	CMAKE_METADATA_BLOCK_MINIMUM_EFFICIENCY;
#else
	0.50L;
#endif

template <
	std::size_t CharacterSetSize,
	CharacterSetType <CharacterSetSize> charSet_t,
	auto sizeInfo = find_encoding_block(CharacterSetSize, size_metadata),
	std::size_t InputBlockSize = sizeInfo.first,
	std::size_t OutputBlockSize = sizeInfo.second,
	std::size_t BufferBlockSize = (4096 / (InputBlockSize * OutputBlockSize)) * InputBlockSize * OutputBlockSize
>
	requires (InputBlockSize > 0 && OutputBlockSize > 0 && InputBlockSize <= 8)
uint64_t encode(input_stream_t& in, output_stream_t& out, const charSet_t& character_set)
{
	if (contains_duplicates(character_set)) {
		throw std::invalid_argument("Character set contains duplicate symbols");
	}

	//std::cerr << "Input buffer patch size: " << BufferBlockSize << ", encoded with (i:" << InputBlockSize << ", o:" << OutputBlockSize << ")" << std::endl;
	uint64_t all_rd_size = 0;
	std::vector<char> input_buffer(BufferBlockSize);
	std::vector<std::uint64_t> digits(OutputBlockSize, 0);
	// uint64_t current_encode_offset = 0;
	while (in)
	{
		in.read(input_buffer.data(), BufferBlockSize);
		std::streamsize bytes_read = in.gcount();
		all_rd_size += bytes_read;
		if (bytes_read <= 0) break;

		for (std::streamsize i = 0; i < bytes_read / static_cast<std::streamsize>(InputBlockSize); ++i)
		{
			const char* ref_ptr = input_buffer.data() + i * InputBlockSize;
			uint64_t value = 0;
			for (decltype(InputBlockSize) j = 0; j < InputBlockSize; j++)
			{
				value <<= 8;
				value += static_cast<std::uint8_t>(/*code*/ref_ptr[j]);
			}

			for (std::size_t j = OutputBlockSize; j-- > 0;)
			{
				const auto remainder = value % CharacterSetSize;
				digits[j] = static_cast<std::uint64_t>(remainder);
				value /= CharacterSetSize;
			}

			if (value != 0) {
				throw std::logic_error("output block is too small");
			}

			std::ranges::for_each(digits, [&out, &character_set](const uint64_t sym_) {
				out.write(character_set.data() + sym_, 1);
				});
		}

		auto tail = bytes_read % static_cast<std::streamsize>(InputBlockSize);
		if (tail != 0)
		{
			const char* ref_ptr = input_buffer.data() + (bytes_read / static_cast<std::streamsize>(InputBlockSize)) * InputBlockSize;
			uint64_t value = 0;
			for (decltype(tail) j = 0; j < tail; ++j)
			{
				value <<= 8;
				value += ref_ptr[j];
			}

			for (std::size_t i = OutputBlockSize; i-- > 0;)
			{
				const auto remainder = value % CharacterSetSize;
				digits[i] = static_cast<std::uint64_t>(remainder);
				value /= CharacterSetSize;
			}

			if (value != 0) {
				throw std::logic_error("output block is too small");
			}

			std::ranges::for_each(digits, [&out, &character_set](const uint64_t sym_) {
				out.write(character_set.data() + sym_, 1);
				});
		}
	}

	return all_rd_size;
}

template <
	std::size_t CharacterSetSize,
	CharacterSetType<CharacterSetSize> charSet_t,
	auto sizeInfo = find_encoding_block(CharacterSetSize, size_metadata),
	std::size_t InputBlockSize = sizeInfo.first,
	std::size_t OutputBlockSize = sizeInfo.second,
	std::size_t BufferBlockSize = (4096 / (InputBlockSize * OutputBlockSize))* InputBlockSize* OutputBlockSize
>
	requires (InputBlockSize > 0 && OutputBlockSize > 0 && InputBlockSize <= 8)
void decode(
	input_stream_t& in,
	output_stream_t& out,
	const charSet_t& character_set,
	std::uint64_t decoded_size)
{
	std::array<int, 256> inverse{};
	inverse.fill(-1);

	for (std::size_t i = 0; i < CharacterSetSize; ++i)
	{
		const auto ch = static_cast<unsigned char>(character_set[i]);

		if (inverse[ch] != -1) {
			throw std::invalid_argument("character_set contains duplicate symbols");
		}

		inverse[ch] = static_cast<int>(i);
	}

	std::vector<char> input_buffer(BufferBlockSize);
	std::array<char, InputBlockSize> output_block{};
	// uint64_t current_decode_offset = 0;

	while (decoded_size > 0)
	{
		in.read(input_buffer.data(), static_cast<std::streamsize>(BufferBlockSize));
		const std::streamsize chars_read = in.gcount();

		if (chars_read <= 0) {
			throw std::runtime_error("encoded stream ended before decoded_size bytes were produced");
		}

		if ((chars_read % static_cast<std::streamsize>(OutputBlockSize)) != 0) {
			throw std::runtime_error("encoded stream is truncated or misaligned");
		}

		const auto blocks =
			chars_read / static_cast<std::streamsize>(OutputBlockSize);

		for (std::streamsize block = 0;
			block < blocks && decoded_size > 0;
			++block)
		{
			const char* ref_ptr =
				input_buffer.data() + block * OutputBlockSize;

			std::uint64_t value = 0;

			for (std::size_t j = 0; j < OutputBlockSize; ++j)
			{
				const auto code = static_cast<unsigned char>(ref_ptr[j]);
				const int digit = inverse[code];

				if (digit < 0) {
					throw std::runtime_error("encoded stream contains a symbol outside the character set");
				}

				value *= CharacterSetSize;
				value += static_cast<std::uint64_t>(digit);
			}

			for (std::size_t i = InputBlockSize; i-- > 0;)
			{
				output_block[i] = static_cast<char>(value & 0xFFu);
				value >>= 8;
			}

			if (value != 0) {
				throw std::logic_error("decoded integer does not fit in InputBlockSize bytes");
			}

			const std::size_t bytes_this_block =
				decoded_size >= InputBlockSize
				? InputBlockSize
				: static_cast<std::size_t>(decoded_size);

			out.write(
				output_block.data() + (InputBlockSize - bytes_this_block),
				static_cast<std::streamsize>(bytes_this_block));

			decoded_size -= bytes_this_block;
		}
	}

	char extra = 0;
	if (in.read(&extra, 1)) {
		throw std::runtime_error(
			"encoded stream contains trailing data after the expected payload");
	}
}

#endif //CCDB_DUMP_H
