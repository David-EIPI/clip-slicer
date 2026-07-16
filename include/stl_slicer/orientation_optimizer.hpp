#pragma once

#include "stl_slicer/model.hpp"
#include "stl_slicer/transform.hpp"
#include "stl_slicer/unsupported_area.hpp"
#include <atomic>
#include <cstddef>
#include <functional>

namespace stl_slicer {

struct OrientationOptimizerOptions {
    std::size_t attempts = 10;
    std::size_t workerCount = 4;
    double convergenceTolerance = 0.1;
    double layerThickness = 0.1;
    double firstLayerOffset = 0.05;
    double segmentationTolerance = 0.01;
    double healingThreshold = 0.01;
    UnsupportedAreaOptions unsupportedArea;
};

struct OrientationCandidate {
    Mat4 transform;
    double unsupportedArea = 0.0;
};

struct OrientationOptimizationResult {
    OrientationCandidate best;
    std::size_t completedAttempts = 0;
    bool cancelled = false;
};

using OrientationImprovementCallback = std::function<void(const OrientationCandidate &)>;
using OrientationProgressCallback = std::function<void(std::size_t completed, std::size_t total)>;
using OrientationInitialScoreCallback = std::function<void(double unsupportedArea)>;

OrientationOptimizationResult
optimizeOrientation(const TriangleMesh &mesh,
                    const OrientationOptimizerOptions &options = {},
                    const std::atomic<bool> *cancel = nullptr,
                    OrientationImprovementCallback improvementCallback = {},
                    OrientationProgressCallback progressCallback = {},
                    OrientationInitialScoreCallback initialScoreCallback = {});

} // namespace stl_slicer
