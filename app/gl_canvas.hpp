#pragma once

#include "gl_api.hpp"
#include "slice_visualization.hpp"
#include "stl_slicer/scene_model.hpp"
#include "stl_slicer/slice.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <wx/glcanvas.h>

class DocumentFrame;
class wxThreadEvent;

enum class SectionAxis { X, Y, Z };
enum class SectionClipping { None, Above, Below };

class ModelCanvas final : public wxGLCanvas {
  public:
    ModelCanvas(wxWindow *parent, DocumentFrame &document);
    ~ModelCanvas() override;
    void ModelsChanged();
    void ModelTransformsChanged();
    void TranslateViewCenter(const stl_slicer::Vec3 &translation);
    void SelectionChanged();
    void SettingsChanged();
    void SetInteractiveSection(bool enabled,
                               SectionAxis axis = SectionAxis::Z,
                               bool autoRotate = false,
                               SectionClipping clipping = SectionClipping::None);
    bool InteractiveSlice() const {
        return interactiveSlice_;
    }
    double SlicePosition() const {
        return slicePosition_;
    }
    double SliceArea() const {
        return sliceArea_;
    }
    std::size_t SliceIndex() const {
        return sliceIndex_;
    }
    std::size_t MaximumSliceIndex() const {
        return maximumSliceIndex_;
    }
    void SetSliceIndex(std::size_t index);
    SectionAxis SliceAxis() const {
        return sectionAxis_;
    }
    bool InteractiveSliceRunning() const {
        return interactiveSliceRunning_;
    }
    void CancelInteractiveSlice();
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
    void OnInteractiveSliceFinished(wxThreadEvent &event);
    void InitializeGl();
    void DrawWorldAxes();
    void DrawModels(const float *viewProjection);
    void DrawUnsupportedVisualization();
    void DrawOverlays(const float *viewProjection);
    void DrawOrientationVane();
    void UpdateInteractiveSlice(bool preserveRunningPreview = false);
    void BeginInteractiveSlice();
    void AlignSectionView();
    void UpdateSectionSliceRange(bool initializeIndex);
    void TransformSelected(const stl_slicer::Mat4 &transform);

    DocumentFrame &document_;
    wxGLContext *context_ = nullptr;
    GLuint program_ = 0, overlayBuffer_ = 0;
    GLuint unsupportedVertexBuffer_ = 0, unsupportedIndexBuffer_ = 0;
    GLsizei unsupportedIndexCount_ = 0;
    GLint matrixUniform_ = -1, modelUniform_ = -1, colorUniform_ = -1, litUniform_ = -1;
    GLint clippingUniform_ = -1, clipPlaneUniform_ = -1;
    std::unordered_map<const stl_slicer::SceneModel *, Buffer> buffers_;
    std::unordered_map<const stl_slicer::SceneModel *,
                       std::shared_ptr<const stl_slicer::TriangleMesh>>
        sliceMeshes_;
    std::vector<stl_slicer::SliceLayer> interactiveLayers_;
    VisualizationMesh unsupportedVisualization_;
    bool unsupportedVisualizationDirty_ = false;
    bool initialized_ = false, interactiveSlice_ = false;
    bool openGlErrorReported_ = false;
    std::thread interactiveSliceWorker_;
    std::atomic<bool> interactiveSliceCancel_{false};
    std::atomic<bool> closing_{false};
    std::uint64_t interactiveSliceGeneration_ = 0;
    bool interactiveSliceRunning_ = false;
    bool interactiveSlicePending_ = false;
    SectionAxis sectionAxis_ = SectionAxis::Z;
    SectionClipping sectionClipping_ = SectionClipping::None;
    stl_slicer::Bounds3 sectionBounds_;
    std::size_t sliceIndex_ = 0;
    std::size_t maximumSliceIndex_ = 0;
    double sliceStepAccumulator_ = 0.0;
    double slicePosition_ = 0.0, sliceArea_ = 0.0;
    double yaw_ = 0.55, pitch_ = -0.45, distance_ = 300.0;
    double fieldOfView_ = 0.75;
    stl_slicer::Vec3 viewCenter_;
    stl_slicer::Vec3 preSectionViewCenter_;
    bool preSectionViewCenterSaved_ = false;
    wxPoint lastMouse_;
};
