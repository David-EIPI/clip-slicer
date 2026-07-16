#include "document_frame.hpp"
#include "embedded_assets.hpp"
#include "gl_canvas.hpp"
#include "main_frame.hpp"
#include "model_transform_dialog.hpp"
#include "slice_visualization.hpp"
#include "stl_slicer/cli_reader.hpp"
#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/orientation_optimizer.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
#include "stl_slicer/stl_writer.hpp"
#include "stl_slicer/support_generator.hpp"
#include "stl_slicer/unsupported_area.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/thread.h>
#include <wx/toolbar.h>

namespace {
enum {
    IdOpen = wxID_HIGHEST + 20,
    IdOpenIntoDocument,
    IdExport,
    IdExportStl,
    IdSlice,
    IdInteractive,
    IdDetectUnsupported,
    IdGenerateSupports,
    IdOptimizeOrientation,
    IdStopOptimization,
    IdShow,
    IdHide,
    IdResetTransform,
    IdTransformModels,
    IdMoveToOrigin,
    IdSettings
};

struct UnsupportedAnalysisPayload {
    VisualizationMesh visualization;
    double totalArea = 0.0;
    std::uint64_t modelRevision = 0;
    bool cancelled = false;
    std::string error;
};

struct SupportGenerationPayload {
    stl_slicer::TriangleMesh supports;
    std::size_t contactPointCount = 0;
    std::size_t processedLayerCount = 0;
    std::uint64_t modelRevision = 0;
    bool cancelled = false;
    std::string error;
};

struct ModelSnapshot {
    std::shared_ptr<const stl_slicer::SceneModel> model;
    stl_slicer::Mat4 transform;
    std::size_t triangleCount = 0;
};

std::vector<ModelSnapshot>
selectedModelSnapshots(const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &models) {
    std::vector<ModelSnapshot> snapshots;
    for (const auto &model : models)
        if (model->selected)
            snapshots.push_back({model, model->transform, model->renderVertices().size() / 3});
    return snapshots;
}

stl_slicer::TriangleMesh combinedTransformedMesh(const std::vector<ModelSnapshot> &snapshots) {
    stl_slicer::TriangleMesh combined;
    std::size_t triangleCount = 0;
    for (const ModelSnapshot &snapshot : snapshots)
        triangleCount += snapshot.triangleCount;
    combined.reserve(triangleCount);
    for (const ModelSnapshot &snapshot : snapshots) {
        const stl_slicer::TriangleMesh mesh = snapshot.model->triangleMesh();
        for (auto triangle : mesh.triangles()) {
            for (auto &vertex : triangle.vertices)
                vertex = snapshot.transform.transformPoint(vertex);
            triangle.normal = snapshot.transform.transformVector(triangle.normal);
            combined.addTriangle(std::move(triangle));
        }
    }
    return combined;
}

enum class OrientationEventType { InitialScore, Improvement, Progress, Finished, Error };

struct OrientationOptimizationPayload {
    OrientationEventType type = OrientationEventType::Progress;
    std::shared_ptr<stl_slicer::SceneModel> model;
    stl_slicer::Mat4 transform;
    double score = 0.0;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::uint64_t modelRevision = 0;
    std::string error;
};

wxBitmap LoadEmbeddedIcon(const unsigned char *data,
                          std::size_t dataSize,
                          const wxArtID &fallback,
                          const wxSize &size) {
    wxBitmap icon = wxBitmap::NewFromPNGData(data, dataSize);
    if (!icon.IsOk() || icon.GetSize() != size)
        return wxArtProvider::GetBitmap(fallback, wxART_TOOLBAR, size);
    return icon;
}
} // namespace

class SliceDialog final : public wxDialog {
  public:
    SliceDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, "Slice models") {
        auto *root = new wxBoxSizer(wxVERTICAL);
        target = new wxRadioBox(this,
                                wxID_ANY,
                                "Output",
                                wxDefaultPosition,
                                wxDefaultSize,
                                {"Same document", "New document"});
        root->Add(target, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
        SetSizerAndFit(root);
    }
    wxRadioBox *target;
};

DocumentFrame::DocumentFrame(wxMDIParentFrame *parent, const wxString &title)
    : wxMDIChildFrame(parent, wxID_ANY, title, wxDefaultPosition, {1000, 700}) {
    BuildMenus();
    auto *root = new wxBoxSizer(wxVERTICAL);
    toolbar_ = new wxToolBar(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_TEXT);
    const wxSize toolSize{24, 24};
    toolbar_->SetToolBitmapSize(toolSize);
    toolbar_->AddTool(
        IdExport, "Export", wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_TOOLBAR, toolSize));
    toolbar_->AddTool(IdSlice,
                      "Slice",
                      LoadEmbeddedIcon(clip_slicer::assets::sliceBreadIconPng,
                                       clip_slicer::assets::sliceBreadIconPngSize,
                                       wxART_EXECUTABLE_FILE,
                                       toolSize));
    toolbar_->AddTool(IdInteractive,
                      "Plane",
                      LoadEmbeddedIcon(clip_slicer::assets::planeSliceIconPng,
                                       clip_slicer::assets::planeSliceIconPngSize,
                                       wxART_LIST_VIEW,
                                       toolSize),
                      "Interactive slicing",
                      wxITEM_CHECK);
    toolbar_->AddTool(IdOptimizeOrientation,
                      "Optimize",
                      LoadEmbeddedIcon(clip_slicer::assets::orientationOptimizerIconPng,
                                       clip_slicer::assets::orientationOptimizerIconPngSize,
                                       wxART_GO_UP,
                                       toolSize),
                      "Optimize model orientation");
    toolbar_->AddTool(IdStopOptimization,
                      "Stop",
                      wxArtProvider::GetBitmap(wxART_STOP, wxART_TOOLBAR, toolSize),
                      "Stop orientation optimization");
    toolbar_->AddTool(
        IdHide, "Hide", wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_TOOLBAR, toolSize));
    toolbar_->AddTool(
        IdShow, "Show", wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, toolSize));
    toolbar_->AddSeparator();
    toolbar_->AddTool(IdDetectUnsupported,
                      "Detect",
                      LoadEmbeddedIcon(clip_slicer::assets::supportDetectIconPng,
                                       clip_slicer::assets::supportDetectIconPngSize,
                                       wxART_FIND,
                                       toolSize),
                      "Detect unsupported areas");
    toolbar_->AddTool(IdGenerateSupports,
                      "Generate",
                      LoadEmbeddedIcon(clip_slicer::assets::supportGenerateIconPng,
                                       clip_slicer::assets::supportGenerateIconPngSize,
                                       wxART_PLUS,
                                       toolSize),
                      "Generate support structures");
    toolbar_->AddSeparator();
    toolbar_->AddTool(IdResetTransform,
                      "Reset",
                      LoadEmbeddedIcon(clip_slicer::assets::resetTransformIconPng,
                                       clip_slicer::assets::resetTransformIconPngSize,
                                       wxART_UNDO,
                                       toolSize),
                      "Reset selected model transformations");
    toolbar_->AddTool(IdTransformModels,
                      "Transform",
                      LoadEmbeddedIcon(clip_slicer::assets::transformModelsIconPng,
                                       clip_slicer::assets::transformModelsIconPngSize,
                                       wxART_REDO,
                                       toolSize),
                      "Transform selected models by exact values");
    toolbar_->AddTool(IdMoveToOrigin,
                      "Origin",
                      LoadEmbeddedIcon(clip_slicer::assets::moveToOriginIconPng,
                                       clip_slicer::assets::moveToOriginIconPngSize,
                                       wxART_GO_HOME,
                                       toolSize),
                      "Move selected models into the positive octant");
    toolbar_->Realize();
    root->Add(toolbar_, 0, wxEXPAND);
    auto *split = new wxSplitterWindow(this);
    modelList_ =
        new wxCheckListBox(split, wxID_ANY, wxDefaultPosition, wxDefaultSize, {}, wxLB_EXTENDED);
    canvas_ = new ModelCanvas(split, *this);
    split->SplitVertically(modelList_, canvas_, 220);
    split->SetMinimumPaneSize(120);
    root->Add(split, 1, wxEXPAND);
    SetSizer(root);
    Bind(wxEVT_ACTIVATE, &DocumentFrame::OnActivate, this);
    Bind(wxEVT_CLOSE_WINDOW, &DocumentFrame::OnClose, this);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) { static_cast<MainFrame *>(GetMDIParent())->OpenDialog(); },
        IdOpen);
    Bind(wxEVT_MENU, &DocumentFrame::OnOpen, this, IdOpenIntoDocument);
    Bind(wxEVT_MENU, &DocumentFrame::OnExport, this, IdExport);
    Bind(wxEVT_MENU, &DocumentFrame::OnExportStl, this, IdExportStl);
    Bind(wxEVT_MENU, &DocumentFrame::OnSlice, this, IdSlice);
    Bind(wxEVT_MENU, &DocumentFrame::OnInteractiveSlice, this, IdInteractive);
    Bind(wxEVT_MENU, &DocumentFrame::OnDetectUnsupported, this, IdDetectUnsupported);
    Bind(wxEVT_THREAD, &DocumentFrame::OnUnsupportedAnalysisFinished, this, IdDetectUnsupported);
    Bind(wxEVT_MENU, &DocumentFrame::OnGenerateSupports, this, IdGenerateSupports);
    Bind(wxEVT_THREAD, &DocumentFrame::OnSupportGenerationFinished, this, IdGenerateSupports);
    Bind(wxEVT_MENU, &DocumentFrame::OnOptimizeOrientation, this, IdOptimizeOrientation);
    Bind(wxEVT_MENU, &DocumentFrame::OnStopOptimization, this, IdStopOptimization);
    Bind(wxEVT_THREAD, &DocumentFrame::OnOrientationOptimizationEvent, this, IdOptimizeOrientation);
    Bind(wxEVT_MENU, &DocumentFrame::OnShow, this, IdShow);
    Bind(wxEVT_MENU, &DocumentFrame::OnHide, this, IdHide);
    Bind(wxEVT_MENU, &DocumentFrame::OnResetTransform, this, IdResetTransform);
    Bind(wxEVT_MENU, &DocumentFrame::OnTransformModels, this, IdTransformModels);
    Bind(wxEVT_MENU, &DocumentFrame::OnMoveToOrigin, this, IdMoveToOrigin);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
            static_cast<MainFrame *>(GetMDIParent())->ShowSettingsDialog();
        },
        IdSettings);
    modelList_->Bind(wxEVT_LISTBOX, &DocumentFrame::OnListSelection, this);
    modelList_->Bind(wxEVT_CHECKLISTBOX, &DocumentFrame::OnListCheck, this);
    UpdateStatus();
    UpdateCommandState();
}
DocumentFrame::~DocumentFrame() {
    closing_.store(true, std::memory_order_relaxed);
    unsupportedAnalysisCancel_.store(true, std::memory_order_relaxed);
    supportGenerationCancel_.store(true, std::memory_order_relaxed);
    optimizationCancel_.store(true, std::memory_order_relaxed);
    if (unsupportedWorker_.joinable())
        unsupportedWorker_.join();
    if (supportGenerationWorker_.joinable())
        supportGenerationWorker_.join();
    if (optimizationWorker_.joinable())
        optimizationWorker_.join();
    DeletePendingEvents();
}
void DocumentFrame::BuildMenus() {
    auto *file = new wxMenu;
    file->Append(IdOpen, "&Open...\tCtrl+O");
    file->Append(IdOpenIntoDocument, "Open into &document...");
    exportItem_ = file->Append(IdExport, "&Export Slices...");
    exportStlItem_ = file->Append(IdExportStl, "Export &STL...");
    file->Append(IdSettings, "&Settings...");
    file->Append(wxID_CLOSE, "&Close");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto *models = new wxMenu;
    models->Append(IdShow, "&Show selected");
    models->Append(IdHide, "&Hide selected");
    models->AppendSeparator();
    resetTransformItem_ = models->Append(IdResetTransform, "&Reset transformations");
    transformModelsItem_ = models->Append(IdTransformModels, "&Transform...");
    moveToOriginItem_ = models->Append(IdMoveToOrigin, "Move to &Origin");
    auto *slice = new wxMenu;
    slice->Append(IdSlice, "&Slice selected...");
    slice->AppendCheckItem(IdInteractive, "&Interactive slicing");
    detectUnsupportedItem_ = slice->Append(IdDetectUnsupported, "&Detect unsupported areas");
    generateSupportsItem_ = slice->Append(IdGenerateSupports, "&Generate supports");
    optimizationItem_ = slice->Append(IdOptimizeOrientation, "&Optimize orientation");
    auto *bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(models, "&Models");
    bar->Append(slice, "&Slice");
    SetMenuBar(bar);
    Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { Close(); }, wxID_CLOSE);
    Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { GetMDIParent()->Close(); }, wxID_EXIT);
}
void DocumentFrame::AddModel(std::shared_ptr<stl_slicer::SceneModel> model) {
    InvalidateUnsupportedAnalysis();
    models_.push_back(std::move(model));
    RefreshModelList();
    canvas_->ModelsChanged();
    UpdateStatus();
}
void DocumentFrame::InvalidateUnsupportedAnalysis() {
    ++modelRevision_;
    if (unsupportedAnalysisRunning_)
        unsupportedAnalysisCancel_.store(true, std::memory_order_relaxed);
    if (supportGenerationRunning_)
        supportGenerationCancel_.store(true, std::memory_order_relaxed);
    if (optimizationRunning_)
        optimizationCancel_.store(true, std::memory_order_relaxed);
    if (canvas_)
        canvas_->ClearUnsupportedVisualization();
}
void DocumentFrame::SettingsChanged() {
    InvalidateUnsupportedAnalysis();
    canvas_->SettingsChanged();
}
void DocumentFrame::InteractiveSliceStateChanged() {
    UpdateCommandState();
}
double DocumentFrame::LayerThickness() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().layerThickness;
}
double DocumentFrame::FirstLayerOffset() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().firstLayerOffset;
}
double DocumentFrame::ContourHealingThreshold() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().contourHealingThreshold;
}
double DocumentFrame::SegmentationTolerance() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().segmentationTolerance;
}
double DocumentFrame::CriticalAngleDegrees() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().criticalAngleDegrees;
}
double DocumentFrame::OverhangCoefficient() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().overhangCoefficient;
}
void DocumentFrame::OpenPath(const wxString &path) {
    try {
        const auto extension = wxFileName(path).GetExt().Lower();
        if (extension == "stl")
            AddModel(std::make_shared<stl_slicer::MeshSceneModel>(
                wxFileName(path).GetFullName().ToStdString(),
                stl_slicer::BinaryStlReader{}.read(path.ToStdString())));
        else if (extension == "cli")
            AddModel(std::make_shared<stl_slicer::SliceSceneModel>(
                wxFileName(path).GetFullName().ToStdString(),
                stl_slicer::CliReader{}.read(path.ToStdString())));
        else
            throw std::runtime_error("Unsupported file extension");
    } catch (const std::exception &e) {
        wxMessageBox(e.what(), "Open failed", wxOK | wxICON_ERROR, this);
    }
}
void DocumentFrame::OnOpen(wxCommandEvent &) {
    wxFileDialog d(this,
                   "Open model",
                   {},
                   {},
                   "3D model files (*.stl;*.cli)|*.stl;*.cli",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
    if (d.ShowModal() == wxID_OK)
        OpenPath(d.GetPath());
}
void DocumentFrame::RefreshModelList() {
    modelList_->Clear();
    for (const auto &m : models_) {
        const int index = modelList_->Append(m->name);
        modelList_->Check(index, m->visible);
        modelList_->SetSelection(index, m->selected);
    }
    UpdateCommandState();
}
void DocumentFrame::UpdateCommandState() {
    const bool modelSelected = std::any_of(
        models_.begin(), models_.end(), [](const auto &model) { return model->selected; });
    const bool sliced = std::any_of(models_.begin(), models_.end(), [](const auto &model) {
        return model->selected && model->isSliced();
    });
    const bool meshSelected = std::any_of(models_.begin(), models_.end(), [](const auto &model) {
        return model->selected && !model->isSliced();
    });
    if (exportItem_)
        exportItem_->Enable(sliced);
    if (exportStlItem_)
        exportStlItem_->Enable(meshSelected);
    if (toolbar_)
        toolbar_->EnableTool(IdExport, sliced);
    if (resetTransformItem_)
        resetTransformItem_->Enable(modelSelected);
    if (transformModelsItem_)
        transformModelsItem_->Enable(modelSelected);
    if (moveToOriginItem_)
        moveToOriginItem_->Enable(modelSelected);
    if (toolbar_) {
        toolbar_->EnableTool(IdResetTransform, modelSelected);
        toolbar_->EnableTool(IdTransformModels, modelSelected);
        toolbar_->EnableTool(IdMoveToOrigin, modelSelected);
    }
    const bool canAnalyze = !unsupportedAnalysisRunning_ && !supportGenerationRunning_ &&
                            !optimizationRunning_ &&
                            std::any_of(models_.begin(), models_.end(), [](const auto &model) {
                                return model->selected;
                            });
    if (detectUnsupportedItem_)
        detectUnsupportedItem_->Enable(canAnalyze);
    if (generateSupportsItem_)
        generateSupportsItem_->Enable(canAnalyze);
    if (toolbar_)
        toolbar_->EnableTool(IdDetectUnsupported, canAnalyze);
    if (toolbar_)
        toolbar_->EnableTool(IdGenerateSupports, canAnalyze);
    if (optimizationItem_)
        optimizationItem_->Enable(canAnalyze);
    if (toolbar_)
        toolbar_->EnableTool(IdOptimizeOrientation, canAnalyze);
    if (toolbar_)
        toolbar_->EnableTool(IdStopOptimization,
                             optimizationRunning_ || unsupportedAnalysisRunning_ ||
                                 supportGenerationRunning_ ||
                                 (canvas_ && canvas_->InteractiveSliceRunning()));
}
void DocumentFrame::OnListSelection(wxCommandEvent &) {
    for (std::size_t i = 0; i < models_.size(); ++i)
        models_[i]->selected = modelList_->IsSelected(i);
    UpdateCommandState();
    canvas_->SelectionChanged();
}
void DocumentFrame::OnListCheck(wxCommandEvent &e) {
    models_[e.GetInt()]->visible = modelList_->IsChecked(e.GetInt());
    canvas_->Refresh();
    UpdateStatus();
}
void DocumentFrame::OnShow(wxCommandEvent &) {
    for (auto &m : models_)
        if (m->selected)
            m->visible = true;
    RefreshModelList();
    canvas_->Refresh();
    UpdateStatus();
}
void DocumentFrame::OnHide(wxCommandEvent &) {
    for (auto &m : models_)
        if (m->selected)
            m->visible = false;
    RefreshModelList();
    canvas_->Refresh();
    UpdateStatus();
}
void DocumentFrame::OnResetTransform(wxCommandEvent &) {
    bool changed = false;
    for (auto &model : models_) {
        if (!model->selected)
            continue;
        model->transform = stl_slicer::Mat4{};
        changed = true;
    }
    if (!changed)
        return;

    InvalidateUnsupportedAnalysis();
    canvas_->ModelTransformsChanged();
    UpdateStatus();
}
void DocumentFrame::OnTransformModels(wxCommandEvent &) {
    TransformDialog dialog(this);
    if (dialog.ShowModal() != wxID_OK)
        return;

    constexpr double pi = 3.14159265358979323846;
    const stl_slicer::Vec3 center = SelectedCenter();
    const stl_slicer::Vec3 translation = dialog.Translation();
    const stl_slicer::Mat4 rotation =
        stl_slicer::Mat4::rotation(dialog.AngleDegrees() * pi / 180.0, dialog.Axis());
    const stl_slicer::Mat4 transform =
        stl_slicer::Mat4::translation(translation.x, translation.y, translation.z) *
        stl_slicer::Mat4::translation(center.x, center.y, center.z) * rotation *
        stl_slicer::Mat4::scale(dialog.UniformScale()) *
        stl_slicer::Mat4::translation(-center.x, -center.y, -center.z);
    for (auto &model : models_) {
        if (model->selected)
            model->transform = transform * model->transform;
    }
    InvalidateUnsupportedAnalysis();
    canvas_->ModelTransformsChanged();
    UpdateStatus();
}
void DocumentFrame::OnMoveToOrigin(wxCommandEvent &) {
    stl_slicer::Bounds3 bounds;
    for (const auto &model : models_) {
        if (!model->selected)
            continue;
        for (const stl_slicer::RenderVertex &vertex : model->renderVertices())
            bounds.include(model->transform.transformPoint({vertex.x, vertex.y, vertex.z}));
    }
    if (!bounds.valid())
        return;

    const stl_slicer::Vec3 translation{-bounds.min.x, -bounds.min.y, -bounds.min.z};
    const stl_slicer::Mat4 transform =
        stl_slicer::Mat4::translation(translation.x, translation.y, translation.z);
    for (auto &model : models_) {
        if (model->selected)
            model->transform = transform * model->transform;
    }
    InvalidateUnsupportedAnalysis();
    canvas_->TranslateViewCenter(translation);
    canvas_->ModelTransformsChanged();
    UpdateStatus();
}
stl_slicer::Bounds3 DocumentFrame::VisibleBounds() const {
    stl_slicer::Bounds3 b;
    for (const auto &m : models_)
        if (m->visible) {
            auto mb = m->worldBounds();
            if (mb.valid()) {
                b.include(mb.min);
                b.include(mb.max);
            }
        }
    return b;
}
stl_slicer::Vec3 DocumentFrame::SelectedCenter() const {
    stl_slicer::Bounds3 b;
    for (const auto &m : models_)
        if (m->selected) {
            auto mb = m->worldBounds();
            if (mb.valid()) {
                b.include(mb.min);
                b.include(mb.max);
            }
        }
    return b.valid() ? stl_slicer::Vec3{(b.min.x + b.max.x) / 2,
                                        (b.min.y + b.max.y) / 2,
                                        (b.min.z + b.max.z) / 2}
                     : stl_slicer::Vec3{};
}
void DocumentFrame::UpdateStatus() {
    auto *parent = static_cast<MainFrame *>(GetMDIParent());
    if (parent->GetActiveChild() != this)
        return;
    PublishStatus();
}

void DocumentFrame::PublishStatus() {
    auto *parent = static_cast<MainFrame *>(GetMDIParent());
    auto b = VisibleBounds();
    std::ostringstream buildVolume;
    buildVolume << std::fixed << std::setprecision(2);
    if (b.valid())
        buildVolume << (b.max.x - b.min.x) << " x " << (b.max.y - b.min.y) << " x "
                    << (b.max.z - b.min.z);
    else
        buildVolume << "0.00 x 0.00 x 0.00";
    std::ostringstream slicePosition;
    slicePosition << "Z: ";
    if (canvas_->InteractiveSlice())
        slicePosition << std::fixed << std::setprecision(3) << canvas_->SlicePosition();
    else
        slicePosition << "--";
    std::ostringstream optimizationProgress;
    if (optimizationRunning_) {
        optimizationProgress << "Run " << optimizationCompleted_ << " of " << optimizationTotal_;
        if (optimizationHasScore_)
            optimizationProgress << " (S=" << std::fixed << std::setprecision(2)
                                 << optimizationBestScore_ << ')';
    }
    parent->SetDocumentStatus(buildVolume.str(), slicePosition.str(), optimizationProgress.str());
}
void DocumentFrame::OnActivate(wxActivateEvent &event) {
    if (event.GetActive())
        PublishStatus();
    event.Skip();
}
void DocumentFrame::OnClose(wxCloseEvent &event) {
    auto *parent = static_cast<MainFrame *>(GetMDIParent());
    if (parent->GetActiveChild() == this)
        parent->SetDocumentStatus({}, {}, {});
    event.Skip();
}
void DocumentFrame::OnSlice(wxCommandEvent &) {
    SliceDialog d(this);
    if (d.ShowModal() != wxID_OK)
        return;
    const double dz = LayerThickness();
    const double start = FirstLayerOffset();
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> made;
    for (const auto &m : models_)
        if (m->selected) {
            try {
                auto data = stl_slicer::Slicer{
                    {dz,
                     SegmentationTolerance(),
                     ContourHealingThreshold(),
                     start}}.slice(stl_slicer::transformedMesh(*m));
                made.push_back(std::make_shared<stl_slicer::SliceSceneModel>(m->name + " slices",
                                                                             std::move(data)));
            } catch (const std::exception &e) {
                wxMessageBox(e.what(), "Slicing failed", wxOK | wxICON_ERROR, this);
                return;
            }
        }
    if (d.target->GetSelection() == 0) {
        for (auto &m : made)
            AddModel(m);
    } else if (!made.empty()) {
        auto *child = new DocumentFrame(GetMDIParent(), "Slices");
        child->Show();
        for (auto &m : made)
            child->AddModel(m);
    }
}
void DocumentFrame::OnExport(wxCommandEvent &) {
    std::vector<std::reference_wrapper<const stl_slicer::SliceData>> slices;
    for (const auto &m : models_)
        if (m->selected && m->isSliced())
            slices.emplace_back(*m->slices());
    if (slices.empty()) {
        wxMessageBox("Select at least one sliced model.");
        return;
    }
    const stl_slicer::SliceData combined = stl_slicer::mergeSlices(slices);
    wxFileDialog d(this,
                   "Export slices",
                   {},
                   "model.cli",
                   "CLI files (*.cli)|*.cli",
                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
    if (d.ShowModal() == wxID_OK)
        try {
            stl_slicer::CliWriter{}.write(combined, d.GetPath().ToStdString());
        } catch (const std::exception &e) {
            wxMessageBox(e.what(), "Export failed", wxOK | wxICON_ERROR, this);
        }
}
void DocumentFrame::OnExportStl(wxCommandEvent &) {
    std::size_t triangleCount = 0;
    for (const auto &model : models_)
        if (model->selected && !model->isSliced())
            triangleCount += model->renderVertices().size() / 3;
    if (triangleCount == 0) {
        wxMessageBox("Select at least one non-sliced 3D model.");
        return;
    }

    stl_slicer::TriangleMesh combined;
    combined.reserve(triangleCount);
    for (const auto &model : models_) {
        if (!model->selected || model->isSliced())
            continue;
        const stl_slicer::TriangleMesh mesh = stl_slicer::transformedMesh(*model);
        for (const auto &triangle : mesh.triangles())
            combined.addTriangle(triangle);
    }

    wxFileDialog dialog(this,
                        "Export STL",
                        {},
                        "model.stl",
                        "STL files (*.stl)|*.stl",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxFD_CHANGE_DIR);
    if (dialog.ShowModal() == wxID_OK) {
        try {
            stl_slicer::BinaryStlWriter{}.write(combined, dialog.GetPath().ToStdString());
        } catch (const std::exception &error) {
            wxMessageBox(error.what(), "Export failed", wxOK | wxICON_ERROR, this);
        }
    }
}
void DocumentFrame::OnInteractiveSlice(wxCommandEvent &) {
    canvas_->SetInteractiveSlice(!canvas_->InteractiveSlice());
}
void DocumentFrame::OnDetectUnsupported(wxCommandEvent &) {
    if (unsupportedAnalysisRunning_ || optimizationRunning_ || supportGenerationRunning_)
        return;
    std::vector<ModelSnapshot> snapshots = selectedModelSnapshots(models_);
    if (snapshots.empty()) {
        wxMessageBox("Select at least one model to analyze.",
                     "Unsupported areas",
                     wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    if (unsupportedWorker_.joinable())
        unsupportedWorker_.join();

    canvas_->ClearUnsupportedVisualization();
    unsupportedAnalysisCancel_.store(false, std::memory_order_relaxed);
    unsupportedAnalysisRunning_ = true;
    UpdateCommandState();
    const std::uint64_t revision = modelRevision_;
    const double healingThreshold = ContourHealingThreshold();
    const double segmentationTolerance = SegmentationTolerance();
    const double layerThickness = LayerThickness();
    const double firstLayerOffset = FirstLayerOffset();
    const double criticalAngleDegrees = CriticalAngleDegrees();
    const double overhangCoefficient = OverhangCoefficient();
    unsupportedWorker_ = std::thread([this,
                                      snapshots = std::move(snapshots),
                                      revision,
                                      healingThreshold,
                                      segmentationTolerance,
                                      layerThickness,
                                      firstLayerOffset,
                                      criticalAngleDegrees,
                                      overhangCoefficient]() mutable {
        auto payload = std::make_shared<UnsupportedAnalysisPayload>();
        payload->modelRevision = revision;
        try {
            stl_slicer::TriangleMesh combined = combinedTransformedMesh(snapshots);
            const stl_slicer::SliceData slices = stl_slicer::Slicer{
                {layerThickness,
                 segmentationTolerance,
                 healingThreshold,
                 firstLayerOffset}}.slice(combined, &unsupportedAnalysisCancel_);
            if (!unsupportedAnalysisCancel_.load(std::memory_order_relaxed)) {
                stl_slicer::UnsupportedAreaResult unsupported =
                    stl_slicer::UnsupportedAreaAnalyzer{
                        {criticalAngleDegrees, overhangCoefficient}}
                        .analyze(slices, &unsupportedAnalysisCancel_);
                if (!unsupportedAnalysisCancel_.load(std::memory_order_relaxed)) {
                    payload->totalArea = unsupported.totalArea;
                    payload->visualization =
                        BuildUnsupportedSurfaces(unsupported.unsupported);
                }
            }
            payload->cancelled =
                unsupportedAnalysisCancel_.load(std::memory_order_relaxed);
        } catch (const std::exception &error) {
            payload->error = error.what();
        }
        if (closing_.load(std::memory_order_relaxed))
            return;
        auto *event = new wxThreadEvent(wxEVT_THREAD, IdDetectUnsupported);
        event->SetPayload(payload);
        wxQueueEvent(this, event);
    });
}

void DocumentFrame::OnGenerateSupports(wxCommandEvent &) {
    if (unsupportedAnalysisRunning_ || supportGenerationRunning_ || optimizationRunning_)
        return;
    std::vector<ModelSnapshot> snapshots = selectedModelSnapshots(models_);
    if (snapshots.empty()) {
        wxMessageBox("Select at least one model to generate supports.",
                     "Generate supports",
                     wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    if (supportGenerationWorker_.joinable())
        supportGenerationWorker_.join();

    const AppSettings settings = static_cast<MainFrame *>(GetMDIParent())->Settings();
    const std::uint64_t revision = modelRevision_;
    supportGenerationCancel_.store(false, std::memory_order_relaxed);
    supportGenerationRunning_ = true;
    canvas_->ClearUnsupportedVisualization();
    UpdateCommandState();

    supportGenerationWorker_ =
        std::thread([this, snapshots = std::move(snapshots), settings, revision]() mutable {
            auto payload = std::make_shared<SupportGenerationPayload>();
            payload->modelRevision = revision;
            try {
                auto source = std::make_shared<const stl_slicer::TriangleMesh>(
                    combinedTransformedMesh(snapshots));
                auto slices = std::make_shared<const stl_slicer::SliceData>(stl_slicer::Slicer{
                    {settings.layerThickness,
                     settings.segmentationTolerance,
                     settings.contourHealingThreshold,
                     settings.firstLayerOffset}}.slice(*source, &supportGenerationCancel_));
                if (!supportGenerationCancel_.load(std::memory_order_relaxed)) {
                    stl_slicer::UnsupportedAreaResult detected =
                        stl_slicer::UnsupportedAreaAnalyzer{
                            {settings.criticalAngleDegrees, settings.overhangCoefficient}}
                            .analyze(*slices, &supportGenerationCancel_);
                    if (supportGenerationCancel_.load(std::memory_order_relaxed)) {
                        payload->cancelled = true;
                    } else {
                        auto unsupported = std::make_shared<const stl_slicer::SliceData>(
                            std::move(detected.unsupported));
                        stl_slicer::SupportGenerationResult generated =
                            stl_slicer::SupportGenerator{
                                {static_cast<std::size_t>(settings.optimizationWorkers),
                                 settings.supportSpacing,
                                 {settings.supportTipTopRadius,
                                  settings.supportTipBottomRadius,
                                  settings.supportTipHeight,
                                  static_cast<std::size_t>(settings.supportCircumferencePoints),
                                  settings.criticalAngleDegrees}}}
                                .generate({source, slices, unsupported}, &supportGenerationCancel_);
                        payload->supports = std::move(generated.supports);
                        payload->contactPointCount = generated.contactPoints.size();
                        payload->processedLayerCount = generated.processedLayerCount;
                        payload->cancelled = generated.cancelled;
                    }
                } else {
                    payload->cancelled = true;
                }
            } catch (const std::exception &error) {
                payload->error = error.what();
            }
            if (closing_.load(std::memory_order_relaxed))
                return;
            auto *event = new wxThreadEvent(wxEVT_THREAD, IdGenerateSupports);
            event->SetPayload(std::move(payload));
            wxQueueEvent(this, event);
        });
}

void DocumentFrame::OnSupportGenerationFinished(wxThreadEvent &event) {
    if (supportGenerationWorker_.joinable())
        supportGenerationWorker_.join();
    supportGenerationRunning_ = false;
    supportGenerationCancel_.store(false, std::memory_order_relaxed);
    UpdateCommandState();

    const auto payload = event.GetPayload<std::shared_ptr<SupportGenerationPayload>>();
    if (!payload->error.empty()) {
        wxMessageBox(payload->error, "Support generation failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->cancelled || payload->modelRevision != modelRevision_)
        return;
    if (payload->supports.triangles().empty()) {
        const wxString message =
            payload->contactPointCount == 0
                ? "No support contact points were detected."
                : wxString::Format(
                      "%llu support contact points were detected.\n"
                      "No contact-tip geometry was generated.",
                      static_cast<unsigned long long>(payload->contactPointCount));
        wxMessageBox(message,
                     "Generate supports",
                     wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    AddModel(std::make_shared<stl_slicer::MeshSceneModel>("Generated supports",
                                                          std::move(payload->supports)));
}
void DocumentFrame::OnUnsupportedAnalysisFinished(wxThreadEvent &event) {
    if (unsupportedWorker_.joinable())
        unsupportedWorker_.join();
    unsupportedAnalysisRunning_ = false;
    unsupportedAnalysisCancel_.store(false, std::memory_order_relaxed);
    UpdateCommandState();

    const auto payload = event.GetPayload<std::shared_ptr<UnsupportedAnalysisPayload>>();
    if (!payload->error.empty()) {
        wxMessageBox(payload->error, "Unsupported-area analysis failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->cancelled || payload->modelRevision != modelRevision_)
        return;
    canvas_->SetUnsupportedVisualization(std::move(payload->visualization));
}

void DocumentFrame::OnOptimizeOrientation(wxCommandEvent &) {
    if (optimizationRunning_ || unsupportedAnalysisRunning_ || supportGenerationRunning_)
        return;

    struct ModelSnapshot {
        std::shared_ptr<stl_slicer::SceneModel> model;
        stl_slicer::TriangleMesh worldMesh;
        stl_slicer::Mat4 baseTransform;
    };
    std::vector<ModelSnapshot> snapshots;
    for (const auto &model : models_) {
        if (model->selected)
            snapshots.push_back({model, stl_slicer::transformedMesh(*model), model->transform});
    }
    if (snapshots.empty()) {
        wxMessageBox("Select at least one model to optimize.",
                     "Orientation optimization",
                     wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    if (optimizationWorker_.joinable())
        optimizationWorker_.join();

    const AppSettings settings = static_cast<MainFrame *>(GetMDIParent())->Settings();
    stl_slicer::OrientationOptimizerOptions options;
    options.attempts = static_cast<std::size_t>(settings.optimizationAttempts);
    options.workerCount = static_cast<std::size_t>(settings.optimizationWorkers);
    options.convergenceTolerance = settings.optimizationTolerance;
    options.layerThickness = settings.layerThickness;
    options.firstLayerOffset = settings.firstLayerOffset;
    options.segmentationTolerance = settings.segmentationTolerance;
    options.healingThreshold = settings.contourHealingThreshold;
    options.unsupportedArea = {settings.criticalAngleDegrees, settings.overhangCoefficient};

    canvas_->ClearUnsupportedVisualization();
    optimizationCancel_.store(false, std::memory_order_relaxed);
    optimizationRunning_ = true;
    optimizationCompleted_ = 0;
    optimizationTotal_ = options.attempts;
    optimizationHasScore_ = false;
    optimizationBestScores_.clear();
    const std::uint64_t revision = modelRevision_;
    UpdateCommandState();
    UpdateStatus();

    optimizationWorker_ =
        std::thread([this, snapshots = std::move(snapshots), options, revision]() mutable {
            const auto post = [this](std::shared_ptr<OrientationOptimizationPayload> payload) {
                if (closing_.load(std::memory_order_relaxed))
                    return;
                auto *event = new wxThreadEvent(wxEVT_THREAD, IdOptimizeOrientation);
                event->SetPayload(std::move(payload));
                wxQueueEvent(this, event);
            };

            try {
                for (auto &snapshot : snapshots) {
                    if (optimizationCancel_.load(std::memory_order_relaxed))
                        break;

                    auto progress = std::make_shared<OrientationOptimizationPayload>();
                    progress->type = OrientationEventType::Progress;
                    progress->total = options.attempts;
                    progress->modelRevision = revision;
                    post(std::move(progress));

                    stl_slicer::optimizeOrientation(
                        snapshot.worldMesh,
                        options,
                        &optimizationCancel_,
                        [&, model = snapshot.model, base = snapshot.baseTransform](
                            const stl_slicer::OrientationCandidate &candidate) {
                            auto improvement = std::make_shared<OrientationOptimizationPayload>();
                            improvement->type = OrientationEventType::Improvement;
                            improvement->model = model;
                            improvement->transform = candidate.transform * base;
                            improvement->score = candidate.unsupportedArea;
                            improvement->modelRevision = revision;
                            post(std::move(improvement));
                        },
                        [&, total = options.attempts](std::size_t completed, std::size_t) {
                            auto progress = std::make_shared<OrientationOptimizationPayload>();
                            progress->type = OrientationEventType::Progress;
                            progress->completed = completed;
                            progress->total = total;
                            progress->modelRevision = revision;
                            post(std::move(progress));
                        },
                        [&, model = snapshot.model](double score) {
                            auto initial = std::make_shared<OrientationOptimizationPayload>();
                            initial->type = OrientationEventType::InitialScore;
                            initial->model = model;
                            initial->score = score;
                            initial->modelRevision = revision;
                            post(std::move(initial));
                        });
                }

                auto finished = std::make_shared<OrientationOptimizationPayload>();
                finished->type = OrientationEventType::Finished;
                finished->modelRevision = revision;
                post(std::move(finished));
            } catch (const std::exception &error) {
                auto failed = std::make_shared<OrientationOptimizationPayload>();
                failed->type = OrientationEventType::Error;
                failed->modelRevision = revision;
                failed->error = error.what();
                post(std::move(failed));
            }
        });
}

void DocumentFrame::OnStopOptimization(wxCommandEvent &) {
    if (unsupportedAnalysisRunning_)
        unsupportedAnalysisCancel_.store(true, std::memory_order_relaxed);
    if (supportGenerationRunning_)
        supportGenerationCancel_.store(true, std::memory_order_relaxed);
    if (optimizationRunning_)
        optimizationCancel_.store(true, std::memory_order_relaxed);
    if (canvas_)
        canvas_->CancelInteractiveSlice();
}

void DocumentFrame::OnOrientationOptimizationEvent(wxThreadEvent &event) {
    const auto payload = event.GetPayload<std::shared_ptr<OrientationOptimizationPayload>>();
    if (payload->type == OrientationEventType::InitialScore) {
        if (payload->modelRevision != modelRevision_) {
            optimizationCancel_.store(true, std::memory_order_relaxed);
            return;
        }
        optimizationBestScores_[payload->model.get()] = payload->score;
        optimizationBestScore_ = payload->score;
        optimizationHasScore_ = true;
        UpdateStatus();
        return;
    }
    if (payload->type == OrientationEventType::Improvement) {
        if (payload->modelRevision != modelRevision_) {
            optimizationCancel_.store(true, std::memory_order_relaxed);
            return;
        }
        const auto previous = optimizationBestScores_.find(payload->model.get());
        if (previous != optimizationBestScores_.end() && previous->second <= payload->score)
            return;
        optimizationBestScores_[payload->model.get()] = payload->score;
        optimizationBestScore_ = payload->score;
        optimizationHasScore_ = true;
        payload->model->transform = payload->transform;
        canvas_->ModelTransformsChanged();
        UpdateStatus();
        return;
    }
    if (payload->type == OrientationEventType::Progress) {
        if (payload->modelRevision == modelRevision_) {
            if (payload->completed == 0) {
                optimizationCompleted_ = 0;
                optimizationHasScore_ = false;
            } else {
                optimizationCompleted_ = std::max(optimizationCompleted_, payload->completed);
            }
            optimizationTotal_ = payload->total;
            UpdateStatus();
        }
        return;
    }

    if (optimizationWorker_.joinable())
        optimizationWorker_.join();
    optimizationRunning_ = false;
    optimizationCancel_.store(false, std::memory_order_relaxed);
    optimizationCompleted_ = 0;
    optimizationTotal_ = 0;
    optimizationHasScore_ = false;
    optimizationBestScores_.clear();
    UpdateCommandState();
    UpdateStatus();

    if (payload->type == OrientationEventType::Error) {
        wxMessageBox(payload->error, "Orientation optimization failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->modelRevision == modelRevision_)
        InvalidateUnsupportedAnalysis();
}
