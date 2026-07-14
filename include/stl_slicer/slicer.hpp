#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/slice.hpp"
#include <atomic>

namespace stl_slicer {

struct SlicerOptions {
    double layerThickness = 0.1;
    double joinTolerance = 0.01;
    double gapClosingTolerance = 0.01;
    double firstLayerOffset = -1.0;
};

class Slicer {
  public:
    explicit Slicer(SlicerOptions options = {});
    SliceData slice(const TriangleMesh &mesh, const std::atomic<bool> *cancel = nullptr) const;
    SliceLayer sliceAt(const TriangleMesh &mesh,
                       double z,
                       const std::atomic<bool> *cancel = nullptr) const;

  private:
    SlicerOptions options_;
};

} // namespace stl_slicer
