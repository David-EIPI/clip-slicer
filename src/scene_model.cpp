// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/scene_model.hpp"
#include <cmath>

namespace stl_slicer {
namespace {
Vec3 normal(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 0) {
        n.x /= length;
        n.y /= length;
        n.z /= length;
    }
    return n;
}
RenderVertex rv(Vec3 p, Vec3 n) {
    return {float(p.x), float(p.y), float(p.z), float(n.x), float(n.y), float(n.z)};
}
} // namespace

MeshSceneModel::MeshSceneModel(std::string modelName, TriangleMesh mesh) : mesh_(std::move(mesh)) {
    name = std::move(modelName);
    vertices_.reserve(mesh_.triangles().size() * 3);
    for (const auto &triangle : mesh_.triangles()) {
        Vec3 n = triangle.normal;
        if (n.x * n.x + n.y * n.y + n.z * n.z < 1e-12)
            n = normal(triangle.vertices[0], triangle.vertices[1], triangle.vertices[2]);
        for (const auto &p : triangle.vertices)
            vertices_.push_back(rv(p, n));
    }
}
TriangleMesh MeshSceneModel::triangleMesh() const {
    return mesh_;
}

SliceSceneModel::SliceSceneModel(std::string modelName, SliceData slices)
    : slices_(std::move(slices)) {
    name = std::move(modelName);
    bounds_ = slices_.sourceBounds;
    buildMesh();
}
void SliceSceneModel::buildMesh() {
    const double fallback =
        slices_.layers.size() > 1 ? slices_.layers[1].z - slices_.layers[0].z : 1.0;
    for (std::size_t li = 0; li < slices_.layers.size(); ++li) {
        const auto &layer = slices_.layers[li];
        const double bottom = li ? slices_.layers[li - 1].z : layer.z - fallback;
        for (const auto &path : layer.paths) {
            if (path.points.size() < 2)
                continue;
            std::vector<Vec3> edgeNormals;
            edgeNormals.reserve(path.points.size() - 1);
            for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
                const auto a = path.points[i], b = path.points[i + 1];
                Vec3 n{b.y - a.y, a.x - b.x, 0};
                if (path.type == PathType::Internal) {
                    n.x = -n.x;
                    n.y = -n.y;
                }
                const double length = std::hypot(n.x, n.y);
                if (length != 0) {
                    n.x /= length;
                    n.y /= length;
                }
                edgeNormals.push_back(n);
            }
            const bool closed = path.points.size() > 2 &&
                                squaredDistance(path.points.front(), path.points.back()) <= 1e-12;
            for (std::size_t i = 0; i < edgeNormals.size(); ++i) {
                const auto a = path.points[i], b = path.points[i + 1];
                const Vec3 segmentNormal = edgeNormals[i];
                if (segmentNormal.x == 0.0 && segmentNormal.y == 0.0)
                    continue;
                const auto blendedNormal = [&](std::size_t vertexIndex) {
                    if (!closed && (vertexIndex == 0 || vertexIndex == edgeNormals.size()))
                        return segmentNormal;
                    const Vec3 first =
                        edgeNormals[(vertexIndex + edgeNormals.size() - 1) % edgeNormals.size()];
                    const Vec3 second = edgeNormals[vertexIndex % edgeNormals.size()];
                    if (first.x * second.x + first.y * second.y < 0.5)
                        return segmentNormal;
                    const double length = std::hypot(first.x + second.x, first.y + second.y);
                    if (length == 0.0)
                        return segmentNormal;
                    return Vec3{(first.x + second.x) / length, (first.y + second.y) / length, 0.0};
                };
                const Vec3 normalA = blendedNormal(i);
                const Vec3 normalB = blendedNormal(i + 1);
                Vec3 p0{a.x, a.y, bottom}, p1{b.x, b.y, bottom}, p2{b.x, b.y, layer.z},
                    p3{a.x, a.y, layer.z};
                if (path.type == PathType::Internal)
                    vertices_.insert(vertices_.end(),
                                     {rv(p0, normalA),
                                      rv(p2, normalB),
                                      rv(p1, normalB),
                                      rv(p0, normalA),
                                      rv(p3, normalA),
                                      rv(p2, normalB)});
                else
                    vertices_.insert(vertices_.end(),
                                     {rv(p0, normalA),
                                      rv(p1, normalB),
                                      rv(p2, normalB),
                                      rv(p0, normalA),
                                      rv(p2, normalB),
                                      rv(p3, normalA)});
                bounds_.include(p0);
                bounds_.include(p2);
            }
        }
    }
}
TriangleMesh SliceSceneModel::triangleMesh() const {
    TriangleMesh mesh;
    for (std::size_t i = 0; i + 2 < vertices_.size(); i += 3) {
        Triangle t;
        for (int k = 0; k < 3; ++k)
            t.vertices[k] = {vertices_[i + k].x, vertices_[i + k].y, vertices_[i + k].z};
        t.normal = {vertices_[i].nx, vertices_[i].ny, vertices_[i].nz};
        mesh.addTriangle(t);
    }
    return mesh;
}
TriangleMesh transformedMesh(const SceneModel &model) {
    TriangleMesh result;
    auto mesh = model.triangleMesh();
    result.reserve(mesh.triangles().size());
    for (auto triangle : mesh.triangles()) {
        for (auto &p : triangle.vertices)
            p = model.transform.transformPoint(p);
        triangle.normal = model.transform.transformVector(triangle.normal);
        result.addTriangle(std::move(triangle));
    }
    return result;
}
} // namespace stl_slicer
