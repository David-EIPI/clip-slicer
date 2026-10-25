// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/slice.hpp"
#include "slice_polygon_utils.hpp"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

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
    if (inputs.size() == 1)
        result.materialVolume = inputs.front().get().materialVolume;
    result.layers.reserve(layerCount);
    for (const SliceData &input : inputs)
        result.layers.insert(result.layers.end(), input.layers.begin(), input.layers.end());
    std::stable_sort(result.layers.begin(),
                     result.layers.end(),
                     [](const SliceLayer &a, const SliceLayer &b) { return a.z < b.z; });
    return result;
}

double printedMaterialVolume(const SliceData &data) {
    if (!std::isfinite(data.thickness) || data.thickness <= 0.0)
        throw std::invalid_argument("Slice thickness must be positive for material volume");

    struct Event {
        double z;
        std::size_t layerIndex;
        bool starts;
    };
    std::vector<Event> events;
    std::vector<Clipper2Lib::Paths64> polygons(data.layers.size());
    std::vector<double> areas(data.layers.size(), 0.0);
    events.reserve(data.layers.size() * 2);
    const double halfThickness = data.thickness * 0.5;
    const double areaScale = slice_polygon::coordinateScale * slice_polygon::coordinateScale;
    for (std::size_t index = 0; index < data.layers.size(); ++index) {
        const SliceLayer &layer = data.layers[index];
        if (!std::isfinite(layer.z))
            throw std::invalid_argument("Slice has a non-finite layer height");
        polygons[index] = slice_polygon::layerPolygons(layer);
        if (polygons[index].empty())
            continue;
        areas[index] = std::abs(Clipper2Lib::Area(polygons[index])) / areaScale;
        events.push_back({layer.z - halfThickness, index, true});
        events.push_back({layer.z + halfThickness, index, false});
    }
    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        return a.z < b.z;
    });

    // Integrate the union cross-section between consecutive slab boundaries.
    std::set<std::size_t> active;
    double volume = 0.0;
    std::size_t eventIndex = 0;
    while (eventIndex < events.size()) {
        const double bottom = events[eventIndex].z;
        while (eventIndex < events.size() && events[eventIndex].z == bottom) {
            const Event &event = events[eventIndex++];
            if (event.starts)
                active.insert(event.layerIndex);
            else
                active.erase(event.layerIndex);
        }
        if (eventIndex == events.size() || active.empty())
            continue;
        const double height = events[eventIndex].z - bottom;
        if (active.size() == 1) {
            volume += areas[*active.begin()] * height;
            continue;
        }
        Clipper2Lib::Paths64 combined;
        for (const std::size_t index : active) {
            const auto &layerPolygons = polygons[index];
            combined.insert(combined.end(), layerPolygons.begin(), layerPolygons.end());
        }
        const auto united = Clipper2Lib::Union(combined, Clipper2Lib::FillRule::NonZero);
        volume += std::abs(Clipper2Lib::Area(united)) / areaScale * height;
    }
    if (!std::isfinite(volume))
        throw std::overflow_error("Printed material volume is outside the supported range");
    return volume;
}

} // namespace stl_slicer
