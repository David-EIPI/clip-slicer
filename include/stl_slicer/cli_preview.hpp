// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/slice.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stl_slicer {

// Pixels are row-major, top to bottom, with one byte per pixel (0 or 1).
struct CliPreview {
    std::uint16_t width = 1;
    std::uint16_t height = 1;
    std::vector<std::uint8_t> pixels;
};

// Views look from the negative X, Y, and Z ends, respectively. Screen-right
// points toward -Y, +X, and -X; screen-up points toward +Z, +Z, and +Y.
std::array<CliPreview, 3> makeCliPreviews(const SliceData &data);

// Alternating zero/one run lengths, starting with zero, as unsigned LEB128.
// The runs cover width * height pixels in row-major order.
std::string encodeCliPreviewBase64(const CliPreview &preview);
CliPreview decodeCliPreviewBase64(std::uint16_t width,
                                 std::uint16_t height,
                                 std::string_view encoded);

} // namespace stl_slicer
