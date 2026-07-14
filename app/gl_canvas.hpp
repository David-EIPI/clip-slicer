#pragma once

#include "slice_visualization.hpp"
#include "stl_slicer/scene_model.hpp"
#include "stl_slicer/slice.hpp"
#include <epoxy/gl.h>
#include <memory>
#include <unordered_map>
#include <wx/glcanvas.h>

class DocumentFrame;

class ModelCanvas final : public wxGLCanvas {
  public:
    ModelCanvas(wxWindow *parent, DocumentFrame &document);
    ~ModelCanvas() override;
    void ModelsChanged();
    void SelectionChanged();
    void SettingsChanged();
    void SetInteractiveSlice(bool enabled);
    bool InteractiveSlice() const {
        return interactiveSlice_;
    }
    double SlicePosition() const {
        return slicePosition_;
    }
    double SliceArea() const {
        return sliceArea_;
    }
    void SetUnsupportedVisualization(VisualizationMesh visualization);
    void ClearUnsupportedVisualization();

  private:
    struct Buffer {
        GLuint id = 0;
        GLsizei count = 0;
        GLuint capId = 0;
        GLuint capIndexId = 0;
        GLsizei capCount = 0;
    };
    void OnPaint(wxPaintEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnMouse(wxMouseEvent &event);
    void InitializeGl();
    void DrawWorldAxes();
    void DrawModels(const float *viewProjection);
    void DrawUnsupportedVisualization();
    void DrawOverlays(const float *viewProjection);
    void DrawOrientationVane();
    void UpdateInteractiveSlice();
    void TransformSelected(const stl_slicer::Mat4 &transform);

    DocumentFrame &document_;
    wxGLContext *context_ = nullptr;
    GLuint program_ = 0, overlayBuffer_ = 0;
    GLuint unsupportedVertexBuffer_ = 0, unsupportedIndexBuffer_ = 0;
    GLsizei unsupportedIndexCount_ = 0;
    GLint matrixUniform_ = -1, modelUniform_ = -1, colorUniform_ = -1, litUniform_ = -1;
    std::unordered_map<const stl_slicer::SceneModel *, Buffer> buffers_;
    std::unordered_map<const stl_slicer::SceneModel *, stl_slicer::TriangleMesh> sliceMeshes_;
    std::vector<stl_slicer::SliceLayer> interactiveLayers_;
    VisualizationMesh unsupportedVisualization_;
    bool unsupportedVisualizationDirty_ = false;
    bool initialized_ = false, interactiveSlice_ = false;
    double slicePosition_ = 0.0, sliceArea_ = 0.0;
    double yaw_ = 0.55, pitch_ = -0.45, distance_ = 300.0;
    double fieldOfView_ = 0.75;
    stl_slicer::Vec3 viewCenter_;
    wxPoint lastMouse_;
};
