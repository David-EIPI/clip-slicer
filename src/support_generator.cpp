#include "stl_slicer/support_generator.hpp"
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace stl_slicer {
namespace {

enum class LayerState { Skipped, Pending, Processing, Complete };

bool cancelled(const std::atomic<bool> *cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

std::vector<SupportContactPoint>
noContactPoints(const SupportGenerationInput &, std::size_t, const std::atomic<bool> *) {
    return {};
}

TriangleMesh
noPillar(const SupportGenerationInput &, const SupportContactPoint &, const std::atomic<bool> *) {
    return {};
}

void validateInput(const SupportGenerationInput &input) {
    if (!input.sourceModel || !input.slices || !input.unsupported)
        throw std::invalid_argument(
            "Support generation requires model, slice, and unsupported data");
    if (input.slices->layers.size() != input.unsupported->layers.size())
        throw std::invalid_argument("Unsupported data must contain one layer for each model slice");
    for (std::size_t index = 0; index < input.slices->layers.size(); ++index)
        if (input.slices->layers[index].z != input.unsupported->layers[index].z)
            throw std::invalid_argument("Unsupported layers must align with model slice heights");
}

} // namespace

SupportGenerator::SupportGenerator(SupportGeneratorOptions options,
                                   SupportGenerationKernels kernels)
    : options_(options), kernels_(std::move(kernels)) {
    if (options_.workerCount == 0)
        throw std::invalid_argument("Support generation requires at least one worker");
    if (!kernels_.detectContactPoints)
        kernels_.detectContactPoints = noContactPoints;
    if (!kernels_.buildPillar)
        kernels_.buildPillar = noPillar;
}

SupportGenerationResult SupportGenerator::generate(const SupportGenerationInput &input,
                                                   const std::atomic<bool> *cancel) const {
    validateInput(input);
    SupportGenerationResult result;
    if (cancelled(cancel)) {
        result.cancelled = true;
        return result;
    }

    struct SharedState {
        std::mutex mutex;
        std::condition_variable workAvailable;
        std::vector<LayerState> layers;
        std::size_t nextLayer = 0;
        std::size_t remainingLayers = 0;
        std::size_t processedLayers = 0;
        std::deque<SupportContactPoint> contactQueue;
        std::vector<SupportContactPoint> detectedContacts;
        std::vector<TriangleMesh> pillarShells;
        bool failed = false;
        std::exception_ptr error;
    } shared;

    shared.layers.reserve(input.unsupported->layers.size());
    for (const SliceLayer &layer : input.unsupported->layers) {
        const bool hasUnsupportedArea = !layer.paths.empty();
        shared.layers.push_back(hasUnsupportedArea ? LayerState::Pending : LayerState::Skipped);
        if (hasUnsupportedArea)
            ++shared.remainingLayers;
    }
    if (shared.remainingLayers == 0)
        return result;

    const auto fail = [&](std::exception_ptr error) {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (!shared.failed) {
                shared.failed = true;
                shared.error = std::move(error);
            }
        }
        shared.workAvailable.notify_all();
    };

    const auto worker = [&]() {
        for (;;) {
            enum class WorkKind { Layer, Contact };
            WorkKind workKind = WorkKind::Layer;
            std::size_t layerIndex = 0;
            SupportContactPoint contactPoint;

            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                shared.workAvailable.wait(lock, [&]() {
                    return shared.failed || cancelled(cancel) ||
                           shared.nextLayer < shared.layers.size() ||
                           !shared.contactQueue.empty() || shared.remainingLayers == 0;
                });
                if (shared.failed || cancelled(cancel))
                    return;

                while (shared.nextLayer < shared.layers.size() &&
                       shared.layers[shared.nextLayer] != LayerState::Pending)
                    ++shared.nextLayer;
                if (shared.nextLayer < shared.layers.size()) {
                    // Processing is the per-layer lock; geometry runs without holding the queue.
                    layerIndex = shared.nextLayer++;
                    shared.layers[layerIndex] = LayerState::Processing;
                    workKind = WorkKind::Layer;
                } else if (!shared.contactQueue.empty()) {
                    contactPoint = shared.contactQueue.front();
                    shared.contactQueue.pop_front();
                    workKind = WorkKind::Contact;
                } else if (shared.remainingLayers == 0) {
                    return;
                } else {
                    continue;
                }
            }

            try {
                if (workKind == WorkKind::Layer) {
                    std::vector<SupportContactPoint> contacts =
                        kernels_.detectContactPoints(input, layerIndex, cancel);
                    {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.layers[layerIndex] = LayerState::Complete;
                        --shared.remainingLayers;
                        ++shared.processedLayers;
                        shared.detectedContacts.insert(
                            shared.detectedContacts.end(), contacts.begin(), contacts.end());
                        for (SupportContactPoint &contact : contacts)
                            shared.contactQueue.push_back(std::move(contact));
                    }
                    shared.workAvailable.notify_all();
                } else {
                    TriangleMesh pillar = kernels_.buildPillar(input, contactPoint, cancel);
                    if (!pillar.triangles().empty()) {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.pillarShells.push_back(std::move(pillar));
                    }
                }
            } catch (...) {
                fail(std::current_exception());
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(options_.workerCount);
    try {
        for (std::size_t index = 0; index < options_.workerCount; ++index)
            workers.emplace_back(worker);
    } catch (...) {
        fail(std::current_exception());
    }
    for (std::thread &thread : workers)
        thread.join();

    if (shared.error)
        std::rethrow_exception(shared.error);
    result.cancelled = cancelled(cancel);
    result.processedLayerCount = shared.processedLayers;
    result.contactPoints = std::move(shared.detectedContacts);
    std::size_t triangleCount = 0;
    for (const TriangleMesh &pillar : shared.pillarShells)
        triangleCount += pillar.triangles().size();
    result.supports.reserve(triangleCount);
    result.supports.setHeader("CLIP Slicer generated supports");
    for (const TriangleMesh &pillar : shared.pillarShells)
        for (const Triangle &triangle : pillar.triangles())
            result.supports.addTriangle(triangle);
    return result;
}

} // namespace stl_slicer
