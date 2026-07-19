// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/stl_writer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stl_slicer {
namespace {

void writeExact(std::ostream &output, const char *data, std::size_t size, const char *what) {
    output.write(data, static_cast<std::streamsize>(size));
    if (!output)
        throw std::runtime_error(std::string("Unable to write binary STL ") + what);
}

void putU16(std::array<unsigned char, 50> &record, std::size_t offset, std::uint16_t value) {
    record[offset] = static_cast<unsigned char>(value);
    record[offset + 1] = static_cast<unsigned char>(value >> 8);
}

void putU32(unsigned char *destination, std::uint32_t value) {
    destination[0] = static_cast<unsigned char>(value);
    destination[1] = static_cast<unsigned char>(value >> 8);
    destination[2] = static_cast<unsigned char>(value >> 16);
    destination[3] = static_cast<unsigned char>(value >> 24);
}

void putFloat(std::array<unsigned char, 50> &record, std::size_t offset, double value) {
    const float converted = static_cast<float>(value);
    if (!std::isfinite(converted))
        throw std::runtime_error("Binary STL component is outside the finite float range");
    std::uint32_t bits;
    std::memcpy(&bits, &converted, sizeof(bits));
    putU32(record.data() + offset, bits);
}

} // namespace

BinaryStlWriter::BinaryStlWriter(BinaryStlWriterOptions options) : options_(std::move(options)) {}

void BinaryStlWriter::write(const TriangleMesh &mesh, const std::filesystem::path &path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Cannot create STL file: " + path.string());
    write(mesh, output);
}

void BinaryStlWriter::write(const TriangleMesh &mesh, std::ostream &output) const {
    if (mesh.triangles().size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Binary STL contains too many triangles");

    std::array<char, 80> header{};
    const std::size_t headerSize = std::min(header.size(), options_.header.size());
    std::copy_n(options_.header.data(), headerSize, header.data());
    writeExact(output, header.data(), header.size(), "header");

    std::array<unsigned char, 4> count{};
    putU32(count.data(), static_cast<std::uint32_t>(mesh.triangles().size()));
    writeExact(output,
               reinterpret_cast<const char *>(count.data()),
               count.size(),
               "triangle count");

    std::array<unsigned char, 50> record{};
    for (const Triangle &triangle : mesh.triangles()) {
        putFloat(record, 0, triangle.normal.x);
        putFloat(record, 4, triangle.normal.y);
        putFloat(record, 8, triangle.normal.z);
        for (std::size_t vertex = 0; vertex < triangle.vertices.size(); ++vertex) {
            const std::size_t offset = 12 + vertex * 12;
            putFloat(record, offset, triangle.vertices[vertex].x);
            putFloat(record, offset + 4, triangle.vertices[vertex].y);
            putFloat(record, offset + 8, triangle.vertices[vertex].z);
        }
        putU16(record, 48, triangle.attribute);
        writeExact(output,
                   reinterpret_cast<const char *>(record.data()),
                   record.size(),
                   "triangle record");
    }
}

} // namespace stl_slicer
