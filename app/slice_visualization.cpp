#include "slice_visualization.hpp"
#include <GL/glu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
using stl_slicer::RenderVertex;

RenderVertex renderVertex(const GLdouble *point, float z, float normalZ) {
    return {float(point[0]), float(point[1]), z, 0.0f, 0.0f, normalZ};
}

struct CapVertexKey {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
    std::uint32_t normalZ;

    bool operator==(const CapVertexKey &other) const {
        return x == other.x && y == other.y && z == other.z && normalZ == other.normalZ;
    }
};

struct CapVertexKeyHash {
    std::size_t operator()(const CapVertexKey &key) const {
        std::size_t hash = key.x;
        for (const std::uint32_t value : {key.y, key.z, key.normalZ})
            hash ^= std::size_t(value) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

CapVertexKey capVertexKey(const RenderVertex &vertex) {
    CapVertexKey key;
    std::memcpy(&key.x, &vertex.x, sizeof(key.x));
    std::memcpy(&key.y, &vertex.y, sizeof(key.y));
    std::memcpy(&key.z, &vertex.z, sizeof(key.z));
    std::memcpy(&key.normalZ, &vertex.nz, sizeof(key.normalZ));
    return key;
}

class Tessellation {
  public:
    Tessellation(float top, float bottom, VisualizationMesh &output)
        : top_(top), bottom_(bottom), output_(output) {}

    void begin(GLenum mode) {
        mode_ = mode;
        primitive_.clear();
    }

    void vertex(const GLdouble *point) {
        primitive_.push_back(renderVertex(point, top_, 1.0f));
    }

    void end() {
        if (mode_ == GL_TRIANGLES) {
            for (std::size_t i = 0; i + 2 < primitive_.size(); i += 3)
                addTriangle(primitive_[i], primitive_[i + 1], primitive_[i + 2]);
        } else if (mode_ == GL_TRIANGLE_FAN) {
            for (std::size_t i = 1; i + 1 < primitive_.size(); ++i)
                addTriangle(primitive_[0], primitive_[i], primitive_[i + 1]);
        } else if (mode_ == GL_TRIANGLE_STRIP) {
            for (std::size_t i = 2; i < primitive_.size(); ++i) {
                if ((i & 1U) == 0)
                    addTriangle(primitive_[i - 2], primitive_[i - 1], primitive_[i]);
                else
                    addTriangle(primitive_[i - 1], primitive_[i - 2], primitive_[i]);
            }
        }
    }

    void combine(const GLdouble coordinates[3], void **output) {
        generated_.push_back({coordinates[0], coordinates[1], coordinates[2]});
        *output = generated_.back().data();
    }

    void error(GLenum code) {
        error_ = reinterpret_cast<const char *>(gluErrorString(code));
    }

    const std::string &error() const {
        return error_;
    }

  private:
    void addTriangle(RenderVertex a, RenderVertex b, RenderVertex c) {
        addIndices(a, b, c);
        a.z = bottom_;
        b.z = bottom_;
        c.z = bottom_;
        a.nz = b.nz = c.nz = -1.0f;
        addIndices(c, b, a);
    }

    void addIndices(const RenderVertex &a, const RenderVertex &b, const RenderVertex &c) {
        output_.indices.insert(output_.indices.end(), {addVertex(a), addVertex(b), addVertex(c)});
    }

    std::uint32_t addVertex(const RenderVertex &vertex) {
        const CapVertexKey key = capVertexKey(vertex);
        const auto found = vertexIndices_.find(key);
        if (found != vertexIndices_.end())
            return found->second;
        if (output_.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Tessellated model has too many cap vertices");
        const auto index = static_cast<std::uint32_t>(output_.vertices.size());
        output_.vertices.push_back(vertex);
        vertexIndices_.emplace(key, index);
        return index;
    }

    float top_;
    float bottom_;
    VisualizationMesh &output_;
    std::unordered_map<CapVertexKey, std::uint32_t, CapVertexKeyHash> vertexIndices_;
    GLenum mode_ = GL_TRIANGLES;
    std::vector<RenderVertex> primitive_;
    std::deque<std::array<GLdouble, 3>> generated_;
    std::string error_;
};

void GLAPIENTRY tessBegin(GLenum mode, void *data) {
    static_cast<Tessellation *>(data)->begin(mode);
}

void GLAPIENTRY tessVertex(void *vertex, void *data) {
    static_cast<Tessellation *>(data)->vertex(static_cast<const GLdouble *>(vertex));
}

void GLAPIENTRY tessEnd(void *data) {
    static_cast<Tessellation *>(data)->end();
}

void GLAPIENTRY
tessCombine(GLdouble coordinates[3], void *[4], GLfloat[4], void **output, void *data) {
    static_cast<Tessellation *>(data)->combine(coordinates, output);
}

void GLAPIENTRY tessError(GLenum code, void *data) {
    static_cast<Tessellation *>(data)->error(code);
}

struct Position {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;

    bool operator==(const Position &other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct PositionHash {
    std::size_t operator()(const Position &position) const {
        std::size_t hash = position.x;
        hash ^= std::size_t(position.y) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= std::size_t(position.z) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

Position positionOf(const RenderVertex &vertex) {
    Position result;
    std::memcpy(&result.x, &vertex.x, sizeof(result.x));
    std::memcpy(&result.y, &vertex.y, sizeof(result.y));
    std::memcpy(&result.z, &vertex.z, sizeof(result.z));
    return result;
}

float normalLength(const RenderVertex &vertex) {
    return std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
}
} // namespace

namespace {

VisualizationMesh buildSliceFaces(const stl_slicer::SliceData &slices, bool includeBottomFaces) {
    VisualizationMesh result;
    if (slices.layers.empty())
        return result;

    const double fallback =
        slices.layers.size() > 1 ? slices.layers[1].z - slices.layers[0].z : 1.0;
    for (std::size_t layerIndex = 0; layerIndex < slices.layers.size(); ++layerIndex) {
        const auto &layer = slices.layers[layerIndex];
        const double bottom =
            includeBottomFaces ? (layerIndex ? slices.layers[layerIndex - 1].z : layer.z - fallback)
                               : layer.z;
        Tessellation tessellation(float(layer.z), float(bottom), result);
        GLUtesselator *tessellator = gluNewTess();
        if (!tessellator)
            throw std::runtime_error("Unable to create GLU tessellator");

        gluTessCallback(tessellator, GLU_TESS_BEGIN_DATA, (_GLUfuncptr)tessBegin);
        gluTessCallback(tessellator, GLU_TESS_VERTEX_DATA, (_GLUfuncptr)tessVertex);
        gluTessCallback(tessellator, GLU_TESS_END_DATA, (_GLUfuncptr)tessEnd);
        gluTessCallback(tessellator, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)tessCombine);
        gluTessCallback(tessellator, GLU_TESS_ERROR_DATA, (_GLUfuncptr)tessError);
        gluTessProperty(tessellator, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
        gluTessNormal(tessellator, 0.0, 0.0, 1.0);

        std::deque<std::array<GLdouble, 3>> points;
        gluTessBeginPolygon(tessellator, &tessellation);
        for (const auto &path : layer.paths) {
            const bool geometricallyClosed =
                path.points.size() >= 4 &&
                stl_slicer::squaredDistance(path.points.front(), path.points.back()) <= 1e-12;
            if (path.points.size() < 4 ||
                (path.type == stl_slicer::PathType::Open && !geometricallyClosed))
                continue;
            gluTessBeginContour(tessellator);
            for (std::size_t pointIndex = 0; pointIndex + 1 < path.points.size(); ++pointIndex) {
                const auto &point = path.points[pointIndex];
                points.push_back({point.x, point.y, layer.z});
                gluTessVertex(tessellator, points.back().data(), points.back().data());
            }
            gluTessEndContour(tessellator);
        }
        gluTessEndPolygon(tessellator);
        gluDeleteTess(tessellator);
        if (!tessellation.error().empty())
            throw std::runtime_error("Unable to tessellate sliced model: " + tessellation.error());
    }
    return result;
}

} // namespace

VisualizationMesh BuildSliceCaps(const stl_slicer::SliceData &slices) {
    return buildSliceFaces(slices, true);
}

VisualizationMesh BuildSliceSurfaces(const stl_slicer::SliceData &slices) {
    return buildSliceFaces(slices, false);
}

void SmoothRenderNormals(std::vector<RenderVertex> &vertices) {
    std::unordered_map<Position, std::vector<std::array<float, 3>>, PositionHash> normals;
    normals.reserve(vertices.size() / 2);
    for (const auto &vertex : vertices)
        normals[positionOf(vertex)].push_back({vertex.nx, vertex.ny, vertex.nz});

    constexpr float creaseCosine = 0.5f;
    for (auto &vertex : vertices) {
        const float originalLength = normalLength(vertex);
        if (originalLength == 0.0f)
            continue;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        for (const auto &candidate : normals[positionOf(vertex)]) {
            const float candidateLength =
                std::sqrt(candidate[0] * candidate[0] + candidate[1] * candidate[1] +
                          candidate[2] * candidate[2]);
            if (candidateLength == 0.0f)
                continue;
            const float dot =
                (vertex.nx * candidate[0] + vertex.ny * candidate[1] + vertex.nz * candidate[2]) /
                (originalLength * candidateLength);
            if (dot >= creaseCosine) {
                x += candidate[0] / candidateLength;
                y += candidate[1] / candidateLength;
                z += candidate[2] / candidateLength;
            }
        }
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length > 0.0f) {
            vertex.nx = x / length;
            vertex.ny = y / length;
            vertex.nz = z / length;
        }
    }
}
