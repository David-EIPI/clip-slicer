#include "stl_slicer/support_generator.hpp"
#include "slice_polygon_utils.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace stl_slicer {
namespace {

enum class LayerState { Skipped, Pending, Processing, Complete };

using Clipper2Lib::FillRule;
using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::PointInPolygonResult;
using Clipper2Lib::PolyPath64;
using Clipper2Lib::PolyTree64;

struct Segment64 {
    Point64 first;
    Point64 second;
};

struct EdgePiece {
    Vec2 first;
    Vec2 second;
};

struct LatticeNode {
    Vec2 point;
    bool remove = false;
    bool moved = false;
};

bool cancelled(const std::atomic<bool> *cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

bool samePoint(const Vec2 &first, const Vec2 &second) {
    constexpr double tolerance = 1.5 / slice_polygon::coordinateScale;
    return squaredDistance(first, second) <= tolerance * tolerance;
}

std::vector<Segment64> boundarySegments(const Paths64 &polygons) {
    std::vector<Segment64> result;
    std::size_t count = 0;
    for (const Path64 &polygon : polygons)
        count += polygon.size();
    result.reserve(count);
    for (const Path64 &polygon : polygons) {
        if (polygon.size() < 2)
            continue;
        for (std::size_t index = 0; index < polygon.size(); ++index)
            result.push_back({polygon[index], polygon[(index + 1) % polygon.size()]});
    }
    return result;
}

std::vector<std::pair<long double, long double>> overlapIntervals(
    const Point64 &first, const Point64 &second, const std::vector<Segment64> &sourceSegments) {
    const long double dx = static_cast<long double>(second.x) - first.x;
    const long double dy = static_cast<long double>(second.y) - first.y;
    const long double length = std::hypot(dx, dy);
    if (length == 0.0L)
        return {};

    std::vector<std::pair<long double, long double>> intervals;
    for (const Segment64 &source : sourceSegments) {
        const auto distanceFromLine = [&](const Point64 &point) {
            const long double px = static_cast<long double>(point.x) - first.x;
            const long double py = static_cast<long double>(point.y) - first.y;
            return std::abs(dx * py - dy * px) / length;
        };
        if (distanceFromLine(source.first) > 0.5L || distanceFromLine(source.second) > 0.5L)
            continue;

        const bool useX = std::abs(dx) >= std::abs(dy);
        const long double denominator = useX ? dx : dy;
        const long double firstProjection =
            useX ? (static_cast<long double>(source.first.x) - first.x) / denominator
                 : (static_cast<long double>(source.first.y) - first.y) / denominator;
        const long double secondProjection =
            useX ? (static_cast<long double>(source.second.x) - first.x) / denominator
                 : (static_cast<long double>(source.second.y) - first.y) / denominator;
        const long double begin = std::max(0.0L, std::min(firstProjection, secondProjection));
        const long double end = std::min(1.0L, std::max(firstProjection, secondProjection));
        if (end - begin > 1e-15L)
            intervals.emplace_back(begin, end);
    }

    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<long double, long double>> merged;
    for (const auto &interval : intervals) {
        if (merged.empty() || interval.first > merged.back().second + 1e-15L)
            merged.push_back(interval);
        else
            merged.back().second = std::max(merged.back().second, interval.second);
    }
    return merged;
}

std::vector<EdgePiece> outerEdgePieces(const Path64 &path,
                                       const std::vector<Segment64> &sourceSegments) {
    std::vector<EdgePiece> pieces;
    if (path.size() < 2)
        return pieces;
    for (std::size_t index = 0; index < path.size(); ++index) {
        const Point64 &first = path[index];
        const Point64 &second = path[(index + 1) % path.size()];
        const long double dx = static_cast<long double>(second.x) - first.x;
        const long double dy = static_cast<long double>(second.y) - first.y;
        for (const auto &interval : overlapIntervals(first, second, sourceSegments)) {
            const auto pointAt = [&](long double parameter) {
                return Vec2{double(static_cast<long double>(first.x) + dx * parameter) /
                                slice_polygon::coordinateScale,
                            double(static_cast<long double>(first.y) + dy * parameter) /
                                slice_polygon::coordinateScale};
            };
            pieces.push_back({pointAt(interval.first), pointAt(interval.second)});
        }
    }
    return pieces;
}

std::vector<std::vector<Vec2>> continuousRuns(const Path64 &path,
                                              const std::vector<Segment64> &sourceSegments) {
    const std::vector<EdgePiece> pieces = outerEdgePieces(path, sourceSegments);
    std::vector<std::vector<Vec2>> runs;
    for (const EdgePiece &piece : pieces) {
        if (runs.empty() || !samePoint(runs.back().back(), piece.first))
            runs.push_back({piece.first, piece.second});
        else
            runs.back().push_back(piece.second);
    }
    if (runs.size() > 1 && samePoint(runs.back().back(), runs.front().front())) {
        std::vector<Vec2> merged = std::move(runs.back());
        merged.insert(merged.end(), runs.front().begin() + 1, runs.front().end());
        runs.front() = std::move(merged);
        runs.pop_back();
    }
    return runs;
}

void collectSolidComponents(const PolyPath64 &node, std::vector<const PolyPath64 *> &components) {
    if (node.Level() > 0 && !node.IsHole())
        components.push_back(&node);
    for (const auto &child : node)
        collectSolidComponents(*child, components);
}

bool componentContains(const PolyPath64 &component, const Point64 &point) {
    if (Clipper2Lib::PointInPolygon(point, component.Polygon()) == PointInPolygonResult::IsOutside)
        return false;
    for (const auto &child : component)
        if (child->IsHole() &&
            Clipper2Lib::PointInPolygon(point, child->Polygon()) == PointInPolygonResult::IsInside)
            return false;
    return true;
}

Vec2 componentCenter(const PolyPath64 &component) {
    const Clipper2Lib::Rect64 bounds = Clipper2Lib::GetBounds(component.Polygon());
    const long double boundsCenterX =
        (static_cast<long double>(bounds.left) + bounds.right) * 0.5L;
    const long double boundsCenterY =
        (static_cast<long double>(bounds.top) + bounds.bottom) * 0.5L;

    std::vector<const Path64 *> boundaries{&component.Polygon()};
    for (const auto &child : component)
        if (child->IsHole())
            boundaries.push_back(&child->Polygon());

    long double areaSum = 0.0L;
    long double centerXSum = 0.0L;
    long double centerYSum = 0.0L;
    for (const Path64 *boundary : boundaries) {
        for (std::size_t index = 0; index < boundary->size(); ++index) {
            const Point64 &first = (*boundary)[index];
            const Point64 &second = (*boundary)[(index + 1) % boundary->size()];
            const long double cross = static_cast<long double>(first.x) * second.y -
                                      static_cast<long double>(second.x) * first.y;
            areaSum += cross;
            centerXSum += (static_cast<long double>(first.x) + second.x) * cross;
            centerYSum += (static_cast<long double>(first.y) + second.y) * cross;
        }
    }
    if (std::abs(areaSum) > 1e-18L) {
        const long double centerX = centerXSum / (3.0L * areaSum);
        const long double centerY = centerYSum / (3.0L * areaSum);
        const Point64 scaled{static_cast<std::int64_t>(std::llround(centerX)),
                             static_cast<std::int64_t>(std::llround(centerY))};
        if (componentContains(component, scaled))
            return {static_cast<double>(centerX / slice_polygon::coordinateScale),
                    static_cast<double>(centerY / slice_polygon::coordinateScale)};
    }

    std::vector<long double> crossings;
    for (const Path64 *boundary : boundaries) {
        for (std::size_t index = 0; index < boundary->size(); ++index) {
            const Point64 &first = (*boundary)[index];
            const Point64 &second = (*boundary)[(index + 1) % boundary->size()];
            if ((first.y <= boundsCenterY && second.y > boundsCenterY) ||
                (second.y <= boundsCenterY && first.y > boundsCenterY)) {
                const long double fraction =
                    (boundsCenterY - first.y) /
                    (static_cast<long double>(second.y) - first.y);
                crossings.push_back(static_cast<long double>(first.x) +
                                    (static_cast<long double>(second.x) - first.x) * fraction);
            }
        }
    }
    std::sort(crossings.begin(), crossings.end());
    long double selectedX = boundsCenterX;
    long double selectedDistance = std::numeric_limits<long double>::infinity();
    bool selected = false;
    for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
        if (crossings[index + 1] <= crossings[index])
            continue;
        const long double centerX = (crossings[index] + crossings[index + 1]) * 0.5L;
        const long double distance = std::abs(centerX - boundsCenterX);
        if (distance < selectedDistance) {
            selectedX = centerX;
            selectedDistance = distance;
            selected = true;
        }
    }
    if (selected)
        return {static_cast<double>(selectedX / slice_polygon::coordinateScale),
                static_cast<double>(boundsCenterY / slice_polygon::coordinateScale)};

    const Point64 &fallback = component.Polygon().front();
    return {double(fallback.x) / slice_polygon::coordinateScale,
            double(fallback.y) / slice_polygon::coordinateScale};
}

std::vector<LatticeNode>
buildLattice(const PolyPath64 &component, double supportSpacing, const std::atomic<bool> *cancel) {
    const Clipper2Lib::Rect64 bounds = Clipper2Lib::GetBounds(component.Polygon());
    const double minX = double(bounds.left) / slice_polygon::coordinateScale;
    const double minY = double(bounds.top) / slice_polygon::coordinateScale;
    const double maxX = double(bounds.right) / slice_polygon::coordinateScale;
    const double maxY = double(bounds.bottom) / slice_polygon::coordinateScale;
    const double rowStep = supportSpacing * std::sqrt(3.0) * 0.5;
    const std::int64_t firstRow = static_cast<std::int64_t>(std::floor(-supportSpacing / rowStep));
    const std::int64_t lastRow =
        static_cast<std::int64_t>(std::ceil((maxY - minY + supportSpacing) / rowStep));

    std::vector<LatticeNode> nodes;
    // The removable guard ring prevents edge samples near bbox corners from
    // exhausting the otherwise one-sided set of lattice candidates.
    for (std::int64_t row = firstRow; row <= lastRow; ++row) {
        if (cancelled(cancel))
            break;
        const double y = minY + static_cast<double>(row) * rowStep;
        const double rowStart = minX + (row % 2 == 0 ? 0.0 : supportSpacing * 0.5);
        const std::int64_t firstColumn = static_cast<std::int64_t>(
            std::ceil((minX - supportSpacing - rowStart) / supportSpacing));
        const std::int64_t lastColumn = static_cast<std::int64_t>(
            std::floor((maxX + supportSpacing - rowStart) / supportSpacing));
        for (std::int64_t column = firstColumn; column <= lastColumn; ++column) {
            const Vec2 point{rowStart + static_cast<double>(column) * supportSpacing, y};
            const Point64 scaled{slice_polygon::scaledCoordinate(point.x),
                                 slice_polygon::scaledCoordinate(point.y)};
            nodes.push_back({point, !componentContains(component, scaled), false});
        }
    }
    return nodes;
}

void moveNearestNodeToEdge(std::vector<LatticeNode> &nodes,
                           const Vec2 &edgePoint,
                           double supportSpacing) {
    const double maximumDistance = supportSpacing * supportSpacing * (1.0 + 1e-12);
    std::size_t nearest = nodes.size();
    double nearestDistance = maximumDistance;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].moved)
            continue;
        const double distance = squaredDistance(nodes[index].point, edgePoint);
        if (distance <= nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }
    if (nearest == nodes.size())
        return;
    nodes[nearest].point = edgePoint;
    nodes[nearest].remove = false;
    nodes[nearest].moved = true;
}

void placeNodesAlongRun(std::vector<LatticeNode> &nodes,
                        const std::vector<Vec2> &run,
                        double supportSpacing) {
    if (run.size() < 2)
        return;
    const bool closed = samePoint(run.front(), run.back());
    double runLength = 0.0;
    for (std::size_t index = 0; index + 1 < run.size(); ++index)
        runLength += std::hypot(run[index + 1].x - run[index].x, run[index + 1].y - run[index].y);

    double traversed = 0.0;
    double nextPlacement = 0.0;
    for (std::size_t index = 0; index + 1 < run.size(); ++index) {
        const Vec2 first = run[index];
        const Vec2 second = run[index + 1];
        const double dx = second.x - first.x;
        const double dy = second.y - first.y;
        const double length = std::hypot(dx, dy);
        if (length == 0.0)
            continue;
        while (nextPlacement <= traversed + length + 1e-12 &&
               (!closed || nextPlacement < runLength - 1e-12)) {
            const double alongSegment = std::max(0.0, nextPlacement - traversed);
            const double parameter = std::min(1.0, alongSegment / length);
            moveNearestNodeToEdge(
                nodes, {first.x + dx * parameter, first.y + dy * parameter}, supportSpacing);
            nextPlacement += supportSpacing;
        }
        traversed += length;
    }
}

std::vector<SupportContactPoint> detectContactPoints(const SupportGenerationInput &input,
                                                     std::size_t layerIndex,
                                                     double supportSpacing,
                                                     const std::atomic<bool> *cancel) {
    const SliceLayer &unsupportedLayer = input.unsupported->layers[layerIndex];
    const SliceLayer &sourceLayer = input.slices->layers[layerIndex];
    const Paths64 unsupportedPolygons = slice_polygon::layerPolygons(unsupportedLayer);
    const Paths64 sourcePolygons = slice_polygon::layerPolygons(sourceLayer);
    if (unsupportedPolygons.empty() || sourcePolygons.empty())
        return {};

    PolyTree64 tree;
    Clipper2Lib::BooleanOp(
        Clipper2Lib::ClipType::Union, FillRule::NonZero, unsupportedPolygons, {}, tree);
    std::vector<const PolyPath64 *> components;
    collectSolidComponents(tree, components);
    const std::vector<Segment64> sourceSegments = boundarySegments(sourcePolygons);

    std::vector<SupportContactPoint> contacts;
    for (const PolyPath64 *component : components) {
        if (cancelled(cancel))
            break;
        std::vector<LatticeNode> nodes = buildLattice(*component, supportSpacing, cancel);
        std::vector<const Path64 *> boundaries{&component->Polygon()};
        for (const auto &child : *component)
            if (child->IsHole())
                boundaries.push_back(&child->Polygon());
        for (const Path64 *boundary : boundaries)
            for (const std::vector<Vec2> &run : continuousRuns(*boundary, sourceSegments))
                placeNodesAlongRun(nodes, run, supportSpacing);

        const std::size_t firstComponentContact = contacts.size();
        for (const LatticeNode &node : nodes) {
            if (!node.remove) {
                contacts.push_back({{node.point.x, node.point.y, sourceLayer.z}, layerIndex});
            }
        }
        if (contacts.size() == firstComponentContact) {
            const Vec2 center = componentCenter(*component);
            contacts.push_back({{center.x, center.y, sourceLayer.z}, layerIndex});
        }
    }
    return contacts;
}

void ensureSupportableIslandContacts(const SupportGenerationInput &input,
                                     std::size_t layerIndex,
                                     std::vector<SupportContactPoint> &contacts,
                                     const SupportTipBuilder &tipBuilder,
                                     const std::atomic<bool> *cancel) {
    const Paths64 unsupportedPolygons =
        slice_polygon::layerPolygons(input.unsupported->layers[layerIndex]);
    if (unsupportedPolygons.empty())
        return;

    PolyTree64 tree;
    Clipper2Lib::BooleanOp(
        Clipper2Lib::ClipType::Union, FillRule::NonZero, unsupportedPolygons, {}, tree);
    std::vector<const PolyPath64 *> components;
    collectSolidComponents(tree, components);
    const double z = input.slices->layers[layerIndex].z;

    for (const PolyPath64 *component : components) {
        if (cancelled(cancel))
            return;
        bool hasValidTip = false;
        for (const SupportContactPoint &contact : contacts) {
            const Point64 point{slice_polygon::scaledCoordinate(contact.position.x),
                                slice_polygon::scaledCoordinate(contact.position.y)};
            if (componentContains(*component, point) &&
                tipBuilder.buildWithAttachment(contact.position, cancel).valid()) {
                hasValidTip = true;
                break;
            }
        }
        if (hasValidTip)
            continue;

        const Vec2 center = componentCenter(*component);
        const Clipper2Lib::Rect64 bounds = Clipper2Lib::GetBounds(component->Polygon());
        struct Candidate {
            Vec2 point;
            double distance = 0.0;
        };
        std::vector<Candidate> candidates;
        constexpr std::size_t divisions = 20;
        candidates.reserve((divisions + 1) * (divisions + 1) + 1);
        candidates.push_back({center, 0.0});
        for (std::size_t yIndex = 0; yIndex <= divisions; ++yIndex) {
            const long double y = static_cast<long double>(bounds.top) +
                                  (static_cast<long double>(bounds.bottom) - bounds.top) * yIndex /
                                      divisions;
            for (std::size_t xIndex = 0; xIndex <= divisions; ++xIndex) {
                const long double x =
                    static_cast<long double>(bounds.left) +
                    (static_cast<long double>(bounds.right) - bounds.left) * xIndex / divisions;
                const Point64 scaled{static_cast<std::int64_t>(std::llround(x)),
                                     static_cast<std::int64_t>(std::llround(y))};
                if (!componentContains(*component, scaled))
                    continue;
                const Vec2 point{double(scaled.x) / slice_polygon::coordinateScale,
                                 double(scaled.y) / slice_polygon::coordinateScale};
                candidates.push_back({point, squaredDistance(point, center)});
            }
        }
        std::sort(candidates.begin(),
                  candidates.end(),
                  [](const Candidate &first, const Candidate &second) {
                      if (first.distance != second.distance)
                          return first.distance < second.distance;
                      if (first.point.y != second.point.y)
                          return first.point.y < second.point.y;
                      return first.point.x < second.point.x;
                  });

        for (const Candidate &candidate : candidates) {
            if (cancelled(cancel))
                return;
            const bool duplicate =
                std::any_of(contacts.begin(), contacts.end(), [&](const SupportContactPoint &point) {
                    return samePoint(candidate.point, {point.position.x, point.position.y});
                });
            if (duplicate)
                continue;
            const Vec3 position{candidate.point.x, candidate.point.y, z};
            if (!tipBuilder.buildWithAttachment(position, cancel).valid())
                continue;
            contacts.push_back({position, layerIndex});
            break;
        }
    }
}

void validateInput(const SupportGenerationInput &input) {
    if (!input.sourceModel || !input.slices || !input.unsupported)
        throw std::invalid_argument(
            "Support generation requires model, slice, and unsupported data");
    if (input.slices->layers.size() != input.unsupported->layers.size())
        throw std::invalid_argument("Unsupported data must contain one layer for each model slice");
    for (std::size_t index = 0; index < input.slices->layers.size(); ++index)
        if (input.slices->layers[index].z != input.unsupported->layers[index].z)
            throw std::invalid_argument("Unsupported layers must align with model slice heights");
}

} // namespace

SupportGenerator::SupportGenerator(SupportGeneratorOptions options,
                                   SupportGenerationKernels kernels)
    : options_(options), kernels_(std::move(kernels)) {
    if (options_.workerCount == 0)
        throw std::invalid_argument("Support generation requires at least one worker");
    if (!std::isfinite(options_.supportSpacing) || options_.supportSpacing <= 0.0)
        throw std::invalid_argument("Support spacing must be a positive finite value");
}

SupportGenerationResult
SupportGenerator::generate(const SupportGenerationInput &input,
                           const std::atomic<bool> *cancel,
                           SupportGenerationProgressCallback progress) const {
    validateInput(input);
    SupportGenerationResult result;
    if (cancelled(cancel)) {
        result.cancelled = true;
        return result;
    }

    struct SharedState {
        std::mutex mutex;
        std::condition_variable workAvailable;
        std::vector<LayerState> layers;
        std::size_t nextLayer = 0;
        std::size_t remainingLayers = 0;
        std::size_t processedLayers = 0;
        std::size_t processedContacts = 0;
        std::deque<SupportContactPoint> contactQueue;
        std::vector<SupportContactPoint> detectedContacts;
        TriangleMesh supports;
        bool failed = false;
        std::exception_ptr error;
    } shared;

    shared.layers.reserve(input.unsupported->layers.size());
    for (const SliceLayer &layer : input.unsupported->layers) {
        const bool hasUnsupportedArea = !layer.paths.empty();
        shared.layers.push_back(hasUnsupportedArea ? LayerState::Pending : LayerState::Skipped);
        if (hasUnsupportedArea)
            ++shared.remainingLayers;
    }
    if (shared.remainingLayers == 0) {
        if (progress)
            progress({SupportGenerationPhase::ContactPoints, 0, 0, true});
        return result;
    }

    SupportContactDetector contactDetector = kernels_.detectContactPoints;
    if (!contactDetector) {
        contactDetector =
            [spacing = options_.supportSpacing](const SupportGenerationInput &input,
                                                std::size_t layerIndex,
                                                const std::atomic<bool> *cancel) {
                return detectContactPoints(input, layerIndex, spacing, cancel);
            };
    }
    SupportPillarBuilder buildPillar = kernels_.buildPillar;
    std::shared_future<std::shared_ptr<const ExternalPillarSpace>> externalSpace;
    struct DefaultBuilderState {
        std::once_flag initializeExternal;
        std::shared_ptr<const ExternalPillarBuilder> external;
        std::once_flag initializeInternal;
        std::shared_ptr<const InternalPillarBuilder> internal;
        std::mutex failureMutex;
        std::size_t internalFailures = 0;
        double lowestFailureZ = std::numeric_limits<double>::infinity();
    };
    std::shared_ptr<DefaultBuilderState> defaultBuilderState;
    if (!buildPillar) {
        auto tipBuilder =
            std::make_shared<const SupportTipBuilder>(input.sourceModel, options_.tip, cancel);
        if (cancelled(cancel)) {
            result.cancelled = true;
            return result;
        }
        double maximumUnsupportedHeight = options_.externalPillar.baseHeight;
        for (const SliceLayer &layer : input.unsupported->layers) {
            if (!layer.paths.empty())
                maximumUnsupportedHeight = std::max(maximumUnsupportedHeight, layer.z);
        }
        const std::launch spaceLaunch =
            options_.workerCount > 1 ? std::launch::async : std::launch::deferred;
        if (progress)
            progress({SupportGenerationPhase::VolumeSegmentation, 0, 0, false});
        externalSpace =
            std::async(spaceLaunch,
                       [slices = input.slices,
                        bounds = input.sourceModel->bounds(),
                        maximumUnsupportedHeight,
                        options = options_.externalPillar,
                        progress,
                        cancel]() {
                           auto space = std::make_shared<const ExternalPillarSpace>(
                               slices, bounds, maximumUnsupportedHeight, options, cancel);
                           if (progress)
                               progress({SupportGenerationPhase::VolumeSegmentation,
                                         0,
                                         0,
                                         true});
                           return space;
                       })
                .share();
        defaultBuilderState = std::make_shared<DefaultBuilderState>();
        contactDetector = [detector = std::move(contactDetector),
                           tipBuilder](const SupportGenerationInput &input,
                                       std::size_t layerIndex,
                                       const std::atomic<bool> *cancel) {
            std::vector<SupportContactPoint> contacts = detector(input, layerIndex, cancel);
            ensureSupportableIslandContacts(
                input, layerIndex, contacts, *tipBuilder, cancel);
            return contacts;
        };
        buildPillar = [tipBuilder,
                       space = externalSpace,
                       state = defaultBuilderState,
                       options = options_.externalPillar,
                       tipOptions = options_.tip](const SupportGenerationInput &input,
                                                  const SupportContactPoint &contact,
                                                  const std::atomic<bool> *cancel) {
            const std::shared_ptr<const ExternalPillarSpace> externalSpace = space.get();
            std::call_once(state->initializeExternal, [&]() {
                state->external =
                    std::make_shared<const ExternalPillarBuilder>(externalSpace, options);
            });

            const auto buildExternal = [&](SupportTipResult &candidate) {
                TriangleMesh shell;
                if (!candidate.valid())
                    return shell;
                TriangleMesh pillar = state->external->build(
                    candidate.pillarAttachment, contact.position, cancel);
                if (cancelled(cancel))
                    return TriangleMesh{};
                if (pillar.triangles().empty())
                    return shell;
                shell.setHeader("CLIP Slicer external support");
                shell.reserve(candidate.mesh.triangles().size() + pillar.triangles().size());
                shell.append(std::move(pillar));
                shell.append(std::move(candidate.mesh));
                return shell;
            };

            SupportTipResult tip = tipBuilder->buildWithAttachment(contact.position, cancel);
            TriangleMesh shell = buildExternal(tip);
            if (!shell.triangles().empty() || cancelled(cancel))
                return shell;

            Vec3 preferredDirection;
            bool hasPreferredDirection = false;
            if (tip.valid()) {
                preferredDirection = {tip.pillarAttachment.x - contact.position.x,
                                      tip.pillarAttachment.y - contact.position.y,
                                      tip.pillarAttachment.z - contact.position.z};
                const double preferredLength =
                    std::sqrt(preferredDirection.x * preferredDirection.x +
                              preferredDirection.y * preferredDirection.y +
                              preferredDirection.z * preferredDirection.z);
                if (preferredLength > 1e-12) {
                    preferredDirection.x /= preferredLength;
                    preferredDirection.y /= preferredLength;
                    preferredDirection.z /= preferredLength;
                    hasPreferredDirection = true;
                }
            }

            constexpr std::size_t maximumFallbackDirections = 64;
            const std::vector<Vec3> directions = externalSpace->reachableTipDirections(
                contact.position, tipOptions.height, maximumFallbackDirections, cancel);
            for (const Vec3 &direction : directions) {
                if (cancelled(cancel))
                    return TriangleMesh{};
                if (hasPreferredDirection) {
                    const double alignment = preferredDirection.x * direction.x +
                                             preferredDirection.y * direction.y +
                                             preferredDirection.z * direction.z;
                    if (alignment > 1.0 - 1e-6)
                        continue;
                }
                SupportTipResult candidate =
                    tipBuilder->buildWithAxis(contact.position, direction, cancel);
                if (!candidate.valid())
                    continue;
                shell = buildExternal(candidate);
                if (!shell.triangles().empty())
                    return shell;
                if (!tip.valid())
                    tip = std::move(candidate);
            }

            if (!tip.valid() || cancelled(cancel))
                return TriangleMesh{};
            std::call_once(state->initializeInternal, [&]() {
                state->internal = std::make_shared<const InternalPillarBuilder>(
                    input.slices, options, tipOptions, cancel);
            });
            InternalPillarResult internal = state->internal->build(
                tip.pillarAttachment, contact.position, cancel);
            if (cancelled(cancel))
                return TriangleMesh{};
            if (!internal.valid()) {
                std::lock_guard<std::mutex> lock(state->failureMutex);
                ++state->internalFailures;
                state->lowestFailureZ = std::min(state->lowestFailureZ, contact.position.z);
                return TriangleMesh{};
            }
            shell.setHeader("CLIP Slicer internal support");
            shell.reserve(tip.mesh.triangles().size() + internal.mesh.triangles().size());
            shell.append(std::move(internal.mesh));
            shell.append(std::move(tip.mesh));
            return shell;
        };
    }

    const auto fail = [&](std::exception_ptr error) {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (!shared.failed) {
                shared.failed = true;
                shared.error = std::move(error);
            }
        }
        shared.workAvailable.notify_all();
    };

    const auto worker = [&]() {
        for (;;) {
            enum class WorkKind { Layer, Contact };
            WorkKind workKind = WorkKind::Layer;
            std::size_t layerIndex = 0;
            SupportContactPoint contactPoint;

            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                shared.workAvailable.wait(lock, [&]() {
                    return shared.failed || cancelled(cancel) ||
                           shared.nextLayer < shared.layers.size() ||
                           !shared.contactQueue.empty() || shared.remainingLayers == 0;
                });
                if (shared.failed || cancelled(cancel))
                    return;

                while (shared.nextLayer < shared.layers.size() &&
                       shared.layers[shared.nextLayer] != LayerState::Pending)
                    ++shared.nextLayer;
                if (shared.nextLayer < shared.layers.size()) {
                    // Processing is the per-layer lock; geometry runs without holding the queue.
                    layerIndex = shared.nextLayer++;
                    shared.layers[layerIndex] = LayerState::Processing;
                    workKind = WorkKind::Layer;
                } else if (!shared.contactQueue.empty()) {
                    contactPoint = shared.contactQueue.front();
                    shared.contactQueue.pop_front();
                    workKind = WorkKind::Contact;
                } else if (shared.remainingLayers == 0) {
                    return;
                } else {
                    continue;
                }
            }

            try {
                if (workKind == WorkKind::Layer) {
                    std::vector<SupportContactPoint> contacts =
                        contactDetector(input, layerIndex, cancel);
                    std::size_t detectedCount = 0;
                    bool detectionComplete = false;
                    {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.layers[layerIndex] = LayerState::Complete;
                        --shared.remainingLayers;
                        ++shared.processedLayers;
                        shared.detectedContacts.insert(
                            shared.detectedContacts.end(), contacts.begin(), contacts.end());
                        for (SupportContactPoint &contact : contacts)
                            shared.contactQueue.push_back(std::move(contact));
                        detectedCount = shared.detectedContacts.size();
                        detectionComplete = shared.remainingLayers == 0;
                    }
                    if (progress) {
                        progress({SupportGenerationPhase::ContactPoints,
                                  detectedCount,
                                  detectionComplete ? detectedCount : 0,
                                  detectionComplete});
                        if (detectionComplete) {
                            progress({SupportGenerationPhase::GeneratingSupports,
                                      0,
                                      detectedCount,
                                      false});
                        }
                    }
                    shared.workAvailable.notify_all();
                } else {
                    TriangleMesh shell = buildPillar(input, contactPoint, cancel);
                    std::size_t processedContacts = 0;
                    std::size_t totalContacts = 0;
                    {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        if (!shell.triangles().empty())
                            shared.supports.append(std::move(shell));
                        processedContacts = ++shared.processedContacts;
                        totalContacts = shared.detectedContacts.size();
                    }
                    if (progress)
                        progress({SupportGenerationPhase::GeneratingSupports,
                                  processedContacts,
                                  totalContacts,
                                  false});
                }
            } catch (...) {
                fail(std::current_exception());
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    const std::size_t generationWorkerCount = options_.workerCount;
    workers.reserve(generationWorkerCount);
    try {
        for (std::size_t index = 0; index < generationWorkerCount; ++index)
            workers.emplace_back(worker);
    } catch (...) {
        fail(std::current_exception());
    }
    for (std::thread &thread : workers)
        thread.join();

    if (shared.error)
        std::rethrow_exception(shared.error);
    if (externalSpace.valid())
        (void)externalSpace.get();
    if (progress)
        progress({SupportGenerationPhase::GeneratingSupports,
                  shared.processedContacts,
                  shared.detectedContacts.size(),
                  true});
    result.cancelled = cancelled(cancel);
    result.processedLayerCount = shared.processedLayers;
    if (defaultBuilderState) {
        result.internalSupportFailureCount = defaultBuilderState->internalFailures;
        if (result.internalSupportFailureCount > 0)
            result.lowestInternalSupportFailureZ = defaultBuilderState->lowestFailureZ;
    }
    result.contactPoints = std::move(shared.detectedContacts);
    result.supports = std::move(shared.supports);
    result.supports.setHeader("CLIP Slicer generated supports");
    return result;
}

} // namespace stl_slicer
