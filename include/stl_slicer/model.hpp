#pragma once

#include "stl_slicer/geometry.hpp"
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace stl_slicer {

struct Triangle {
    Vec3 normal;
    std::array<Vec3, 3> vertices;
    std::uint16_t attribute = 0;
    double minZ = 0.0;
    double maxZ = 0.0;
};

class TriangleMesh {
  public:
    const std::string &header() const noexcept {
        return header_;
    }
    const std::vector<Triangle> &triangles() const noexcept {
        return triangles_;
    }
    const Bounds3 &bounds() const noexcept {
        return bounds_;
    }

    void setHeader(std::string header) {
        header_ = std::move(header);
    }
    void reserve(std::size_t count) {
        triangles_.reserve(count);
    }
    void addTriangle(Triangle triangle) {
        triangle.minZ = triangle.vertices[0].z;
        triangle.maxZ = triangle.vertices[0].z;
        for (const auto &vertex : triangle.vertices) {
            triangle.minZ = std::min(triangle.minZ, vertex.z);
            triangle.maxZ = std::max(triangle.maxZ, vertex.z);
        }
        for (const auto &vertex : triangle.vertices)
            bounds_.include(vertex);
        triangles_.push_back(std::move(triangle));
    }
    void append(TriangleMesh mesh) {
        if (mesh.triangles_.empty())
            return;
        if (mesh.bounds_.valid()) {
            bounds_.include(mesh.bounds_.min);
            bounds_.include(mesh.bounds_.max);
        }
        triangles_.insert(triangles_.end(),
                          std::make_move_iterator(mesh.triangles_.begin()),
                          std::make_move_iterator(mesh.triangles_.end()));
    }

  private:
    std::string header_;
    std::vector<Triangle> triangles_;
    Bounds3 bounds_;
};

} // namespace stl_slicer
