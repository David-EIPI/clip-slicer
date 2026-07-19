// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/stl_reader.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace stl_slicer {
namespace {

void readExact(std::istream &in, char *data, std::size_t size, const char *what) {
    if (!in.read(data, static_cast<std::streamsize>(size)))
        throw std::runtime_error(std::string("Truncated binary STL while reading ") + what);
}

std::uint16_t u16le(const unsigned char *p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t u32le(const unsigned char *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

float f32le(const unsigned char *p) {
    const std::uint32_t bits = u32le(p);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value))
        throw std::runtime_error("Binary STL contains a non-finite coordinate");
    return value;
}

} // namespace

TriangleMesh BinaryStlReader::read(const std::filesystem::path &path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open STL file: " + path.string());
    return read(input);
}

TriangleMesh BinaryStlReader::read(std::istream &input) const {
    std::array<char, 80> header{};
    readExact(input, header.data(), header.size(), "header");
    std::array<unsigned char, 4> countBytes{};
    readExact(
        input, reinterpret_cast<char *>(countBytes.data()), countBytes.size(), "triangle count");
    const std::uint32_t count = u32le(countBytes.data());
    if (count > (std::numeric_limits<std::size_t>::max() / sizeof(Triangle)))
        throw std::runtime_error("Binary STL triangle count is too large");

    TriangleMesh mesh;
    mesh.setHeader(std::string(header.data(), header.size()));
    mesh.reserve(count);
    std::array<unsigned char, 50> record{};
    for (std::uint32_t i = 0; i < count; ++i) {
        readExact(input, reinterpret_cast<char *>(record.data()), record.size(), "triangle record");
        Triangle triangle;
        triangle.normal = {f32le(&record[0]), f32le(&record[4]), f32le(&record[8])};
        for (std::size_t v = 0; v < 3; ++v) {
            const std::size_t offset = 12 + v * 12;
            triangle.vertices[v] = {
                f32le(&record[offset]), f32le(&record[offset + 4]), f32le(&record[offset + 8])};
        }
        triangle.attribute = u16le(&record[48]);
        mesh.addTriangle(std::move(triangle));
    }
    return mesh;
}

} // namespace stl_slicer
