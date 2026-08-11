// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/geometry.hpp"

enum class AlignmentAxis { X, Y, Z };
enum class AlignmentType { Minimum, Center, Maximum };

stl_slicer::Vec3 AlignmentTranslation(const stl_slicer::Bounds3 &modelBounds,
                                      const stl_slicer::Bounds3 &selectionBounds,
                                      AlignmentAxis axis,
                                      AlignmentType type);
