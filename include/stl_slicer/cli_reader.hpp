// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/slice.hpp"
#include <filesystem>
#include <istream>

namespace stl_slicer {

class CliReader {
  public:
    SliceData read(const std::filesystem::path &path) const;
    SliceData read(std::istream &input) const;
};

} // namespace stl_slicer
