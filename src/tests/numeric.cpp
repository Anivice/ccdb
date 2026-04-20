#include "numeric/numeric.h"

#include <iostream>

int main() {
    Numeric::Numeric<8> cell1, cell2;
    for (uint64_t i = 0; i < 655360; i++)
    {
        const int8_t a = rand(), b = rand();
        cell1 = a;
        cell2 = b;
        const auto tmp = cell1 - cell2;
        const auto tmp2 = cell1 + cell2;

        const auto _1_result_a = static_cast<int8_t>(static_cast<int64_t>(tmp));
        const auto _1_result_b = static_cast<int8_t>(a - b);
        const auto _2_result_a = static_cast<int8_t>(static_cast<int64_t>(tmp2));
        const auto _2_result_b = static_cast<int8_t>(a + b);

        if (_1_result_a != _1_result_b || _2_result_a != _2_result_b) {
            return 1;
        }
    }
}
