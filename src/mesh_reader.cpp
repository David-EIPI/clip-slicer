// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/mesh_reader.hpp"
#include "stl_slicer/obj_reader.hpp"
#include "stl_slicer/stl_reader.hpp"
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace stl_slicer {
namespace {

std::uint32_t littleEndian32(const unsigned char *bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

bool asciiSample(const unsigned char *bytes, std::size_t size) {
    const bool bom = size >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
                     bytes[2] == 0xbf;
    for (std::size_t index = bom ? 3 : 0; index < size; ++index) {
        const unsigned char value = bytes[index];
        if (value != '\n' && value != '\r' && value != '\t' &&
            (value < 32 || value > 126))
            return false;
    }
    return true;
}

} // namespace

MeshFormat detectMeshFormat(const unsigned char *sample,
                            std::size_t sampleSize,
                            std::uint64_t totalSize) {
    if (sampleSize >= 84 && totalSize >= 84) {
        const std::uint64_t triangles = littleEndian32(sample + 80);
        if (84ULL + triangles * 50ULL == totalSize)
            return MeshFormat::BinaryStl;
    }
    if (!asciiSample(sample, sampleSize))
        return MeshFormat::Unknown;
    std::string text(reinterpret_cast<const char *>(sample), sampleSize);
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
        text.erase(0, 3);
    std::istringstream input(text);
    std::string line;
    bool objPreamble = false;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#')
            continue;
        std::string keyword;
        std::istringstream tokens(line.substr(first));
        tokens >> keyword;
        for (char &character : keyword)
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        if (keyword == "solid")
            return MeshFormat::AsciiStl;
        if (keyword == "v" || keyword == "f")
            return MeshFormat::Obj;
        if (keyword != "mtllib" && keyword != "o" && keyword != "g" &&
            keyword != "s" && keyword != "vn" && keyword != "vt" &&
            keyword != "usemtl")
            return MeshFormat::Unknown;
        objPreamble = true;
    }
    return objPreamble ? MeshFormat::Obj : MeshFormat::Unknown;
}

TriangleMesh MeshReader::read(const std::filesystem::path &path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open mesh file: " + path.string());
    return read(input);
}

TriangleMesh MeshReader::read(std::istream &input) const {
    const std::streampos start = input.tellg();
    if (start == std::streampos(-1))
        throw std::invalid_argument("Mesh input stream must be seekable");
    input.seekg(0, std::ios::end);
    const std::streampos end = input.tellg();
    if (end == std::streampos(-1) || end < start)
        throw std::runtime_error("Cannot determine mesh input size");
    input.seekg(start);

    std::array<unsigned char, 4096> sample{};
    input.read(reinterpret_cast<char *>(sample.data()), sample.size());
    const std::size_t sampleSize = static_cast<std::size_t>(input.gcount());
    input.clear();
    input.seekg(start);
    const MeshFormat format = detectMeshFormat(
        sample.data(), sampleSize, static_cast<std::uint64_t>(end - start));
    switch (format) {
    case MeshFormat::BinaryStl:
        return BinaryStlReader{}.read(input);
    case MeshFormat::AsciiStl:
        return AsciiStlReader{}.read(input);
    case MeshFormat::Obj:
        return ObjReader{}.read(input);
    case MeshFormat::Unknown:
        throw std::runtime_error("Unsupported or unrecognized mesh format");
    }
    throw std::runtime_error("Unsupported mesh format");
}

} // namespace stl_slicer
