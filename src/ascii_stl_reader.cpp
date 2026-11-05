// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/stl_reader.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace stl_slicer {
namespace {

enum class State { Solid, Facet, OuterLoop, Vertex, EndLoop, EndFacet };

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool point(std::istringstream &line, Vec3 &value) {
    return bool(line >> value.x >> value.y >> value.z) &&
           std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool hasExtra(std::istringstream &line) {
    std::string token;
    return bool(line >> token);
}

} // namespace

TriangleMesh AsciiStlReader::read(const std::filesystem::path &path) const {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open ASCII STL file: " + path.string());
    return read(input);
}

TriangleMesh AsciiStlReader::read(std::istream &input) const {
    TriangleMesh mesh;
    State state = State::Solid;
    Triangle triangle;
    std::size_t vertexCount = 0;
    std::size_t lineNumber = 0;
    std::string text;
    while (std::getline(input, text)) {
        ++lineNumber;
        if (lineNumber == 1 && text.compare(0, 3, "\xef\xbb\xbf") == 0)
            text.erase(0, 3);
        std::istringstream line(text);
        line.imbue(std::locale::classic());
        std::string keyword;
        if (!(line >> keyword))
            continue;
        keyword = lower(keyword);
        bool valid = false;
        if (state == State::Solid && keyword == "solid") {
            state = State::Facet;
            valid = true;
        } else if (state == State::Facet && keyword == "endsolid") {
            state = State::Solid;
            valid = true;
        } else if (state == State::Facet && keyword == "facet") {
            std::string normalKeyword;
            valid = bool(line >> normalKeyword) && lower(normalKeyword) == "normal" &&
                    point(line, triangle.normal) && !hasExtra(line);
            if (valid)
                state = State::OuterLoop;
        } else if (state == State::OuterLoop && keyword == "outer") {
            std::string loopKeyword;
            valid = bool(line >> loopKeyword) && lower(loopKeyword) == "loop" && !hasExtra(line);
            if (valid) {
                vertexCount = 0;
                state = State::Vertex;
            }
        } else if (state == State::Vertex && keyword == "vertex") {
            valid = point(line, triangle.vertices[vertexCount]) && !hasExtra(line);
            if (valid && ++vertexCount == triangle.vertices.size())
                state = State::EndLoop;
        } else if (state == State::EndLoop && keyword == "endloop") {
            valid = !hasExtra(line);
            if (valid)
                state = State::EndFacet;
        } else if (state == State::EndFacet && keyword == "endfacet") {
            valid = !hasExtra(line);
            if (valid) {
                mesh.addTriangle(triangle);
                state = State::Facet;
            }
        }
        if (!valid)
            throw std::runtime_error("Invalid ASCII STL at line " + std::to_string(lineNumber));
    }
    if (input.bad() || state != State::Solid || mesh.triangles().empty())
        throw std::runtime_error("Incomplete or empty ASCII STL file");
    return mesh;
}

} // namespace stl_slicer
