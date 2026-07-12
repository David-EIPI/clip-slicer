#pragma once

#include "stl_slicer/slice.hpp"
#include <filesystem>
#include <ostream>

namespace stl_slicer {

enum class CliEncoding { Binary, Ascii };

struct CliWriterOptions {
    CliEncoding encoding = CliEncoding::Binary;
    double unitsMillimeters = 1.0;
    std::uint32_t modelId = 1;
};

class CliWriter {
public:
    explicit CliWriter(CliWriterOptions options = {});
    void write(const SliceData& data, const std::filesystem::path& path) const;
    void write(const SliceData& data, std::ostream& output) const;

private:
    CliWriterOptions options_;
};

} // namespace stl_slicer
