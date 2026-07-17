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

        for (const LatticeNode &node : nodes)
            if (!node.remove)
                contacts.push_back({{node.point.x, node.point.y, sourceLayer.z}, layerIndex});
    }
    return contacts;
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
    if (!kernels_.detectContactPoints)
        kernels_.detectContactPoints =
            [spacing = options_.supportSpacing](const SupportGenerationInput &input,
                                                std::size_t layerIndex,
                                                const std::atomic<bool> *cancel) {
                return detectContactPoints(input, layerIndex, spacing, cancel);
            };
}

SupportGenerationResult SupportGenerator::generate(const SupportGenerationInput &input,
                                                   const std::atomic<bool> *cancel) const {
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
        std::deque<SupportContactPoint> contactQueue;
        std::vector<SupportContactPoint> detectedContacts;
        std::vector<TriangleMesh> supportShells;
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
    if (shared.remainingLayers == 0)
        return result;

    SupportPillarBuilder buildPillar = kernels_.buildPillar;
    std::shared_future<std::shared_ptr<const ExternalPillarSpace>> externalSpace;
    if (!buildPillar) {
        SupportTipBuilder tipBuilder(input.sourceModel, options_.tip, cancel);
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
        externalSpace =
            std::async(spaceLaunch,
                       [slices = input.slices,
                        bounds = input.sourceModel->bounds(),
                        maximumUnsupportedHeight,
                        options = options_.externalPillar,
                        cancel]() {
                           return std::make_shared<const ExternalPillarSpace>(
                               slices, bounds, maximumUnsupportedHeight, options, cancel);
                       })
                .share();
        buildPillar = [tipBuilder = std::move(tipBuilder),
                       space = externalSpace,
                       options = options_.externalPillar](const SupportGenerationInput &,
                                                          const SupportContactPoint &contact,
                                                          const std::atomic<bool> *cancel) {
            SupportTipResult tip = tipBuilder.buildWithAttachment(contact.position, cancel);
            if (!tip.valid() || cancelled(cancel))
                return TriangleMesh{};
            TriangleMesh pillar =
                ExternalPillarBuilder(space.get(), options).build(tip.pillarAttachment, cancel);
            if (pillar.triangles().empty() || cancelled(cancel))
                return TriangleMesh{};

            TriangleMesh shell;
            shell.setHeader("CLIP Slicer external support");
            shell.reserve(tip.mesh.triangles().size() + pillar.triangles().size());
            for (const Triangle &triangle : pillar.triangles())
                shell.addTriangle(triangle);
            for (const Triangle &triangle : tip.mesh.triangles())
                shell.addTriangle(triangle);
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
                        kernels_.detectContactPoints(input, layerIndex, cancel);
                    {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.layers[layerIndex] = LayerState::Complete;
                        --shared.remainingLayers;
                        ++shared.processedLayers;
                        shared.detectedContacts.insert(
                            shared.detectedContacts.end(), contacts.begin(), contacts.end());
                        for (SupportContactPoint &contact : contacts)
                            shared.contactQueue.push_back(std::move(contact));
                    }
                    shared.workAvailable.notify_all();
                } else {
                    TriangleMesh shell = buildPillar(input, contactPoint, cancel);
                    if (!shell.triangles().empty()) {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.supportShells.push_back(std::move(shell));
                    }
                }
            } catch (...) {
                fail(std::current_exception());
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    const std::size_t generationWorkerCount = externalSpace.valid() && options_.workerCount > 1
                                                  ? options_.workerCount - 1
                                                  : options_.workerCount;
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
    result.cancelled = cancelled(cancel);
    result.processedLayerCount = shared.processedLayers;
    result.contactPoints = std::move(shared.detectedContacts);
    std::size_t triangleCount = 0;
    for (const TriangleMesh &shell : shared.supportShells)
        triangleCount += shell.triangles().size();
    result.supports.reserve(triangleCount);
    result.supports.setHeader("CLIP Slicer generated supports");
    for (const TriangleMesh &shell : shared.supportShells)
        for (const Triangle &triangle : shell.triangles())
            result.supports.addTriangle(triangle);
    return result;
}

} // namespace stl_slicer
