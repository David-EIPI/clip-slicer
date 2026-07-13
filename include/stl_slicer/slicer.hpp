#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/slice.hpp"

namespace stl_slicer {

struct SlicerOptions {
    double layerThickness = 0.1;
    double joinTolerance = 1e-5;
    double gapClosingToleranceMultiplier = 2.0;
    double firstLayerOffset = -1.0;
};

class Slicer {
  public:
    explicit Slicer(SlicerOptions options = {});
    SliceData slice(const TriangleMesh &mesh) const;
    SliceLayer sliceAt(const TriangleMesh &mesh, double z) const;

  private:
    SlicerOptions options_;
};

} // namespace stl_slicer
