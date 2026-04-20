#ifndef CCDB_NUMERIC_H
#define CCDB_NUMERIC_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>

namespace Numeric {
    template < typename T > concept UnsignedIntType = std::is_unsigned_v<T>;
    template < typename T > concept SignedIntType = std::is_signed_v<T>;
    template < typename T > concept IntType = (std::is_signed_v<T> || std::is_unsigned_v<T>);

    template < UnsignedIntType UintType >
    constexpr UintType cell_div(UintType a, UintType b) { return a / b + (a % b == 0 ? 0 : 1); }

    constexpr uint64_t max_number_by_bits(const uint64_t bits)
    {
        if (bits > 64) throw std::runtime_error("Maximum number of bits is 64!");
        if (bits == 0) return 0;
        uint64_t result = 1;
        for (uint64_t i = 0; i < bits - 1; ++i) {
            result <<= 1;
            result |= 1;
        }

        return result;
    }

    template < typename Func >
    concept SignedValueOverFlowHandler_Concept = requires(Func Func_) {
            { std::invoke(Func_) } -> std::same_as<void>;
    };

    template <
        const uint64_t NumericValueBitSize, // The bit size for numeric value
        typename NumericValueIsSigned = signed, // Is this numeric value signed
        UnsignedIntType BaseUnsignedIntUnitType = uint8_t, // The base type for data storage
        SignedValueOverFlowHandler_Concept SignedValueOverFlowHandlerType = std::function<void()>
    >
    requires (
           std::is_same_v<BaseUnsignedIntUnitType, uint8_t>
        || std::is_same_v<BaseUnsignedIntUnitType, uint16_t>
        || std::is_same_v<BaseUnsignedIntUnitType, uint32_t>
        || std::is_same_v<BaseUnsignedIntUnitType, uint64_t>
        || std::is_same_v<NumericValueIsSigned, signed>
        || std::is_same_v<NumericValueIsSigned, unsigned>
    )
    class Numeric {
    public:
        static constexpr uint64_t BaseUnsignedIntUnitTypeBitSize = std::min((uint64_t)(sizeof(BaseUnsignedIntUnitType) * 8), NumericValueBitSize); // Max bit size for each numeric cell
        static constexpr uint64_t BaseUnsignedIntUnitTypeMaxNumber = max_number_by_bits(BaseUnsignedIntUnitTypeBitSize); // Max number for each numeric cell
        static constexpr uint64_t RequiredByteBlocks = cell_div(NumericValueBitSize, BaseUnsignedIntUnitTypeBitSize); // Required cells for storage
        static constexpr uint64_t TailingBitsInTheLastCell = NumericValueBitSize % BaseUnsignedIntUnitTypeBitSize == 0 ? BaseUnsignedIntUnitTypeBitSize : NumericValueBitSize % BaseUnsignedIntUnitTypeBitSize; // Bits in the last cell
        static constexpr uint64_t MaxNumberForTailingCell = max_number_by_bits(TailingBitsInTheLastCell); // Max number for the last numeric cell
        static constexpr uint64_t MaxNumerForThisNumeric_ApplicableOnlyIfBelow64Bits = max_number_by_bits(std::min(NumericValueBitSize, static_cast<uint64_t>(64))); // Max number for this numeric value, 0 if bits goes beyond 64
        static constexpr bool BooleanNumericValueIsSigned = std::is_same_v<NumericValueIsSigned, signed>;
        SignedValueOverFlowHandlerType SignedValueOverFlowHandler_;

        class CellType
        {
        public:
            BaseUnsignedIntUnitType data_ { };
            const BaseUnsignedIntUnitType cell_bits_usable_ { };

            struct {
                uint64_t overflow_:1;
            } CellFlags;

            // Initers:
            CellType(CellType &&) noexcept = default;
            CellType(const CellType &) = default;
            explicit CellType(const BaseUnsignedIntUnitType usable_bits) : cell_bits_usable_(usable_bits) { }
            ~CellType() = default;

            template < SignedIntType IntType >
            CellType operator = (const IntType & other)
            {
                if (other > BaseUnsignedIntUnitTypeMaxNumber) {
                    throw std::runtime_error("Assignment of numeric value overflows!");
                }

                data_ = other;
                return *this;
            }

            CellType operator = (const CellType & other)
            {
                if (cell_bits_usable_ != other.cell_bits_usable_) {
                    throw std::runtime_error("Assignment of two incompatible cells!");
                }

                data_ = other.data_;
                CellFlags = other.CellFlags;
                return *this;
            }

            // Basic Ops:
            CellType operator + (const CellType & other) const
            {
                if (other.cell_bits_usable_ != cell_bits_usable_) {
                    throw std::runtime_error("Cell does not have the same number of bits!");
                }

                if (other.data_ && data_) {
                    CellType result(cell_bits_usable_);
                    BaseUnsignedIntUnitType space = BaseUnsignedIntUnitTypeMaxNumber - data_;
                    result.CellFlags.overflow_ = (space < other.data_);
                    result.data_ = data_ + other.data_;
                    return result;
                }

                CellType result(cell_bits_usable_);
                result.data_ = data_ == 0 ? other.data_ : data_;
                result.CellFlags = { };
                return result;
            }

            template < UnsignedIntType Uint >
            CellType operator + (const Uint other) const
            {
                CellType b(cell_bits_usable_);
                b.data_ = other & max_number_by_bits(cell_bits_usable_);
                return *this + b;
            }
        };

        std::array < std::unique_ptr < CellType >, RequiredByteBlocks > cells_;

        explicit Numeric(SignedValueOverFlowHandlerType SignedValueOverFlowHandler = []{ })
        {
            for (uint64_t i = 0; i < cells_.size(); ++i) {
                cells_[i] = std::make_unique < CellType >
                    (i == cells_.size() - 1 ? TailingBitsInTheLastCell : BaseUnsignedIntUnitTypeBitSize);
            }

            SignedValueOverFlowHandler_ = SignedValueOverFlowHandler;
        }

        Numeric & operator = (const Numeric & other)
        {
            for (uint64_t i = 0; i < cells_.size(); ++i) {
                cells_[i] = std::make_unique < CellType >
                    (i == cells_.size() - 1 ? TailingBitsInTheLastCell : BaseUnsignedIntUnitTypeBitSize);
                cells_[i]->data_ = other.cells_[i]->data_;
            }

            SignedValueOverFlowHandler_ = other.SignedValueOverFlowHandler_;
            return *this;
        }

        Numeric(const Numeric & other) {
            this->operator=(other);
        }

        Numeric(Numeric && other) = default;

        template < IntType IntType >
        Numeric & operator = (const IntType other)
        {
            const std::make_unsigned_t<IntType> * ptr;
            ptr = reinterpret_cast<const decltype(ptr)>(&other);
            if (*ptr > MaxNumerForThisNumeric_ApplicableOnlyIfBelow64Bits) {
                throw std::runtime_error("Assignment of numeric value overflows!");
            }

            auto load = [this](const uint64_t data)
            {
                int index = 0;
                const uint64_t target = data;
                const BaseUnsignedIntUnitType * ptr = reinterpret_cast<const BaseUnsignedIntUnitType *>(&target);
                std::ranges::for_each(cells_, [&](auto & cell_ptr) {
                    cell_ptr->data_ = (index < (sizeof (target) / sizeof (BaseUnsignedIntUnitType)) ? ptr[index++] : 0);
                    cell_ptr->CellFlags = { };
                });
            };

            if (other > 0) {
                load(other);
            } else {
                uint64_t target = -other;
                // -N = ~N + 1
                target = ~target + 1;
                target &= MaxNumerForThisNumeric_ApplicableOnlyIfBelow64Bits; // mask unwanted bits
                load(target);
            }

            return *this;
        }

        template < IntType IntType >
        Numeric(IntType val)
        {
            for (uint64_t i = 0; i < this->cells_.size(); ++i) {
                this->cells_[i] = std::make_unique < CellType >
                    (i == this->cells_.size() - 1 ? TailingBitsInTheLastCell : BaseUnsignedIntUnitTypeBitSize);
            }

            if (!this->SignedValueOverFlowHandler_)
                this->SignedValueOverFlowHandler_ = []{ };
            this->operator=(val);
        }

        // Basic Ops:
        Numeric operator + (const Numeric & other) const
        {
            Numeric result;
            for (uint64_t i = 0; i < cells_.size(); ++i) {
                const auto numA = *cells_[i];
                const auto numB = *other.cells_[i];
                const auto step1 = numA + numB;
                const auto step2 = (i > 0 ? result.cells_[i - 1]->CellFlags.overflow_ : 0);
                *result.cells_[i] = step1 + step2;
                result.cells_[i]->CellFlags.overflow_ |= step1.CellFlags.overflow_;
            }

            if (result.cells_.back()->data_ > MaxNumberForTailingCell || result.cells_.back()->CellFlags.overflow_) { // overflow within last cell
                // this indicates an overflow of the whole numeric value
                // wrap the value if unsigned: C_Result = RealResult % (UINT_MAX + 1)
                if constexpr (BooleanNumericValueIsSigned) {
                    SignedValueOverFlowHandler_();
                }

                result.cells_.back()->data_ &= MaxNumberForTailingCell;
            }

            std::ranges::for_each(result.cells_, [&](auto & cell_ptr) {
                cell_ptr->CellFlags = { };
            });
            return result;
        }

        template < IntType Uint >
        Numeric operator + (const Uint other) const
        {
            Numeric result = other;
            return *this + result;
        }

        Numeric operator ~ () const
        {
            Numeric result = *this;
            std::ranges::for_each(result.cells_, [&](auto & cell_ptr) {
                cell_ptr->data_ = ~cell_ptr->data_;
                cell_ptr->CellFlags = { };
            });
            result.cells_.back()->data_ &= MaxNumberForTailingCell;
            return result;
        }

        Numeric operator - (const Numeric & other) const
        {
            // A - B = A + (~B) + 1
            Numeric a = *this, b = other;
            return a + (~b) + 1;
        }

        template < typename Type >
        Numeric & operator += (Type val) {
            *this = *this + val;
            return *this;
        }

        template < typename Type >
        Numeric & operator -= (Type val) {
            *this = *this - val;
            return *this;
        }

        Numeric & operator ++() {
            *this = *this + 1;
            return *this;
        }

        Numeric & operator --() {
            *this = *this - 1;
            return *this;
        }

        operator uint64_t () const
        {
            static_assert(!BooleanNumericValueIsSigned, "Cannot convert signed value to unsigned!");
            uint64_t result = 0;
            BaseUnsignedIntUnitType * ptr = reinterpret_cast<BaseUnsignedIntUnitType *>(&result);
            for (uint64_t i = 0; i < cells_.size(); ++i) {
                ptr[i] = cells_[i]->data_;
            }

            return result;
        }

        operator int64_t () const
        {
            static_assert(BooleanNumericValueIsSigned, "Cannot convert unsigned value to signed!");
            int64_t result = 0;
            BaseUnsignedIntUnitType * ptr = reinterpret_cast<BaseUnsignedIntUnitType *>(&result);
            for (uint64_t i = 0; i < cells_.size(); ++i) {
                ptr[i] = cells_[i]->data_;
            }

            if (const auto mask = 0x01 << (cells_.back()->cell_bits_usable_ - 1); cells_.back()->data_ & mask) {
                result |= max_number_by_bits(64) & ~max_number_by_bits(NumericValueBitSize);
            }

            return result;
        }
    };
}

#endif //CCDB_NUMERIC_H
