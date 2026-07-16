#pragma once

#include "stl_slicer/slice.hpp"
#include <clipper2/clipper.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stl_slicer::slice_polygon {

constexpr double coordinateScale = 100000.0;

inline bool isClosed(const SlicePath &path) {
    return path.points.size() >= 4 &&
           squaredDistance(path.points.front(), path.points.back()) <= 1e-12;
}

inline std::int64_t scaledCoordinate(double coordinate) {
    constexpr double limit =
        double(std::numeric_limits<std::int64_t>::max()) / (coordinateScale * 2.0);
    if (!std::isfinite(coordinate) || std::abs(coordinate) > limit)
        throw std::invalid_argument("Slice coordinate is outside the supported Clipper range");
    return std::llround(coordinate * coordinateScale);
}

inline Clipper2Lib::Paths64 layerPolygons(const SliceLayer &layer) {
    Clipper2Lib::Paths64 result;
    result.reserve(layer.paths.size());
    for (const SlicePath &path : layer.paths) {
        if (!isClosed(path))
            continue;

        Clipper2Lib::Path64 polygon;
        polygon.reserve(path.points.size() - 1);
        for (std::size_t index = 0; index + 1 < path.points.size(); ++index) {
            const Vec2 &point = path.points[index];
            polygon.emplace_back(scaledCoordinate(point.x), scaledCoordinate(point.y));
        }
        if (polygon.size() >= 3)
            result.push_back(std::move(polygon));
    }
    return Clipper2Lib::Union(result, Clipper2Lib::FillRule::NonZero);
}

} // namespace stl_slicer::slice_polygon
