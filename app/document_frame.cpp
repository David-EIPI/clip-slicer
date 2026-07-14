#include "document_frame.hpp"
#include "embedded_assets.hpp"
#include "gl_canvas.hpp"
#include "main_frame.hpp"
#include "slice_visualization.hpp"
#include "stl_slicer/cli_reader.hpp"
#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
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
    IdSlice,
    IdInteractive,
    IdAnalyzeUnsupported,
    IdShow,
    IdHide,
    IdSettings
};

struct UnsupportedAnalysisPayload {
    VisualizationMesh visualization;
    double totalArea = 0.0;
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
        auto *grid = new wxFlexGridSizer(2, 6, 8);
        grid->Add(new wxStaticText(this, wxID_ANY, "Thickness (mm):"), 0, wxALIGN_CENTER_VERTICAL);
        thickness = new wxTextCtrl(this, wxID_ANY, "0.1");
        grid->Add(thickness, 1, wxEXPAND);
        grid->Add(
            new wxStaticText(this, wxID_ANY, "Starting height (mm):"), 0, wxALIGN_CENTER_VERTICAL);
        start = new wxTextCtrl(this, wxID_ANY, "0.1");
        grid->Add(start, 1, wxEXPAND);
        grid->AddGrowableCol(1);
        root->Add(grid, 0, wxEXPAND | wxALL, 12);
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
    wxTextCtrl *thickness, *start;
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
    toolbar_->AddTool(IdAnalyzeUnsupported,
                      "Supports",
                      LoadEmbeddedIcon(clip_slicer::assets::unsupportedAreaIconPng,
                                       clip_slicer::assets::unsupportedAreaIconPngSize,
                                       wxART_REPORT_VIEW,
                                       toolSize),
                      "Highlight unsupported areas");
    toolbar_->AddTool(
        IdHide, "Hide", wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_TOOLBAR, toolSize));
    toolbar_->AddTool(
        IdShow, "Show", wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, toolSize));
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
    Bind(wxEVT_MENU, &DocumentFrame::OnSlice, this, IdSlice);
    Bind(wxEVT_MENU, &DocumentFrame::OnInteractiveSlice, this, IdInteractive);
    Bind(wxEVT_MENU, &DocumentFrame::OnAnalyzeUnsupported, this, IdAnalyzeUnsupported);
    Bind(wxEVT_THREAD, &DocumentFrame::OnUnsupportedAnalysisFinished, this, IdAnalyzeUnsupported);
    Bind(wxEVT_MENU, &DocumentFrame::OnShow, this, IdShow);
    Bind(wxEVT_MENU, &DocumentFrame::OnHide, this, IdHide);
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
    if (unsupportedWorker_.joinable())
        unsupportedWorker_.join();
}
void DocumentFrame::BuildMenus() {
    auto *file = new wxMenu;
    file->Append(IdOpen, "&Open...\tCtrl+O");
    file->Append(IdOpenIntoDocument, "Open into &document...");
    exportItem_ = file->Append(IdExport, "&Export Slices...");
    file->Append(IdSettings, "&Settings...");
    file->Append(wxID_CLOSE, "&Close");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto *view = new wxMenu;
    view->Append(IdShow, "&Show selected");
    view->Append(IdHide, "&Hide selected");
    auto *slice = new wxMenu;
    slice->Append(IdSlice, "&Slice selected...");
    slice->AppendCheckItem(IdInteractive, "&Interactive slicing");
    unsupportedItem_ = slice->Append(IdAnalyzeUnsupported, "Highlight &unsupported areas");
    auto *bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(view, "&View");
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
    if (canvas_)
        canvas_->ClearUnsupportedVisualization();
}
void DocumentFrame::SettingsChanged() {
    InvalidateUnsupportedAnalysis();
    canvas_->SettingsChanged();
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
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
    const bool sliced =
        std::any_of(models_.begin(), models_.end(), [](const auto &m) { return m->isSliced(); });
    if (exportItem_)
        exportItem_->Enable(sliced);
    if (toolbar_)
        toolbar_->EnableTool(IdExport, sliced);
    const bool canAnalyze = !unsupportedAnalysisRunning_ &&
                            std::any_of(models_.begin(), models_.end(), [](const auto &model) {
                                return model->selected;
                            });
    if (unsupportedItem_)
        unsupportedItem_->Enable(canAnalyze);
    if (toolbar_)
        toolbar_->EnableTool(IdAnalyzeUnsupported, canAnalyze);
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
    parent->SetDocumentStatus(buildVolume.str(), slicePosition.str());
}
void DocumentFrame::OnActivate(wxActivateEvent &event) {
    if (event.GetActive())
        PublishStatus();
    event.Skip();
}
void DocumentFrame::OnClose(wxCloseEvent &event) {
    auto *parent = static_cast<MainFrame *>(GetMDIParent());
    if (parent->GetActiveChild() == this)
        parent->SetDocumentStatus({}, {});
    event.Skip();
}
void DocumentFrame::OnSlice(wxCommandEvent &) {
    SliceDialog d(this);
    if (d.ShowModal() != wxID_OK)
        return;
    double dz, start;
    if (!d.thickness->GetValue().ToDouble(&dz) || !d.start->GetValue().ToDouble(&start) ||
        dz <= 0 || start < 0) {
        wxMessageBox("Enter positive thickness and non-negative start height.");
        return;
    }
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
                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (d.ShowModal() == wxID_OK)
        try {
            stl_slicer::CliWriter{}.write(combined, d.GetPath().ToStdString());
        } catch (const std::exception &e) {
            wxMessageBox(e.what(), "Export failed", wxOK | wxICON_ERROR, this);
        }
}
void DocumentFrame::OnInteractiveSlice(wxCommandEvent &) {
    canvas_->SetInteractiveSlice(!canvas_->InteractiveSlice());
}
void DocumentFrame::OnAnalyzeUnsupported(wxCommandEvent &) {
    struct ModelSnapshot {
        std::shared_ptr<const stl_slicer::SceneModel> model;
        stl_slicer::Mat4 transform;
        std::size_t triangleCount = 0;
    };
    std::vector<ModelSnapshot> snapshots;
    for (const auto &model : models_)
        if (model->selected)
            snapshots.push_back({model, model->transform, model->renderVertices().size() / 3});
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
    unsupportedAnalysisRunning_ = true;
    UpdateCommandState();
    const std::uint64_t revision = modelRevision_;
    const double healingThreshold = ContourHealingThreshold();
    const double segmentationTolerance = SegmentationTolerance();
    const double criticalAngleDegrees = CriticalAngleDegrees();
    const double overhangCoefficient = OverhangCoefficient();
    unsupportedWorker_ = std::thread([this,
                                      snapshots = std::move(snapshots),
                                      revision,
                                      healingThreshold,
                                      segmentationTolerance,
                                      criticalAngleDegrees,
                                      overhangCoefficient]() mutable {
        auto payload = std::make_shared<UnsupportedAnalysisPayload>();
        payload->modelRevision = revision;
        try {
            stl_slicer::TriangleMesh combined;
            std::size_t triangleCount = 0;
            for (const auto &snapshot : snapshots)
                triangleCount += snapshot.triangleCount;
            combined.reserve(triangleCount);
            for (const auto &snapshot : snapshots) {
                const stl_slicer::TriangleMesh mesh = snapshot.model->triangleMesh();
                for (auto triangle : mesh.triangles()) {
                    for (auto &vertex : triangle.vertices)
                        vertex = snapshot.transform.transformPoint(vertex);
                    triangle.normal = snapshot.transform.transformVector(triangle.normal);
                    combined.addTriangle(std::move(triangle));
                }
            }
            const stl_slicer::SliceData slices =
                stl_slicer::Slicer{{0.1, segmentationTolerance, healingThreshold, 0.05}}.slice(
                    combined);
            stl_slicer::UnsupportedAreaResult unsupported =
                stl_slicer::UnsupportedAreaAnalyzer{{criticalAngleDegrees, overhangCoefficient}}
                    .analyze(slices);
            payload->totalArea = unsupported.totalArea;
            payload->visualization = BuildSliceSurfaces(unsupported.unsupported);
        } catch (const std::exception &error) {
            payload->error = error.what();
        }
        auto *event = new wxThreadEvent(wxEVT_THREAD, IdAnalyzeUnsupported);
        event->SetPayload(payload);
        wxQueueEvent(this, event);
    });
}
void DocumentFrame::OnUnsupportedAnalysisFinished(wxThreadEvent &event) {
    if (unsupportedWorker_.joinable())
        unsupportedWorker_.join();
    unsupportedAnalysisRunning_ = false;
    UpdateCommandState();

    const auto payload = event.GetPayload<std::shared_ptr<UnsupportedAnalysisPayload>>();
    if (!payload->error.empty()) {
        wxMessageBox(payload->error, "Unsupported-area analysis failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->modelRevision != modelRevision_)
        return;
    canvas_->SetUnsupportedVisualization(std::move(payload->visualization));
}
