#pragma once

#include "stl_slicer/slice.hpp"
#include <atomic>

namespace stl_slicer {

struct UnsupportedAreaOptions {
    double criticalAngleDegrees = 30.0;
    double overhangCoefficient = 1.0;
};

struct UnsupportedAreaResult {
    SliceData unsupported;
    double totalArea = 0.0;
};

class UnsupportedAreaAnalyzer {
  public:
    explicit UnsupportedAreaAnalyzer(UnsupportedAreaOptions options = {});
    UnsupportedAreaResult analyze(const SliceData &slices,
                                  const std::atomic<bool> *cancel = nullptr) const;

  private:
    UnsupportedAreaOptions options_;
};

} // namespace stl_slicer
