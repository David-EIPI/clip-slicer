#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/slice.hpp"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace stl_slicer {

struct SupportContactPoint {
    Vec3 position;
    std::size_t layerIndex = 0;
};

struct SupportGenerationInput {
    std::shared_ptr<const TriangleMesh> sourceModel;
    std::shared_ptr<const SliceData> slices;
    std::shared_ptr<const SliceData> unsupported;
};

using SupportContactDetector = std::function<std::vector<SupportContactPoint>(
    const SupportGenerationInput &, std::size_t layerIndex, const std::atomic<bool> *cancel)>;
using SupportPillarBuilder = std::function<TriangleMesh(
    const SupportGenerationInput &, const SupportContactPoint &, const std::atomic<bool> *cancel)>;

struct SupportGenerationKernels {
    // Kernels are invoked concurrently and must not mutate shared input data.
    SupportContactDetector detectContactPoints;
    SupportPillarBuilder buildPillar;
};

struct SupportGeneratorOptions {
    std::size_t workerCount = 4;
};

struct SupportGenerationResult {
    TriangleMesh supports;
    std::vector<SupportContactPoint> contactPoints;
    std::size_t processedLayerCount = 0;
    bool cancelled = false;
};

class SupportGenerator {
  public:
    explicit SupportGenerator(SupportGeneratorOptions options = {},
                              SupportGenerationKernels kernels = {});
    SupportGenerationResult generate(const SupportGenerationInput &input,
                                     const std::atomic<bool> *cancel = nullptr) const;

  private:
    SupportGeneratorOptions options_;
    SupportGenerationKernels kernels_;
};

} // namespace stl_slicer
