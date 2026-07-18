#pragma once

#include "stl_slicer/scene_model.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <wx/checklst.h>
#include <wx/mdi.h>

class ModelCanvas;
class wxActivateEvent;
class wxCloseEvent;
class wxDPIChangedEvent;
class wxKeyEvent;
class wxMenuItem;
class wxPanel;
class wxScrollBar;
class wxScrollEvent;
class wxSpinCtrl;
class wxSpinEvent;
class wxToolBar;
class wxThreadEvent;

class DocumentFrame final : public wxMDIChildFrame {
  public:
    DocumentFrame(wxMDIParentFrame *parent, const wxString &title);
    ~DocumentFrame() override;
    const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &Models() const {
        return models_;
    }
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> &Models() {
        return models_;
    }
    void AddModel(std::shared_ptr<stl_slicer::SceneModel> model);
    stl_slicer::Bounds3 ModelBounds() const;
    stl_slicer::Bounds3 VisibleBounds() const;
    stl_slicer::Vec3 SelectedCenter() const;
    void RefreshModelList();
    void UpdateStatus();
    void OpenPath(const wxString &path);
    void InvalidateUnsupportedAnalysis();
    void SettingsChanged();
    void InteractiveSlicePositionChanged();
    double LayerThickness() const;
    double FirstLayerOffset() const;
    double ContourHealingThreshold() const;
    double SegmentationTolerance() const;
    double CriticalAngleDegrees() const;
    double OverhangCoefficient() const;
    double CrossSectionDisplayDistance() const;
    stl_slicer::Bounds3 SelectedBounds() const;

  private:
    void BuildMenus();
    void OnOpen(wxCommandEvent &);
    void OnActivate(wxActivateEvent &);
    void OnClose(wxCloseEvent &);
    void OnDPIChanged(wxDPIChangedEvent &event);
    void OnExport(wxCommandEvent &);
    void OnExportStl(wxCommandEvent &);
    void OnSlice(wxCommandEvent &);
    void OnInteractiveSlice(wxCommandEvent &);
    void OnSectionIndexChanged(wxSpinEvent &event);
    void OnSectionScroll(wxScrollEvent &event);
    void OnSectionScrollKey(wxKeyEvent &event);
    void OnDetectUnsupported(wxCommandEvent &);
    void OnGenerateSupports(wxCommandEvent &);
    void OnSupportGenerationProgress(wxThreadEvent &event);
    void OnSupportGenerationFinished(wxThreadEvent &event);
    void OnUnsupportedAnalysisFinished(wxThreadEvent &event);
    void OnOptimizeOrientation(wxCommandEvent &);
    void OnStopOptimization(wxCommandEvent &);
    void OnOrientationOptimizationEvent(wxThreadEvent &event);
    void OnShow(wxCommandEvent &);
    void OnHide(wxCommandEvent &);
    void OnDeleteModels(wxCommandEvent &);
    void OnResetTransform(wxCommandEvent &);
    void OnTransformModels(wxCommandEvent &);
    void OnMoveToOrigin(wxCommandEvent &);
    void OnListSelection(wxCommandEvent &);
    void OnListCheck(wxCommandEvent &);
    void PublishStatus();
    void UpdateCommandState();
    void UpdateSectionControls();
    void UpdateToolbarBitmaps();

    enum class SupportProgressStage : std::size_t {
        Slicing,
        FindingUnsupported,
        ContactPoints,
        VolumeSegmentation,
        GeneratingSupports,
        Count
    };

    struct SupportProgressState {
        std::size_t completed = 0;
        std::size_t total = 0;
        bool started = false;
        bool finished = false;
    };

    std::vector<std::shared_ptr<stl_slicer::SceneModel>> models_;
    wxCheckListBox *modelList_ = nullptr;
    ModelCanvas *canvas_ = nullptr;
    wxPanel *sectionControls_ = nullptr;
    wxSpinCtrl *sectionIndex_ = nullptr;
    wxScrollBar *sectionScroll_ = nullptr;
    wxToolBar *toolbar_ = nullptr;
    wxMenuItem *exportItem_ = nullptr;
    wxMenuItem *exportStlItem_ = nullptr;
    wxMenuItem *detectUnsupportedItem_ = nullptr;
    wxMenuItem *generateSupportsItem_ = nullptr;
    wxMenuItem *optimizationItem_ = nullptr;
    wxMenuItem *deleteModelsItem_ = nullptr;
    wxMenuItem *resetTransformItem_ = nullptr;
    wxMenuItem *transformModelsItem_ = nullptr;
    wxMenuItem *moveToOriginItem_ = nullptr;
    wxMenuItem *sectionItem_ = nullptr;
    std::thread unsupportedWorker_;
    std::thread supportGenerationWorker_;
    std::thread optimizationWorker_;
    std::atomic<bool> unsupportedAnalysisCancel_{false};
    std::atomic<bool> supportGenerationCancel_{false};
    std::atomic<bool> optimizationCancel_{false};
    std::atomic<bool> closing_{false};
    std::uint64_t modelRevision_ = 0;
    bool unsupportedAnalysisRunning_ = false;
    bool supportGenerationRunning_ = false;
    std::string supportGenerationSummary_;
    std::array<SupportProgressState,
               static_cast<std::size_t>(SupportProgressStage::Count)>
        supportProgress_{};
    bool optimizationRunning_ = false;
    std::size_t optimizationCompleted_ = 0;
    std::size_t optimizationTotal_ = 0;
    double optimizationBestScore_ = 0.0;
    bool optimizationHasScore_ = false;
    std::unordered_map<const stl_slicer::SceneModel *, double> optimizationBestScores_;
};
