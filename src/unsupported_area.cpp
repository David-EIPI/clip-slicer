#include "stl_slicer/unsupported_area.hpp"
#include <clipper2/clipper.h>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace stl_slicer {
namespace {

using Clipper2Lib::EndType;
using Clipper2Lib::FillRule;
using Clipper2Lib::JoinType;
using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::PolyPath64;
using Clipper2Lib::PolyTree64;

constexpr double coordinateScale = 100000.0;
constexpr double propagationTolerance = 0.001 * coordinateScale;

bool isClosed(const SlicePath &path) {
    return path.points.size() >= 4 &&
           squaredDistance(path.points.front(), path.points.back()) <= 1e-12;
}

std::int64_t scaledCoordinate(double coordinate) {
    constexpr double limit =
        double(std::numeric_limits<std::int64_t>::max()) / (coordinateScale * 2.0);
    if (!std::isfinite(coordinate) || std::abs(coordinate) > limit)
        throw std::invalid_argument("Slice coordinate is outside the supported Clipper range");
    return std::llround(coordinate * coordinateScale);
}

Paths64 layerPolygons(const SliceLayer &layer) {
    Paths64 result;
    result.reserve(layer.paths.size());
    for (const auto &path : layer.paths) {
        if (!isClosed(path))
            continue;

        Path64 polygon;
        polygon.reserve(path.points.size() - 1);
        for (std::size_t index = 0; index + 1 < path.points.size(); ++index) {
            const auto &point = path.points[index];
            polygon.emplace_back(scaledCoordinate(point.x), scaledCoordinate(point.y));
        }
        if (polygon.size() >= 3)
            result.push_back(std::move(polygon));
    }
    return Clipper2Lib::Union(result, FillRule::NonZero);
}

std::vector<SlicePath> slicePaths(const Paths64 &polygons) {
    std::vector<SlicePath> result;
    result.reserve(polygons.size());
    for (const auto &polygon : polygons) {
        if (polygon.size() < 3)
            continue;
        SlicePath path;
        path.type = Clipper2Lib::Area(polygon) >= 0.0 ? PathType::External : PathType::Internal;
        path.points.reserve(polygon.size() + 1);
        for (const Point64 &point : polygon)
            path.points.push_back(
                {double(point.x) / coordinateScale, double(point.y) / coordinateScale});
        path.points.push_back(path.points.front());
        result.push_back(std::move(path));
    }
    return result;
}

void appendSupportedComponents(const PolyPath64 &node,
                               const Paths64 &previousLayer,
                               const Paths64 &reachable,
                               Paths64 &supported) {
    if (node.Level() > 0 && !node.IsHole()) {
        Paths64 component{node.Polygon()};
        for (const auto &child : node)
            if (child->IsHole())
                component.push_back(child->Polygon());

        if (!Clipper2Lib::Intersect(component, previousLayer, FillRule::NonZero).empty()) {
            Paths64 portion = Clipper2Lib::Intersect(component, reachable, FillRule::NonZero);
            supported.insert(supported.end(),
                             std::make_move_iterator(portion.begin()),
                             std::make_move_iterator(portion.end()));
        }
    }
    for (const auto &child : node)
        appendSupportedComponents(*child, previousLayer, reachable, supported);
}

Paths64 supportedLayer(const Paths64 &complete, const Paths64 &previousLayer, double radius) {
    const Paths64 reachable = Clipper2Lib::InflatePaths(
        previousLayer, radius, JoinType::Round, EndType::Polygon, 2.0, propagationTolerance);
    PolyTree64 components;
    Clipper2Lib::BooleanOp(
        Clipper2Lib::ClipType::Union, FillRule::NonZero, complete, {}, components);
    Paths64 supported;
    appendSupportedComponents(components, previousLayer, reachable, supported);
    supported = Clipper2Lib::SimplifyPaths(supported, propagationTolerance);
    return Clipper2Lib::Union(supported, FillRule::NonZero);
}

} // namespace

UnsupportedAreaAnalyzer::UnsupportedAreaAnalyzer(UnsupportedAreaOptions options)
    : options_(options) {
    if (!std::isfinite(options_.criticalAngleDegrees) || options_.criticalAngleDegrees <= 0.0 ||
        options_.criticalAngleDegrees >= 90.0)
        throw std::invalid_argument("Critical support angle must be between 0 and 90 degrees");
}

UnsupportedAreaResult UnsupportedAreaAnalyzer::analyze(const SliceData &slices) const {
    UnsupportedAreaResult result;
    result.unsupported.sourceBounds = slices.sourceBounds;
    result.unsupported.thickness = slices.thickness;
    result.unsupported.layers.reserve(slices.layers.size());
    if (slices.layers.empty())
        return result;

    const double pi = std::acos(-1.0);
    const double tangent = std::tan(options_.criticalAngleDegrees * pi / 180.0);
    Paths64 previousLayer;

    for (std::size_t layerIndex = 0; layerIndex < slices.layers.size(); ++layerIndex) {
        const SliceLayer &layer = slices.layers[layerIndex];
        const Paths64 complete = layerPolygons(layer);
        Paths64 supported;
        Paths64 unsupported;

        if (layerIndex == 0) {
            supported = complete;
        } else {
            if (layer.z < slices.layers[layerIndex - 1].z)
                throw std::invalid_argument("Slice layers must be ordered by increasing height");
            const double dz = std::max(0.0, layer.z - slices.layers[layerIndex - 1].z);
            const double radius = dz * (1.0 + 1.0 / tangent) * coordinateScale;
            supported = supportedLayer(complete, previousLayer, radius);
            unsupported = Clipper2Lib::Difference(complete, supported, FillRule::NonZero);
        }

        SliceLayer unsupportedLayer;
        unsupportedLayer.z = layer.z;
        unsupportedLayer.paths = slicePaths(unsupported);
        result.unsupported.layers.push_back(std::move(unsupportedLayer));
        result.totalArea +=
            std::abs(Clipper2Lib::Area(unsupported)) / (coordinateScale * coordinateScale);
        previousLayer = complete;
    }
    return result;
}

} // namespace stl_slicer
