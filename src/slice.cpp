#include "stl_slicer/slice.hpp"
#include <algorithm>

namespace stl_slicer {

SliceData mergeSlices(const std::vector<std::reference_wrapper<const SliceData>> &inputs) {
    SliceData result;
    if (inputs.empty())
        return result;

    result.thickness = inputs.front().get().thickness;
    std::size_t layerCount = 0;
    for (const SliceData &input : inputs) {
        layerCount += input.layers.size();
        if (input.sourceBounds.valid()) {
            result.sourceBounds.include(input.sourceBounds.min);
            result.sourceBounds.include(input.sourceBounds.max);
        }
    }
    result.layers.reserve(layerCount);
    for (const SliceData &input : inputs)
        result.layers.insert(result.layers.end(), input.layers.begin(), input.layers.end());
    std::stable_sort(result.layers.begin(),
                     result.layers.end(),
                     [](const SliceLayer &a, const SliceLayer &b) { return a.z < b.z; });
    return result;
}

} // namespace stl_slicer
