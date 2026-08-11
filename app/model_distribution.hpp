// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "model_alignment.hpp"
#include <array>
#include <cstddef>
#include <vector>

enum class DistributionStrideMode { CenterDistance, BoundingBoxGap };

struct DistributionParameters {
    std::array<unsigned int, 3> cells = {1, 1, 1};
    stl_slicer::Vec3 stride;
    std::array<DistributionStrideMode, 3> strideModes = {
        DistributionStrideMode::CenterDistance,
        DistributionStrideMode::CenterDistance,
        DistributionStrideMode::CenterDistance};
    std::array<AlignmentAxis, 3> axisOrder = {
        AlignmentAxis::X, AlignmentAxis::Y, AlignmentAxis::Z};
};

std::size_t DistributionCapacity(const DistributionParameters &parameters);
std::vector<stl_slicer::Vec3>
DistributionTranslations(const std::vector<stl_slicer::Bounds3> &modelBounds,
                         const DistributionParameters &parameters);
