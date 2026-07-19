// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/scene_model.hpp"
#include <cstdint>
#include <vector>

struct VisualizationMesh {
    std::vector<stl_slicer::RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
};

VisualizationMesh BuildSliceCaps(const stl_slicer::SliceData &slices);
VisualizationMesh BuildSliceSurfaces(const stl_slicer::SliceData &slices);
VisualizationMesh BuildUnsupportedSurfaces(const stl_slicer::SliceData &slices);
void SmoothRenderNormals(std::vector<stl_slicer::RenderVertex> &vertices);
