// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_distribution.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool close(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

stl_slicer::Bounds3 bounds(double centerX,
                           double centerY,
                           double centerZ,
                           double sizeX,
                           double sizeY,
                           double sizeZ) {
    return {{centerX - sizeX * 0.5, centerY - sizeY * 0.5, centerZ - sizeZ * 0.5},
            {centerX + sizeX * 0.5, centerY + sizeY * 0.5, centerZ + sizeZ * 0.5}};
}

void testCenterDistanceAndAnchor() {
    const std::vector<stl_slicer::Bounds3> models = {
        bounds(10, 20, 30, 2, 2, 2),
        bounds(-4, 8, 2, 2, 2, 2),
        bounds(7, -3, 9, 2, 2, 2)};
    DistributionParameters parameters;
    parameters.cells = {2, 2, 1};
    parameters.stride = {5, 7, 0};

    const auto translations = DistributionTranslations(models, parameters);
    require(close(translations[0].x, 0) && close(translations[0].y, 0),
            "first model must anchor the grid");
    require(close(translations[1].x, 19) && close(translations[1].y, 12),
            "X must advance first");
    require(close(translations[2].x, 3) && close(translations[2].y, 30),
            "Y row placement is wrong");
}

void testFillOrder() {
    const std::vector<stl_slicer::Bounds3> models = {
        bounds(0, 0, 0, 1, 1, 1), bounds(0, 0, 0, 1, 1, 1)};
    DistributionParameters parameters;
    parameters.cells = {2, 2, 1};
    parameters.stride = {5, 7, 0};
    parameters.axisOrder = {AlignmentAxis::Y, AlignmentAxis::X, AlignmentAxis::Z};

    const auto translations = DistributionTranslations(models, parameters);
    require(close(translations[1].x, 0) && close(translations[1].y, 7),
            "selected fill order was ignored");
}

void testVariablePlaneGap() {
    const std::vector<stl_slicer::Bounds3> models = {
        bounds(0, 0, 0, 2, 1, 1),
        bounds(20, 0, 0, 4, 1, 1),
        bounds(40, 10, 0, 10, 1, 1),
        bounds(60, 10, 0, 6, 1, 1)};
    DistributionParameters parameters;
    parameters.cells = {2, 2, 1};
    parameters.stride = {1, 10, 0};
    parameters.strideModes[0] = DistributionStrideMode::BoundingBoxGap;

    const auto translations = DistributionTranslations(models, parameters);
    require(close(20 + translations[1].x, 9) &&
                close(40 + translations[2].x, 0) &&
                close(60 + translations[3].x, 9),
            "the largest part on each index plane must determine neighboring gaps");
    require(close(9 - 10 * 0.5 - 6 * 0.5, 1),
            "variable-width planes do not preserve the requested gap");
}

void testInactiveAxesPreserveCoordinates() {
    const std::vector<stl_slicer::Bounds3> models = {
        bounds(10, 20, 30, 2, 2, 2),
        bounds(-4, 8, 2, 2, 2, 2),
        bounds(7, -3, 9, 2, 2, 2)};
    DistributionParameters parameters;
    parameters.cells = {3, 0, 0};
    parameters.stride = {5, -7, -9};

    require(DistributionCapacity(parameters) == 3,
            "inactive axes must not reduce distribution capacity");
    const auto translations = DistributionTranslations(models, parameters);
    require(close(translations[1].x, 19) && close(translations[2].x, 13),
            "active-axis placement is wrong when other axes are inactive");
    for (const stl_slicer::Vec3 &translation : translations)
        require(close(translation.y, 0) && close(translation.z, 0),
                "inactive-axis coordinates must remain unchanged");

    parameters.cells = {2, 0, 3};
    require(DistributionCapacity(parameters) == 6,
            "capacity must multiply only active axes");
}

void testInsufficientCapacity() {
    DistributionParameters parameters;
    bool threw = false;
    try {
        DistributionTranslations(
            {bounds(0, 0, 0, 1, 1, 1), bounds(0, 0, 0, 1, 1, 1)}, parameters);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    require(threw, "insufficient capacity must be rejected");
}
} // namespace

int main() {
    try {
        testCenterDistanceAndAnchor();
        testFillOrder();
        testVariablePlaneGap();
        testInactiveAxesPreserveCoordinates();
        testInsufficientCapacity();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
