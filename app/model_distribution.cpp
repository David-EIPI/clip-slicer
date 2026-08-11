// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_distribution.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
std::size_t axisIndex(AlignmentAxis axis) {
    return static_cast<std::size_t>(axis);
}

double coordinate(const stl_slicer::Vec3 &point, std::size_t axis) {
    if (axis == 0)
        return point.x;
    if (axis == 1)
        return point.y;
    return point.z;
}

void setCoordinate(stl_slicer::Vec3 &point, std::size_t axis, double value) {
    if (axis == 0)
        point.x = value;
    else if (axis == 1)
        point.y = value;
    else
        point.z = value;
}

double center(const stl_slicer::Bounds3 &bounds, std::size_t axis) {
    return (coordinate(bounds.min, axis) + coordinate(bounds.max, axis)) * 0.5;
}

double extent(const stl_slicer::Bounds3 &bounds, std::size_t axis) {
    return coordinate(bounds.max, axis) - coordinate(bounds.min, axis);
}

bool validOrder(const std::array<AlignmentAxis, 3> &order) {
    std::array<bool, 3> seen{};
    for (const AlignmentAxis value : order) {
        const std::size_t axis = axisIndex(value);
        if (axis >= seen.size() || seen[axis])
            return false;
        seen[axis] = true;
    }
    return true;
}
} // namespace

std::size_t DistributionCapacity(const DistributionParameters &parameters) {
    std::size_t capacity = 1;
    for (const unsigned int count : parameters.cells) {
        if (count == 0)
            return 0;
        if (capacity > std::numeric_limits<std::size_t>::max() / count)
            return std::numeric_limits<std::size_t>::max();
        capacity *= count;
    }
    return capacity;
}

std::vector<stl_slicer::Vec3>
DistributionTranslations(const std::vector<stl_slicer::Bounds3> &modelBounds,
                         const DistributionParameters &parameters) {
    if (modelBounds.empty())
        return {};
    if (DistributionCapacity(parameters) < modelBounds.size())
        throw std::invalid_argument("distribution grid has fewer cells than models");
    if (!validOrder(parameters.axisOrder))
        throw std::invalid_argument("distribution axis order is invalid");
    for (const stl_slicer::Bounds3 &bounds : modelBounds)
        if (!bounds.valid())
            throw std::invalid_argument("model bounds are invalid");
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (coordinate(parameters.stride, axis) < 0.0)
            throw std::invalid_argument("distribution distance cannot be negative");

    std::vector<std::array<std::size_t, 3>> cells(modelBounds.size());
    for (std::size_t model = 0; model < modelBounds.size(); ++model) {
        std::size_t remaining = model;
        for (const AlignmentAxis orderedAxis : parameters.axisOrder) {
            const std::size_t axis = axisIndex(orderedAxis);
            cells[model][axis] = remaining % parameters.cells[axis];
            remaining /= parameters.cells[axis];
        }
    }

    const stl_slicer::Vec3 anchor{center(modelBounds.front(), 0),
                                  center(modelBounds.front(), 1),
                                  center(modelBounds.front(), 2)};
    std::array<std::vector<double>, 3> planeCoordinates;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        planeCoordinates[axis].resize(parameters.cells[axis], coordinate(anchor, axis));
        const double distance = coordinate(parameters.stride, axis);
        if (parameters.strideModes[axis] == DistributionStrideMode::CenterDistance) {
            for (std::size_t plane = 1; plane < planeCoordinates[axis].size(); ++plane)
                planeCoordinates[axis][plane] =
                    coordinate(anchor, axis) + plane * distance;
            continue;
        }

        std::vector<double> planeExtents(parameters.cells[axis], 0.0);
        for (std::size_t model = 0; model < modelBounds.size(); ++model) {
            const std::size_t plane = cells[model][axis];
            planeExtents[plane] =
                std::max(planeExtents[plane], extent(modelBounds[model], axis));
        }
        for (std::size_t plane = 1; plane < planeCoordinates[axis].size(); ++plane) {
            planeCoordinates[axis][plane] =
                planeCoordinates[axis][plane - 1] + planeExtents[plane - 1] * 0.5 +
                distance + planeExtents[plane] * 0.5;
        }
    }

    std::vector<stl_slicer::Vec3> translations(modelBounds.size());
    for (std::size_t model = 0; model < modelBounds.size(); ++model) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            setCoordinate(translations[model],
                          axis,
                          planeCoordinates[axis][cells[model][axis]] -
                              center(modelBounds[model], axis));
        }
    }
    return translations;
}
