// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace stl_slicer {

enum class PathType : std::uint32_t { Internal = 0, External = 1, Open = 2 };

struct SlicePath {
    PathType type = PathType::Open;
    std::vector<Vec2> points;
};

struct SliceLayer {
    double z = 0.0;
    std::vector<SlicePath> paths;
};

struct SliceData {
    Bounds3 sourceBounds;
    double thickness = 0.1;
    std::vector<SliceLayer> layers;
};

SliceData mergeSlices(const std::vector<std::reference_wrapper<const SliceData>> &inputs);

} // namespace stl_slicer
