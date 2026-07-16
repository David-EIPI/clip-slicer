#pragma once

#include "stl_slicer/model.hpp"
#include <atomic>
#include <cstddef>
#include <memory>

namespace stl_slicer {

struct SupportTipOptions {
    double topRadius = 0.25;
    double bottomRadius = 0.75;
    double height = 2.0;
    std::size_t circumferencePoints = 12;
    double criticalAngleDegrees = 30.0;
};

class SupportTipBuilder {
  public:
    SupportTipBuilder(std::shared_ptr<const TriangleMesh> sourceModel,
                      SupportTipOptions options = {},
                      const std::atomic<bool> *cancel = nullptr);
    TriangleMesh build(const Vec3 &contactPoint,
                       const std::atomic<bool> *cancel = nullptr) const;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

} // namespace stl_slicer
