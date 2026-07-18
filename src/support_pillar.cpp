#include "stl_slicer/support_pillar.hpp"
#include "slice_polygon_utils.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stl_slicer {
namespace {

using Clipper2Lib::EndType;
using Clipper2Lib::JoinType;
using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::PointInPolygonResult;

constexpr double pi = 3.14159265358979323846;
constexpr double offsetArcTolerance = 0.001 * slice_polygon::coordinateScale;

bool cancelled(const std::atomic<bool> *cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

Vec3 add(const Vec3 &first, const Vec3 &second) {
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

Vec3 subtract(const Vec3 &first, const Vec3 &second) {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

Vec3 multiply(const Vec3 &value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(const Vec3 &first, const Vec3 &second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

Vec3 cross(const Vec3 &first, const Vec3 &second) {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

double length(const Vec3 &value) {
    return std::sqrt(dot(value, value));
}

Vec3 normalized(const Vec3 &value) {
    const double magnitude = length(value);
    return magnitude > 1e-15 ? multiply(value, 1.0 / magnitude) : Vec3{};
}

Vec3 rotateAroundAxis(const Vec3 &value, const Vec3 &axis, double angle) {
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return add(add(multiply(value, cosine), multiply(cross(axis, value), sine)),
               multiply(axis, dot(axis, value) * (1.0 - cosine)));
}

std::vector<Vec3> smoothCenterline(const std::vector<Vec3> &route,
                                   const Vec3 &lowerGuide,
                                   const Vec3 *tipCenter,
                                   const ExternalPillarOptions &options) {
    if (route.size() < 2)
        return route;

    std::vector<double> distances(route.size(), 0.0);
    for (std::size_t index = 1; index < route.size(); ++index)
        distances[index] =
            distances[index - 1] + length(subtract(route[index], route[index - 1]));
    if (distances.back() <= 1e-12)
        return route;

    std::vector<Vec3> extended;
    extended.reserve(route.size() + 2);
    extended.push_back(lowerGuide);
    extended.insert(extended.end(), route.begin(), route.end());
    if (tipCenter && length(subtract(*tipCenter, route.back())) > 1e-12)
        extended.push_back(*tipCenter);

    const bool hasTipGuide = extended.size() == route.size() + 2;
    const std::size_t finalCorner = extended.size() - 2;
    std::vector<Vec3> result;
    result.reserve(route.size() + options.circumferencePoints * 2);
    for (std::size_t corner = 1; corner <= finalCorner; ++corner) {
        const Vec3 incoming = subtract(extended[corner], extended[corner - 1]);
        const Vec3 outgoing = subtract(extended[corner + 1], extended[corner]);
        const double incomingLength = length(incoming);
        const double outgoingLength = length(outgoing);
        const Vec3 incomingDirection = normalized(incoming);
        const Vec3 outgoingDirection = normalized(outgoing);
        const double cosine = std::clamp(dot(incomingDirection, outgoingDirection), -1.0, 1.0);
        const double angle = std::acos(cosine);
        const std::size_t routeIndex = corner - 1;
        const double fraction = distances[routeIndex] / distances.back();
        const double desiredRadius =
            options.bottomRadius + (options.topRadius - options.bottomRadius) * fraction;
        const Vec3 axis = normalized(cross(incomingDirection, outgoingDirection));
        const double incomingLimit = corner == 1 ? 0.95 : 0.45;
        const double outgoingLimit =
            hasTipGuide && corner == finalCorner ? 0.95 : 0.45;
        const bool terminalJoint = corner == 1 ||
                                   (hasTipGuide && corner == finalCorner);
        const double tangentScale = std::tan(angle * 0.5);
        const double maximumTangentDistance =
            std::min(incomingLimit * incomingLength, outgoingLimit * outgoingLength);
        const double radius = terminalJoint && tangentScale > 1e-12
                                  ? std::min(desiredRadius,
                                             maximumTangentDistance / tangentScale)
                                  : desiredRadius;
        const double tangentDistance = radius * tangentScale;
        const double fitTolerance =
            1e-12 * std::max({1.0, incomingLength, outgoingLength});
        const bool canSmooth = angle > 1e-4 && angle < pi - 1e-4 && length(axis) > 1e-12 &&
                               radius > 1e-9 &&
                               tangentDistance <= incomingLimit * incomingLength + fitTolerance &&
                               tangentDistance <= outgoingLimit * outgoingLength + fitTolerance;
        if (!canSmooth) {
            result.push_back(extended[corner]);
            continue;
        }

        const Vec3 arcStart =
            subtract(extended[corner], multiply(incomingDirection, tangentDistance));
        const Vec3 arcEnd = add(extended[corner],
                                multiply(outgoingDirection, tangentDistance));
        const Vec3 centerDirection = normalized(subtract(outgoingDirection, incomingDirection));
        const Vec3 arcCenter =
            add(extended[corner], multiply(centerDirection, radius / std::cos(angle * 0.5)));
        const Vec3 startRadius = subtract(arcStart, arcCenter);
        result.push_back(arcStart);
        const std::size_t arcSegments = std::max<std::size_t>(
            2,
            static_cast<std::size_t>(std::ceil(
                static_cast<double>(options.circumferencePoints) * angle / (2.0 * pi))));
        for (std::size_t segment = 1; segment < arcSegments; ++segment) {
            const double segmentAngle =
                angle * static_cast<double>(segment) / static_cast<double>(arcSegments);
            result.push_back(add(arcCenter,
                                 rotateAroundAxis(startRadius, axis, segmentAngle)));
        }
        result.push_back(arcEnd);
    }
    if (!hasTipGuide)
        result.push_back(route.back());
    return result;
}

void addTriangle(TriangleMesh &mesh, const Vec3 &first, const Vec3 &second, const Vec3 &third) {
    Triangle triangle;
    triangle.vertices = {first, second, third};
    triangle.normal = normalized(cross(subtract(second, first), subtract(third, first)));
    mesh.addTriangle(std::move(triangle));
}

bool appendPillarTube(TriangleMesh &mesh,
                      std::vector<Vec3> centers,
                      const Vec3 &lowerGuide,
                      const Vec3 *upperGuide,
                      const ExternalPillarOptions &options,
                      const std::vector<Vec3> &unitCircle,
                      const std::atomic<bool> *cancel) {
    centers = smoothCenterline(centers, lowerGuide, upperGuide, options);
    if (centers.size() < 2)
        return false;

    std::vector<double> distances(centers.size(), 0.0);
    for (std::size_t index = 1; index < centers.size(); ++index)
        distances[index] =
            distances[index - 1] + length(subtract(centers[index], centers[index - 1]));
    if (distances.back() <= 1e-12)
        return false;

    const std::size_t pointCount = options.circumferencePoints;
    std::vector<Vec3> rings(centers.size() * pointCount);
    Vec3 previousFirstDirection;
    for (std::size_t centerIndex = 0; centerIndex < centers.size(); ++centerIndex) {
        if (cancelled(cancel))
            return false;
        Vec3 tangent;
        if (centerIndex == 0)
            tangent = subtract(centers[1], centers[0]);
        else if (centerIndex + 1 == centers.size())
            tangent = subtract(centers.back(), centers[centers.size() - 2]);
        else
            tangent = subtract(centers[centerIndex + 1], centers[centerIndex - 1]);
        tangent = normalized(tangent);
        Vec3 firstDirection;
        if (centerIndex > 0) {
            firstDirection = normalized(subtract(
                previousFirstDirection,
                multiply(tangent, dot(previousFirstDirection, tangent))));
        }
        if (length(firstDirection) <= 1e-12) {
            const Vec3 helper =
                std::abs(tangent.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
            firstDirection = normalized(cross(helper, tangent));
        }
        previousFirstDirection = firstDirection;
        const Vec3 secondDirection = normalized(cross(tangent, firstDirection));
        const double fraction = distances[centerIndex] / distances.back();
        const double radius =
            options.bottomRadius + (options.topRadius - options.bottomRadius) * fraction;
        for (std::size_t point = 0; point < pointCount; ++point) {
            const Vec3 radial = add(multiply(firstDirection, unitCircle[point].x),
                                    multiply(secondDirection, unitCircle[point].y));
            rings[centerIndex * pointCount + point] =
                add(centers[centerIndex], multiply(radial, radius));
        }
    }

    for (std::size_t ring = 0; ring + 1 < centers.size(); ++ring) {
        for (std::size_t point = 0; point < pointCount; ++point) {
            const std::size_t next = (point + 1) % pointCount;
            addTriangle(mesh,
                        rings[ring * pointCount + point],
                        rings[ring * pointCount + next],
                        rings[(ring + 1) * pointCount + next]);
            addTriangle(mesh,
                        rings[ring * pointCount + point],
                        rings[(ring + 1) * pointCount + next],
                        rings[(ring + 1) * pointCount + point]);
        }
    }
    for (std::size_t point = 0; point < pointCount; ++point) {
        const std::size_t next = (point + 1) % pointCount;
        addTriangle(mesh, centers.front(), rings[next], rings[point]);
        const std::size_t finalRing = (centers.size() - 1) * pointCount;
        addTriangle(mesh,
                    centers.back(),
                    rings[finalRing + point],
                    rings[finalRing + next]);
    }
    return true;
}

void validateOptions(const ExternalPillarOptions &options) {
    const auto positiveFinite = [](double value) { return std::isfinite(value) && value > 0.0; };
    if (!positiveFinite(options.latticeCellSize))
        throw std::invalid_argument("Support lattice cell size must be positive and finite");
    if (!std::isfinite(options.modelIsolation) || options.modelIsolation < 0.0)
        throw std::invalid_argument("Model isolation must be non-negative and finite");
    if (!std::isfinite(options.minimumSupportAngleDegrees) ||
        options.minimumSupportAngleDegrees < 5.0 || options.minimumSupportAngleDegrees >= 90.0)
        throw std::invalid_argument("Minimum support angle must be from 5 to 90 degrees");
    if (!positiveFinite(options.baseHeight) || !positiveFinite(options.baseRadius) ||
        !positiveFinite(options.bottomRadius) || !positiveFinite(options.topRadius))
        throw std::invalid_argument("External pillar dimensions must be positive and finite");
    if (options.circumferencePoints < 3 || options.circumferencePoints > 1024)
        throw std::invalid_argument("Pillar circumference point count must be 3 to 1024");
}

struct BitGrid {
    BitGrid() = default;

    BitGrid(std::size_t width, std::size_t height)
        : width(width), height(height), words((width * height + 63) / 64) {}

    bool test(std::size_t x, std::size_t y) const {
        const std::size_t index = y * width + x;
        return (words[index / 64] & (std::uint64_t{1} << (index % 64))) != 0;
    }

    void set(std::size_t x, std::size_t y) {
        const std::size_t index = y * width + x;
        words[index / 64] |= std::uint64_t{1} << (index % 64);
    }

    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint64_t> words;
};

struct ClearanceLayer {
    double z = 0.0;
    Paths64 polygons;
    BitGrid blocked;
    Paths64 transitionPolygons;
    BitGrid transitionBlocked;
};

struct GridOffset {
    int x = 0;
    int y = 0;
};

bool polygonsContain(const Paths64 &polygons, const Point64 &point) {
    bool inside = false;
    for (const Path64 &polygon : polygons) {
        const PointInPolygonResult result = Clipper2Lib::PointInPolygon(point, polygon);
        if (result == PointInPolygonResult::IsOn)
            return true;
        if (result == PointInPolygonResult::IsInside)
            inside = !inside;
    }
    return inside;
}

} // namespace

struct ExternalPillarSpace::Impl {
    Impl(std::shared_ptr<const SliceData> sourceSlices,
         Bounds3 sourceBounds,
         double requestedMaximumHeight,
         ExternalPillarOptions pillarOptions,
         const std::atomic<bool> *cancel)
        : slices(std::move(sourceSlices)), bounds(sourceBounds), options(pillarOptions) {
        validateOptions(options);
        if (!slices || !bounds.valid())
            throw std::invalid_argument("External pillar space requires slices and model bounds");
        if (!std::isfinite(requestedMaximumHeight))
            throw std::invalid_argument("External pillar maximum height must be finite");
        if (cancelled(cancel))
            return;

        maximumHeight = std::max(options.baseHeight, requestedMaximumHeight);
        const double band = (options.baseRadius + options.modelIsolation) * 2.0;
        originX =
            std::floor((bounds.min.x - band) / options.latticeCellSize) * options.latticeCellSize;
        originY =
            std::floor((bounds.min.y - band) / options.latticeCellSize) * options.latticeCellSize;
        const double maximumX =
            std::ceil((bounds.max.x + band) / options.latticeCellSize) * options.latticeCellSize;
        const double maximumY =
            std::ceil((bounds.max.y + band) / options.latticeCellSize) * options.latticeCellSize;
        width =
            static_cast<std::size_t>(std::llround((maximumX - originX) / options.latticeCellSize)) +
            1;
        height =
            static_cast<std::size_t>(std::llround((maximumY - originY) / options.latticeCellSize)) +
            1;
        levelCount = static_cast<std::size_t>(std::ceil((maximumHeight - options.baseHeight) /
                                                        options.latticeCellSize)) +
                     1;

        if (width == 0 || height == 0 || levelCount == 0 ||
            width > std::numeric_limits<std::size_t>::max() / height ||
            width * height > std::numeric_limits<std::size_t>::max() / levelCount)
            throw std::length_error("Support lattice dimensions overflow addressable memory");
        cellCountPerLevel = width * height;
        const std::size_t totalCells = cellCountPerLevel * levelCount;
        buildOffsets();
        compactPredecessors = offsets.size() < std::numeric_limits<std::uint8_t>::max();
        const std::size_t predecessorSize =
            compactPredecessors ? sizeof(std::uint8_t) : sizeof(std::uint16_t);
        constexpr std::size_t maximumPredecessorBytes = 512ULL * 1024ULL * 1024ULL;
        if (totalCells > maximumPredecessorBytes / predecessorSize)
            throw std::length_error(
                "Support lattice exceeds 512 MiB; increase the lattice cell size");
        if (compactPredecessors)
            predecessors8.assign(totalCells, 0);
        else
            predecessors16.assign(totalCells, 0);
        buildClearanceLayers(cancel);
        if (cancelled(cancel))
            return;
        propagate(cancel);
        complete = !cancelled(cancel);
    }

    double levelZ(std::size_t level) const {
        return options.baseHeight + static_cast<double>(level) * options.latticeCellSize;
    }

    double clearanceRadius(double z) const {
        if (options.topRadius >= options.bottomRadius)
            return options.topRadius;
        const double denominator =
            std::max(options.latticeCellSize, maximumHeight - options.baseHeight);
        const double fraction = std::clamp((z - options.baseHeight) / denominator, 0.0, 1.0);
        return options.bottomRadius + (options.topRadius - options.bottomRadius) * fraction;
    }

    Paths64 expandedLayer(const SliceLayer &layer, double radius, double isolation) const {
        const Paths64 polygons = slice_polygon::layerPolygons(layer);
        if (polygons.empty())
            return {};
        const double gridGuard = options.latticeCellSize * std::sqrt(2.0) * 0.5;
        return Clipper2Lib::InflatePaths(polygons,
                                         (radius + isolation + gridGuard) *
                                             slice_polygon::coordinateScale,
                                         JoinType::Round,
                                         EndType::Polygon,
                                         2.0,
                                         offsetArcTolerance);
    }

    void buildOffsets() {
        const double angle = options.minimumSupportAngleDegrees * pi / 180.0;
        horizontalCellsPerLevel = 1.0 / std::tan(angle);
        const int extent = static_cast<int>(std::ceil(horizontalCellsPerLevel));
        for (int y = -extent; y <= extent; ++y) {
            for (int x = -extent; x <= extent; ++x) {
                if (std::hypot(static_cast<double>(x), static_cast<double>(y)) <=
                    horizontalCellsPerLevel + 1e-12)
                    offsets.push_back({x, y});
            }
        }
        std::sort(
            offsets.begin(), offsets.end(), [](const GridOffset &first, const GridOffset &second) {
                const int firstDistance = first.x * first.x + first.y * first.y;
                const int secondDistance = second.x * second.x + second.y * second.y;
                if (firstDistance != secondDistance)
                    return firstDistance < secondDistance;
                if (first.y != second.y)
                    return first.y < second.y;
                return first.x < second.x;
            });
        if (offsets.size() >= std::numeric_limits<std::uint16_t>::max())
            throw std::length_error("Minimum support angle produces too many lattice neighbors");
    }

    std::uint16_t predecessor(std::size_t index) const {
        return compactPredecessors ? predecessors8[index] : predecessors16[index];
    }

    void setPredecessor(std::size_t index, std::uint16_t value) {
        if (compactPredecessors)
            predecessors8[index] = static_cast<std::uint8_t>(value);
        else
            predecessors16[index] = value;
    }

    void buildClearanceLayers(const std::atomic<bool> *cancel) {
        clearanceLayers.reserve(slices->layers.size());
        for (const SliceLayer &layer : slices->layers) {
            if (cancelled(cancel))
                return;
            if (layer.z < 0.0 || layer.z > maximumHeight + options.latticeCellSize)
                continue;
            ClearanceLayer clearance;
            clearance.z = layer.z;
            const double radius = clearanceRadius(layer.z);
            clearance.polygons = expandedLayer(layer, radius, options.modelIsolation);
            clearance.blocked = rasterize(clearance.polygons);
            if (options.modelIsolation > 0.0) {
                clearance.transitionPolygons = expandedLayer(layer, radius, 0.0);
                clearance.transitionBlocked = rasterize(clearance.transitionPolygons);
            }
            clearanceLayers.push_back(std::move(clearance));
        }
    }

    BitGrid rasterize(const Paths64 &polygons) const {
        BitGrid result(width, height);
        if (polygons.empty())
            return result;

        std::vector<std::vector<double>> crossings(height);
        for (const Path64 &polygon : polygons) {
            if (polygon.size() < 3)
                continue;
            for (std::size_t index = 0; index < polygon.size(); ++index) {
                const Point64 &first = polygon[index];
                const Point64 &second = polygon[(index + 1) % polygon.size()];
                const double firstX = double(first.x) / slice_polygon::coordinateScale;
                const double firstY = double(first.y) / slice_polygon::coordinateScale;
                const double secondX = double(second.x) / slice_polygon::coordinateScale;
                const double secondY = double(second.y) / slice_polygon::coordinateScale;
                if (firstY == secondY)
                    continue;
                const double lowerY = std::min(firstY, secondY);
                const double upperY = std::max(firstY, secondY);
                const long long firstRow =
                    std::max<long long>(0,
                                        static_cast<long long>(std::ceil((lowerY - originY) /
                                                                         options.latticeCellSize)));
                const long long finalRow =
                    std::min<long long>(static_cast<long long>(height) - 1,
                                        static_cast<long long>(std::ceil((upperY - originY) /
                                                                         options.latticeCellSize)) -
                                            1);
                for (long long row = firstRow; row <= finalRow; ++row) {
                    const double y = originY + static_cast<double>(row) * options.latticeCellSize;
                    const double parameter = (y - firstY) / (secondY - firstY);
                    crossings[static_cast<std::size_t>(row)].push_back(firstX + (secondX - firstX) *
                                                                                    parameter);
                }
            }
        }

        for (std::size_t y = 0; y < height; ++y) {
            std::vector<double> &row = crossings[y];
            std::sort(row.begin(), row.end());
            for (std::size_t crossing = 0; crossing + 1 < row.size(); crossing += 2) {
                const long long firstColumn =
                    std::max<long long>(0,
                                        static_cast<long long>(std::ceil((row[crossing] - originX) /
                                                                         options.latticeCellSize)));
                const long long finalColumn = std::min<long long>(
                    static_cast<long long>(width) - 1,
                    static_cast<long long>(
                        std::floor((row[crossing + 1] - originX) / options.latticeCellSize)));
                for (long long x = firstColumn; x <= finalColumn; ++x)
                    result.set(static_cast<std::size_t>(x), y);
            }
        }
        return result;
    }

    bool segmentClear(const std::vector<const ClearanceLayer *> &obstacles,
                      std::size_t sourceX,
                      std::size_t sourceY,
                      std::size_t targetX,
                      std::size_t targetY,
                      double sourceZ,
                      double targetZ) const {
        for (const ClearanceLayer *obstacle : obstacles) {
            const double fraction =
                std::clamp((obstacle->z - sourceZ) / (targetZ - sourceZ), 0.0, 1.0);
            const long long x = std::llround(
                static_cast<double>(sourceX) +
                (static_cast<double>(targetX) - static_cast<double>(sourceX)) * fraction);
            const long long y = std::llround(
                static_cast<double>(sourceY) +
                (static_cast<double>(targetY) - static_cast<double>(sourceY)) * fraction);
            if (x >= 0 && y >= 0 && x < static_cast<long long>(width) &&
                y < static_cast<long long>(height) &&
                obstacle->blocked.test(static_cast<std::size_t>(x), static_cast<std::size_t>(y)))
                return false;
        }
        return true;
    }

    void initializeBase(const std::atomic<bool> *cancel) {
        BitGrid blocked(width, height);
        for (const SliceLayer &layer : slices->layers) {
            if (cancelled(cancel))
                return;
            if (layer.z < 0.0 || layer.z > options.baseHeight + 1e-12)
                continue;
            const BitGrid layerBlocked =
                rasterize(expandedLayer(layer, options.baseRadius, options.modelIsolation));
            for (std::size_t word = 0; word < blocked.words.size(); ++word)
                blocked.words[word] |= layerBlocked.words[word];
        }
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                if (!blocked.test(x, y))
                    setPredecessor(y * width + x, 1);
            }
        }
    }

    void propagate(const std::atomic<bool> *cancel) {
        initializeBase(cancel);
        std::size_t firstClearance = 0;
        while (firstClearance < clearanceLayers.size() &&
               clearanceLayers[firstClearance].z <= options.baseHeight + 1e-12)
            ++firstClearance;

        for (std::size_t level = 1; level < levelCount; ++level) {
            if (cancelled(cancel))
                return;
            const double sourceZ = levelZ(level - 1);
            const double targetZ = levelZ(level);
            std::vector<const ClearanceLayer *> obstacles;
            while (firstClearance < clearanceLayers.size() &&
                   clearanceLayers[firstClearance].z <= targetZ + 1e-12) {
                if (clearanceLayers[firstClearance].z > sourceZ + 1e-12)
                    obstacles.push_back(&clearanceLayers[firstClearance]);
                ++firstClearance;
            }

            const std::size_t previousOffset = (level - 1) * cellCountPerLevel;
            const std::size_t currentOffset = level * cellCountPerLevel;
            for (std::size_t y = 0; y < height; ++y) {
                if (cancelled(cancel))
                    return;
                for (std::size_t x = 0; x < width; ++x) {
                    for (std::size_t offsetIndex = 0; offsetIndex < offsets.size(); ++offsetIndex) {
                        const long long sourceX =
                            static_cast<long long>(x) + offsets[offsetIndex].x;
                        const long long sourceY =
                            static_cast<long long>(y) + offsets[offsetIndex].y;
                        if (sourceX < 0 || sourceY < 0 ||
                            sourceX >= static_cast<long long>(width) ||
                            sourceY >= static_cast<long long>(height))
                            continue;
                        const std::size_t sourceIndex = previousOffset +
                                                        static_cast<std::size_t>(sourceY) * width +
                                                        static_cast<std::size_t>(sourceX);
                        if (predecessor(sourceIndex) == 0)
                            continue;
                        if (!segmentClear(obstacles,
                                          static_cast<std::size_t>(sourceX),
                                          static_cast<std::size_t>(sourceY),
                                          x,
                                          y,
                                          sourceZ,
                                          targetZ))
                            continue;
                        setPredecessor(currentOffset + y * width + x,
                                       static_cast<std::uint16_t>(offsetIndex + 1));
                        break;
                    }
                }
            }
        }
    }

    bool exactSegmentClear(const Vec3 &first,
                           const Vec3 &second,
                           bool tipTransition) const {
        if (second.z <= first.z)
            return false;
        auto layer = std::upper_bound(
            clearanceLayers.begin(),
            clearanceLayers.end(),
            first.z + 1e-12,
            [](double z, const ClearanceLayer &candidate) { return z < candidate.z; });
        for (; layer != clearanceLayers.end() && layer->z <= second.z + 1e-12; ++layer) {
            const double fraction = (layer->z - first.z) / (second.z - first.z);
            const Vec3 point = add(first, multiply(subtract(second, first), fraction));
            const Point64 scaled{slice_polygon::scaledCoordinate(point.x),
                                 slice_polygon::scaledCoordinate(point.y)};
            const Paths64 &polygons =
                tipTransition ? layer->transitionPolygons : layer->polygons;
            if (polygonsContain(polygons, scaled))
                return false;
        }
        return true;
    }

    std::vector<Vec3> simplifyRoute(std::vector<Vec3> route,
                                    const std::atomic<bool> *cancel) const {
        if (route.size() <= 2 || cancelled(cancel))
            return route;
        std::vector<Vec3> simplified;
        simplified.reserve(route.size());
        simplified.push_back(route.front());
        Vec3 previousDirection = normalized(subtract(route[1], route[0]));
        for (std::size_t index = 1; index + 1 < route.size(); ++index) {
            const Vec3 nextDirection = normalized(subtract(route[index + 1], route[index]));
            if (dot(previousDirection, nextDirection) < 1.0 - 1e-12) {
                simplified.push_back(route[index]);
                previousDirection = nextDirection;
            }
        }
        simplified.push_back(route.back());
        return cancelled(cancel) ? std::vector<Vec3>{} : simplified;
    }

    bool cellSegmentClear(std::size_t sourceX,
                          std::size_t sourceY,
                          std::size_t targetX,
                          std::size_t targetY,
                          double sourceZ,
                          double targetZ,
                          bool tipTransition) const {
        auto layer = std::upper_bound(
            clearanceLayers.begin(),
            clearanceLayers.end(),
            sourceZ + 1e-12,
            [](double z, const ClearanceLayer &candidate) { return z < candidate.z; });
        for (; layer != clearanceLayers.end() && layer->z <= targetZ + 1e-12; ++layer) {
            const double fraction = (layer->z - sourceZ) / (targetZ - sourceZ);
            const long long x = std::llround(
                static_cast<double>(sourceX) +
                (static_cast<double>(targetX) - static_cast<double>(sourceX)) * fraction);
            const long long y = std::llround(
                static_cast<double>(sourceY) +
                (static_cast<double>(targetY) - static_cast<double>(sourceY)) * fraction);
            const BitGrid &blocked = tipTransition ? layer->transitionBlocked : layer->blocked;
            if (x < 0 || y < 0 || x >= static_cast<long long>(width) ||
                y >= static_cast<long long>(height) ||
                blocked.test(static_cast<std::size_t>(x), static_cast<std::size_t>(y)))
                return false;
        }
        return true;
    }

    std::vector<Vec3> routeUsingClearance(const Vec3 &attachment,
                                          bool tipTransition,
                                          const std::atomic<bool> *cancel) const {
        if (!complete || cancelled(cancel) || attachment.z <= options.baseHeight)
            return {};
        const double tangent = std::tan(options.minimumSupportAngleDegrees * pi / 180.0);
        const std::size_t firstLevel =
            std::min(levelCount - 1,
                     static_cast<std::size_t>(std::floor((attachment.z - options.baseHeight) /
                                                         options.latticeCellSize)));

        struct ConnectorNode {
            std::size_t level = 0;
            std::size_t x = 0;
            std::size_t y = 0;
            std::size_t parent = std::numeric_limits<std::size_t>::max();
        };
        constexpr std::size_t noNode = std::numeric_limits<std::size_t>::max();
        std::vector<ConnectorNode> nodes;
        std::vector<std::size_t> frontier;

        auto addInitialCells = [&](std::size_t level, bool reachableOnly) {
            const double z = levelZ(level);
            const double horizontalLimit = (attachment.z - z) / tangent;
            if (horizontalLimit < 0.0)
                return;
            const long long minimumX = std::max<long long>(
                0,
                static_cast<long long>(std::ceil((attachment.x - horizontalLimit - originX) /
                                                 options.latticeCellSize)));
            const long long maximumX = std::min<long long>(
                static_cast<long long>(width) - 1,
                static_cast<long long>(std::floor((attachment.x + horizontalLimit - originX) /
                                                  options.latticeCellSize)));
            const long long minimumY = std::max<long long>(
                0,
                static_cast<long long>(std::ceil((attachment.y - horizontalLimit - originY) /
                                                 options.latticeCellSize)));
            const long long maximumY = std::min<long long>(
                static_cast<long long>(height) - 1,
                static_cast<long long>(std::floor((attachment.y + horizontalLimit - originY) /
                                                  options.latticeCellSize)));
            for (long long y = minimumY; y <= maximumY; ++y) {
                for (long long x = minimumX; x <= maximumX; ++x) {
                    const std::size_t cell =
                        static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (reachableOnly && predecessor(level * cellCountPerLevel + cell) == 0)
                        continue;
                    const Vec3 point{originX + static_cast<double>(x) * options.latticeCellSize,
                                     originY + static_cast<double>(y) * options.latticeCellSize,
                                     z};
                    const double horizontalDistance =
                        std::hypot(point.x - attachment.x, point.y - attachment.y);
                    if (horizontalDistance > horizontalLimit + 1e-12 ||
                        !exactSegmentClear(point, attachment, tipTransition))
                        continue;
                    nodes.push_back({level,
                                     static_cast<std::size_t>(x),
                                     static_cast<std::size_t>(y),
                                     noNode});
                    frontier.push_back(nodes.size() - 1);
                }
            }
        };

        std::size_t connectorLevel = firstLevel;
        addInitialCells(connectorLevel, false);
        if (frontier.empty() && connectorLevel > 0) {
            --connectorLevel;
            addInitialCells(connectorLevel, false);
        }
        if (frontier.empty())
            return {};

        std::size_t selectedNode = noNode;
        double selectedDistance = std::numeric_limits<double>::infinity();
        const auto selectReachableNode = [&](std::size_t firstNode) {
            for (std::size_t index = firstNode; index < frontier.size(); ++index) {
                const std::size_t nodeIndex = frontier[index];
                const ConnectorNode &node = nodes[nodeIndex];
                const double x = originX + static_cast<double>(node.x) * options.latticeCellSize;
                const double y = originY + static_cast<double>(node.y) * options.latticeCellSize;
                const double distance = std::hypot(x - attachment.x, y - attachment.y);
                if (predecessor(node.level * cellCountPerLevel + node.y * width + node.x) != 0 &&
                    distance < selectedDistance) {
                    selectedNode = nodeIndex;
                    selectedDistance = distance;
                }
            }
        };
        selectReachableNode(0);

        constexpr std::size_t directProbeLevels = 4;
        for (std::size_t probe = 1;
             selectedNode == noNode && probe <= directProbeLevels && probe <= connectorLevel;
             ++probe) {
            const std::size_t firstNode = frontier.size();
            const std::size_t savedNodeCount = nodes.size();
            addInitialCells(connectorLevel - probe, true);
            selectReachableNode(firstNode);
            if (selectedNode == noNode) {
                frontier.resize(firstNode);
                nodes.resize(savedNodeCount);
            }
        }

        std::vector<std::ptrdiff_t> nextNodeAtCell(cellCountPerLevel, -1);
        while (selectedNode == noNode && connectorLevel > 0 && !frontier.empty()) {
            if (cancelled(cancel))
                return {};
            std::vector<std::size_t> nextFrontier;
            const double sourceZ = levelZ(connectorLevel - 1);
            const double targetZ = levelZ(connectorLevel);
            for (std::size_t nodeIndex : frontier) {
                const ConnectorNode node = nodes[nodeIndex];
                for (const GridOffset &offset : offsets) {
                    const long long x = static_cast<long long>(node.x) + offset.x;
                    const long long y = static_cast<long long>(node.y) + offset.y;
                    if (x < 0 || y < 0 || x >= static_cast<long long>(width) ||
                        y >= static_cast<long long>(height))
                        continue;
                    const std::size_t cell =
                        static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (nextNodeAtCell[cell] >= 0 ||
                        !cellSegmentClear(static_cast<std::size_t>(x),
                                          static_cast<std::size_t>(y),
                                          node.x,
                                          node.y,
                                          sourceZ,
                                          targetZ,
                                          tipTransition))
                        continue;
                    nodes.push_back({connectorLevel - 1,
                                     static_cast<std::size_t>(x),
                                     static_cast<std::size_t>(y),
                                     nodeIndex});
                    const std::size_t newNode = nodes.size() - 1;
                    nextNodeAtCell[cell] = static_cast<std::ptrdiff_t>(newNode);
                    nextFrontier.push_back(newNode);
                    if (predecessor((connectorLevel - 1) * cellCountPerLevel + cell) != 0) {
                        selectedNode = newNode;
                        break;
                    }
                }
                if (selectedNode != noNode)
                    break;
            }
            for (std::size_t nodeIndex : nextFrontier) {
                const ConnectorNode &node = nodes[nodeIndex];
                nextNodeAtCell[node.y * width + node.x] = -1;
            }
            frontier = std::move(nextFrontier);
            --connectorLevel;
        }
        if (selectedNode == noNode)
            return {};

        std::size_t selectedLevel = nodes[selectedNode].level;
        std::size_t selectedX = nodes[selectedNode].x;
        std::size_t selectedY = nodes[selectedNode].y;
        std::vector<Vec3> descending;
        descending.push_back({originX + static_cast<double>(selectedX) * options.latticeCellSize,
                              originY + static_cast<double>(selectedY) * options.latticeCellSize,
                              levelZ(selectedLevel)});
        while (selectedLevel > 0) {
            const std::uint16_t code =
                predecessor(selectedLevel * cellCountPerLevel + selectedY * width + selectedX);
            if (code == 0 || code > offsets.size())
                return {};
            const GridOffset &offset = offsets[code - 1];
            const long long predecessorX = static_cast<long long>(selectedX) + offset.x;
            const long long predecessorY = static_cast<long long>(selectedY) + offset.y;
            if (predecessorX < 0 || predecessorY < 0 ||
                predecessorX >= static_cast<long long>(width) ||
                predecessorY >= static_cast<long long>(height))
                return {};
            --selectedLevel;
            selectedX = static_cast<std::size_t>(predecessorX);
            selectedY = static_cast<std::size_t>(predecessorY);
            descending.push_back(
                {originX + static_cast<double>(selectedX) * options.latticeCellSize,
                 originY + static_cast<double>(selectedY) * options.latticeCellSize,
                 levelZ(selectedLevel)});
        }

        std::vector<Vec3> route;
        route.reserve(descending.size() + nodes.size() + 1);
        for (auto iterator = descending.rbegin(); iterator != descending.rend(); ++iterator) {
            if (!route.empty()) {
                const Vec3 difference = subtract(*iterator, route.back());
                if (length(difference) <= 1e-12)
                    continue;
            }
            route.push_back(*iterator);
        }
        std::size_t connector = nodes[selectedNode].parent;
        while (connector != noNode) {
            const ConnectorNode &node = nodes[connector];
            route.push_back({originX + static_cast<double>(node.x) * options.latticeCellSize,
                             originY + static_cast<double>(node.y) * options.latticeCellSize,
                             levelZ(node.level)});
            connector = node.parent;
        }
        route.push_back(attachment);
        return simplifyRoute(std::move(route), cancel);
    }

    std::vector<Vec3> route(const Vec3 &attachment,
                            bool allowTipTransition,
                            const std::atomic<bool> *cancel) const {
        std::vector<Vec3> result = routeUsingClearance(attachment, false, cancel);
        if (!result.empty() || !allowTipTransition || options.modelIsolation <= 0.0 ||
            cancelled(cancel))
            return result;

        // A contact tip necessarily begins inside the requested isolation envelope.
        // Let its connector cross that envelope with physical pillar clearance, then
        // join the globally reachable lattice where full model isolation is enforced.
        return routeUsingClearance(attachment, true, cancel);
    }

    std::shared_ptr<const SliceData> slices;
    Bounds3 bounds;
    ExternalPillarOptions options;
    double maximumHeight = 0.0;
    double originX = 0.0;
    double originY = 0.0;
    double horizontalCellsPerLevel = 0.0;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t levelCount = 0;
    std::size_t cellCountPerLevel = 0;
    std::vector<GridOffset> offsets;
    std::vector<std::uint8_t> predecessors8;
    std::vector<std::uint16_t> predecessors16;
    std::vector<ClearanceLayer> clearanceLayers;
    bool compactPredecessors = true;
    bool complete = false;
};

ExternalPillarSpace::ExternalPillarSpace(std::shared_ptr<const SliceData> slices,
                                         Bounds3 modelBounds,
                                         double maximumHeight,
                                         ExternalPillarOptions options,
                                         const std::atomic<bool> *cancel)
    : impl_(
          std::make_shared<Impl>(std::move(slices), modelBounds, maximumHeight, options, cancel)) {}

std::vector<Vec3> ExternalPillarSpace::route(const Vec3 &attachment,
                                             const std::atomic<bool> *cancel) const {
    return impl_->route(attachment, false, cancel);
}

std::vector<Vec3> ExternalPillarSpace::routeFromTip(
    const Vec3 &attachment,
    const std::atomic<bool> *cancel) const {
    return impl_->route(attachment, true, cancel);
}

bool ExternalPillarSpace::valid() const noexcept {
    return impl_ && impl_->complete;
}

ExternalPillarBuilder::ExternalPillarBuilder(std::shared_ptr<const ExternalPillarSpace> space,
                                             ExternalPillarOptions options)
    : space_(std::move(space)), options_(options) {
    validateOptions(options_);
    if (!space_)
        throw std::invalid_argument("External pillar builder requires a reachability space");
    unitCircle_.reserve(options_.circumferencePoints);
    for (std::size_t point = 0; point < options_.circumferencePoints; ++point) {
        const double angle =
            2.0 * pi * static_cast<double>(point) /
            static_cast<double>(options_.circumferencePoints);
        unitCircle_.push_back({std::cos(angle), std::sin(angle), 0.0});
    }
}

TriangleMesh ExternalPillarBuilder::build(const Vec3 &attachment,
                                          const std::atomic<bool> *cancel) const {
    return buildImpl(attachment, nullptr, cancel);
}

TriangleMesh ExternalPillarBuilder::build(const Vec3 &attachment,
                                          const Vec3 &tipCenter,
                                          const std::atomic<bool> *cancel) const {
    return buildImpl(attachment, &tipCenter, cancel);
}

TriangleMesh ExternalPillarBuilder::buildImpl(const Vec3 &attachment,
                                              const Vec3 *tipCenter,
                                              const std::atomic<bool> *cancel) const {
    TriangleMesh result;
    std::vector<Vec3> centers = tipCenter ? space_->routeFromTip(attachment, cancel)
                                          : space_->route(attachment, cancel);
    if (centers.size() < 2 || cancelled(cancel))
        return result;
    const std::size_t pointCount = options_.circumferencePoints;
    result.setHeader("CLIP Slicer external support pillar");
    const Vec3 baseBottom{centers.front().x, centers.front().y, 0.0};
    const Vec3 baseTop{centers.front().x, centers.front().y, options_.baseHeight};
    std::vector<Vec3> baseBottomRing;
    std::vector<Vec3> baseTopRing;
    baseBottomRing.reserve(pointCount);
    baseTopRing.reserve(pointCount);
    for (std::size_t point = 0; point < pointCount; ++point) {
        const Vec3 radial{options_.baseRadius * unitCircle_[point].x,
                          options_.baseRadius * unitCircle_[point].y,
                          0.0};
        baseBottomRing.push_back(add(baseBottom, radial));
        baseTopRing.push_back(add(baseTop, radial));
    }

    result.reserve(8 * pointCount);
    for (std::size_t point = 0; point < pointCount; ++point) {
        const std::size_t next = (point + 1) % pointCount;
        addTriangle(result, baseBottomRing[point], baseTopRing[next], baseTopRing[point]);
        addTriangle(result, baseBottomRing[point], baseBottomRing[next], baseTopRing[next]);
        addTriangle(result, baseBottom, baseBottomRing[next], baseBottomRing[point]);
        addTriangle(result, baseTop, baseTopRing[point], baseTopRing[next]);
    }
    if (!appendPillarTube(
            result, std::move(centers), baseBottom, tipCenter, options_, unitCircle_, cancel))
        return {};
    return result;
}

struct InternalPillarBuilder::Impl {
    struct Layer {
        double z = 0.0;
        Paths64 original;
        Paths64 expanded;
    };

    struct BoundaryPoint {
        Vec3 point;
        double distance = std::numeric_limits<double>::infinity();
        bool valid = false;
    };

    Impl(std::shared_ptr<const SliceData> sourceSlices,
         ExternalPillarOptions pillarOptions,
         SupportTipOptions contactTipOptions,
         const std::atomic<bool> *cancel)
        : slices(std::move(sourceSlices)),
          pillar(pillarOptions),
          tip(contactTipOptions) {
        validateOptions(pillar);
        if (!slices)
            throw std::invalid_argument("Internal pillar generation requires model slices");
        if (!std::isfinite(tip.topRadius) || tip.topRadius <= 0.0 ||
            !std::isfinite(tip.bottomRadius) || tip.bottomRadius <= 0.0 ||
            !std::isfinite(tip.height) || tip.height <= 0.0 ||
            tip.circumferencePoints < 3 || tip.circumferencePoints > 1024)
            throw std::invalid_argument("Internal support tip dimensions are invalid");

        const double angle = pillar.minimumSupportAngleDegrees * pi / 180.0;
        tangentMinimumAngle = std::tan(angle);
        cosineMinimumAngle = std::cos(angle);
        internalTube = pillar;
        internalTube.bottomRadius = tip.bottomRadius;
        internalTube.topRadius = tip.bottomRadius;
        buildCircle(pillar.circumferencePoints, pillarCircle);
        buildCircle(tip.circumferencePoints, tipCircle);

        const double clearance = tip.bottomRadius + pillar.modelIsolation;
        layers.reserve(slices->layers.size());
        for (const SliceLayer &slice : slices->layers) {
            if (cancelled(cancel))
                return;
            Layer layer;
            layer.z = slice.z;
            layer.original = slice_polygon::layerPolygons(slice);
            if (!layer.original.empty()) {
                layer.expanded = Clipper2Lib::InflatePaths(
                    layer.original,
                    clearance * slice_polygon::coordinateScale,
                    JoinType::Round,
                    EndType::Polygon,
                    2.0,
                    offsetArcTolerance);
            }
            layers.push_back(std::move(layer));
        }
        complete = true;
    }

    static void buildCircle(std::size_t pointCount, std::vector<Vec3> &circle) {
        circle.reserve(pointCount);
        for (std::size_t point = 0; point < pointCount; ++point) {
            const double angle =
                2.0 * pi * static_cast<double>(point) / static_cast<double>(pointCount);
            circle.push_back({std::cos(angle), std::sin(angle), 0.0});
        }
    }

    static Vec2 closestPointOnSegment(double x,
                                      double y,
                                      double firstX,
                                      double firstY,
                                      double secondX,
                                      double secondY) {
        const double dx = secondX - firstX;
        const double dy = secondY - firstY;
        const double denominator = dx * dx + dy * dy;
        const double parameter = denominator > 1e-18
                                     ? std::clamp(((x - firstX) * dx + (y - firstY) * dy) /
                                                      denominator,
                                                  0.0,
                                                  1.0)
                                     : 0.0;
        return {firstX + parameter * dx, firstY + parameter * dy};
    }

    BoundaryPoint nearestBoundary(const Layer &layer, double x, double y) const {
        BoundaryPoint result;
        for (const Path64 &polygon : layer.original) {
            for (std::size_t index = 0; index < polygon.size(); ++index) {
                const Point64 &first = polygon[index];
                const Point64 &second = polygon[(index + 1) % polygon.size()];
                const double firstX =
                    static_cast<double>(first.x) / slice_polygon::coordinateScale;
                const double firstY =
                    static_cast<double>(first.y) / slice_polygon::coordinateScale;
                const double secondX =
                    static_cast<double>(second.x) / slice_polygon::coordinateScale;
                const double secondY =
                    static_cast<double>(second.y) / slice_polygon::coordinateScale;
                const Vec2 point =
                    closestPointOnSegment(x, y, firstX, firstY, secondX, secondY);
                const double distance = std::hypot(point.x - x, point.y - y);
                if (distance < result.distance) {
                    result.point = {point.x, point.y, layer.z};
                    result.distance = distance;
                    result.valid = true;
                }
            }
        }
        return result;
    }

    BoundaryPoint findContact(double x, double y, double endpointZ) const {
        BoundaryPoint result;
        const double minimumZ = endpointZ - tip.height;
        for (auto iterator = layers.rbegin(); iterator != layers.rend(); ++iterator) {
            if (iterator->z >= endpointZ - 1e-12)
                continue;
            if (iterator->z < minimumZ - 1e-12)
                break;
            if (iterator->original.empty())
                continue;
            const double verticalDistance = endpointZ - iterator->z;
            const double coneRadius = verticalDistance / tangentMinimumAngle;
            BoundaryPoint candidate = nearestBoundary(*iterator, x, y);
            if (!candidate.valid || candidate.distance > coneRadius + 1e-9)
                continue;
            if (!result.valid || candidate.distance < result.distance - 1e-9 ||
                (std::abs(candidate.distance - result.distance) <= 1e-9 &&
                 candidate.point.z > result.point.z))
                result = candidate;
        }
        return result;
    }

    bool routeClear(const std::vector<Vec3> &route) const {
        if (route.size() < 2)
            return false;
        std::size_t segment = 0;
        for (const Layer &layer : layers) {
            if (layer.z <= route.front().z + 1e-9 ||
                layer.z >= route.back().z - 1e-9 || layer.expanded.empty())
                continue;
            while (segment + 1 < route.size() && route[segment + 1].z < layer.z - 1e-9)
                ++segment;
            if (segment + 1 >= route.size())
                break;
            const double dz = route[segment + 1].z - route[segment].z;
            if (dz <= 1e-12)
                return false;
            const double fraction = (layer.z - route[segment].z) / dz;
            const Vec3 point =
                add(route[segment], multiply(subtract(route[segment + 1], route[segment]),
                                             fraction));
            if (polygonsContain(layer.expanded,
                                {slice_polygon::scaledCoordinate(point.x),
                                 slice_polygon::scaledCoordinate(point.y)}))
                return false;
        }
        return true;
    }

    TriangleMesh buildContactTip(const Vec3 &contact,
                                 const Vec3 &attachment,
                                 const std::atomic<bool> *cancel) const {
        TriangleMesh result;
        const Vec3 axis = normalized(subtract(attachment, contact));
        if (length(axis) <= 1e-12)
            return result;
        const Vec3 helper =
            std::abs(axis.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
        const Vec3 firstDirection = normalized(cross(helper, axis));
        const Vec3 secondDirection = normalized(cross(axis, firstDirection));
        std::vector<Vec3> contactRing(tip.circumferencePoints);
        std::vector<Vec3> attachmentRing(tip.circumferencePoints);
        for (std::size_t point = 0; point < tip.circumferencePoints; ++point) {
            const Vec3 radial = add(multiply(firstDirection, tipCircle[point].x),
                                    multiply(secondDirection, tipCircle[point].y));
            contactRing[point] = add(contact, multiply(radial, tip.topRadius));
            attachmentRing[point] = add(attachment, multiply(radial, tip.bottomRadius));
        }
        result.reserve(tip.circumferencePoints * 4);
        for (std::size_t point = 0; point < tip.circumferencePoints; ++point) {
            if (cancelled(cancel))
                return {};
            const std::size_t next = (point + 1) % tip.circumferencePoints;
            addTriangle(result, contactRing[point], contactRing[next], attachmentRing[next]);
            addTriangle(result, contactRing[point], attachmentRing[next], attachmentRing[point]);
            addTriangle(result, contact, contactRing[next], contactRing[point]);
            addTriangle(result, attachment, attachmentRing[point], attachmentRing[next]);
        }
        return result;
    }

    InternalPillarResult makeSupport(const BoundaryPoint &candidate,
                                     const Vec3 &topAttachment,
                                     const Vec3 &topContact,
                                     const std::atomic<bool> *cancel) const {
        InternalPillarResult result;
        const double dx = topAttachment.x - candidate.point.x;
        const double dy = topAttachment.y - candidate.point.y;
        const double horizontalDistance = std::hypot(dx, dy);
        const double horizontalTipLength =
            std::min(horizontalDistance, tip.height * cosineMinimumAngle);
        const double horizontalScale =
            horizontalDistance > 1e-12 ? horizontalTipLength / horizontalDistance : 0.0;
        const double verticalTipLength =
            std::sqrt(std::max(0.0, tip.height * tip.height -
                                        horizontalTipLength * horizontalTipLength));
        const Vec3 baseAttachment{candidate.point.x + dx * horizontalScale,
                                  candidate.point.y + dy * horizontalScale,
                                  candidate.point.z + verticalTipLength};
        const double remainingHorizontal = horizontalDistance - horizontalTipLength;
        const Vec3 connection{topAttachment.x,
                              topAttachment.y,
                              baseAttachment.z + remainingHorizontal * tangentMinimumAngle};
        if (connection.z >= topAttachment.z - 1e-9 ||
            length(subtract(topAttachment, baseAttachment)) < tip.bottomRadius * 2.0)
            return result;

        std::vector<Vec3> route;
        route.reserve(3);
        route.push_back(baseAttachment);
        if (length(subtract(connection, baseAttachment)) > 1e-9)
            route.push_back(connection);
        route.push_back(topAttachment);
        if (!routeClear(route) || cancelled(cancel))
            return result;

        TriangleMesh baseTip = buildContactTip(candidate.point, baseAttachment, cancel);
        if (baseTip.triangles().empty() || cancelled(cancel))
            return result;
        result.mesh.setHeader("CLIP Slicer internal support");
        result.mesh.append(std::move(baseTip));
        if (!appendPillarTube(result.mesh,
                              std::move(route),
                              candidate.point,
                              &topContact,
                              internalTube,
                              pillarCircle,
                              cancel))
            return {};
        result.baseContact = candidate.point;
        return result;
    }

    InternalPillarResult build(const Vec3 &topAttachment,
                               const Vec3 &topContact,
                               const std::atomic<bool> *cancel) const {
        if (!complete || layers.empty() || cancelled(cancel) ||
            topAttachment.z <= layers.front().z)
            return {};
        for (auto endpoint = layers.rbegin(); endpoint != layers.rend(); ++endpoint) {
            if (cancelled(cancel))
                return {};
            if (endpoint->z >= topAttachment.z - 1e-9)
                continue;
            const BoundaryPoint candidate =
                findContact(topAttachment.x, topAttachment.y, endpoint->z);
            if (candidate.valid) {
                InternalPillarResult result =
                    makeSupport(candidate, topAttachment, topContact, cancel);
                if (result.valid())
                    return result;
            }
            if (!endpoint->expanded.empty() &&
                polygonsContain(endpoint->expanded,
                                {slice_polygon::scaledCoordinate(topAttachment.x),
                                 slice_polygon::scaledCoordinate(topAttachment.y)}))
                return {};
        }
        return {};
    }

    std::shared_ptr<const SliceData> slices;
    ExternalPillarOptions pillar;
    ExternalPillarOptions internalTube;
    SupportTipOptions tip;
    std::vector<Layer> layers;
    std::vector<Vec3> pillarCircle;
    std::vector<Vec3> tipCircle;
    double tangentMinimumAngle = 0.0;
    double cosineMinimumAngle = 0.0;
    bool complete = false;
};

InternalPillarBuilder::InternalPillarBuilder(std::shared_ptr<const SliceData> slices,
                                             ExternalPillarOptions pillarOptions,
                                             SupportTipOptions tipOptions,
                                             const std::atomic<bool> *cancel)
    : impl_(std::make_shared<Impl>(
          std::move(slices), pillarOptions, tipOptions, cancel)) {}

InternalPillarResult InternalPillarBuilder::build(const Vec3 &topAttachment,
                                                  const Vec3 &topContact,
                                                  const std::atomic<bool> *cancel) const {
    return impl_->build(topAttachment, topContact, cancel);
}

} // namespace stl_slicer
