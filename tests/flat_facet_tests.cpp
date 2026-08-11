// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/flat_facet.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace stl_slicer;

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void requireNear(double actual, double expected, double tolerance, const char *message) {
    if (std::abs(actual - expected) > tolerance)
        throw std::runtime_error(message);
}

void addQuad(TriangleMesh &mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    mesh.addTriangle({{}, {a, b, c}, 0});
    mesh.addTriangle({{}, {a, c, d}, 0});
}

TriangleMesh box(double x, double y, double z) {
    TriangleMesh mesh;
    const Vec3 a{0, 0, 0}, b{x, 0, 0}, c{x, y, 0}, d{0, y, 0};
    const Vec3 e{0, 0, z}, f{x, 0, z}, g{x, y, z}, h{0, y, z};
    addQuad(mesh, a, d, c, b);
    addQuad(mesh, e, f, g, h);
    addQuad(mesh, a, b, f, e);
    addQuad(mesh, b, c, g, f);
    addQuad(mesh, c, d, h, g);
    addQuad(mesh, d, a, e, h);
    return mesh;
}

TriangleMesh transformed(TriangleMesh source, const Mat4 &transform) {
    TriangleMesh result;
    result.reserve(source.triangles().size());
    for (Triangle triangle : source.triangles()) {
        for (Vec3 &vertex : triangle.vertices)
            vertex = transform.transformPoint(vertex);
        result.addTriangle(std::move(triangle));
    }
    return result;
}

void testBoxFacetsAreMergedAndSorted() {
    const TriangleMesh mesh = box(2, 3, 4);
    const std::vector<FlatFacet> facets = FlatFacetDetector{}.detect(mesh);
    require(facets.size() == 6, "box should have six outer facets");
    requireNear(facets[0].area, 12.0, 1e-12, "largest box facet area is incorrect");
    requireNear(facets[1].area, 12.0, 1e-12, "second box facet area is incorrect");
    requireNear(facets[2].area, 8.0, 1e-12, "box facet sorting is incorrect");
    for (const FlatFacet &facet : facets)
        require(facet.triangleIndices.size() == 2, "coplanar box triangles were not merged");
}

void testInternalPlaneIsDiscarded() {
    TriangleMesh mesh = box(2, 3, 4);
    addQuad(mesh, {0.2, 0.2, 2}, {1.8, 0.2, 2}, {1.8, 2.8, 2}, {0.2, 2.8, 2});
    const std::vector<FlatFacet> facets = FlatFacetDetector{}.detect(mesh);
    require(facets.size() == 6, "internal plane was accepted as an outer facet");
}

void testNormalChainDoesNotBecomeOneFacet() {
    TriangleMesh mesh;
    double z = 0.0;
    Vec3 previousBottom{0.0, 0.0, z};
    Vec3 previousTop{0.0, 1.0, z};
    for (int segment = 0; segment < 3; ++segment) {
        z += static_cast<double>(segment) * 0.03;
        const Vec3 nextBottom{static_cast<double>(segment + 1), 0.0, z};
        const Vec3 nextTop{static_cast<double>(segment + 1), 1.0, z};
        addQuad(mesh, previousBottom, nextBottom, nextTop, previousTop);
        previousBottom = nextBottom;
        previousTop = nextTop;
    }
    FlatFacetOptions options;
    options.flatnessTolerance = 1.0 - std::cos(0.04);
    const std::vector<FlatFacet> facets = FlatFacetDetector{options}.detect(mesh);
    require(facets.size() == 2,
            "pairwise flatness allowed a gradual normal chain to form one facet");
}

void testDisconnectedCoplanarFragmentsAreMerged() {
    TriangleMesh mesh;
    mesh.addTriangle({{}, {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}, 0});
    mesh.addTriangle({{}, {Vec3{3, 0, 0}, Vec3{4, 0, 0}, Vec3{3, 1, 0}}, 0});
    mesh.addTriangle(
        {{}, {Vec3{6, 0, 0.05}, Vec3{7, 0, 0.05}, Vec3{6, 1, 0.05}}, 0});

    FlatFacetOptions options;
    options.coplanarTolerance = 0.1;
    const std::vector<FlatFacet> merged = FlatFacetDetector{options}.detect(mesh);
    require(merged.size() == 1 && merged.front().triangleIndices.size() == 3,
            "disconnected fragments inside the plane tolerance were not merged");
    requireNear(merged.front().area, 1.5, 1e-12, "merged fragment area is incorrect");

    options.coplanarTolerance = 0.01;
    const std::vector<FlatFacet> separated = FlatFacetDetector{options}.detect(mesh);
    require(separated.size() == 2,
            "parallel fragments outside the plane tolerance were merged");
}

void testFacetCountAndRelativeAreaLimits() {
    FlatFacetOptions limitedOptions;
    limitedOptions.maximumFacetCount = 3;
    require(FlatFacetDetector{limitedOptions}.detect(box(2, 3, 4)).size() == 3,
            "facet result did not honor the configured count limit");

    const std::vector<FlatFacet> thinBoxFacets =
        FlatFacetDetector{}.detect(box(100, 100, 0.5));
    require(thinBoxFacets.size() == 2,
            "outer facets below one percent of the largest were retained");

    TriangleMesh reversedWinding;
    reversedWinding.addTriangle({{}, {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}}, 0});
    reversedWinding.addTriangle({{}, {Vec3{0, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 1, 0}}, 0});
    const std::vector<FlatFacet> windingFacets = FlatFacetDetector{}.detect(reversedWinding);
    require(windingFacets.size() == 1 && windingFacets.front().triangleIndices.size() == 2,
            "opposite triangle winding split a flat facet");
}

void testAlignmentPlacesSelectedFacetOnPlatform() {
    const Mat4 initial = Mat4::translation(7.0, -3.0, 5.0) *
                         Mat4::rotation(0.61, {1.0, 2.0, -0.5});
    const TriangleMesh world = transformed(box(2, 3, 4), initial);
    const std::vector<FlatFacet> facets = FlatFacetDetector{}.detect(world);
    require(!facets.empty(), "transformed box has no facets");
    const FlatFacet &selected = facets.front();
    const Mat4 alignment = alignFacetToBuildPlatform(world, selected);
    const Vec3 alignedNormal = alignment.transformVector(selected.outwardNormal);
    requireNear(alignedNormal.x, 0.0, 1e-10, "aligned facet normal has an X component");
    requireNear(alignedNormal.y, 0.0, 1e-10, "aligned facet normal has a Y component");
    requireNear(alignedNormal.z, -1.0, 1e-10, "aligned facet does not face the platform");

    double minimumZ = std::numeric_limits<double>::infinity();
    for (const Triangle &triangle : world.triangles())
        for (const Vec3 &vertex : triangle.vertices)
            minimumZ = std::min(minimumZ, alignment.transformPoint(vertex).z);
    requireNear(minimumZ, 0.0, 1e-10, "aligned model does not rest on the platform");
    for (const std::size_t triangleIndex : selected.triangleIndices)
        for (const Vec3 &vertex : world.triangles()[triangleIndex].vertices)
            requireNear(alignment.transformPoint(vertex).z,
                        0.0,
                        1e-9,
                        "selected facet is not on the platform");
}

} // namespace

int main() {
    try {
        testBoxFacetsAreMergedAndSorted();
        testInternalPlaneIsDiscarded();
        testNormalChainDoesNotBecomeOneFacet();
        testDisconnectedCoplanarFragmentsAreMerged();
        testFacetCountAndRelativeAreaLimits();
        testAlignmentPlacesSelectedFacetOnPlatform();
        std::cout << "Flat facet tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
