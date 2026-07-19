// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"
#include <array>
#include <cmath>

namespace stl_slicer {

class Mat4 {
  public:
    Mat4() : values_{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1} {}
    explicit Mat4(std::array<double, 16> values) : values_(values) {}

    static Mat4 translation(double x, double y, double z) {
        Mat4 result;
        result.values_[12] = x;
        result.values_[13] = y;
        result.values_[14] = z;
        return result;
    }
    static Mat4 scale(double value) {
        Mat4 result;
        result.values_[0] = value;
        result.values_[5] = value;
        result.values_[10] = value;
        return result;
    }
    static Mat4 rotation(double radians, Vec3 axis) {
        const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (length == 0.0)
            return {};
        axis.x /= length;
        axis.y /= length;
        axis.z /= length;
        const double c = std::cos(radians), s = std::sin(radians), t = 1.0 - c;
        return Mat4({t * axis.x * axis.x + c,
                     t * axis.x * axis.y + s * axis.z,
                     t * axis.x * axis.z - s * axis.y,
                     0,
                     t * axis.x * axis.y - s * axis.z,
                     t * axis.y * axis.y + c,
                     t * axis.y * axis.z + s * axis.x,
                     0,
                     t * axis.x * axis.z + s * axis.y,
                     t * axis.y * axis.z - s * axis.x,
                     t * axis.z * axis.z + c,
                     0,
                     0,
                     0,
                     0,
                     1});
    }

    const std::array<double, 16> &values() const {
        return values_;
    }
    Vec3 transformPoint(const Vec3 &point) const {
        return {values_[0] * point.x + values_[4] * point.y + values_[8] * point.z + values_[12],
                values_[1] * point.x + values_[5] * point.y + values_[9] * point.z + values_[13],
                values_[2] * point.x + values_[6] * point.y + values_[10] * point.z + values_[14]};
    }
    Vec3 transformVector(const Vec3 &vector) const {
        return {values_[0] * vector.x + values_[4] * vector.y + values_[8] * vector.z,
                values_[1] * vector.x + values_[5] * vector.y + values_[9] * vector.z,
                values_[2] * vector.x + values_[6] * vector.y + values_[10] * vector.z};
    }
    Mat4 operator*(const Mat4 &other) const {
        std::array<double, 16> result{};
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                for (int k = 0; k < 4; ++k)
                    result[column * 4 + row] +=
                        values_[k * 4 + row] * other.values_[column * 4 + k];
        return Mat4(result);
    }

  private:
    std::array<double, 16> values_;
};

inline Bounds3 transformedBounds(const Bounds3 &bounds, const Mat4 &transform) {
    Bounds3 result;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                result.include(transform.transformPoint({x ? bounds.max.x : bounds.min.x,
                                                         y ? bounds.max.y : bounds.min.y,
                                                         z ? bounds.max.z : bounds.min.z}));
    return result;
}

} // namespace stl_slicer
