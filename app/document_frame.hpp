// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/scene_model.hpp"
#include "model_tree_model.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <wx/dataview.h>
#include <wx/mdi.h>

enum class ModelLayoutOperation;

class ModelCanvas;
class ModelLayoutDialog;
class TransformDialog;
class wxActivateEvent;
class wxCloseEvent;
class wxDPIChangedEvent;
class wxDataViewEvent;
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
    void OpenWorkspace(const wxString &path);
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
    void OnSave(wxCommandEvent &);
    void OnSaveAs(wxCommandEvent &);
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
    void OnUnsupportedAnalysisProgress(wxThreadEvent &event);
    void OnOptimizeOrientation(wxCommandEvent &);
    void OnStopOptimization(wxCommandEvent &);
    void OnOrientationOptimizationEvent(wxThreadEvent &event);
    void OnShow(wxCommandEvent &);
    void OnHide(wxCommandEvent &);
    void OnDeleteModels(wxCommandEvent &);
    void OnResetTransform(wxCommandEvent &);
    void OnTransformModels(wxCommandEvent &);
    void OnArrangeModels(wxCommandEvent &);
    void OnAlignModels(wxCommandEvent &);
    void OnDistributeModels(wxCommandEvent &);
    void OnMultiplyModels(wxCommandEvent &);
    void ShowModelLayoutDialog(ModelLayoutOperation initialOperation);
    void ApplyModelLayoutDialog();
    void ApplyTransformDialog();
    void OnMoveToOrigin(wxCommandEvent &);
    void OnPlaceFacetOnPlatform(wxCommandEvent &);
    void OnModelSelectionChanged(wxDataViewEvent &);
    void OnModelContextMenu(wxDataViewEvent &);
    void SyncModelSelection();
    void OnModelVisibilityChanged(wxDataViewEvent &);
    void OnModelGroupExpanded(wxDataViewEvent &);
    void OnModelGroupCollapsed(wxDataViewEvent &);
    void OnModelDragBegin(wxDataViewEvent &);
    void OnModelDropPossible(wxDataViewEvent &);
    void OnModelDrop(wxDataViewEvent &);
    void OnNewModelGroup(wxCommandEvent &);
    void OnUngroupModels(wxCommandEvent &);
    void PublishStatus();
    bool SaveWorkspaceDocument(bool saveAs);
    bool LoadWorkspaceDocument(const wxString &path, bool establishWorkspacePath);
    void ShowMissingWorkspaceFiles(const std::vector<wxString> &paths);
    void UpdateCommandState();
    void UpdateSectionControls();
    void UpdateToolbarBitmaps();
    void PruneModelGroups();
    void CreateModelGroup(std::string name,
                          const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &members);
    void RemoveModelsFromGroups(const std::vector<stl_slicer::SceneModel *> &models);
    std::uint64_t ModelGroupId(const stl_slicer::SceneModel *model) const;

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
    wxDataViewCtrl *modelList_ = nullptr;
    ModelTreeModel *modelListModel_ = nullptr;
    std::vector<DocumentModelGroup> modelGroups_;
    std::uint64_t nextModelGroupId_ = 1;
    wxString workspacePath_;
    wxString workspaceProgress_;
    bool embedModelsOnSave_ = false;
    bool updatingModelList_ = false;
    ModelCanvas *canvas_ = nullptr;
    wxPanel *sectionControls_ = nullptr;
    wxSpinCtrl *sectionIndex_ = nullptr;
    wxScrollBar *sectionScroll_ = nullptr;
    wxToolBar *toolbar_ = nullptr;
    TransformDialog *transformDialog_ = nullptr;
    ModelLayoutDialog *modelLayoutDialog_ = nullptr;
    wxMenuItem *exportItem_ = nullptr;
    wxMenuItem *exportStlItem_ = nullptr;
    wxMenuItem *detectUnsupportedItem_ = nullptr;
    wxMenuItem *generateSupportsItem_ = nullptr;
    wxMenuItem *optimizationItem_ = nullptr;
    wxMenuItem *deleteModelsItem_ = nullptr;
    wxMenuItem *resetTransformItem_ = nullptr;
    wxMenuItem *transformModelsItem_ = nullptr;
    wxMenuItem *alignModelsItem_ = nullptr;
    wxMenuItem *distributeModelsItem_ = nullptr;
    wxMenuItem *multiplyModelsItem_ = nullptr;
    wxMenuItem *newModelGroupItem_ = nullptr;
    wxMenuItem *ungroupModelsItem_ = nullptr;
    wxMenuItem *moveToOriginItem_ = nullptr;
    wxMenuItem *placeFacetItem_ = nullptr;
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
    bool unsupportedProgressVisible_ = false;
    std::size_t unsupportedProgressCompleted_ = 0;
    std::size_t unsupportedProgressTotal_ = 0;
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
