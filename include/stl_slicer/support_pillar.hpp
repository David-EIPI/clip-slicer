#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/slice.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace stl_slicer {

struct ExternalPillarOptions {
    double latticeCellSize = 0.5;
    double minimumSupportAngleDegrees = 30.0;
    double baseHeight = 0.5;
    double baseRadius = 2.0;
    double bottomRadius = 0.75;
    double topRadius = 0.5;
    double modelIsolation = 1.0;
    std::size_t circumferencePoints = 12;
};

class ExternalPillarSpace {
  public:
    ExternalPillarSpace(std::shared_ptr<const SliceData> slices,
                        Bounds3 modelBounds,
                        double maximumHeight,
                        ExternalPillarOptions options = {},
                        const std::atomic<bool> *cancel = nullptr);

    std::vector<Vec3> route(const Vec3 &attachment,
                            const std::atomic<bool> *cancel = nullptr) const;
    bool valid() const noexcept;

  private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

class ExternalPillarBuilder {
  public:
    ExternalPillarBuilder(std::shared_ptr<const ExternalPillarSpace> space,
                          ExternalPillarOptions options = {});
    TriangleMesh build(const Vec3 &attachment, const std::atomic<bool> *cancel = nullptr) const;

  private:
    std::shared_ptr<const ExternalPillarSpace> space_;
    ExternalPillarOptions options_;
    std::vector<Vec3> unitCircle_;
};

} // namespace stl_slicer
