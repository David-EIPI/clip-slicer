// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/slice.hpp"
#include <algorithm>

namespace stl_slicer {

SliceData mergeSlices(const std::vector<std::reference_wrapper<const SliceData>> &inputs) {
    SliceData result;
    if (inputs.empty())
        return result;

    result.thickness = inputs.front().get().thickness;
    double materialVolume = 0.0;
    bool hasAllVolumes = true;
    std::size_t layerCount = 0;
    for (const SliceData &input : inputs) {
        layerCount += input.layers.size();
        if (input.materialVolume)
            materialVolume += *input.materialVolume;
        else
            hasAllVolumes = false;
        if (input.sourceBounds.valid()) {
            result.sourceBounds.include(input.sourceBounds.min);
            result.sourceBounds.include(input.sourceBounds.max);
        }
    }
    if (hasAllVolumes)
        result.materialVolume = materialVolume;
    result.layers.reserve(layerCount);
    for (const SliceData &input : inputs)
        result.layers.insert(result.layers.end(), input.layers.begin(), input.layers.end());
    std::stable_sort(result.layers.begin(),
                     result.layers.end(),
                     [](const SliceLayer &a, const SliceLayer &b) { return a.z < b.z; });
    return result;
}

} // namespace stl_slicer
