#pragma once

#include "stl_slicer/scene_model.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>
#include <wx/checklst.h>
#include <wx/mdi.h>

class ModelCanvas;
class wxActivateEvent;
class wxCloseEvent;
class wxMenuItem;
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
    stl_slicer::Bounds3 VisibleBounds() const;
    stl_slicer::Vec3 SelectedCenter() const;
    void RefreshModelList();
    void UpdateStatus();
    void OpenPath(const wxString &path);
    void InvalidateUnsupportedAnalysis();
    void SettingsChanged();
    double ContourHealingThreshold() const;
    double SegmentationTolerance() const;
    double CriticalAngleDegrees() const;
    double OverhangCoefficient() const;

  private:
    void BuildMenus();
    void OnOpen(wxCommandEvent &);
    void OnActivate(wxActivateEvent &);
    void OnClose(wxCloseEvent &);
    void OnExport(wxCommandEvent &);
    void OnSlice(wxCommandEvent &);
    void OnInteractiveSlice(wxCommandEvent &);
    void OnAnalyzeUnsupported(wxCommandEvent &);
    void OnUnsupportedAnalysisFinished(wxThreadEvent &event);
    void OnOptimizeOrientation(wxCommandEvent &);
    void OnOrientationOptimizationEvent(wxThreadEvent &event);
    void OnShow(wxCommandEvent &);
    void OnHide(wxCommandEvent &);
    void OnListSelection(wxCommandEvent &);
    void OnListCheck(wxCommandEvent &);
    void PublishStatus();
    void UpdateCommandState();

    std::vector<std::shared_ptr<stl_slicer::SceneModel>> models_;
    wxCheckListBox *modelList_ = nullptr;
    ModelCanvas *canvas_ = nullptr;
    wxToolBar *toolbar_ = nullptr;
    wxMenuItem *exportItem_ = nullptr;
    wxMenuItem *unsupportedItem_ = nullptr;
    wxMenuItem *optimizationItem_ = nullptr;
    std::thread unsupportedWorker_;
    std::thread optimizationWorker_;
    std::atomic<bool> optimizationCancel_{false};
    std::atomic<bool> closing_{false};
    std::uint64_t modelRevision_ = 0;
    bool unsupportedAnalysisRunning_ = false;
    bool optimizationRunning_ = false;
    std::size_t optimizationCompleted_ = 0;
    std::size_t optimizationTotal_ = 0;
    double optimizationBestScore_ = 0.0;
    bool optimizationHasScore_ = false;
    std::unordered_map<const stl_slicer::SceneModel *, double> optimizationBestScores_;
};
