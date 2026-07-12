#include "stl_slicer/slicer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stl_slicer {
namespace {

struct Segment { Vec2 a; Vec2 b; };

struct EndpointCell {
    std::int64_t x;
    std::int64_t y;

    bool operator==(const EndpointCell& other) const { return x == other.x && y == other.y; }
};

struct EndpointCellHash {
    std::size_t operator()(const EndpointCell& cell) const {
        const std::uint64_t x = static_cast<std::uint64_t>(cell.x);
        const std::uint64_t y = static_cast<std::uint64_t>(cell.y);
        const std::uint64_t mixed = x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2));
        return static_cast<std::size_t>(mixed);
    }
};

bool near(const Vec2& a, const Vec2& b, double tolerance) {
    return squaredDistance(a, b) <= tolerance * tolerance;
}

class EndpointIndex {
public:
    EndpointIndex(const std::vector<Segment>& segments, double tolerance)
        : segments_(segments), tolerance_(tolerance), seen_(segments.size(), 0) {
        buckets_.reserve(segments.size() * 2);
        for (std::size_t i = 0; i < segments.size(); ++i) {
            buckets_[cell(segments[i].a)].push_back(i);
            buckets_[cell(segments[i].b)].push_back(i);
        }
    }

    std::size_t find(const Vec2& front, const Vec2& back, const std::vector<bool>& alive) {
        if (++generation_ == 0) {
            std::fill(seen_.begin(), seen_.end(), 0);
            ++generation_;
        }
        std::size_t best = segments_.size();
        visitNeighbors(front, front, back, alive, best);
        visitNeighbors(back, front, back, alive, best);
        return best;
    }

private:
    EndpointCell cell(const Vec2& point) const {
        const long double x = std::floor(static_cast<long double>(point.x) / tolerance_);
        const long double y = std::floor(static_cast<long double>(point.y) / tolerance_);
        constexpr long double low = static_cast<long double>(std::numeric_limits<std::int64_t>::min()) + 1;
        constexpr long double high = static_cast<long double>(std::numeric_limits<std::int64_t>::max()) - 1;
        if (x < low || x > high || y < low || y > high)
            throw std::runtime_error("Slice coordinate is too large for endpoint indexing");
        return {static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)};
    }

    void visitNeighbors(const Vec2& point, const Vec2& front, const Vec2& back,
                        const std::vector<bool>& alive, std::size_t& best) {
        const EndpointCell center = cell(point);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto bucket = buckets_.find({center.x + dx, center.y + dy});
                if (bucket == buckets_.end()) continue;
                for (const std::size_t index : bucket->second) {
                    if (!alive[index] || seen_[index] == generation_) continue;
                    seen_[index] = generation_;
                    const Segment& segment = segments_[index];
                    if ((near(back, segment.a, tolerance_) || near(back, segment.b, tolerance_) ||
                         near(front, segment.b, tolerance_) || near(front, segment.a, tolerance_)) &&
                        index < best)
                        best = index;
                }
            }
        }
    }

    const std::vector<Segment>& segments_;
    double tolerance_;
    std::unordered_map<EndpointCell, std::vector<std::size_t>, EndpointCellHash> buckets_;
    std::vector<std::uint64_t> seen_;
    std::uint64_t generation_ = 0;
};

Vec2 interpolate(const Vec3& a, const Vec3& b, double z) {
    const double t = (z - a.z) / (b.z - a.z);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
}

bool triangleSegment(const Triangle& triangle, double z, double tolerance, Segment& result) {
    Vec2 points[2];
    std::size_t pointCount = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3& a = triangle.vertices[i];
        const Vec3& b = triangle.vertices[(i + 1) % 3];
        // Vertices on the plane belong to the upper half-space. This half-open rule
        // makes a shared vertex contribute consistently and ignores coplanar faces.
        const bool aBelow = a.z < z;
        const bool bBelow = b.z < z;
        if (aBelow != bBelow) points[pointCount++] = interpolate(a, b, z);
    }
    if (pointCount != 2 || near(points[0], points[1], tolerance)) return false;
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
    if (segments.empty()) return paths;

    EndpointIndex endpointIndex(segments, tolerance);
    std::vector<bool> alive(segments.size(), true);
    std::size_t remaining = segments.size();
    std::size_t nextSeed = segments.size();
    while (remaining != 0) {
        do { --nextSeed; } while (!alive[nextSeed]);
        std::vector<Vec2> prepended;
        std::vector<Vec2> appended;
        appended.push_back(segments[nextSeed].a);
        appended.push_back(segments[nextSeed].b);
        alive[nextSeed] = false;
        --remaining;

        const auto front = [&]() -> const Vec2& {
            return prepended.empty() ? appended.front() : prepended.back();
        };
        while (!near(front(), appended.back(), tolerance)) {
            const std::size_t index = endpointIndex.find(front(), appended.back(), alive);
            if (index == segments.size()) break;
            const Segment& segment = segments[index];
            if (near(appended.back(), segment.a, tolerance)) {
                appended.push_back(segment.b);
            } else if (near(appended.back(), segment.b, tolerance)) {
                appended.push_back(segment.a);
            } else if (near(front(), segment.b, tolerance)) {
                prepended.push_back(segment.a);
            } else {
                prepended.push_back(segment.b);
            }
            alive[index] = false;
            --remaining;
        }

        SlicePath path;
        if (near(front(), appended.back(), tolerance)) {
            appended.back() = front();
            path.type = PathType::External;
        } else {
            path.type = PathType::Open;
        }
        path.points.reserve(prepended.size() + appended.size());
        path.points.insert(path.points.end(), prepended.rbegin(), prepended.rend());
        path.points.insert(path.points.end(), appended.begin(), appended.end());
        paths.push_back(std::move(path));
    }
    return paths;
}

void healOpenPaths(std::vector<SlicePath>& paths, double tolerance) {
    struct Match {
        std::size_t first = 0;
        std::size_t second = 0;
        bool firstAtFront = false;
        bool secondAtBack = false;
        double distanceSquared = std::numeric_limits<double>::infinity();
    };

    const double toleranceSquared = tolerance * tolerance;
    for (;;) {
        bool closedPath = false;
        for (auto& path : paths) {
            if (path.type == PathType::Open && path.points.size() > 2 &&
                squaredDistance(path.points.front(), path.points.back()) <= toleranceSquared) {
                path.points.back() = path.points.front();
                path.type = PathType::External;
                closedPath = true;
            }
        }

        Match best;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (paths[i].type != PathType::Open || paths[i].points.empty()) continue;
            for (std::size_t j = i + 1; j < paths.size(); ++j) {
                if (paths[j].type != PathType::Open || paths[j].points.empty()) continue;
                const Vec2 firstEnds[] = {paths[i].points.back(), paths[i].points.front()};
                const Vec2 secondEnds[] = {paths[j].points.front(), paths[j].points.back()};
                for (std::size_t a = 0; a < 2; ++a) {
                    for (std::size_t b = 0; b < 2; ++b) {
                        const double distance = squaredDistance(firstEnds[a], secondEnds[b]);
                        if (distance <= toleranceSquared && distance < best.distanceSquared)
                            best = {i, j, a == 1, b == 1, distance};
                    }
                }
            }
        }

        if (!std::isfinite(best.distanceSquared)) {
            if (!closedPath) break;
            continue;
        }

        auto& first = paths[best.first].points;
        auto& second = paths[best.second].points;
        if (best.firstAtFront) std::reverse(first.begin(), first.end());
        if (best.secondAtBack) std::reverse(second.begin(), second.end());
        first.insert(first.end(), std::next(second.begin()), second.end());
        paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(best.second));
    }
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
    if (!std::isfinite(options_.gapClosingToleranceMultiplier) ||
        options_.gapClosingToleranceMultiplier < 1.0)
        throw std::invalid_argument("Gap-closing tolerance multiplier must be at least one");
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
        healOpenPaths(layer.paths,
                      options_.joinTolerance * options_.gapClosingToleranceMultiplier);
        classifyClosedPaths(layer.paths);
        result.layers.push_back(std::move(layer));
    }
    return result;
}

} // namespace stl_slicer
