#pragma once

#include "stl_slicer/geometry.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace stl_slicer {

struct Triangle {
    Vec3 normal;
    std::array<Vec3, 3> vertices;
    std::uint16_t attribute = 0;
};

class TriangleMesh {
public:
    const std::string& header() const noexcept { return header_; }
    const std::vector<Triangle>& triangles() const noexcept { return triangles_; }
    const Bounds3& bounds() const noexcept { return bounds_; }

    void setHeader(std::string header) { header_ = std::move(header); }
    void reserve(std::size_t count) { triangles_.reserve(count); }
    void addTriangle(Triangle triangle) {
        for (const auto& vertex : triangle.vertices) bounds_.include(vertex);
        triangles_.push_back(std::move(triangle));
    }

private:
    std::string header_;
    std::vector<Triangle> triangles_;
    Bounds3 bounds_;
};

} // namespace stl_slicer
