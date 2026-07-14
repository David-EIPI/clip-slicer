#pragma once

#include "stl_slicer/model.hpp"
#include <filesystem>
#include <ostream>
#include <string>

namespace stl_slicer {

struct BinaryStlWriterOptions {
    std::string header = "CLIP Slicer binary STL";
};

class BinaryStlWriter {
  public:
    explicit BinaryStlWriter(BinaryStlWriterOptions options = {});
    void write(const TriangleMesh &mesh, const std::filesystem::path &path) const;
    void write(const TriangleMesh &mesh, std::ostream &output) const;

  private:
    BinaryStlWriterOptions options_;
};

} // namespace stl_slicer
