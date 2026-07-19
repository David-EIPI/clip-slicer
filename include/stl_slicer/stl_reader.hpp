// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/model.hpp"
#include <filesystem>
#include <istream>

namespace stl_slicer {

class BinaryStlReader {
  public:
    TriangleMesh read(const std::filesystem::path &path) const;
    TriangleMesh read(std::istream &input) const;
};

} // namespace stl_slicer
