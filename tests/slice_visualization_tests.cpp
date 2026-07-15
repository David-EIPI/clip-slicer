#include "slice_visualization.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace stl_slicer;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

double topArea(const VisualizationMesh &mesh) {
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 6) {
        const auto &a = mesh.vertices[mesh.indices[i]];
        const auto &b = mesh.vertices[mesh.indices[i + 1]];
        const auto &c = mesh.vertices[mesh.indices[i + 2]];
        area += ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5;
    }
    return area;
}

void testCapsWithHole() {
    SliceData slices;
    SliceLayer layer;
    layer.z = 1.0;
    layer.paths.push_back({PathType::External, {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0}}});
    layer.paths.push_back({PathType::Internal, {{1, 1}, {1, 3}, {3, 3}, {3, 1}, {1, 1}}});
    slices.layers.push_back(std::move(layer));

    const auto caps = BuildSliceCaps(slices);
    require(!caps.indices.empty(), "Tessellation produced no cap triangles");
    require(std::abs(topArea(caps) - 12.0) < 1e-6, "Tessellation did not preserve a hole");
    for (std::size_t i = 0; i + 5 < caps.indices.size(); i += 6) {
        const auto &top = caps.vertices[caps.indices[i]];
        const auto &bottom = caps.vertices[caps.indices[i + 3]];
        require(top.z == 1.0f && top.nz == 1.0f, "Top cap orientation is incorrect");
        require(bottom.z == 0.0f && bottom.nz == -1.0f, "Bottom cap orientation is incorrect");
    }
}

void testGeometricallyClosedOpenPath() {
    SliceData slices;
    SliceLayer layer;
    layer.z = 2.0;
    layer.paths.push_back({PathType::Open, {{0, 0}, {2, 0}, {0, 2}, {0, 0}}});
    slices.layers.push_back(std::move(layer));
    require(std::abs(topArea(BuildSliceCaps(slices)) - 2.0) < 1e-6,
            "Closed direction-2 contour was not tessellated");
}

void testSliceSurfacesAreNotExtruded() {
    SliceData slices;
    slices.layers.push_back({3.0, {{PathType::External, {{0, 0}, {1, 0}, {1, 1}, {0, 0}}}}});
    const VisualizationMesh surfaces = BuildSliceSurfaces(slices);
    require(!surfaces.indices.empty(), "Surface tessellation produced no triangles");
    for (const auto &vertex : surfaces.vertices)
        require(vertex.z == 3.0f, "Unsupported surface was extruded between layers");
}

void testUnsupportedSurfacesUsePreviousLayerHeight() {
    SliceData slices;
    slices.layers.push_back({2.75, {}});
    slices.layers.push_back({3.0, {{PathType::External, {{0, 0}, {1, 0}, {1, 1}, {0, 0}}}}});

    const VisualizationMesh surfaces = BuildUnsupportedSurfaces(slices);
    require(!surfaces.indices.empty(), "Unsupported surface tessellation produced no triangles");
    for (const auto &vertex : surfaces.vertices)
        require(vertex.z == 2.75f, "Unsupported surface did not use the previous layer height");
}

void testNormalSmoothingPreservesCreases() {
    std::vector<RenderVertex> vertices = {
        {0, 0, 0, 1, 0, 0}, {0, 0, 0, 0.8660254f, 0.5f, 0}, {0, 0, 0, 0, 0, 1}};
    SmoothRenderNormals(vertices);
    require(vertices[0].ny > 0.2f, "Neighboring normals were not blended");
    require(vertices[2].nx == 0.0f && vertices[2].nz == 1.0f,
            "Sharp crease was incorrectly smoothed");
}
} // namespace

int main() {
    try {
        testCapsWithHole();
        testGeometricallyClosedOpenPath();
        testSliceSurfacesAreNotExtruded();
        testUnsupportedSurfacesUsePreviousLayerHeight();
        testNormalSmoothingPreservesCreases();
        std::cout << "All visualization tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
