#include "gl_canvas.hpp"
#include "document_frame.hpp"
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
void main(){float light=lit==0?1.0:0.28+0.72*abs(dot(normalize(worldNormal),normalize(vec3(0.3,-0.5,0.8))));outputColor=vec4(color.rgb*light,color.a);})";
} // namespace

ModelCanvas::ModelCanvas(wxWindow *parent, DocumentFrame &document)
    : wxGLCanvas(parent, wxID_ANY, nullptr), document_(document) {
    context_ = new wxGLContext(this);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ModelCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &ModelCanvas::OnSize, this);
    for (const auto event : {wxEVT_MOTION,
                             wxEVT_LEFT_DOWN,
                             wxEVT_LEFT_UP,
                             wxEVT_RIGHT_DOWN,
                             wxEVT_RIGHT_UP,
                             wxEVT_MOUSEWHEEL})
        Bind(event, &ModelCanvas::OnMouse, this);
}
ModelCanvas::~ModelCanvas() {
    if (context_) {
        SetCurrent(*context_);
        for (auto &entry : buffers_)
            glDeleteBuffers(1, &entry.second.id);
        if (overlayBuffer_)
            glDeleteBuffers(1, &overlayBuffer_);
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
    if (initialized_) {
        SetCurrent(*context_);
        for (auto &e : buffers_)
            glDeleteBuffers(1, &e.second.id);
        buffers_.clear();
    }
    auto b = document_.VisibleBounds();
    if (b.valid())
        distance_ = std::max({b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z}) * 2.3 + 1;
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
        auto b = document_.VisibleBounds();
        stl_slicer::Vec3 c = b.valid() ? stl_slicer::Vec3{(b.min.x + b.max.x) / 2,
                                                          (b.min.y + b.max.y) / 2,
                                                          (b.min.z + b.max.z) / 2}
                                       : stl_slicer::Vec3{};
        auto projection = perspective(0.75f,
                                      std::max(1, size.x) / float(std::max(1, size.y)),
                                      0.1f,
                                      float(distance_ * 20 + 1000));
        auto view =
            multiply(translation(0, 0, float(-distance_)),
                     multiply(rotation(float(pitch_), 1, 0, 0),
                              multiply(rotation(float(yaw_), 0, 0, 1),
                                       translation(float(-c.x), float(-c.y), float(-c.z)))));
        auto vp = multiply(projection, view);
        glUseProgram(program_);
        glUniformMatrix4fv(matrixUniform_, 1, GL_FALSE, vp.data());
        DrawModels(vp.data());
        DrawOverlays(vp.data());
        glUseProgram(0);
        SwapBuffers();
    } catch (const std::exception &e) {
        wxLogError("OpenGL: %s", e.what());
    }
}
void ModelCanvas::DrawModels(const float *) {
    const std::array<std::array<float, 4>, 3> colors = {
        {{.30f, .48f, .70f, 1}, {.68f, .42f, .24f, 1}, {.36f, .62f, .48f, 1}}};
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
            glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(v[0]), v.data(), GL_STATIC_DRAW);
            b.count = GLsizei(v.size());
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
        ++ci;
    }
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
        glDepthMask(GL_TRUE);
        for (const auto &layer : interactiveLayers_)
            for (const auto &p : layer.paths) {
                v.clear();
                for (const auto &q : p.points)
                    v.push_back({float(q.x), float(q.y), z + .02f, 0, 0, 1});
                const float line[] = {.05f, .15f, .08f, 1};
                upload(GL_LINE_STRIP, line);
            }
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
    double area = 0;
    interactiveLayers_.clear();
    if (interactiveSlice_)
        for (const auto &m : document_.Models())
            if (m->selected) {
                interactiveLayers_.push_back(
                    stl_slicer::Slicer{}.sliceAt(stl_slicer::transformedMesh(*m), slicePosition_));
                for (const auto &p : interactiveLayers_.back().paths)
                    for (std::size_t i = 0; i + 1 < p.points.size(); ++i)
                        area += (p.points[i].x * p.points[i + 1].y -
                                 p.points[i + 1].x * p.points[i].y) /
                                2;
            }
    document_.UpdateStatus(slicePosition_, std::abs(area), interactiveSlice_);
}
void ModelCanvas::TransformSelected(const stl_slicer::Mat4 &t) {
    for (auto &m : document_.Models())
        if (m->selected)
            m->transform = t * m->transform;
    UpdateInteractiveSlice();
    Refresh();
}
void ModelCanvas::OnMouse(wxMouseEvent &e) {
    const wxPoint now = e.GetPosition();
    const int dx = now.x - lastMouse_.x, dy = now.y - lastMouse_.y;
    if (e.Dragging() && e.LeftIsDown()) {
        if (e.ControlDown()) {
            yaw_ += dx * .008;
            pitch_ += dy * .008;
        } else {
            auto c = document_.SelectedCenter();
            stl_slicer::Mat4 r = e.ShiftDown()
                                     ? stl_slicer::Mat4::rotation(dx * .008, {0, 0, 1})
                                     : stl_slicer::Mat4::rotation(std::hypot(dx, dy) * .008,
                                                                  {-double(dy), double(dx), 0});
            TransformSelected(stl_slicer::Mat4::translation(c.x, c.y, c.z) * r *
                              stl_slicer::Mat4::translation(-c.x, -c.y, -c.z));
        }
    } else if (e.Dragging() && e.RightIsDown()) {
        double scale = distance_ / std::max(200, GetClientSize().y);
        TransformSelected(stl_slicer::Mat4::translation(dx * scale, -dy * scale, 0));
    }
    if (e.GetWheelRotation()) {
        double steps = double(e.GetWheelRotation()) / e.GetWheelDelta();
        if (e.ShiftDown() && interactiveSlice_) {
            slicePosition_ += steps * .1;
            auto b = document_.VisibleBounds();
            slicePosition_ = std::clamp(slicePosition_, b.min.z, b.max.z);
            UpdateInteractiveSlice();
        } else if (e.ControlDown()) {
            auto c = document_.SelectedCenter();
            double s = std::pow(1.05, steps);
            TransformSelected(stl_slicer::Mat4::translation(c.x, c.y, c.z) *
                              stl_slicer::Mat4::scale(s) *
                              stl_slicer::Mat4::translation(-c.x, -c.y, -c.z));
        } else
            distance_ /= std::pow(1.12, steps);
        Refresh();
    }
    lastMouse_ = now;
    e.Skip();
}
void ModelCanvas::OnSize(wxSizeEvent &e) {
    Refresh();
    e.Skip();
}
