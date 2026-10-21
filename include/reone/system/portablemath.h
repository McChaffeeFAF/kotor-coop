/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cmath>

namespace reone {

/**
 * sin, cos and atan2 built from +, -, *, / and floor alone. Those are exactly
 * rounded by IEEE 754, so the results are bit-identical on every platform and
 * compiler (given no fused multiply-add contraction), unlike the C library's
 * transcendental functions which differ by an ulp between glibc, MSVC and
 * libc++. Simulation code uses these so a Linux host and a Windows client
 * agree on headings and positions. Accurate to about 1e-13 (double).
 */
namespace portable {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 1.57079632679489661923;
constexpr double kTwoPi = 6.28318530717958647692;

namespace detail {

// sin(x) for |x| <= pi/4: x * (1 - x^2/3! + x^4/5! - ...)
inline double sinKernel(double x) {
    constexpr double kCoeffs[] = {
        1.0, -1.0 / 6.0, 1.0 / 120.0, -1.0 / 5040.0, 1.0 / 362880.0,
        -1.0 / 39916800.0, 1.0 / 6227020800.0, -1.0 / 1307674368000.0};
    double x2 = x * x;
    double p = 0.0;
    for (int i = 7; i >= 0; --i) {
        p = kCoeffs[i] + x2 * p;
    }
    return x * p;
}

// cos(x) for |x| <= pi/4: 1 - x^2/2! + x^4/4! - ...
inline double cosKernel(double x) {
    constexpr double kCoeffs[] = {
        1.0, -1.0 / 2.0, 1.0 / 24.0, -1.0 / 720.0, 1.0 / 40320.0,
        -1.0 / 3628800.0, 1.0 / 479001600.0, -1.0 / 87178291200.0};
    double x2 = x * x;
    double p = 0.0;
    for (int i = 7; i >= 0; --i) {
        p = kCoeffs[i] + x2 * p;
    }
    return p;
}

// Quadrant reduction: x = q * pi/2 + r with |r| <= pi/4.
inline double reduce(double x, int &quadrant) {
    double q = std::floor(x / kHalfPi + 0.5);
    double r = x - q * kHalfPi;
    double m = q - std::floor(q / 4.0) * 4.0;
    if (!(m >= 0.0 && m < 4.0)) {
        m = 0.0; // a non-finite argument
    }
    quadrant = static_cast<int>(m);
    if (!(r >= -kHalfPi && r <= kHalfPi)) {
        // Past ~1e15 the reduction has no precision left (and the kernels would
        // overflow to inf, so tan = inf/inf = NaN): treat the remainder as 0.
        r = 0.0;
    }
    return r;
}

// atan(x) for |x| <= 1
inline double atanUnit(double x) {
    constexpr double kTan15 = 0.26794919243112270647;  // 2 - sqrt(3)
    constexpr double kSqrt3 = 1.73205080756887729353;
    constexpr double kPiOver6 = 0.52359877559829887308;
    double offset = 0.0;
    if (x > kTan15) {
        x = (x * kSqrt3 - 1.0) / (x + kSqrt3);
        offset = kPiOver6;
    }
    double x2 = x * x;
    double p = 0.0;
    for (int k = 41; k >= 1; k -= 2) {
        double term = 1.0 / static_cast<double>(k);
        p = ((((k / 2) % 2) == 0) ? term : -term) + x2 * p;
    }
    return offset + x * p;
}

} // namespace detail

inline double sin(double x) {
    int q;
    double r = detail::reduce(x, q);
    switch (q) {
    case 0: return detail::sinKernel(r);
    case 1: return detail::cosKernel(r);
    case 2: return -detail::sinKernel(r);
    default: return -detail::cosKernel(r);
    }
}

inline double cos(double x) {
    int q;
    double r = detail::reduce(x, q);
    switch (q) {
    case 0: return detail::cosKernel(r);
    case 1: return -detail::sinKernel(r);
    case 2: return -detail::cosKernel(r);
    default: return detail::sinKernel(r);
    }
}

inline double atan2(double y, double x) {
    if (x == 0.0 && y == 0.0) {
        return 0.0;
    }
    double ax = x < 0.0 ? -x : x;
    double ay = y < 0.0 ? -y : y;
    double angle = ay <= ax ? detail::atanUnit(ay / ax) : kHalfPi - detail::atanUnit(ax / ay);
    if (x < 0.0) {
        angle = kPi - angle;
    }
    return y < 0.0 ? -angle : angle;
}

inline float sinf(float x) { return static_cast<float>(sin(static_cast<double>(x))); }
inline float cosf(float x) { return static_cast<float>(cos(static_cast<double>(x))); }
inline float atan2f(float y, float x) { return static_cast<float>(atan2(static_cast<double>(y), static_cast<double>(x))); }

} // namespace portable

} // namespace reone
