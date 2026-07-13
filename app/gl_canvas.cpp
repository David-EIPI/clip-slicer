#include "gl_canvas.hpp"
#include "document_frame.hpp"
#include "slice_visualization.hpp"
#include "stl_slicer/slicer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <wx/dcclient.h>

namespace {
using Matrix = std::array<float, 16>;
Matrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}
Matrix translation(float x, float y, float z) {
    auto r = identity();
    r[12] = x;
    r[13] = y;
    r[14] = z;
    return r;
}
Matrix rotation(float angle, float x, float y, float z) {
    float l = std::sqrt(x * x + y * y + z * z);
    x /= l;
    y /= l;
    z /= l;
    float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    return {t * x * x + c,
            t * x * y + s * z,
            t * x * z - s * y,
            0,
            t * x * y - s * z,
            t * y * y + c,
            t * y * z + s * x,
            0,
            t * x * z + s * y,
            t * y * z - s * x,
            t * z * z + c,
            0,
            0,
            0,
            0,
            1};
}
Matrix perspective(float fovy, float aspect, float nearZ, float farZ) {
    float f = 1 / std::tan(fovy / 2);
    Matrix r{};
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (farZ + nearZ) / (nearZ - farZ);
    r[11] = -1;
    r[14] = 2 * farZ * nearZ / (nearZ - farZ);
    return r;
}
Matrix modelMatrix(const stl_slicer::Mat4 &m) {
    Matrix r;
    for (std::size_t i = 0; i < 16; ++i)
        r[i] = float(m.values()[i]);
    return r;
}
GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        throw std::runtime_error(log);
    }
    return s;
}
const char *vertexShader = R"(#version 150
in vec3 position; in vec3 normal; uniform mat4 viewProjection; uniform mat4 model;
out vec3 worldNormal; void main(){gl_Position=viewProjection*model*vec4(position,1.0);worldNormal=mat3(model)*normal;})";
const char *fragmentShader = R"(#version 150
in vec3 worldNormal; uniform vec4 color; uniform int lit; out vec4 outputColor;
void main(){float light=lit==0?1.0:0.68+0.32*abs(dot(normalize(worldNormal),normalize(vec3(0.3,-0.5,0.8))));outputColor=vec4(color.rgb*light,color.a);})";

const int *glAttributes() {
    static const int attributes[] = {
        WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, WX_GL_STENCIL_SIZE, 8, 0};
    return attributes;
}
} // namespace

ModelCanvas::ModelCanvas(wxWindow *parent, DocumentFrame &document)
    : wxGLCanvas(parent, wxID_ANY, glAttributes()), document_(document) {
    context_ = new wxGLContext(this);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ModelCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &ModelCanvas::OnSize, this);
    for (const auto event : {wxEVT_MOTION,
                             wxEVT_LEFT_DOWN,
                             wxEVT_LEFT_UP,
                             wxEVT_MIDDLE_DOWN,
                             wxEVT_MIDDLE_UP,
                             wxEVT_RIGHT_DOWN,
                             wxEVT_RIGHT_UP,
                             wxEVT_MOUSEWHEEL})
        Bind(event, &ModelCanvas::OnMouse, this);
}
ModelCanvas::~ModelCanvas() {
    if (context_) {
        SetCurrent(*context_);
        for (auto &entry : buffers_) {
            glDeleteBuffers(1, &entry.second.id);
            if (entry.second.capId)
                glDeleteBuffers(1, &entry.second.capId);
            if (entry.second.capIndexId)
                glDeleteBuffers(1, &entry.second.capIndexId);
        }
        if (overlayBuffer_)
            glDeleteBuffers(1, &overlayBuffer_);
        if (unsupportedVertexBuffer_)
            glDeleteBuffers(1, &unsupportedVertexBuffer_);
        if (unsupportedIndexBuffer_)
            glDeleteBuffers(1, &unsupportedIndexBuffer_);
        if (program_)
            glDeleteProgram(program_);
        delete context_;
    }
}
void ModelCanvas::InitializeGl() {
    if (initialized_)
        return;
    SetCurrent(*context_);
    GLuint vs = shader(GL_VERTEX_SHADER, vertexShader),
           fs = shader(GL_FRAGMENT_SHADER, fragmentShader);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glBindAttribLocation(program_, 0, "position");
    glBindAttribLocation(program_, 1, "normal");
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok)
        throw std::runtime_error("Unable to link OpenGL shaders");
    matrixUniform_ = glGetUniformLocation(program_, "viewProjection");
    modelUniform_ = glGetUniformLocation(program_, "model");
    colorUniform_ = glGetUniformLocation(program_, "color");
    litUniform_ = glGetUniformLocation(program_, "lit");
    glGenBuffers(1, &overlayBuffer_);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    initialized_ = true;
}
void ModelCanvas::ModelsChanged() {
    ClearUnsupportedVisualization();
    if (initialized_) {
        SetCurrent(*context_);
        for (auto &e : buffers_) {
            glDeleteBuffers(1, &e.second.id);
            if (e.second.capId)
                glDeleteBuffers(1, &e.second.capId);
            if (e.second.capIndexId)
                glDeleteBuffers(1, &e.second.capIndexId);
        }
        buffers_.clear();
    }
    sliceMeshes_.clear();
    auto b = document_.VisibleBounds();
    if (b.valid()) {
        distance_ = std::max({b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z}) * 2.3 + 1;
        viewCenter_ = {(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2};
    }
    UpdateInteractiveSlice();
    Refresh();
}
void ModelCanvas::SelectionChanged() {
    UpdateInteractiveSlice();
    Refresh();
}
void ModelCanvas::OnPaint(wxPaintEvent &) {
    wxPaintDC dc(this);
    if (!IsShownOnScreen())
        return;
    try {
        InitializeGl();
        SetCurrent(*context_);
        auto size = GetClientSize();
        glViewport(0, 0, size.x, size.y);
        glClearColor(0.72f, 0.86f, 0.72f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        auto projection = perspective(float(fieldOfView_),
                                      std::max(1, size.x) / float(std::max(1, size.y)),
                                      0.1f,
                                      float(distance_ * 20 + 1000));
        auto view = multiply(translation(0, 0, float(-distance_)),
                             multiply(rotation(float(pitch_), 1, 0, 0),
                                      multiply(rotation(float(yaw_), 0, 0, 1),
                                               translation(float(-viewCenter_.x),
                                                           float(-viewCenter_.y),
                                                           float(-viewCenter_.z)))));
        auto vp = multiply(projection, view);
        glUseProgram(program_);
        glUniformMatrix4fv(matrixUniform_, 1, GL_FALSE, vp.data());
        DrawWorldAxes();
        DrawModels(vp.data());
        DrawUnsupportedVisualization();
        DrawOverlays(vp.data());
        DrawOrientationVane();
        glUseProgram(0);
        SwapBuffers();
    } catch (const std::exception &e) {
        wxLogError("OpenGL: %s", e.what());
    }
}
void ModelCanvas::SetUnsupportedVisualization(VisualizationMesh visualization) {
    unsupportedVisualization_ = std::move(visualization);
    unsupportedVisualizationDirty_ = true;
    Refresh();
}
void ModelCanvas::ClearUnsupportedVisualization() {
    unsupportedVisualization_.vertices.clear();
    unsupportedVisualization_.indices.clear();
    unsupportedVisualizationDirty_ = true;
    Refresh();
}
void ModelCanvas::DrawUnsupportedVisualization() {
    if (unsupportedVisualizationDirty_) {
        if (!unsupportedVertexBuffer_)
            glGenBuffers(1, &unsupportedVertexBuffer_);
        if (!unsupportedIndexBuffer_)
            glGenBuffers(1, &unsupportedIndexBuffer_);
        glBindBuffer(GL_ARRAY_BUFFER, unsupportedVertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     unsupportedVisualization_.vertices.size() * sizeof(stl_slicer::RenderVertex),
                     unsupportedVisualization_.vertices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, unsupportedIndexBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     unsupportedVisualization_.indices.size() * sizeof(std::uint32_t),
                     unsupportedVisualization_.indices.data(),
                     GL_STATIC_DRAW);
        unsupportedIndexCount_ = GLsizei(unsupportedVisualization_.indices.size());
        unsupportedVisualizationDirty_ = false;
    }
    if (!unsupportedIndexCount_)
        return;

    glBindBuffer(GL_ARRAY_BUFFER, unsupportedVertexBuffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, unsupportedIndexBuffer_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(stl_slicer::RenderVertex), nullptr);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(stl_slicer::RenderVertex),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    const auto model = identity();
    glUniformMatrix4fv(modelUniform_, 1, GL_FALSE, model.data());
    const float color[] = {1.0f, 0.12f, 0.04f, 0.88f};
    glUniform4fv(colorUniform_, 1, color);
    glUniform1i(litUniform_, 0);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDrawElements(GL_TRIANGLES, unsupportedIndexCount_, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
}
void ModelCanvas::DrawWorldAxes() {
    const wxSize canvasSize = GetClientSize();
    const double aspect = double(std::max(1, canvasSize.x)) / std::max(1, canvasSize.y);
    const double inverseViewportScale = distance_ * std::tan(fieldOfView_ * 0.5);
    constexpr double tickDensity = 2.0;
    const double tickSpacing =
        std::pow(10.0, std::round(std::log10(std::max(inverseViewportScale / tickDensity, 1e-12))));
    const double radius =
        distance_ * (1.0 + 2.0 * std::tan(fieldOfView_ * 0.5) * std::hypot(1.0, aspect)) + 1.0;
    const double tickHalfSize = std::max(
        0.12, 6.0 * distance_ * std::tan(fieldOfView_ * 0.5) / std::max(200, canvasSize.y));
    const std::array<double, 3> center = {viewCenter_.x, viewCenter_.y, viewCenter_.z};
    const std::array<std::array<float, 4>, 3> colors = {
        {{.16f, .34f, .62f, .72f}, {.68f, .20f, .20f, .72f}, {.12f, .62f, .22f, .72f}}};
    const auto pointOnAxis = [](std::size_t axis, double position) {
        stl_slicer::Vec3 point;
        if (axis == 0)
            point.x = position;
        else if (axis == 1)
            point.y = position;
        else
            point.z = position;
        return point;
    };
    const auto offsetCoordinate =
        [](stl_slicer::Vec3 &point, std::size_t coordinate, double offset) {
            if (coordinate == 0)
                point.x += offset;
            else if (coordinate == 1)
                point.y += offset;
            else
                point.z += offset;
        };

    auto identityMatrix = identity();
    glUniformMatrix4fv(modelUniform_, 1, GL_FALSE, identityMatrix.data());
    glUniform1i(litUniform_, 0);
    glLineWidth(1.0f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        std::vector<stl_slicer::RenderVertex> vertices;
        const double tickRadius = std::min(radius, 10000.0);
        const double tickMinimum = center[axis] - tickRadius;
        const double tickMaximum = center[axis] + tickRadius;
        const double lineMinimum = std::min(-radius, center[axis] - radius);
        const double lineMaximum = std::max(radius, center[axis] + radius);
        const double firstTick = std::ceil(tickMinimum / tickSpacing);
        const double lastTick = std::floor(tickMaximum / tickSpacing);
        const std::size_t tickCount =
            static_cast<std::size_t>(std::max(0.0, lastTick - firstTick + 1.0));
        vertices.reserve(2 + tickCount * 4);

        const stl_slicer::Vec3 start = pointOnAxis(axis, lineMinimum);
        const stl_slicer::Vec3 end = pointOnAxis(axis, lineMaximum);
        vertices.push_back({float(start.x), float(start.y), float(start.z), 0.0f, 0.0f, 1.0f});
        vertices.push_back({float(end.x), float(end.y), float(end.z), 0.0f, 0.0f, 1.0f});

        const std::size_t firstPerpendicular = (axis + 1) % 3;
        const std::size_t secondPerpendicular = (axis + 2) % 3;
        for (std::size_t tick = 0; tick < tickCount; ++tick) {
            const double position = (firstTick + double(tick)) * tickSpacing;
            const stl_slicer::Vec3 point = pointOnAxis(axis, position);
            stl_slicer::Vec3 a = point;
            stl_slicer::Vec3 b = point;
            offsetCoordinate(a, firstPerpendicular, -tickHalfSize);
            offsetCoordinate(b, firstPerpendicular, tickHalfSize);
            vertices.push_back({float(a.x), float(a.y), float(a.z), 0.0f, 0.0f, 1.0f});
            vertices.push_back({float(b.x), float(b.y), float(b.z), 0.0f, 0.0f, 1.0f});
            a = point;
            b = point;
            offsetCoordinate(a, secondPerpendicular, -tickHalfSize);
            offsetCoordinate(b, secondPerpendicular, tickHalfSize);
            vertices.push_back({float(a.x), float(a.y), float(a.z), 0.0f, 0.0f, 1.0f});
            vertices.push_back({float(b.x), float(b.y), float(b.z), 0.0f, 0.0f, 1.0f});
        }

        glBindBuffer(GL_ARRAY_BUFFER, overlayBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(vertices[0]),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(stl_slicer::RenderVertex), nullptr);
        glVertexAttribPointer(1,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(stl_slicer::RenderVertex),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        glUniform4fv(colorUniform_, 1, colors[axis].data());
        glDrawArrays(GL_LINES, 0, GLsizei(vertices.size()));
    }
}
void ModelCanvas::DrawModels(const float *) {
    const std::array<std::array<float, 4>, 3> colors = {
        {{.18f, .62f, .72f, 1}, {.68f, .42f, .24f, 1}, {.36f, .62f, .48f, 1}}};
    std::size_t ci = 0;
    for (const auto &m : document_.Models()) {
        if (!m->visible) {
            ++ci;
            continue;
        }
        auto it = buffers_.find(m.get());
        if (it == buffers_.end()) {
            Buffer b;
            glGenBuffers(1, &b.id);
            glBindBuffer(GL_ARRAY_BUFFER, b.id);
            const auto &v = m->renderVertices();
            if (m->isSliced()) {
                glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(v[0]), v.data(), GL_STATIC_DRAW);
            } else {
                std::vector<stl_slicer::RenderVertex> smoothVertices(v.begin(), v.end());
                SmoothRenderNormals(smoothVertices);
                glBufferData(GL_ARRAY_BUFFER,
                             smoothVertices.size() * sizeof(smoothVertices[0]),
                             smoothVertices.data(),
                             GL_STATIC_DRAW);
            }
            b.count = GLsizei(v.size());
            if (const auto *slices = m->slices()) {
                auto caps = BuildSliceCaps(*slices);
                glGenBuffers(1, &b.capId);
                glBindBuffer(GL_ARRAY_BUFFER, b.capId);
                glBufferData(GL_ARRAY_BUFFER,
                             caps.vertices.size() * sizeof(caps.vertices[0]),
                             caps.vertices.data(),
                             GL_STATIC_DRAW);
                glGenBuffers(1, &b.capIndexId);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.capIndexId);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             caps.indices.size() * sizeof(caps.indices[0]),
                             caps.indices.data(),
                             GL_STATIC_DRAW);
                b.capCount = GLsizei(caps.indices.size());
            }
            it = buffers_.emplace(m.get(), b).first;
        }
        glBindBuffer(GL_ARRAY_BUFFER, it->second.id);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(stl_slicer::RenderVertex), nullptr);
        glVertexAttribPointer(1,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(stl_slicer::RenderVertex),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        auto matrix = modelMatrix(m->transform);
        glUniformMatrix4fv(modelUniform_, 1, GL_FALSE, matrix.data());
        auto color = colors[ci % 3];
        if (m->selected) {
            color[0] = std::min(1.f, color[0] + .15f);
            color[1] = std::min(1.f, color[1] + .15f);
        }
        glUniform4fv(colorUniform_, 1, color.data());
        glUniform1i(litUniform_, 1);
        glDrawArrays(GL_TRIANGLES, 0, it->second.count);
        if (it->second.capId) {
            glBindBuffer(GL_ARRAY_BUFFER, it->second.capId);
            glVertexAttribPointer(
                0, 3, GL_FLOAT, GL_FALSE, sizeof(stl_slicer::RenderVertex), nullptr);
            glVertexAttribPointer(1,
                                  3,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(stl_slicer::RenderVertex),
                                  reinterpret_cast<void *>(3 * sizeof(float)));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.capIndexId);
            glDrawElements(GL_TRIANGLES, it->second.capCount, GL_UNSIGNED_INT, nullptr);
        }
        ++ci;
    }
}
void ModelCanvas::DrawOrientationVane() {
    const wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0)
        return;

    const stl_slicer::Mat4 worldToCamera =
        stl_slicer::Mat4::rotation(pitch_, {1, 0, 0}) * stl_slicer::Mat4::rotation(yaw_, {0, 0, 1});
    const std::array<stl_slicer::Vec3, 3> directions = {worldToCamera.transformVector({1, 0, 0}),
                                                        worldToCamera.transformVector({0, 1, 0}),
                                                        worldToCamera.transformVector({0, 0, 1})};
    const std::array<std::array<float, 4>, 3> colors = {
        {{.08f, .35f, 1.0f, 1.0f}, {1.0f, .12f, .10f, 1.0f}, {.12f, 1.0f, .20f, 1.0f}}};
    const double centerX = std::max(42.0, size.x * 0.065);
    const double centerY = std::max(42.0, size.y * 0.065);
    const double arrowLength = std::min(size.x, size.y) * 0.055;
    const auto vertex = [&](double x, double y) {
        return stl_slicer::RenderVertex{
            float(x * 2.0 / size.x - 1.0), float(y * 2.0 / size.y - 1.0), 0, 0, 0, 1};
    };
    const auto upload = [&](GLenum mode,
                            const std::vector<stl_slicer::RenderVertex> &vertices,
                            const std::array<float, 4> &color) {
        glBindBuffer(GL_ARRAY_BUFFER, overlayBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(vertices[0]),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(stl_slicer::RenderVertex), nullptr);
        glVertexAttribPointer(1,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(stl_slicer::RenderVertex),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        glUniform4fv(colorUniform_, 1, color.data());
        glDrawArrays(mode, 0, GLsizei(vertices.size()));
    };

    auto identityMatrix = identity();
    glUniformMatrix4fv(matrixUniform_, 1, GL_FALSE, identityMatrix.data());
    glUniformMatrix4fv(modelUniform_, 1, GL_FALSE, identityMatrix.data());
    glUniform1i(litUniform_, 0);
    glDisable(GL_DEPTH_TEST);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double endX = centerX + directions[axis].x * arrowLength;
        const double endY = centerY + directions[axis].y * arrowLength;
        const double dx = endX - centerX;
        const double dy = endY - centerY;
        const double length = std::hypot(dx, dy);
        std::vector<stl_slicer::RenderVertex> vertices;
        if (length < 8.0) {
            constexpr double radius = 5.0;
            vertices = {vertex(centerX, centerY + radius),
                        vertex(centerX - radius, centerY),
                        vertex(centerX, centerY - radius),
                        vertex(centerX, centerY + radius),
                        vertex(centerX, centerY - radius),
                        vertex(centerX + radius, centerY)};
        } else {
            const double ux = dx / length;
            const double uy = dy / length;
            const double px = -uy;
            const double py = ux;
            const double shaftEndX = endX - ux * 9.0;
            const double shaftEndY = endY - uy * 9.0;
            constexpr double halfWidth = 2.2;
            constexpr double headHalfWidth = 6.0;
            vertices = {vertex(centerX + px * halfWidth, centerY + py * halfWidth),
                        vertex(centerX - px * halfWidth, centerY - py * halfWidth),
                        vertex(shaftEndX - px * halfWidth, shaftEndY - py * halfWidth),
                        vertex(centerX + px * halfWidth, centerY + py * halfWidth),
                        vertex(shaftEndX - px * halfWidth, shaftEndY - py * halfWidth),
                        vertex(shaftEndX + px * halfWidth, shaftEndY + py * halfWidth),
                        vertex(endX, endY),
                        vertex(shaftEndX + px * headHalfWidth, shaftEndY + py * headHalfWidth),
                        vertex(shaftEndX - px * headHalfWidth, shaftEndY - py * headHalfWidth)};
        }
        upload(GL_TRIANGLES, vertices, colors[axis]);
    }

    const std::array<std::array<double, 2>, 3> fallbackDirections = {
        {{{-0.7, -0.7}}, {{0.7, -0.7}}, {{0.0, 1.0}}}};
    const std::array<float, 4> labelColor = {0.0f, 0.0f, 0.0f, 1.0f};
    constexpr double halfWidth = 4.0;
    constexpr double halfHeight = 5.0;
    constexpr double labelOffset = 11.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double dx = directions[axis].x * arrowLength;
        const double dy = directions[axis].y * arrowLength;
        const double length = std::hypot(dx, dy);
        const double ux = length > 1.0 ? dx / length : fallbackDirections[axis][0];
        const double uy = length > 1.0 ? dy / length : fallbackDirections[axis][1];
        const double x = centerX + dx + ux * labelOffset;
        const double y = centerY + dy + uy * labelOffset;
        std::vector<stl_slicer::RenderVertex> label;
        if (axis == 0) {
            label = {vertex(x - halfWidth, y - halfHeight),
                     vertex(x + halfWidth, y + halfHeight),
                     vertex(x - halfWidth, y + halfHeight),
                     vertex(x + halfWidth, y - halfHeight)};
        } else if (axis == 1) {
            label = {vertex(x - halfWidth, y + halfHeight),
                     vertex(x, y),
                     vertex(x + halfWidth, y + halfHeight),
                     vertex(x, y),
                     vertex(x, y),
                     vertex(x, y - halfHeight)};
        } else {
            label = {vertex(x - halfWidth, y + halfHeight),
                     vertex(x + halfWidth, y + halfHeight),
                     vertex(x + halfWidth, y + halfHeight),
                     vertex(x - halfWidth, y - halfHeight),
                     vertex(x - halfWidth, y - halfHeight),
                     vertex(x + halfWidth, y - halfHeight)};
        }
        glLineWidth(2.0f);
        upload(GL_LINES, label, labelColor);
    }
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}
void ModelCanvas::DrawOverlays(const float *) {
    auto b = document_.VisibleBounds();
    if (!b.valid())
        return;
    const float z = float(b.min.z - std::max(0.5, (b.max.z - b.min.z) * .03));
    std::vector<stl_slicer::RenderVertex> v = {{float(b.min.x), float(b.min.y), z, 0, 0, 1},
                                               {float(b.max.x), float(b.min.y), z, 0, 0, 1},
                                               {float(b.max.x), float(b.max.y), z, 0, 0, 1},
                                               {float(b.min.x), float(b.min.y), z, 0, 0, 1},
                                               {float(b.max.x), float(b.max.y), z, 0, 0, 1},
                                               {float(b.min.x), float(b.max.y), z, 0, 0, 1}};
    auto upload = [&](GLenum mode, const float color[4]) {
        glBindBuffer(GL_ARRAY_BUFFER, overlayBuffer_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(v[0]), v.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(v[0]), nullptr);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(v[0]), reinterpret_cast<void *>(12));
        auto id = identity();
        glUniformMatrix4fv(modelUniform_, 1, GL_FALSE, id.data());
        glUniform4fv(colorUniform_, 1, color);
        glUniform1i(litUniform_, 0);
        glDrawArrays(mode, 0, GLsizei(v.size()));
    };
    const float plate[] = {.22f, .24f, .24f, 1};
    upload(GL_TRIANGLES, plate);
    v = {{float(b.min.x), float(b.min.y), z, 0, 0, -1},
         {float(b.max.x), float(b.max.y), z, 0, 0, -1},
         {float(b.max.x), float(b.min.y), z, 0, 0, -1},
         {float(b.min.x), float(b.min.y), z, 0, 0, -1},
         {float(b.min.x), float(b.max.y), z, 0, 0, -1},
         {float(b.max.x), float(b.max.y), z, 0, 0, -1}};
    const float plateUnderside[] = {.62f, .65f, .67f, 1};
    upload(GL_TRIANGLES, plateUnderside);
    if (interactiveSlice_) {
        v = {{float(b.min.x), float(b.min.y), float(slicePosition_), 0, 0, 1},
             {float(b.max.x), float(b.min.y), float(slicePosition_), 0, 0, 1},
             {float(b.max.x), float(b.max.y), float(slicePosition_), 0, 0, 1},
             {float(b.min.x), float(b.min.y), float(slicePosition_), 0, 0, 1},
             {float(b.max.x), float(b.max.y), float(slicePosition_), 0, 0, 1},
             {float(b.min.x), float(b.max.y), float(slicePosition_), 0, 0, 1}};
        glDepthMask(GL_FALSE);
        const float plane[] = {.25f, .65f, .80f, .28f};
        upload(GL_TRIANGLES, plane);
        v = {{float(b.min.x), float(b.min.y), float(slicePosition_), 0, 0, -1},
             {float(b.max.x), float(b.max.y), float(slicePosition_), 0, 0, -1},
             {float(b.max.x), float(b.min.y), float(slicePosition_), 0, 0, -1},
             {float(b.min.x), float(b.min.y), float(slicePosition_), 0, 0, -1},
             {float(b.min.x), float(b.max.y), float(slicePosition_), 0, 0, -1},
             {float(b.max.x), float(b.max.y), float(slicePosition_), 0, 0, -1}};
        const float planeUnderside[] = {.92f, .43f, .16f, .34f};
        upload(GL_TRIANGLES, planeUnderside);
        glDepthMask(GL_TRUE);

        const float projectionZ = z;
        v.clear();
        for (const auto &layer : interactiveLayers_)
            for (const auto &path : layer.paths)
                for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
                    const auto &a = path.points[i];
                    const auto &edge = path.points[i + 1];
                    v.push_back({float(b.min.x), float(b.min.y), projectionZ, 0, 0, 1});
                    v.push_back({float(a.x), float(a.y), projectionZ, 0, 0, 1});
                    v.push_back({float(edge.x), float(edge.y), projectionZ, 0, 0, 1});
                }

        glClear(GL_STENCIL_BUFFER_BIT);
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xff);
        glStencilFunc(GL_ALWAYS, 0, 0xff);
        glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        upload(GL_TRIANGLES, plate);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glStencilMask(0x00);
        glStencilFunc(GL_NOTEQUAL, 0, 0xff);
        v = {{float(b.min.x), float(b.min.y), projectionZ, 0, 0, 1},
             {float(b.max.x), float(b.min.y), projectionZ, 0, 0, 1},
             {float(b.max.x), float(b.max.y), projectionZ, 0, 0, 1},
             {float(b.min.x), float(b.min.y), projectionZ, 0, 0, 1},
             {float(b.max.x), float(b.max.y), projectionZ, 0, 0, 1},
             {float(b.min.x), float(b.max.y), projectionZ, 0, 0, 1}};
        const float projection[] = {.08f, .34f, .16f, 1};
        glDepthFunc(GL_LEQUAL);
        upload(GL_TRIANGLES, projection);
        glDepthFunc(GL_LESS);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0xff);
    }
}
void ModelCanvas::SetInteractiveSlice(bool enabled) {
    interactiveSlice_ = enabled;
    auto b = document_.VisibleBounds();
    if (enabled && b.valid())
        slicePosition_ = (b.min.z + b.max.z) / 2;
    UpdateInteractiveSlice();
    Refresh();
}
void ModelCanvas::UpdateInteractiveSlice() {
    double area = 0.0;
    interactiveLayers_.clear();
    if (interactiveSlice_)
        for (const auto &m : document_.Models())
            if (m->selected) {
                auto mesh = sliceMeshes_.find(m.get());
                if (mesh == sliceMeshes_.end()) {
                    mesh = sliceMeshes_.emplace(m.get(), stl_slicer::transformedMesh(*m)).first;
                }
                interactiveLayers_.push_back(
                    stl_slicer::Slicer{}.sliceAt(mesh->second, slicePosition_));
                for (const auto &path : interactiveLayers_.back().paths)
                    for (std::size_t i = 0; i + 1 < path.points.size(); ++i)
                        area += (path.points[i].x * path.points[i + 1].y -
                                 path.points[i + 1].x * path.points[i].y) /
                                2;
            }
    sliceArea_ = std::abs(area);
    document_.UpdateStatus();
}
void ModelCanvas::TransformSelected(const stl_slicer::Mat4 &t) {
    for (auto &m : document_.Models())
        if (m->selected) {
            m->transform = t * m->transform;
            sliceMeshes_.erase(m.get());
        }
    document_.InvalidateUnsupportedAnalysis();
    UpdateInteractiveSlice();
    Refresh();
}
void ModelCanvas::OnMouse(wxMouseEvent &e) {
    const wxPoint now = e.GetPosition();
    const int dx = now.x - lastMouse_.x, dy = now.y - lastMouse_.y;
    const bool transformModels = e.ShiftDown();
    const stl_slicer::Mat4 screenToWorld = stl_slicer::Mat4::rotation(-yaw_, {0, 0, 1}) *
                                           stl_slicer::Mat4::rotation(-pitch_, {1, 0, 0});
    const double worldUnitsPerPixel =
        2.0 * distance_ * std::tan(fieldOfView_ * 0.5) / std::max(200, GetClientSize().y);
    const auto moveSlicePlane = [&](double steps) {
        slicePosition_ += steps * 0.1;
        const auto bounds = document_.VisibleBounds();
        if (bounds.valid())
            slicePosition_ = std::clamp(slicePosition_, bounds.min.z, bounds.max.z);
        UpdateInteractiveSlice();
        Refresh();
    };
    const auto scaleTarget = [&](double steps) {
        if (transformModels) {
            auto c = document_.SelectedCenter();
            const double scale = std::pow(1.05, steps);
            TransformSelected(stl_slicer::Mat4::translation(c.x, c.y, c.z) *
                              stl_slicer::Mat4::scale(scale) *
                              stl_slicer::Mat4::translation(-c.x, -c.y, -c.z));
        } else {
            fieldOfView_ = std::clamp(fieldOfView_ / std::pow(1.12, steps), 0.01, 2.2);
            Refresh();
        }
    };
    const auto translateParallel = [&]() {
        const stl_slicer::Vec3 screenDelta{dx * worldUnitsPerPixel, -dy * worldUnitsPerPixel, 0};
        const stl_slicer::Vec3 worldDelta = screenToWorld.transformVector(screenDelta);
        if (transformModels) {
            TransformSelected(
                stl_slicer::Mat4::translation(worldDelta.x, worldDelta.y, worldDelta.z));
        } else {
            viewCenter_.x -= worldDelta.x;
            viewCenter_.y -= worldDelta.y;
            viewCenter_.z -= worldDelta.z;
            Refresh();
        }
    };
    const auto translateNormal = [&]() {
        if (transformModels) {
            const stl_slicer::Vec3 worldDelta =
                screenToWorld.transformVector({0, 0, -dy * worldUnitsPerPixel});
            TransformSelected(
                stl_slicer::Mat4::translation(worldDelta.x, worldDelta.y, worldDelta.z));
        } else {
            distance_ = std::max(0.1, distance_ + dy * worldUnitsPerPixel);
            Refresh();
        }
    };

    if (e.Dragging()) {
        if (e.LeftIsDown()) {
            if (e.AltDown() && e.ShiftDown() && interactiveSlice_) {
                moveSlicePlane(-dy / 24.0);
            } else if (e.ControlDown()) {
                scaleTarget(-dy / 24.0);
            } else if (transformModels) {
                auto c = document_.SelectedCenter();
                const stl_slicer::Vec3 axis =
                    screenToWorld.transformVector({-double(dy), double(dx), 0});
                const stl_slicer::Mat4 rotation =
                    stl_slicer::Mat4::rotation(std::hypot(dx, dy) * .008, axis);
                TransformSelected(stl_slicer::Mat4::translation(c.x, c.y, c.z) * rotation *
                                  stl_slicer::Mat4::translation(-c.x, -c.y, -c.z));
            } else {
                yaw_ += dx * .008;
                pitch_ += dy * .008;
                Refresh();
            }
        } else if (e.MiddleIsDown() || (e.RightIsDown() && e.ControlDown())) {
            translateParallel();
        } else if (e.RightIsDown()) {
            translateNormal();
        }
    }
    if (e.GetWheelRotation()) {
        const double steps = double(e.GetWheelRotation()) / e.GetWheelDelta();
        if (e.AltDown() && interactiveSlice_) {
            moveSlicePlane(steps);
        } else
            scaleTarget(steps);
        Refresh();
    }
    lastMouse_ = now;
    e.Skip();
}
void ModelCanvas::OnSize(wxSizeEvent &e) {
    Refresh();
    e.Skip();
}
