// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "stl_slicer/obj_reader.hpp"
#include <clipper2/clipper.triangulation.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace stl_slicer {
namespace {

Vec3 faceNormal(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 normal{u.y * v.z - u.z * v.y,
                u.z * v.x - u.x * v.z,
                u.x * v.y - u.y * v.x};
    const double length = std::hypot(normal.x, normal.y, normal.z);
    if (length == 0.0 || !std::isfinite(length))
        throw std::runtime_error("OBJ face contains a degenerate triangle");
    normal.x /= length;
    normal.y /= length;
    normal.z /= length;
    return normal;
}

std::size_t vertexIndex(const std::string &reference, std::size_t available) {
    const std::string number = reference.substr(0, reference.find('/'));
    std::size_t consumed = 0;
    const long long index = std::stoll(number, &consumed);
    if (consumed != number.size() || index == 0 ||
        available > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
        throw std::runtime_error("Invalid OBJ face vertex index");
    const long long count = static_cast<long long>(available);
    if (index > count || index < -count)
        throw std::runtime_error("OBJ face vertex index is out of range");
    return static_cast<std::size_t>(index > 0 ? index - 1 : count + index);
}

int projectionAxis(const std::vector<Vec3> &points) {
    Vec3 normal;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Vec3 &a = points[index];
        const Vec3 &b = points[(index + 1) % points.size()];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    const double ax = std::abs(normal.x);
    const double ay = std::abs(normal.y);
    const double az = std::abs(normal.z);
    if (std::max({ax, ay, az}) == 0.0)
        throw std::runtime_error("OBJ polygon has zero projected area");
    if (ax >= ay && ax >= az)
        return 0;
    return ay >= az ? 1 : 2;
}

Clipper2Lib::PointD project(const Vec3 &point, int droppedAxis, const Vec3 &origin) {
    if (droppedAxis == 0)
        return {point.y - origin.y, point.z - origin.z};
    if (droppedAxis == 1)
        return {point.z - origin.z, point.x - origin.x};
    return {point.x - origin.x, point.y - origin.y};
}

double twiceArea(const Clipper2Lib::PathD &points) {
    double result = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto &a = points[index];
        const auto &b = points[(index + 1) % points.size()];
        result += a.x * b.y - b.x * a.y;
    }
    return result;
}

void addFace(TriangleMesh &mesh, const std::vector<Vec3> &points) {
    if (points.size() == 3) {
        Triangle triangle;
        triangle.vertices = {points[0], points[1], points[2]};
        triangle.normal = faceNormal(points[0], points[1], points[2]);
        mesh.addTriangle(std::move(triangle));
        return;
    }

    const int axis = projectionAxis(points);
    Clipper2Lib::PathD polygon;
    polygon.reserve(points.size());
    for (const Vec3 &point : points)
        polygon.push_back(project(point, axis, points.front()));
    const double winding = twiceArea(polygon);
    if (winding == 0.0)
        throw std::runtime_error("OBJ polygon has zero projected area");

    Clipper2Lib::PathsD triangles;
    const auto result = Clipper2Lib::Triangulate({polygon}, 6, triangles, false);
    if (result != Clipper2Lib::TriangulateResult::success || triangles.empty())
        throw std::runtime_error("OBJ polygon could not be triangulated");
    for (const auto &polygonTriangle : triangles) {
        if (polygonTriangle.size() != 3)
            throw std::runtime_error("OBJ triangulator returned a non-triangle");
        Triangle triangle;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto &vertex = polygonTriangle[corner];
            double nearest = std::numeric_limits<double>::infinity();
            std::size_t match = points.size();
            for (std::size_t index = 0; index < polygon.size(); ++index) {
                const double dx = vertex.x - polygon[index].x;
                const double dy = vertex.y - polygon[index].y;
                const double distance = dx * dx + dy * dy;
                if (distance < nearest) {
                    nearest = distance;
                    match = index;
                }
            }
            if (nearest > 1e-10)
                throw std::runtime_error("OBJ triangulation changed a face vertex");
            triangle.vertices[corner] = points[match];
        }
        if (winding * twiceArea(polygonTriangle) < 0.0)
            std::swap(triangle.vertices[1], triangle.vertices[2]);
        triangle.normal = faceNormal(
            triangle.vertices[0], triangle.vertices[1], triangle.vertices[2]);
        mesh.addTriangle(std::move(triangle));
    }
}

} // namespace

TriangleMesh ObjReader::read(const std::filesystem::path &path) const {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open OBJ file: " + path.string());
    return read(input);
}

TriangleMesh ObjReader::read(std::istream &input) const {
    std::vector<Vec3> vertices;
    TriangleMesh mesh;
    std::string text;
    std::size_t lineNumber = 0;
    while (std::getline(input, text)) {
        ++lineNumber;
        if (lineNumber == 1 && text.compare(0, 3, "\xef\xbb\xbf") == 0)
            text.erase(0, 3);
        std::istringstream line(text);
        line.imbue(std::locale::classic());
        std::string keyword;
        if (!(line >> keyword) || keyword[0] == '#')
            continue;
        if (keyword == "v") {
            Vec3 point;
            if (!(line >> point.x >> point.y >> point.z) ||
                !std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.z))
                throw std::runtime_error("Invalid OBJ vertex at line " +
                                         std::to_string(lineNumber));
            vertices.push_back(point);
        } else if (keyword == "f") {
            try {
                std::vector<Vec3> face;
                std::string reference;
                while (line >> reference) {
                    if (reference[0] == '#')
                        break;
                    face.push_back(vertices[vertexIndex(reference, vertices.size())]);
                }
                if (face.size() > 3 && face.front().x == face.back().x &&
                    face.front().y == face.back().y && face.front().z == face.back().z)
                    face.pop_back();
                if (face.size() < 3)
                    throw std::runtime_error("face has fewer than three vertices");
                addFace(mesh, face);
            } catch (const std::exception &error) {
                throw std::runtime_error("Invalid OBJ face at line " +
                                         std::to_string(lineNumber) + ": " + error.what());
            }
        } else if (keyword == "curv" || keyword == "curv2" || keyword == "surf") {
            throw std::runtime_error("Free-form OBJ surfaces are not supported");
        }
    }
    if (input.bad() || mesh.triangles().empty())
        throw std::runtime_error("OBJ file contains no triangle faces");
    return mesh;
}

} // namespace stl_slicer
