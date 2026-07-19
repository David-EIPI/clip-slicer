// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/orientation_optimizer.hpp"
#include "stl_slicer/slicer.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace stl_slicer {
namespace {

constexpr double initialStepDegrees = 5.0;
constexpr double minimumStepDegrees = 0.5;
constexpr std::size_t failedDirectionsPerStep = 12;

bool cancelled(const std::atomic<bool> *cancel, const std::atomic<bool> &failed) {
    return failed.load(std::memory_order_relaxed) ||
           (cancel && cancel->load(std::memory_order_relaxed));
}

Mat4 centeredRotation(const Mat4 &rotation, const Vec3 &center) {
    return Mat4::translation(center.x, center.y, center.z) * rotation *
           Mat4::translation(-center.x, -center.y, -center.z);
}

TriangleMesh transformed(const TriangleMesh &mesh, const Mat4 &transform) {
    TriangleMesh result;
    result.reserve(mesh.triangles().size());
    for (auto triangle : mesh.triangles()) {
        for (auto &vertex : triangle.vertices)
            vertex = transform.transformPoint(vertex);
        triangle.normal = transform.transformVector(triangle.normal);
        result.addTriangle(std::move(triangle));
    }
    return result;
}

double score(const TriangleMesh &mesh,
             const Mat4 &transform,
             const OrientationOptimizerOptions &options,
             const std::atomic<bool> *cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed))
        return std::numeric_limits<double>::infinity();
    const TriangleMesh candidate = transformed(mesh, transform);
    const SliceData slices = Slicer{{options.layerThickness,
                                     options.segmentationTolerance,
                                     options.healingThreshold,
                                     options.firstLayerOffset}}
                                 .slice(candidate, cancel);
    if (cancel && cancel->load(std::memory_order_relaxed))
        return std::numeric_limits<double>::infinity();
    return UnsupportedAreaAnalyzer{options.unsupportedArea}.analyze(slices, cancel).totalArea;
}

Vec3 randomUnitVector(std::mt19937_64 &random) {
    std::normal_distribution<double> normal;
    for (;;) {
        Vec3 vector{normal(random), normal(random), normal(random)};
        const double length =
            std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
        if (length > 1e-12)
            return {vector.x / length, vector.y / length, vector.z / length};
    }
}

Mat4 randomRotation(std::mt19937_64 &random) {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double u1 = uniform(random);
    const double u2 = uniform(random);
    const double u3 = uniform(random);
    const double pi = std::acos(-1.0);
    const double x = std::sqrt(1.0 - u1) * std::sin(2.0 * pi * u2);
    const double y = std::sqrt(1.0 - u1) * std::cos(2.0 * pi * u2);
    const double z = std::sqrt(u1) * std::sin(2.0 * pi * u3);
    const double w = std::sqrt(u1) * std::cos(2.0 * pi * u3);
    const double angle = 2.0 * std::acos(std::clamp(w, -1.0, 1.0));
    const double axisLength = std::sqrt(x * x + y * y + z * z);
    return axisLength > 1e-12 ? Mat4::rotation(angle, {x, y, z}) : Mat4{};
}

bool claimAttempt(std::atomic<std::size_t> &remaining, std::size_t &attemptIndex) {
    std::size_t available = remaining.load(std::memory_order_relaxed);
    while (available != 0) {
        if (remaining.compare_exchange_weak(available, available - 1, std::memory_order_relaxed)) {
            attemptIndex = available - 1;
            return true;
        }
    }
    return false;
}

} // namespace

OrientationOptimizationResult
optimizeOrientation(const TriangleMesh &mesh,
                    const OrientationOptimizerOptions &options,
                    const std::atomic<bool> *cancel,
                    OrientationImprovementCallback improvementCallback,
                    OrientationProgressCallback progressCallback,
                    OrientationInitialScoreCallback initialScoreCallback) {
    if (!mesh.bounds().valid())
        throw std::invalid_argument("Cannot optimize an empty mesh");
    if (options.attempts == 0)
        throw std::invalid_argument("Orientation optimization requires at least one attempt");
    if (options.workerCount == 0)
        throw std::invalid_argument("Orientation optimization requires at least one worker");
    if (!std::isfinite(options.convergenceTolerance) || options.convergenceTolerance <= 0.0)
        throw std::invalid_argument("Convergence tolerance must be a positive finite value");
    if (!std::isfinite(options.firstLayerOffset) || options.firstLayerOffset <= 0.0)
        throw std::invalid_argument("First-layer offset must be a positive finite value");

    const Bounds3 &bounds = mesh.bounds();
    const Vec3 center{(bounds.min.x + bounds.max.x) * 0.5,
                      (bounds.min.y + bounds.max.y) * 0.5,
                      (bounds.min.z + bounds.max.z) * 0.5};
    OrientationOptimizationResult result;
    if (cancel && cancel->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }
    const double baselineScore = score(mesh, {}, options, cancel);
    if (cancel && cancel->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        return result;
    }
    result.best = {{}, baselineScore};
    if (initialScoreCallback)
        initialScoreCallback(baselineScore);
    std::atomic<std::size_t> remaining{options.attempts};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> failed{false};
    std::mutex bestMutex;
    std::mutex errorMutex;
    std::exception_ptr error;

    const auto publish = [&](const OrientationCandidate &candidate) {
        bool improved = false;
        {
            std::lock_guard<std::mutex> lock(bestMutex);
            if (candidate.unsupportedArea < result.best.unsupportedArea) {
                result.best = candidate;
                improved = true;
            }
        }
        if (improved && improvementCallback)
            improvementCallback(candidate);
    };

    const auto worker = [&](std::size_t workerIndex) {
        try {
            std::random_device device;
            std::seed_seq seed{device(),
                               device(),
                               static_cast<unsigned int>(workerIndex),
                               static_cast<unsigned int>(options.attempts)};
            std::mt19937_64 random(seed);
            std::size_t reverseAttempt = 0;
            while (!cancelled(cancel, failed) && claimAttempt(remaining, reverseAttempt)) {
                const std::size_t attempt = options.attempts - reverseAttempt - 1;
                OrientationCandidate current;
                if (attempt == 0) {
                    current = {{}, baselineScore};
                } else {
                    current.transform = centeredRotation(randomRotation(random), center);
                    current.unsupportedArea = score(mesh, current.transform, options, cancel);
                    publish(current);
                }

                double stepDegrees = initialStepDegrees;
                Vec3 direction = randomUnitVector(random);
                std::size_t failures = 0;
                while (!cancelled(cancel, failed) &&
                       current.unsupportedArea > options.convergenceTolerance) {
                    const double radians = stepDegrees * std::acos(-1.0) / 180.0;
                    OrientationCandidate candidate;
                    candidate.transform =
                        centeredRotation(Mat4::rotation(radians, direction), center) *
                        current.transform;
                    candidate.unsupportedArea = score(mesh, candidate.transform, options, cancel);
                    if (current.unsupportedArea - candidate.unsupportedArea >
                        options.convergenceTolerance) {
                        current = candidate;
                        failures = 0;
                        publish(current);
                    } else {
                        direction = randomUnitVector(random);
                        if (++failures >= failedDirectionsPerStep) {
                            if (stepDegrees <= minimumStepDegrees)
                                break;
                            stepDegrees = std::max(minimumStepDegrees, stepDegrees * 0.5);
                            failures = 0;
                        }
                    }
                }

                const std::size_t completedCount =
                    completed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (progressCallback)
                    progressCallback(completedCount, options.attempts);
            }
        } catch (...) {
            failed.store(true, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(errorMutex);
            if (!error)
                error = std::current_exception();
        }
    };

    const std::size_t workerCount = std::min(options.workerCount, options.attempts);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index)
        workers.emplace_back(worker, index);
    for (auto &thread : workers)
        thread.join();
    if (error)
        std::rethrow_exception(error);

    result.completedAttempts = completed.load(std::memory_order_relaxed);
    result.cancelled = cancel && cancel->load(std::memory_order_relaxed);
    return result;
}

} // namespace stl_slicer
