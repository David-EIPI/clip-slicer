#include "stl_slicer/cli_reader.hpp"
#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/orientation_optimizer.hpp"
#include "stl_slicer/scene_model.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
#include "stl_slicer/stl_writer.hpp"
#include "stl_slicer/support_generator.hpp"
#include "stl_slicer/unsupported_area.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace stl_slicer;

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void addFace(TriangleMesh &mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    mesh.addTriangle({{}, {a, b, c}, 0});
    mesh.addTriangle({{}, {a, c, d}, 0});
}

void addBox(TriangleMesh &mesh, double x0, double y0, double z0, double x1, double y1, double z1) {
    const Vec3 a{x0, y0, z0}, b{x1, y0, z0}, c{x1, y1, z0}, d{x0, y1, z0};
    const Vec3 e{x0, y0, z1}, f{x1, y0, z1}, g{x1, y1, z1}, h{x0, y1, z1};
    addFace(mesh, a, d, c, b);
    addFace(mesh, e, f, g, h);
    addFace(mesh, a, b, f, e);
    addFace(mesh, b, c, g, f);
    addFace(mesh, c, d, h, g);
    addFace(mesh, d, a, e, h);
}

double area(const SlicePath &path) {
    double result = 0;
    for (std::size_t i = 0; i + 1 < path.points.size(); ++i)
        result += path.points[i].x * path.points[i + 1].y - path.points[i + 1].x * path.points[i].y;
    return result / 2;
}

void appendU16(std::string &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value));
    bytes.push_back(static_cast<char>(value >> 8));
}

void appendU32(std::string &bytes, std::uint32_t value) {
    appendU16(bytes, static_cast<std::uint16_t>(value));
    appendU16(bytes, static_cast<std::uint16_t>(value >> 16));
}

void appendFloat(std::string &bytes, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

void testBinaryStlReader() {
    std::string bytes(80, 'H');
    appendU32(bytes, 1);
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, -1.f, 1.f, 0.f, 2.f, 0.f, 1.f, 0.5f})
        appendFloat(bytes, value);
    appendU16(bytes, 7);
    std::istringstream input(bytes, std::ios::binary);
    const auto mesh = BinaryStlReader{}.read(input);
    require(mesh.triangles().size() == 1, "STL triangle count");
    require(mesh.triangles()[0].attribute == 7, "STL attribute");
    require(mesh.triangles()[0].minZ == -1 && mesh.triangles()[0].maxZ == 2,
            "STL triangle Z bounds");
    require(mesh.bounds().max.x == 1 && mesh.bounds().max.y == 1, "STL bounds");

    bytes.pop_back();
    std::istringstream truncated(bytes, std::ios::binary);
    bool rejected = false;
    try {
        (void)BinaryStlReader{}.read(truncated);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "truncated STL rejected");
}

void testBinaryStlWriter() {
    TriangleMesh source;
    Triangle triangle;
    triangle.normal = {0, 0, 1};
    triangle.vertices = {{{1.25, -2.5, 3.75}, {4, 5, 6}, {7, 8, 9}}};
    triangle.attribute = 42;
    source.addTriangle(triangle);

    std::ostringstream output(std::ios::binary);
    BinaryStlWriter{{"Writer test"}}.write(source, output);
    const std::string bytes = output.str();
    require(bytes.size() == 134, "binary STL writer size");

    std::istringstream input(bytes, std::ios::binary);
    const TriangleMesh roundTrip = BinaryStlReader{}.read(input);
    require(roundTrip.triangles().size() == 1, "binary STL writer triangle count");
    const Triangle &written = roundTrip.triangles().front();
    require(written.attribute == 42, "binary STL writer attribute");
    require(written.normal.z == 1 && written.vertices[0].x == 1.25 &&
                written.vertices[0].y == -2.5 && written.vertices[2].z == 9,
            "binary STL writer geometry");

    MeshSceneModel model("translated", source);
    model.transform = Mat4::translation(10, 20, 30);
    std::ostringstream transformedOutput(std::ios::binary);
    BinaryStlWriter{}.write(transformedMesh(model), transformedOutput);
    std::istringstream transformedInput(transformedOutput.str(), std::ios::binary);
    const TriangleMesh transformedMeshData = BinaryStlReader{}.read(transformedInput);
    const Triangle &transformed = transformedMeshData.triangles().front();
    require(transformed.vertices[0].x == 11.25 && transformed.vertices[0].y == 17.5 &&
                transformed.vertices[0].z == 33.75,
            "binary STL transformed geometry");
}

void testCubeSlices() {
    TriangleMesh cube;
    addBox(cube, 0, 0, 0, 10, 10, 1);
    const auto data = Slicer{{0.25, 1e-7}}.slice(cube);
    require(data.layers.size() == 4, "cube layer count");
    for (const auto &layer : data.layers) {
        require(layer.paths.size() == 1, "cube path count");
        require(layer.paths[0].type == PathType::External, "cube external path");
        require(layer.paths[0].points.front().x == layer.paths[0].points.back().x &&
                    layer.paths[0].points.front().y == layer.paths[0].points.back().y,
                "cube closed path");
        require(area(layer.paths[0]) > 99.999, "cube CCW area");
    }
}

void testSliceCancellation() {
    TriangleMesh cube;
    addBox(cube, 0, 0, 0, 10, 10, 1);
    std::atomic<bool> cancel{true};
    require(Slicer{{0.25, 1e-7}}.slice(cube, &cancel).layers.empty(),
            "cancelled full slice produced layers");
    require(Slicer{{0.25, 1e-7}}.sliceAt(cube, 0.5, &cancel).paths.empty(),
            "cancelled interactive slice produced paths");
}

void testNestedContours() {
    TriangleMesh mesh;
    addBox(mesh, 0, 0, 0, 10, 10, 1);
    addBox(mesh, 3, 3, 0, 7, 7, 1);
    addBox(mesh, 4, 4, 0, 6, 6, 1);
    const auto data = Slicer{{0.5, 1e-7}}.slice(mesh);
    require(data.layers.front().paths.size() == 3, "nested path count");
    unsigned external = 0, internal = 0;
    for (const auto &path : data.layers.front().paths) {
        if (path.type == PathType::External) {
            ++external;
            require(area(path) > 0, "external CCW");
        }
        if (path.type == PathType::Internal) {
            ++internal;
            require(area(path) < 0, "internal CW");
        }
    }
    require(external == 2 && internal == 1, "nested classification");
    require(data.layers.front().paths[0].type == PathType::External &&
                data.layers.front().paths[1].type == PathType::Internal &&
                data.layers.front().paths[2].type == PathType::External,
            "contours ordered from parent to descendant");
}

void testToleranceIndexedConnection() {
    TriangleMesh mesh;
    const double epsilon = 1e-4;
    const auto addSliceSegment = [&](Vec2 a, Vec2 b) {
        const Vec3 below{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 0.0};
        mesh.addTriangle({{}, {below, {a.x, a.y, 0.5}, {b.x, b.y, 0.5}}, 0});
    };
    addSliceSegment({0, 0}, {1, 0});
    addSliceSegment({1 + epsilon, 0}, {1, 1});
    addSliceSegment({1, 1 + epsilon}, {0, 1});
    addSliceSegment({-epsilon, 1}, {0, -epsilon});

    const auto data = Slicer{{0.5, 1e-3}}.slice(mesh);
    require(data.layers.size() == 1, "tolerance layer count");
    require(data.layers[0].paths.size() == 1, "indexed endpoints joined across cells");
    require(data.layers[0].paths[0].type == PathType::External, "tolerance path closed");
}

void testTolerancePreservesShortEdges() {
    TriangleMesh mesh;
    addBox(mesh, 0, 0, 0, 0.05, 1, 1);

    const auto data = Slicer{{0.5, 0.1, 0.1}}.slice(mesh);
    require(data.layers.size() == 2, "short-edge layer count");
    for (const auto &layer : data.layers) {
        require(layer.paths.size() == 1, "short valid segments were discarded");
        require(layer.paths[0].type == PathType::External,
                "nearby path ends closed before exact segments were connected");
        require(std::abs(area(layer.paths[0]) - 0.05) < 1e-9, "short-edge contour area changed");
    }
}

void testSmallGapHealing() {
    TriangleMesh mesh;
    const double gap = 4e-5;
    const auto addSliceSegment = [&](Vec2 a, Vec2 b) {
        const Vec3 below{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 0.0};
        mesh.addTriangle({{}, {below, {a.x, a.y, 0.5}, {b.x, b.y, 0.5}}, 0});
    };
    addSliceSegment({0, 0}, {1, 0});
    addSliceSegment({1 + gap, 0}, {1, 1});
    addSliceSegment({1, 1 + gap}, {0, 1});
    addSliceSegment({-gap, 1}, {0, -gap});

    const auto data = Slicer{{0.5, 1e-5}}.slice(mesh);
    require(data.layers[0].paths.size() == 1, "small STL gaps healed");
    require(data.layers[0].paths[0].type == PathType::External, "healed path closed");

    const auto strict = Slicer{{0.5, 1e-5, 2e-5}}.slice(mesh);
    require(strict.layers[0].paths[0].type == PathType::Open,
            "explicit gap-healing threshold was ignored");
}

void testReversedSharedEdgeInterpolation() {
    TriangleMesh mesh;
    const Vec3 low{42.40000534057617, 30.656009674072266, -13.128999710083008};
    const Vec3 high{42.55600357055664, 30.66800308227539, -12.777997970581055};
    mesh.addTriangle(
        {{}, {low, high, {42.577003479003906, 30.944011688232422, -12.834999084472656}}, 0});
    mesh.addTriangle(
        {{}, {high, low, {42.5410041809082, 30.404010772705078, -12.948999404907227}}, 0});

    const auto data = Slicer{{0.350999450683594, 1e-12, 1e-12}}.slice(mesh);
    require(data.layers.size() == 1, "shared-edge interpolation layer");
    require(data.layers[0].paths.size() == 1, "reversed shared-edge points identical");
    require(data.layers[0].paths[0].points.size() == 3,
            "shared-edge segments connected without healing tolerance");
}

void testCliWriters() {
    TriangleMesh cube;
    addBox(cube, -20, 50, 10, -19, 51, 11);
    const auto data = Slicer{{0.5, 1e-7}}.slice(cube);
    std::ostringstream ascii;
    CliWriter{{CliEncoding::Ascii, 1.0, 3}}.write(data, ascii);
    require(ascii.str().find("$$DIMENSION/0,0,0,1,1,1\n") != std::string::npos,
            "ASCII CLI dimensions start at origin");
    require(ascii.str().find("$$USERDATA/CLIPSlicer,0,\n") != std::string::npos,
            "CLI reader compatibility directive");
    require(ascii.str().find("$$LAYER/0.25\n") != std::string::npos,
            "ASCII CLI first layer at half step");
    require(ascii.str().find("$$POLYLINE/3,1,") != std::string::npos, "ASCII CLI polyline");
    require(ascii.str().find(",-20") == std::string::npos &&
                ascii.str().find(",50") == std::string::npos,
            "ASCII CLI XY coordinates recentered");
    require(ascii.str().find("$$GEOMETRYEND") != std::string::npos, "ASCII CLI end");

    std::ostringstream binary(std::ios::binary);
    CliWriter{}.write(data, binary);
    const std::string output = binary.str();
    require(output.find("$$USERDATA/CLIPSlicer,0,\n") != std::string::npos,
            "binary CLI reader compatibility directive");
    const auto end = output.find("$$HEADEREND");
    require(end != std::string::npos, "binary CLI header");
    const std::size_t geometry = end + std::strlen("$$HEADEREND");
    require(static_cast<unsigned char>(output[geometry]) == 127 && output[geometry + 1] == 0,
            "binary CLI layer-long command");
    float firstZ = 0.0f;
    std::memcpy(&firstZ, output.data() + geometry + 2, sizeof(firstZ));
    require(std::abs(firstZ - 0.25f) < 1e-6f, "binary CLI first layer at half step");

    std::istringstream cliInput(output, std::ios::binary);
    const auto roundTrip = CliReader{}.read(cliInput);
    require(roundTrip.layers.size() == data.layers.size(), "binary CLI reader layer count");
    require(roundTrip.layers.front().paths.size() == data.layers.front().paths.size(),
            "binary CLI reader path count");
}

void testSingleAndOffsetSlices() {
    TriangleMesh cube;
    addBox(cube, 0, 0, 0, 1, 1, 1);
    const auto layer = Slicer{}.sliceAt(cube, 0.5);
    require(layer.paths.size() == 1 && layer.paths[0].type == PathType::External,
            "single-plane slice");
    const auto offset = Slicer{{0.25, 1e-7, 0.01, 0.125}}.slice(cube);
    require(offset.layers.size() == 4 && std::abs(offset.layers.front().z - 0.125) < 1e-12,
            "custom first-layer offset");
}

void testMergeSlices() {
    SliceData first;
    first.thickness = 0.1;
    first.layers = {{0.1, {{{PathType::External, {{0, 0}, {1, 0}}}}}},
                    {0.3, {{{PathType::External, {{3, 0}, {4, 0}}}}}}};
    SliceData second;
    second.thickness = 0.1;
    second.layers = {{0.2, {{{PathType::External, {{1, 0}, {2, 0}}}}}},
                     {0.3, {{{PathType::External, {{4, 0}, {5, 0}}}}}}};

    const SliceData merged = mergeSlices({std::cref(first), std::cref(second)});
    require(merged.layers.size() == 4, "Merged slices lost a model's layers");
    require(merged.layers[0].z == 0.1 && merged.layers[1].z == 0.2 && merged.layers[2].z == 0.3 &&
                merged.layers[3].z == 0.3,
            "Merged slices are not stably ordered by height");
    require(merged.layers[2].paths[0].points[0].x == 3 &&
                merged.layers[3].paths[0].points[0].x == 4,
            "Equal-height layers did not preserve model order");

    std::ostringstream output(std::ios::binary);
    CliWriter{}.write(merged, output);
    std::istringstream input(output.str(), std::ios::binary);
    const SliceData roundTrip = CliReader{}.read(input);
    require(roundTrip.layers.size() == 4,
            "CLI export did not preserve layers from every merged model");
}

SlicePath square(double x0, double y0, double x1, double y1) {
    return {PathType::External, {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}};
}

void testUnsupportedAreas() {
    SliceData slices;
    slices.thickness = 0.1;
    slices.layers = {{0.05, {square(0, 0, 10, 10)}},
                     {0.15, {square(0, 0, 12, 10), square(20, 20, 21, 21)}}};

    const UnsupportedAreaResult result = UnsupportedAreaAnalyzer{{45.0}}.analyze(slices);
    require(result.unsupported.layers.size() == 2, "unsupported layer count");
    require(result.unsupported.layers.front().paths.empty(), "first layer must be supported");
    require(!result.unsupported.layers.back().paths.empty(), "overhang was not detected");
    require(result.totalArea > 16.0 && result.totalArea < 21.1,
            "unsupported overhang and orphan area are incorrect");

    SliceData supportedStack;
    supportedStack.layers = {{0.05, {square(0, 0, 10, 10)}}, {0.15, {square(0.1, 0.1, 9.9, 9.9)}}};
    const auto supported = UnsupportedAreaAnalyzer{{30.0}}.analyze(supportedStack);
    require(supported.totalArea < 1e-9, "supported inset was marked unsupported");

    SliceData nearbyOrphan;
    nearbyOrphan.layers = {{0.05, {square(0, 0, 10, 10)}}, {0.15, {square(10.1, 0, 10.2, 0.1)}}};
    const auto orphan = UnsupportedAreaAnalyzer{{30.0}}.analyze(nearbyOrphan);
    require(std::abs(orphan.totalArea - 0.01) < 1e-6,
            "nearby orphan was supported without material beneath its contour");

    SliceData supportedByOverhang;
    supportedByOverhang.layers = {
        {0.05, {square(0, 0, 1, 1)}}, {0.15, {square(0, 0, 2, 1)}}, {0.25, {square(0, 0, 2, 1)}}};
    const auto propagated = UnsupportedAreaAnalyzer{{45.0}}.analyze(supportedByOverhang);
    require(!propagated.unsupported.layers[1].paths.empty(),
            "middle-layer overhang was not detected");
    require(propagated.unsupported.layers[2].paths.empty(),
            "unsupported layer area did not support the layer above it");

    SliceData coefficientStack;
    coefficientStack.layers = {{0.05, {square(0, 0, 1, 1)}}, {0.15, {square(0.5, 0, 1.5, 1)}}};
    const auto defaultOverhang = UnsupportedAreaAnalyzer{{45.0, 1.0}}.analyze(coefficientStack);
    const auto extendedOverhang = UnsupportedAreaAnalyzer{{45.0, 5.0}}.analyze(coefficientStack);
    require(defaultOverhang.totalArea > 0.29 && defaultOverhang.totalArea < 0.31,
            "default overhang coefficient changed");
    require(extendedOverhang.totalArea < 1e-9,
            "overhang coefficient did not extend the supported radius");
}

void testOrientationOptimizer() {
    TriangleMesh cube;
    addBox(cube, 0, 0, 0, 1, 1, 1);
    OrientationOptimizerOptions options;
    options.attempts = 1;
    options.workerCount = 1;
    options.layerThickness = 0.5;
    options.firstLayerOffset = 0.25;
    std::size_t completed = 0;
    double publishedScore = -1.0;
    const auto result = optimizeOrientation(
        cube,
        options,
        nullptr,
        [&](const OrientationCandidate &candidate) { publishedScore = candidate.unsupportedArea; },
        [&](std::size_t current, std::size_t total) {
            completed = current;
            require(total == 1, "optimizer progress total");
        },
        [&](double score) { publishedScore = score; });
    require(!result.cancelled, "optimizer unexpectedly cancelled");
    require(result.completedAttempts == 1 && completed == 1, "optimizer attempt progress");
    require(result.best.unsupportedArea < 1e-9, "supported cube optimization score");
    require(publishedScore < 1e-9, "optimizer did not publish its baseline score");

    std::atomic<bool> cancel{true};
    const auto cancelled = optimizeOrientation(cube, options, &cancel);
    require(cancelled.cancelled && cancelled.completedAttempts == 0,
            "optimizer ignored cancellation before startup");

    options.firstLayerOffset = 0.0;
    bool invalidOffsetRejected = false;
    try {
        (void)optimizeOrientation(cube, options);
    } catch (const std::invalid_argument &) {
        invalidOffsetRejected = true;
    }
    require(invalidOffsetRejected, "optimizer accepted an invalid first-layer offset");
}

void testSupportGenerationScheduling() {
    auto source = std::make_shared<TriangleMesh>();
    addBox(*source, 0, 0, 0, 1, 1, 1);
    auto slices = std::make_shared<SliceData>();
    auto unsupported = std::make_shared<SliceData>();
    for (std::size_t index = 0; index < 5; ++index) {
        const double z = 0.05 + static_cast<double>(index) * 0.1;
        slices->layers.push_back({z, {square(0, 0, 1, 1)}});
        unsupported->layers.push_back({z,
                                       index == 1 || index == 2 || index == 4
                                           ? std::vector<SlicePath>{square(0, 0, 0.5, 0.5)}
                                           : std::vector<SlicePath>{}});
    }

    std::array<std::atomic<int>, 5> layerCalls{};
    std::atomic<int> pillarCalls{0};
    SupportGenerationInput input{source, slices, unsupported};
    SupportGenerationKernels kernels;
    kernels.detectContactPoints = [&](const SupportGenerationInput &received,
                                      std::size_t layerIndex,
                                      const std::atomic<bool> *) {
        require(received.sourceModel == source && received.slices == slices &&
                    received.unsupported == unsupported,
                "support workers did not share the immutable inputs");
        ++layerCalls[layerIndex];
        return std::vector<SupportContactPoint>{
            {{double(layerIndex), 0.0, received.slices->layers[layerIndex].z}, layerIndex},
            {{double(layerIndex), 1.0, received.slices->layers[layerIndex].z}, layerIndex}};
    };
    kernels.buildPillar = [&](const SupportGenerationInput &received,
                              const SupportContactPoint &contact,
                              const std::atomic<bool> *) {
        require(received.sourceModel == source, "pillar workers did not share the source model");
        ++pillarCalls;
        TriangleMesh pillar;
        const Vec3 base{contact.position.x, contact.position.y, 0.0};
        pillar.addTriangle({{}, {base, {base.x + 0.1, base.y, 0.0}, contact.position}, 0});
        return pillar;
    };

    const SupportGenerationResult result =
        SupportGenerator{{4}, std::move(kernels)}.generate(input);
    require(result.processedLayerCount == 3, "support generator processed the wrong layer count");
    require(result.contactPoints.size() == 6, "support generator lost detected contact points");
    require(pillarCalls == 6 && result.supports.triangles().size() == 6,
            "support generator did not build one shell per contact point");
    for (std::size_t index = 0; index < layerCalls.size(); ++index)
        require(layerCalls[index] == (index == 1 || index == 2 || index == 4 ? 1 : 0),
                "support slice work was not claimed exactly once");

    std::atomic<bool> cancel{true};
    const SupportGenerationResult cancelled = SupportGenerator{{4}}.generate(input, &cancel);
    require(cancelled.cancelled && cancelled.processedLayerCount == 0,
            "support generation ignored cancellation before startup");

    SupportGenerationKernels failingKernels;
    failingKernels.detectContactPoints =
        [](const SupportGenerationInput &, std::size_t, const std::atomic<bool> *)
        -> std::vector<SupportContactPoint> { throw std::runtime_error("contact failure"); };
    bool workerErrorPropagated = false;
    try {
        (void)SupportGenerator{{4}, std::move(failingKernels)}.generate(input);
    } catch (const std::runtime_error &) {
        workerErrorPropagated = true;
    }
    require(workerErrorPropagated, "support worker failure was not propagated");
}

} // namespace

int main() {
    try {
        testBinaryStlReader();
        testBinaryStlWriter();
        testCubeSlices();
        testSliceCancellation();
        testNestedContours();
        testToleranceIndexedConnection();
        testTolerancePreservesShortEdges();
        testSmallGapHealing();
        testReversedSharedEdgeInterpolation();
        testCliWriters();
        testSingleAndOffsetSlices();
        testMergeSlices();
        testUnsupportedAreas();
        testOrientationOptimizer();
        testSupportGenerationScheduling();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
