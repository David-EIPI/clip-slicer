// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/cli_reader.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace stl_slicer {
namespace {
std::uint16_t readU16(std::istream &in) {
    unsigned char b[2];
    if (!in.read(reinterpret_cast<char *>(b), 2))
        throw std::runtime_error("Truncated CLI command");
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}
std::uint32_t readU32(std::istream &in) {
    unsigned char b[4];
    if (!in.read(reinterpret_cast<char *>(b), 4))
        throw std::runtime_error("Truncated CLI parameter");
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
           (std::uint32_t(b[3]) << 24);
}
float readFloat(std::istream &in) {
    const auto bits = readU32(in);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
} // namespace

SliceData CliReader::read(const std::filesystem::path &path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open CLI file: " + path.string());
    return read(input);
}

SliceData CliReader::read(std::istream &input) const {
    constexpr const char *endMarker = "$$HEADEREND";
    std::string header;
    char character;
    while (input.get(character)) {
        header.push_back(character);
        if (header.size() >= std::strlen(endMarker) &&
            header.compare(
                header.size() - std::strlen(endMarker), std::strlen(endMarker), endMarker) == 0)
            break;
    }
    if (header.size() < std::strlen(endMarker) ||
        header.compare(header.size() - std::strlen(endMarker), std::strlen(endMarker), endMarker) !=
            0)
        throw std::runtime_error("CLI header has no HEADEREND");
    const bool binary = header.find("$$BINARY") != std::string::npos;
    double units = 1.0;
    const auto unitsAt = header.find("$$UNITS/");
    if (unitsAt != std::string::npos)
        units = std::stod(header.substr(unitsAt + 8));
    if (!binary)
        throw std::runtime_error("Only binary CLI input is currently supported");
    SliceData result;
    while (input.peek() != std::char_traits<char>::eof()) {
        const auto command = readU16(input);
        if (command == 127) {
            result.layers.push_back({readFloat(input) * units, {}});
        } else if (command == 130) {
            if (result.layers.empty())
                throw std::runtime_error("CLI polyline precedes first layer");
            (void)readU32(input);
            const auto direction = readU32(input), count = readU32(input);
            SlicePath path;
            path.type = direction <= 2 ? static_cast<PathType>(direction) : PathType::Open;
            path.points.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
                path.points.push_back({readFloat(input) * units, readFloat(input) * units});
            result.layers.back().paths.push_back(std::move(path));
        } else
            throw std::runtime_error("Unsupported binary CLI command: " + std::to_string(command));
    }
    if (result.layers.size() > 1)
        result.thickness = result.layers[1].z - result.layers[0].z;
    for (const auto &layer : result.layers)
        for (const auto &path : layer.paths)
            for (const auto &p : path.points)
                result.sourceBounds.include({p.x, p.y, layer.z});
    return result;
}
} // namespace stl_slicer
