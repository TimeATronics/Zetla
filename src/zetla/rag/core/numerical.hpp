#pragma once
#include "types.hpp"
#include <cmath>

namespace hgnfs::numerical {

inline float safe_arcosh(float x) {
    double xd = static_cast<double>((x < 1.0f + ARCCOSH_TOL) ? (1.0f + ARCCOSH_TOL) : x);
    return static_cast<float>(std::log(xd + std::sqrt(xd * xd - 1.0)));
}

inline float clamp_norm(float z_norm) {
    return (z_norm > COSH_SINH_CLIP) ? COSH_SINH_CLIP : z_norm;
}

}  // namespace hgnfs::numerical
