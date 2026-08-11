// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/flat_facet.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stl_slicer {
namespace {

constexpr double normalEpsilon = 1e-15;

Vec3 subtract(const Vec3 &first, const Vec3 &second) {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

Vec3 negate(const Vec3 &value) {
    return {-value.x, -value.y, -value.z};
}

Vec3 cross(const Vec3 &first, const Vec3 &second) {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

double dot(const Vec3 &first, const Vec3 &second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

double length(const Vec3 &value) {
    return std::sqrt(dot(value, value));
}

Vec3 normalized(const Vec3 &value) {
    const double magnitude = length(value);
    return magnitude > normalEpsilon
               ? Vec3{value.x / magnitude, value.y / magnitude, value.z / magnitude}
               : Vec3{};
}

struct VertexKey {
    std::array<std::uint64_t, 3> coordinates{};

    bool operator==(const VertexKey &other) const noexcept {
        return coordinates == other.coordinates;
    }

    bool operator<(const VertexKey &other) const noexcept {
        return coordinates < other.coordinates;
    }
};

std::uint64_t coordinateBits(double value) {
    if (value == 0.0)
        value = 0.0;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "Unexpected double representation");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

VertexKey vertexKey(const Vec3 &vertex) {
    return {{coordinateBits(vertex.x), coordinateBits(vertex.y), coordinateBits(vertex.z)}};
}

struct EdgeKey {
    VertexKey first;
    VertexKey second;

    bool operator==(const EdgeKey &other) const noexcept {
        return first == other.first && second == other.second;
    }
};

EdgeKey edgeKey(const Vec3 &first, const Vec3 &second) {
    VertexKey a = vertexKey(first);
    VertexKey b = vertexKey(second);
    if (b < a)
        std::swap(a, b);
    return {a, b};
}

std::size_t combineHash(std::size_t seed, std::uint64_t value) {
    const std::size_t hash = std::hash<std::uint64_t>{}(value);
    return seed ^ (hash + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U));
}

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey &edge) const noexcept {
        std::size_t result = 0;
        for (const std::uint64_t coordinate : edge.first.coordinates)
            result = combineHash(result, coordinate);
        for (const std::uint64_t coordinate : edge.second.coordinates)
            result = combineHash(result, coordinate);
        return result;
    }
};

struct TriangleInfo {
    Vec3 normal;
    double area = 0.0;
    std::array<EdgeKey, 3> edges;
    bool valid = false;
};

struct FacetCandidate {
    FlatFacet facet;
    Vec3 normal;
    Vec3 normalSum;
    Vec3 seedNormal;
    std::vector<Vec3> memberNormals;
    double maximumSeedAngle = 0.0;
};

TriangleInfo triangleInfo(const Triangle &triangle) {
    TriangleInfo result;
    const Vec3 first = subtract(triangle.vertices[1], triangle.vertices[0]);
    const Vec3 second = subtract(triangle.vertices[2], triangle.vertices[0]);
    const Vec3 areaVector = cross(first, second);
    const double twiceArea = length(areaVector);
    if (!std::isfinite(twiceArea) || twiceArea <= normalEpsilon)
        return result;
    result.normal = {areaVector.x / twiceArea, areaVector.y / twiceArea, areaVector.z / twiceArea};
    result.area = twiceArea * 0.5;
    result.edges = {edgeKey(triangle.vertices[0], triangle.vertices[1]),
                    edgeKey(triangle.vertices[1], triangle.vertices[2]),
                    edgeKey(triangle.vertices[2], triangle.vertices[0])};
    result.valid = true;
    return result;
}

Vec3 alignedWith(const Vec3 &normal, const Vec3 &reference) {
    return dot(normal, reference) < 0.0 ? negate(normal) : normal;
}

bool withinFlatness(double normalDot, double tolerance) {
    return 1.0 - std::clamp(normalDot, -1.0, 1.0) <=
           tolerance + 8.0 * std::numeric_limits<double>::epsilon();
}

struct ProjectionRange {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();

    void include(double projection) {
        minimum = std::min(minimum, projection);
        maximum = std::max(maximum, projection);
    }

    void include(const ProjectionRange &other) {
        minimum = std::min(minimum, other.minimum);
        maximum = std::max(maximum, other.maximum);
    }

    double width() const {
        return maximum - minimum;
    }
};

ProjectionRange triangleProjectionRange(const Triangle &triangle, const Vec3 &normal) {
    ProjectionRange range;
    for (const Vec3 &vertex : triangle.vertices)
        range.include(dot(normal, vertex));
    return range;
}

ProjectionRange facetProjectionRange(const TriangleMesh &mesh,
                                     const std::vector<std::size_t> &indices,
                                     const Vec3 &normal) {
    ProjectionRange range;
    for (const std::size_t index : indices)
        range.include(triangleProjectionRange(mesh.triangles()[index], normal));
    return range;
}

struct NormalBin {
    std::array<std::int64_t, 3> coordinates{};

    bool operator==(const NormalBin &other) const noexcept {
        return coordinates == other.coordinates;
    }
};

struct NormalBinHash {
    std::size_t operator()(const NormalBin &bin) const noexcept {
        std::size_t result = 0;
        for (const std::int64_t coordinate : bin.coordinates)
            result = combineHash(result, static_cast<std::uint64_t>(coordinate));
        return result;
    }
};

NormalBin normalBin(const Vec3 &normal, double binWidth) {
    return {{{static_cast<std::int64_t>(std::floor(normal.x / binWidth)),
              static_cast<std::int64_t>(std::floor(normal.y / binWidth)),
              static_cast<std::int64_t>(std::floor(normal.z / binWidth))}}};
}

bool normalsCompatible(const FacetCandidate &merged,
                       const FacetCandidate &patch,
                       const FlatFacetOptions &options,
                       double maximumAngle,
                       double angleEpsilon,
                       double &candidateMaximumAngle) {
    candidateMaximumAngle = 0.0;
    for (const Vec3 &normal : patch.memberNormals) {
        const Vec3 aligned = alignedWith(normal, merged.seedNormal);
        const double seedDot = dot(merged.seedNormal, aligned);
        if (!withinFlatness(seedDot, options.flatnessTolerance))
            return false;
        candidateMaximumAngle = std::max(
            candidateMaximumAngle,
            std::acos(std::clamp(seedDot, -1.0, 1.0)));
    }
    if (candidateMaximumAngle + merged.maximumSeedAngle <= maximumAngle + angleEpsilon)
        return true;

    for (const Vec3 &candidateNormal : patch.memberNormals) {
        const Vec3 aligned = alignedWith(candidateNormal, merged.seedNormal);
        const bool compatible = std::all_of(
            merged.memberNormals.begin(),
            merged.memberNormals.end(),
            [&](const Vec3 &memberNormal) {
                return withinFlatness(dot(memberNormal, aligned), options.flatnessTolerance);
            });
        if (!compatible)
            return false;
    }
    return true;
}

void appendPatch(FacetCandidate &merged,
                 const FacetCandidate &patch,
                 const std::vector<TriangleInfo> &information,
                 double candidateMaximumAngle) {
    merged.facet.triangleIndices.insert(merged.facet.triangleIndices.end(),
                                        patch.facet.triangleIndices.begin(),
                                        patch.facet.triangleIndices.end());
    for (std::size_t index = 0; index < patch.memberNormals.size(); ++index) {
        const Vec3 aligned = alignedWith(patch.memberNormals[index], merged.seedNormal);
        merged.memberNormals.push_back(aligned);
        const double area = information[patch.facet.triangleIndices[index]].area;
        merged.normalSum.x += aligned.x * area;
        merged.normalSum.y += aligned.y * area;
        merged.normalSum.z += aligned.z * area;
    }
    merged.facet.area += patch.facet.area;
    merged.maximumSeedAngle = std::max(merged.maximumSeedAngle, candidateMaximumAngle);
    merged.normal = normalized(merged.normalSum);
}

std::vector<FacetCandidate> mergeCoplanarPatches(
    const TriangleMesh &mesh,
    const std::vector<TriangleInfo> &information,
    std::vector<FacetCandidate> patches,
    const FlatFacetOptions &options,
    double maximumAngle) {
    std::sort(patches.begin(), patches.end(), [](const auto &first, const auto &second) {
        if (first.facet.area != second.facet.area)
            return first.facet.area > second.facet.area;
        return first.facet.triangleIndices.front() < second.facet.triangleIndices.front();
    });
    const double angleEpsilon = 8.0 * std::numeric_limits<double>::epsilon();
    const double planeEpsilon = std::max(1e-12, options.coplanarTolerance * 1e-12);
    const double binWidth = std::sqrt(2.0 * options.flatnessTolerance);
    std::unordered_map<NormalBin, std::vector<std::size_t>, NormalBinHash> normalIndex;
    normalIndex.reserve(patches.size());
    std::vector<FacetCandidate> mergedPatches;
    std::vector<ProjectionRange> planeRanges;
    mergedPatches.reserve(patches.size());
    planeRanges.reserve(patches.size());

    for (FacetCandidate &patch : patches) {
        const NormalBin bin = normalBin(patch.seedNormal, binWidth);
        bool wasMerged = false;
        for (std::int64_t xOffset = -1; xOffset <= 1 && !wasMerged; ++xOffset) {
            for (std::int64_t yOffset = -1; yOffset <= 1 && !wasMerged; ++yOffset) {
                for (std::int64_t zOffset = -1; zOffset <= 1 && !wasMerged; ++zOffset) {
                    const NormalBin nearby{{{bin.coordinates[0] + xOffset,
                                             bin.coordinates[1] + yOffset,
                                             bin.coordinates[2] + zOffset}}};
                    const auto indexed = normalIndex.find(nearby);
                    if (indexed == normalIndex.end())
                        continue;
                    for (const std::size_t mergedIndex : indexed->second) {
                        FacetCandidate &merged = mergedPatches[mergedIndex];
                        double candidateMaximumAngle = 0.0;
                        if (!normalsCompatible(merged,
                                               patch,
                                               options,
                                               maximumAngle,
                                               angleEpsilon,
                                               candidateMaximumAngle))
                            continue;

                        const Vec3 &sample = mesh.triangles()[
                            patch.facet.triangleIndices.front()].vertices.front();
                        const double sampleProjection = dot(merged.seedNormal, sample);
                        const ProjectionRange &planeRange = planeRanges[mergedIndex];
                        if (sampleProjection <
                                planeRange.maximum - options.coplanarTolerance - planeEpsilon ||
                            sampleProjection >
                                planeRange.minimum + options.coplanarTolerance + planeEpsilon)
                            continue;

                        ProjectionRange combinedRange = planeRange;
                        combinedRange.include(facetProjectionRange(
                            mesh, patch.facet.triangleIndices, merged.seedNormal));
                        if (combinedRange.width() > options.coplanarTolerance + planeEpsilon)
                            continue;

                        appendPatch(merged, patch, information, candidateMaximumAngle);
                        planeRanges[mergedIndex] = combinedRange;
                        wasMerged = true;
                        break;
                    }
                }
            }
        }
        if (wasMerged)
            continue;

        const std::size_t mergedIndex = mergedPatches.size();
        planeRanges.push_back(facetProjectionRange(
            mesh, patch.facet.triangleIndices, patch.seedNormal));
        normalIndex[bin].push_back(mergedIndex);
        normalIndex[normalBin(negate(patch.seedNormal), binWidth)].push_back(mergedIndex);
        mergedPatches.push_back(std::move(patch));
    }
    return mergedPatches;
}

bool orientOutward(const TriangleMesh &mesh, FacetCandidate &candidate, double planeTolerance) {
    double facetMinimum = std::numeric_limits<double>::infinity();
    double facetMaximum = -std::numeric_limits<double>::infinity();
    for (const std::size_t index : candidate.facet.triangleIndices) {
        for (const Vec3 &vertex : mesh.triangles()[index].vertices) {
            const double projection = dot(candidate.normal, vertex);
            facetMinimum = std::min(facetMinimum, projection);
            facetMaximum = std::max(facetMaximum, projection);
        }
    }

    double modelMinimum = std::numeric_limits<double>::infinity();
    double modelMaximum = -std::numeric_limits<double>::infinity();
    for (const Triangle &triangle : mesh.triangles()) {
        for (const Vec3 &vertex : triangle.vertices) {
            const double projection = dot(candidate.normal, vertex);
            modelMinimum = std::min(modelMinimum, projection);
            modelMaximum = std::max(modelMaximum, projection);
        }
    }

    if (modelMaximum <= facetMaximum + planeTolerance) {
        candidate.facet.outwardNormal = candidate.normal;
        return true;
    }
    if (modelMinimum >= facetMinimum - planeTolerance) {
        candidate.facet.outwardNormal = negate(candidate.normal);
        return true;
    }
    return false;
}

Bounds3 transformedVertexBounds(const TriangleMesh &mesh, const Mat4 &transform) {
    Bounds3 bounds;
    for (const Triangle &triangle : mesh.triangles())
        for (const Vec3 &vertex : triangle.vertices)
            bounds.include(transform.transformPoint(vertex));
    return bounds;
}

} // namespace

FlatFacetDetector::FlatFacetDetector(FlatFacetOptions options) : options_(options) {
    if (!std::isfinite(options_.flatnessTolerance) || options_.flatnessTolerance <= 0.0 ||
        options_.flatnessTolerance >= 1.0)
        throw std::invalid_argument("Facet flatness tolerance must be between 0 and 1");
    if (!std::isfinite(options_.minimumRelativeArea) || options_.minimumRelativeArea < 0.0 ||
        options_.minimumRelativeArea > 1.0)
        throw std::invalid_argument("Minimum relative facet area must be between 0 and 1");
    if (options_.maximumFacetCount == 0)
        throw std::invalid_argument("Maximum facet count must be positive");
    if (!std::isfinite(options_.coplanarTolerance) || options_.coplanarTolerance <= 0.0)
        throw std::invalid_argument("Coplanar facet tolerance must be positive");
}

std::vector<FlatFacet> FlatFacetDetector::detect(const TriangleMesh &mesh) const {
    const std::vector<Triangle> &triangles = mesh.triangles();
    std::vector<TriangleInfo> information;
    information.reserve(triangles.size());
    std::unordered_map<EdgeKey, std::vector<std::size_t>, EdgeKeyHash> edgeOwners;
    edgeOwners.reserve(triangles.size() * 2);
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        information.push_back(triangleInfo(triangles[index]));
        if (!information.back().valid)
            continue;
        for (const EdgeKey &edge : information.back().edges)
            edgeOwners[edge].push_back(index);
    }

    std::vector<bool> assigned(triangles.size(), false);
    std::vector<std::size_t> considered(triangles.size(), 0);
    std::size_t regionId = 0;
    const double maximumAngle = std::acos(1.0 - options_.flatnessTolerance);
    std::vector<FacetCandidate> candidates;

    for (std::size_t seed = 0; seed < triangles.size(); ++seed) {
        if (assigned[seed] || !information[seed].valid)
            continue;
        ++regionId;
        if (regionId == 0) {
            std::fill(considered.begin(), considered.end(), 0);
            ++regionId;
        }

        const Vec3 seedNormal = information[seed].normal;
        std::vector<std::size_t> members{seed};
        std::vector<Vec3> memberNormals{seedNormal};
        std::vector<std::size_t> queue{seed};
        assigned[seed] = true;
        considered[seed] = regionId;
        double maximumSeedAngle = 0.0;
        double area = information[seed].area;
        Vec3 normalSum{seedNormal.x * area, seedNormal.y * area, seedNormal.z * area};
        ProjectionRange planeRange = triangleProjectionRange(triangles[seed], seedNormal);
        const double planeEpsilon =
            std::max(1e-12, options_.coplanarTolerance * 1e-12);

        for (std::size_t position = 0; position < queue.size(); ++position) {
            const std::size_t current = queue[position];
            for (const EdgeKey &edge : information[current].edges) {
                const auto owners = edgeOwners.find(edge);
                if (owners == edgeOwners.end())
                    continue;
                for (const std::size_t neighbor : owners->second) {
                    if (assigned[neighbor] || considered[neighbor] == regionId ||
                        !information[neighbor].valid)
                        continue;
                    considered[neighbor] = regionId;
                    const Vec3 candidateNormal =
                        alignedWith(information[neighbor].normal, seedNormal);
                    const double seedDot = dot(seedNormal, candidateNormal);
                    if (!withinFlatness(seedDot, options_.flatnessTolerance))
                        continue;
                    const double seedAngle = std::acos(std::clamp(seedDot, -1.0, 1.0));
                    bool compatible = seedAngle + maximumSeedAngle <=
                                      maximumAngle + 8.0 * std::numeric_limits<double>::epsilon();
                    if (!compatible) {
                        compatible = std::all_of(
                            memberNormals.begin(),
                            memberNormals.end(),
                            [&](const Vec3 &memberNormal) {
                                return withinFlatness(dot(memberNormal, candidateNormal),
                                                      options_.flatnessTolerance);
                            });
                    }
                    if (!compatible)
                        continue;
                    ProjectionRange combinedRange = planeRange;
                    combinedRange.include(
                        triangleProjectionRange(triangles[neighbor], seedNormal));
                    if (combinedRange.width() > options_.coplanarTolerance + planeEpsilon)
                        continue;

                    assigned[neighbor] = true;
                    members.push_back(neighbor);
                    memberNormals.push_back(candidateNormal);
                    queue.push_back(neighbor);
                    maximumSeedAngle = std::max(maximumSeedAngle, seedAngle);
                    const double triangleArea = information[neighbor].area;
                    area += triangleArea;
                    normalSum.x += candidateNormal.x * triangleArea;
                    normalSum.y += candidateNormal.y * triangleArea;
                    normalSum.z += candidateNormal.z * triangleArea;
                    planeRange = combinedRange;
                }
            }
        }

        FacetCandidate candidate;
        candidate.facet.triangleIndices = std::move(members);
        candidate.facet.area = area;
        candidate.normal = normalized(normalSum);
        candidate.normalSum = normalSum;
        candidate.seedNormal = seedNormal;
        candidate.memberNormals = std::move(memberNormals);
        candidate.maximumSeedAngle = maximumSeedAngle;
        candidates.push_back(std::move(candidate));
    }

    candidates = mergeCoplanarPatches(
        mesh, information, std::move(candidates), options_, maximumAngle);

    std::sort(candidates.begin(), candidates.end(), [](const auto &first, const auto &second) {
        if (first.facet.area != second.facet.area)
            return first.facet.area > second.facet.area;
        return first.facet.triangleIndices.front() < second.facet.triangleIndices.front();
    });

    std::vector<FlatFacet> facets;
    facets.reserve(std::min(options_.maximumFacetCount, candidates.size()));
    double largestArea = 0.0;
    const Bounds3 &bounds = mesh.bounds();
    const double diagonal = bounds.valid() ? length(subtract(bounds.max, bounds.min)) : 0.0;
    const double planeTolerance = std::max(1e-9, diagonal * 1e-9);
    for (FacetCandidate &candidate : candidates) {
        if (largestArea > 0.0 &&
            candidate.facet.area < largestArea * options_.minimumRelativeArea)
            break;
        if (!orientOutward(mesh, candidate, planeTolerance))
            continue;
        if (largestArea == 0.0)
            largestArea = candidate.facet.area;
        facets.push_back(std::move(candidate.facet));
        if (facets.size() >= options_.maximumFacetCount)
            break;
    }
    return facets;
}

Mat4 alignFacetToBuildPlatform(const TriangleMesh &worldMesh, const FlatFacet &facet) {
    if (!worldMesh.bounds().valid())
        throw std::invalid_argument("Cannot align a facet of an empty mesh");
    const Vec3 source = normalized(facet.outwardNormal);
    if (length(source) <= normalEpsilon)
        throw std::invalid_argument("Cannot align a facet with a zero normal");

    const Vec3 target{0.0, 0.0, -1.0};
    const double normalDot = std::clamp(dot(source, target), -1.0, 1.0);
    Vec3 axis = cross(source, target);
    if (length(axis) <= normalEpsilon && normalDot < 0.0)
        axis = cross(source,
                     std::abs(source.x) < 0.9 ? Vec3{1.0, 0.0, 0.0}
                                              : Vec3{0.0, 1.0, 0.0});
    const Mat4 rotation = normalDot > 1.0 - normalEpsilon
                              ? Mat4{}
                              : Mat4::rotation(std::acos(normalDot), axis);
    const Bounds3 &bounds = worldMesh.bounds();
    const Vec3 center{(bounds.min.x + bounds.max.x) * 0.5,
                      (bounds.min.y + bounds.max.y) * 0.5,
                      (bounds.min.z + bounds.max.z) * 0.5};
    const Mat4 centered = Mat4::translation(center.x, center.y, center.z) * rotation *
                          Mat4::translation(-center.x, -center.y, -center.z);
    const Bounds3 rotatedBounds = transformedVertexBounds(worldMesh, centered);
    return Mat4::translation(0.0, 0.0, -rotatedBounds.min.z) * centered;
}

} // namespace stl_slicer
