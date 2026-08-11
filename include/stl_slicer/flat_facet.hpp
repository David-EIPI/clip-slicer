// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/transform.hpp"
#include <cstddef>
#include <vector>

namespace stl_slicer {

struct FlatFacet {
    std::vector<std::size_t> triangleIndices;
    Vec3 outwardNormal;
    double area = 0.0;
};

struct FlatFacetOptions {
    double flatnessTolerance = 1e-3;
    double minimumRelativeArea = 0.01;
    std::size_t maximumFacetCount = 10;
    double coplanarTolerance = 0.1;
};

class FlatFacetDetector {
  public:
    explicit FlatFacetDetector(FlatFacetOptions options = {});
    std::vector<FlatFacet> detect(const TriangleMesh &mesh) const;

  private:
    FlatFacetOptions options_;
};

// Returns a world-space transform which rotates the facet outward normal to
// negative Z about the mesh center, then places the rotated mesh on Z=0.
Mat4 alignFacetToBuildPlatform(const TriangleMesh &worldMesh, const FlatFacet &facet);

} // namespace stl_slicer
