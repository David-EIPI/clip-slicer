#include "stl_slicer/slicer.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace stl_slicer {
namespace {

struct Segment { Vec2 a; Vec2 b; };

bool near(const Vec2& a, const Vec2& b, double tolerance) {
    return squaredDistance(a, b) <= tolerance * tolerance;
}

Vec2 interpolate(const Vec3& a, const Vec3& b, double z) {
    const double t = (z - a.z) / (b.z - a.z);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
}

bool triangleSegment(const Triangle& triangle, double z, double tolerance, Segment& result) {
    std::vector<Vec2> points;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3& a = triangle.vertices[i];
        const Vec3& b = triangle.vertices[(i + 1) % 3];
        // Vertices on the plane belong to the upper half-space. This half-open rule
        // makes a shared vertex contribute consistently and ignores coplanar faces.
        const bool aBelow = a.z < z;
        const bool bBelow = b.z < z;
        if (aBelow != bBelow) points.push_back(interpolate(a, b, z));
    }
    if (points.size() != 2 || near(points[0], points[1], tolerance)) return false;
    result = {points[0], points[1]};
    return true;
}

double signedArea(const std::vector<Vec2>& points) {
    double area = 0.0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        area += points[i].x * points[i + 1].y - points[i + 1].x * points[i].y;
    return area * 0.5;
}

bool pointInPolygon(const Vec2& point, const std::vector<Vec2>& polygon) {
    bool inside = false;
    const std::size_t count = polygon.size() - 1;
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

std::vector<SlicePath> connectSegments(std::vector<Segment> segments, double tolerance) {
    std::vector<SlicePath> paths;
    while (!segments.empty()) {
        SlicePath path;
        path.points.push_back(segments.back().a);
        path.points.push_back(segments.back().b);
        segments.pop_back();

        bool extended = true;
        while (extended && !near(path.points.front(), path.points.back(), tolerance)) {
            extended = false;
            for (auto it = segments.begin(); it != segments.end(); ++it) {
                if (near(path.points.back(), it->a, tolerance)) {
                    path.points.push_back(it->b);
                } else if (near(path.points.back(), it->b, tolerance)) {
                    path.points.push_back(it->a);
                } else if (near(path.points.front(), it->b, tolerance)) {
                    path.points.insert(path.points.begin(), it->a);
                } else if (near(path.points.front(), it->a, tolerance)) {
                    path.points.insert(path.points.begin(), it->b);
                } else {
                    continue;
                }
                segments.erase(it);
                extended = true;
                break;
            }
        }

        if (near(path.points.front(), path.points.back(), tolerance)) {
            path.points.back() = path.points.front();
            path.type = PathType::External;
        } else {
            path.type = PathType::Open;
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

void classifyClosedPaths(std::vector<SlicePath>& paths) {
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (paths[i].type == PathType::Open || paths[i].points.size() < 4) continue;
        std::size_t depth = 0;
        const Vec2 sample = paths[i].points.front();
        for (std::size_t j = 0; j < paths.size(); ++j) {
            if (i != j && paths[j].type != PathType::Open &&
                std::abs(signedArea(paths[j].points)) > std::abs(signedArea(paths[i].points)) &&
                pointInPolygon(sample, paths[j].points))
                ++depth;
        }
        const bool external = (depth % 2) == 0;
        paths[i].type = external ? PathType::External : PathType::Internal;
        const bool ccw = signedArea(paths[i].points) > 0.0;
        if (ccw != external) std::reverse(paths[i].points.begin(), paths[i].points.end());
    }
}

} // namespace

Slicer::Slicer(SlicerOptions options) : options_(options) {
    if (!std::isfinite(options_.layerThickness) || options_.layerThickness <= 0.0)
        throw std::invalid_argument("Layer thickness must be a positive finite value");
    if (!std::isfinite(options_.joinTolerance) || options_.joinTolerance <= 0.0)
        throw std::invalid_argument("Join tolerance must be a positive finite value");
}

SliceData Slicer::slice(const TriangleMesh& mesh) const {
    if (!mesh.bounds().valid()) throw std::invalid_argument("Cannot slice an empty mesh");
    SliceData result;
    result.sourceBounds = mesh.bounds();
    result.thickness = options_.layerThickness;

    const auto& triangles = mesh.triangles();
    std::vector<std::size_t> minZIndex(triangles.size());
    std::vector<std::size_t> maxZIndex(triangles.size());
    std::iota(minZIndex.begin(), minZIndex.end(), 0);
    std::iota(maxZIndex.begin(), maxZIndex.end(), 0);
    std::sort(minZIndex.begin(), minZIndex.end(), [&](std::size_t a, std::size_t b) {
        return triangles[a].minZ < triangles[b].minZ;
    });
    std::sort(maxZIndex.begin(), maxZIndex.end(), [&](std::size_t a, std::size_t b) {
        return triangles[a].maxZ < triangles[b].maxZ;
    });

    const std::size_t inactive = triangles.size();
    std::vector<std::size_t> active;
    std::vector<std::size_t> activePosition(triangles.size(), inactive);
    std::size_t minCursor = 0;
    std::size_t maxCursor = 0;

    const auto removeActive = [&](std::size_t triangleIndex) {
        const std::size_t position = activePosition[triangleIndex];
        if (position == inactive) return;
        const std::size_t moved = active.back();
        active[position] = moved;
        activePosition[moved] = position;
        active.pop_back();
        activePosition[triangleIndex] = inactive;
    };

    // Calculate z from an integer layer index to avoid accumulated floating-point drift.
    for (std::size_t index = 1;; ++index) {
        const double z = mesh.bounds().min.z + static_cast<double>(index) * options_.layerThickness;
        if (z > mesh.bounds().max.z + options_.joinTolerance) break;
        SliceLayer layer;
        layer.z = std::min(z, mesh.bounds().max.z);

        while (maxCursor < maxZIndex.size() && triangles[maxZIndex[maxCursor]].maxZ < layer.z) {
            removeActive(maxZIndex[maxCursor]);
            ++maxCursor;
        }
        while (minCursor < minZIndex.size() && triangles[minZIndex[minCursor]].minZ < layer.z) {
            const std::size_t triangleIndex = minZIndex[minCursor++];
            // A thin triangle may lie wholly between this and the preceding plane.
            if (triangles[triangleIndex].maxZ >= layer.z) {
                activePosition[triangleIndex] = active.size();
                active.push_back(triangleIndex);
            }
        }

        std::vector<Segment> segments;
        segments.reserve(active.size());
        std::vector<std::size_t> orderedActive = active;
        std::sort(orderedActive.begin(), orderedActive.end());
        for (const std::size_t triangleIndex : orderedActive) {
            Segment segment;
            if (triangleSegment(triangles[triangleIndex], layer.z, options_.joinTolerance, segment))
                segments.push_back(segment);
        }
        layer.paths = connectSegments(std::move(segments), options_.joinTolerance);
        classifyClosedPaths(layer.paths);
        result.layers.push_back(std::move(layer));
    }
    return result;
}

} // namespace stl_slicer
