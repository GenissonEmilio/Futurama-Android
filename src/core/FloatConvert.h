#pragma once

#include "core/Types.h"

#include <cmath>
#include <limits>

namespace fut::numeric {

// The Xbox's x87/SSE conversions return integer-indefinite on NaN or overflow.
// A C++ cast outside the signed range has undefined behavior, including on ARM.
inline s32 X87RoundToInt32(double value) {
    const double rounded = std::nearbyint(value);
    if (!(rounded >= -2147483648.0 && rounded < 2147483648.0))
        return std::numeric_limits<s32>::min();
    return static_cast<s32>(rounded);
}

inline s32 X86TruncateToInt32(f32 value) {
    if (!(double(value) >= -2147483648.0 && double(value) < 2147483648.0))
        return std::numeric_limits<s32>::min();
    return static_cast<s32>(value);
}

}  // namespace fut::numeric
