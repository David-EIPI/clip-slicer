// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/model.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>

namespace stl_slicer {

enum class MeshFormat { BinaryStl, AsciiStl, Obj, Unknown };

MeshFormat detectMeshFormat(const unsigned char *sample,
                            std::size_t sampleSize,
                            std::uint64_t totalSize);

class MeshReader {
  public:
    TriangleMesh read(const std::filesystem::path &path) const;
    TriangleMesh read(std::istream &input) const;
};

} // namespace stl_slicer
