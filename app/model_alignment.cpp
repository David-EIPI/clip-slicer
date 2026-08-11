// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_alignment.hpp"

namespace {
double coordinate(const stl_slicer::Vec3 &point, AlignmentAxis axis) {
    if (axis == AlignmentAxis::X)
        return point.x;
    if (axis == AlignmentAxis::Y)
        return point.y;
    return point.z;
}

double
alignmentCoordinate(const stl_slicer::Bounds3 &bounds, AlignmentAxis axis, AlignmentType type) {
    const double minimum = coordinate(bounds.min, axis);
    const double maximum = coordinate(bounds.max, axis);
    if (type == AlignmentType::Minimum)
        return minimum;
    if (type == AlignmentType::Maximum)
        return maximum;
    return (minimum + maximum) / 2.0;
}
} // namespace

stl_slicer::Vec3 AlignmentTranslation(const stl_slicer::Bounds3 &modelBounds,
                                      const stl_slicer::Bounds3 &selectionBounds,
                                      AlignmentAxis axis,
                                      AlignmentType type) {
    if (!modelBounds.valid() || !selectionBounds.valid())
        return {};
    const double offset = alignmentCoordinate(selectionBounds, axis, type) -
                          alignmentCoordinate(modelBounds, axis, type);
    if (axis == AlignmentAxis::X)
        return {offset, 0, 0};
    if (axis == AlignmentAxis::Y)
        return {0, offset, 0};
    return {0, 0, offset};
}
