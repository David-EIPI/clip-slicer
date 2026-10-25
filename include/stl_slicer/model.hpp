// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"
#include <array>
#include <cmath>
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
    // Exact for a closed, consistently wound triangle shell; independent of global winding.
    double materialVolume() const noexcept {
        return std::abs(signedSixVolume_ / 6.0);
    }

    void setHeader(std::string header) {
        header_ = std::move(header);
    }
    void reserve(std::size_t count) {
        triangles_.reserve(count);
    }
    void addTriangle(Triangle triangle) {
        accumulateVolume(triangle);
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
        for (const Triangle &triangle : mesh.triangles_)
            accumulateVolume(triangle);
        if (mesh.bounds_.valid()) {
            bounds_.include(mesh.bounds_.min);
            bounds_.include(mesh.bounds_.max);
        }
        triangles_.insert(triangles_.end(),
                          std::make_move_iterator(mesh.triangles_.begin()),
                          std::make_move_iterator(mesh.triangles_.end()));
    }

  private:
    void accumulateVolume(const Triangle &triangle) {
        if (!hasVolumeOrigin_) {
            volumeOrigin_ = triangle.vertices[0];
            hasVolumeOrigin_ = true;
        }
        const Vec3 &origin = volumeOrigin_;
        const Vec3 a{triangle.vertices[0].x - origin.x,
                     triangle.vertices[0].y - origin.y,
                     triangle.vertices[0].z - origin.z};
        const Vec3 b{triangle.vertices[1].x - origin.x,
                     triangle.vertices[1].y - origin.y,
                     triangle.vertices[1].z - origin.z};
        const Vec3 c{triangle.vertices[2].x - origin.x,
                     triangle.vertices[2].y - origin.y,
                     triangle.vertices[2].z - origin.z};
        const double sixVolume = a.x * (b.y * c.z - b.z * c.y) +
                                 a.y * (b.z * c.x - b.x * c.z) +
                                 a.z * (b.x * c.y - b.y * c.x);
        const double corrected = sixVolume - volumeCompensation_;
        const double sum = signedSixVolume_ + corrected;
        volumeCompensation_ = (sum - signedSixVolume_) - corrected;
        signedSixVolume_ = sum;
    }

    std::string header_;
    std::vector<Triangle> triangles_;
    Bounds3 bounds_;
    Vec3 volumeOrigin_;
    double signedSixVolume_ = 0.0;
    double volumeCompensation_ = 0.0;
    bool hasVolumeOrigin_ = false;
};

} // namespace stl_slicer
