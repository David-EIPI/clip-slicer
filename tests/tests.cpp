#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace stl_slicer;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void addFace(TriangleMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    mesh.addTriangle({{}, {a, b, c}, 0});
    mesh.addTriangle({{}, {a, c, d}, 0});
}

void addBox(TriangleMesh& mesh, double x0, double y0, double z0,
            double x1, double y1, double z1) {
    const Vec3 a{x0,y0,z0}, b{x1,y0,z0}, c{x1,y1,z0}, d{x0,y1,z0};
    const Vec3 e{x0,y0,z1}, f{x1,y0,z1}, g{x1,y1,z1}, h{x0,y1,z1};
    addFace(mesh, a,d,c,b); addFace(mesh, e,f,g,h);
    addFace(mesh, a,b,f,e); addFace(mesh, b,c,g,f);
    addFace(mesh, c,d,h,g); addFace(mesh, d,a,e,h);
}

double area(const SlicePath& path) {
    double result = 0;
    for (std::size_t i = 0; i + 1 < path.points.size(); ++i)
        result += path.points[i].x * path.points[i+1].y - path.points[i+1].x * path.points[i].y;
    return result / 2;
}

void appendU16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value)); bytes.push_back(static_cast<char>(value >> 8));
}

void appendU32(std::string& bytes, std::uint32_t value) {
    appendU16(bytes, static_cast<std::uint16_t>(value));
    appendU16(bytes, static_cast<std::uint16_t>(value >> 16));
}

void appendFloat(std::string& bytes, float value) {
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); appendU32(bytes, bits);
}

void testBinaryStlReader() {
    std::string bytes(80, 'H'); appendU32(bytes, 1);
    for (float value : {0.f,0.f,1.f, 0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f}) appendFloat(bytes, value);
    appendU16(bytes, 7);
    std::istringstream input(bytes, std::ios::binary);
    const auto mesh = BinaryStlReader{}.read(input);
    require(mesh.triangles().size() == 1, "STL triangle count");
    require(mesh.triangles()[0].attribute == 7, "STL attribute");
    require(mesh.bounds().max.x == 1 && mesh.bounds().max.y == 1, "STL bounds");

    bytes.pop_back();
    std::istringstream truncated(bytes, std::ios::binary);
    bool rejected = false;
    try { (void)BinaryStlReader{}.read(truncated); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "truncated STL rejected");
}

void testCubeSlices() {
    TriangleMesh cube; addBox(cube, 0, 0, 0, 10, 10, 1);
    const auto data = Slicer{{0.25, 1e-7}}.slice(cube);
    require(data.layers.size() == 4, "cube layer count");
    for (const auto& layer : data.layers) {
        require(layer.paths.size() == 1, "cube path count");
        require(layer.paths[0].type == PathType::External, "cube external path");
        require(layer.paths[0].points.front().x == layer.paths[0].points.back().x &&
                layer.paths[0].points.front().y == layer.paths[0].points.back().y, "cube closed path");
        require(area(layer.paths[0]) > 99.999, "cube CCW area");
    }
}

void testNestedContours() {
    TriangleMesh mesh;
    addBox(mesh, 0, 0, 0, 10, 10, 1);
    addBox(mesh, 3, 3, 0, 7, 7, 1);
    const auto data = Slicer{{0.5, 1e-7}}.slice(mesh);
    require(data.layers.front().paths.size() == 2, "nested path count");
    unsigned external = 0, internal = 0;
    for (const auto& path : data.layers.front().paths) {
        if (path.type == PathType::External) { ++external; require(area(path) > 0, "external CCW"); }
        if (path.type == PathType::Internal) { ++internal; require(area(path) < 0, "internal CW"); }
    }
    require(external == 1 && internal == 1, "nested classification");
}

void testCliWriters() {
    TriangleMesh cube; addBox(cube, 0, 0, 0, 1, 1, 1);
    const auto data = Slicer{{0.5, 1e-7}}.slice(cube);
    std::ostringstream ascii;
    CliWriter{{CliEncoding::Ascii, 1.0, 3}}.write(data, ascii);
    require(ascii.str().find("$$POLYLINE/3,1,") != std::string::npos, "ASCII CLI polyline");
    require(ascii.str().find("$$GEOMETRYEND") != std::string::npos, "ASCII CLI end");

    std::ostringstream binary(std::ios::binary);
    CliWriter{}.write(data, binary);
    const std::string output = binary.str();
    const auto end = output.find("$$HEADEREND");
    require(end != std::string::npos, "binary CLI header");
    const std::size_t geometry = end + std::strlen("$$HEADEREND");
    require(static_cast<unsigned char>(output[geometry]) == 127 && output[geometry + 1] == 0,
            "binary CLI layer-long command");
}

} // namespace

int main() {
    try {
        testBinaryStlReader(); testCubeSlices(); testNestedContours(); testCliWriters();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
