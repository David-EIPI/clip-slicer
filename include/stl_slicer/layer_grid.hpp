// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <algorithm>
#include <cmath>

namespace stl_slicer {

inline double firstBuildLayerAbove(double minimumZ,
                                   double layerThickness,
                                   double firstLayerOffset) {
    const double relativeIndex = (minimumZ - firstLayerOffset) / layerThickness;
    const long long layerIndex =
        std::max(0LL, static_cast<long long>(std::floor(relativeIndex + 1e-9)) + 1);
    return firstLayerOffset + static_cast<double>(layerIndex) * layerThickness;
}

} // namespace stl_slicer
