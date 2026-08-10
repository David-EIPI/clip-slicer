// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "document_frame.hpp"
#include "help_topics.hpp"
#include "embedded_assets.hpp"
#include "gl_canvas.hpp"
#include "main_frame.hpp"
#include "model_transform_dialog.hpp"
#include "model_multiply_dialog.hpp"
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
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <wx/artprov.h>
#include <wx/checkbox.h>
#include <wx/dataobj.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/thread.h>
#include <wx/toolbar.h>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
enum {
    IdOpen = wxID_HIGHEST + 20,
    IdOpenIntoDocument,
    IdExport,
    IdExportStl,
    IdSlice,
    IdInteractive,
    IdDetectUnsupported,
    IdUnsupportedAnalysisProgress,
    IdGenerateSupports,
    IdSupportGenerationProgress,
    IdOptimizeOrientation,
    IdStopOptimization,
    IdShow,
    IdHide,
    IdDeleteModels,
    IdResetTransform,
    IdTransformModels,
    IdMultiplyModels,
    IdNewModelGroup,
    IdUngroupModels,
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

struct UnsupportedAnalysisProgressPayload {
    std::size_t completed = 0;
    std::size_t total = 0;
    std::uint64_t modelRevision = 0;
};

struct SupportGenerationPayload {
    stl_slicer::TriangleMesh supports;
    std::size_t contactPointCount = 0;
    std::size_t processedLayerCount = 0;
    std::size_t internalSupportFailureCount = 0;
    double lowestInternalSupportFailureZ = 0.0;
    std::uint64_t modelRevision = 0;
    bool cancelled = false;
    std::string error;
};

struct SupportGenerationProgressPayload {
    std::size_t stage = 0;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::uint64_t modelRevision = 0;
    bool finished = false;
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
        const stl_slicer::TriangleMesh &mesh = snapshot.model->triangleMesh();
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
    if (!icon.IsOk())
        return wxArtProvider::GetBitmap(fallback, wxART_TOOLBAR, size);
    if (icon.GetSize() == size)
        return icon;
    wxImage image = icon.ConvertToImage();
    image.Rescale(size.GetWidth(), size.GetHeight(), wxIMAGE_QUALITY_HIGH);
    return wxBitmap(image);
}

wxBitmapBundle ModelGroupIcon() {
    static constexpr char svg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><path fill="#d9a62e" stroke="#795a12" stroke-width="1" d="M1.5 3.5h5l1.5 2h6.5v7.5H1.5z"/><path fill="#f3c84b" d="M2.5 6.5h11v5.5h-11z"/></svg>)svg";
    return wxBitmapBundle::FromSVG(svg, {16, 16});
}
wxBitmapBundle MeshModelIcon() {
    static constexpr char svg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><path fill="#4b9fca" fill-opacity=".35" stroke="#236887" stroke-width="1.2" stroke-linejoin="round" d="M8 1.5 14 12.5 2 12.5z"/><path fill="none" stroke="#236887" stroke-width="1" d="M8 1.5v11M2 12.5 11 7"/></svg>)svg";
    return wxBitmapBundle::FromSVG(svg, {16, 16});
}
wxBitmapBundle SlicedModelIcon() {
    static constexpr char svg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><g fill="#58ad77" stroke="#27613d" stroke-width="1"><path d="m2 4 6-2 6 2-6 2z"/><path d="m2 8 6-2 6 2-6 2z"/><path d="m2 12 6-2 6 2-6 2z"/></g></svg>)svg";
    return wxBitmapBundle::FromSVG(svg, {16, 16});
}

#ifdef __WXGTK__
void CloseDocumentTab(GtkButton *, gpointer data) {
    static_cast<DocumentFrame *>(data)->Close();
}

void AddDocumentTabCloseButton(DocumentFrame &document, const wxString &title) {
    wxMDIParentFrame *parent = document.GetMDIParent();
    wxMDIClientWindowBase *client = parent ? parent->GetClientWindow() : nullptr;
    GtkWidget *notebookWidget = client ? client->GetHandle() : nullptr;
    GtkWidget *documentWidget = document.GetHandle();
    if (!GTK_IS_NOTEBOOK(notebookWidget) || !documentWidget)
        return;

    GtkWidget *tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new(title.utf8_str());
    GtkWidget *close = gtk_button_new();
    GtkWidget *icon = gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(close), icon);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close, FALSE);
    gtk_widget_set_name(close, "tab-close-button");
    gtk_widget_set_tooltip_text(close, "Close document");
    gtk_box_pack_start(GTK_BOX(tab), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tab), close, FALSE, FALSE, 0);
    gtk_widget_show_all(tab);
    gtk_notebook_set_tab_label(
        GTK_NOTEBOOK(notebookWidget), documentWidget, tab);
    g_signal_connect(close, "clicked", G_CALLBACK(CloseDocumentTab), &document);
}
#endif
} // namespace

class SliceDialog final : public wxDialog {
  public:
    SliceDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, "Slice models") {
        clip_slicer::help::Assign(this, clip_slicer::help::sliceDialog);
        auto *root = new wxBoxSizer(wxVERTICAL);
        target = new wxRadioBox(this,
                                wxID_ANY,
                                "Output",
                                wxDefaultPosition,
                                wxDefaultSize,
                                {"Same document", "New document"});
        clip_slicer::help::Assign(target, clip_slicer::help::sliceOutput);
        root->Add(target, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        root->Add(
            CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxHELP), 0, wxEXPAND | wxALL, 12);
        SetSizerAndFit(root);
        clip_slicer::help::Enable(this);
    }
    wxRadioBox *target;
};

class SectionDialog final : public wxDialog {
  public:
    explicit SectionDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, "Cross-section") {
        clip_slicer::help::Assign(this, clip_slicer::help::sectionDialog);
        auto *root = new wxBoxSizer(wxVERTICAL);
        axis = new wxRadioBox(this,
                              wxID_ANY,
                              "Section plane normal",
                              wxDefaultPosition,
                              wxDefaultSize,
                              {"X axis", "Y axis", "Z axis"},
                              1,
                              wxRA_SPECIFY_ROWS);
        clip_slicer::help::Assign(axis, clip_slicer::help::sectionAxis);
        axis->SetSelection(2);
        root->Add(axis, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        clipping = new wxRadioBox(this,
                                  wxID_ANY,
                                  "Clipping",
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  {"No", "Above", "Below"},
                                  1,
                                  wxRA_SPECIFY_ROWS);
        clip_slicer::help::Assign(clipping, clip_slicer::help::sectionClipping);
        clipping->SetSelection(0);
        root->Add(clipping, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        autoRotate = new wxCheckBox(this, wxID_ANY, "Auto rotate for best view");
        clip_slicer::help::Assign(autoRotate, clip_slicer::help::sectionAutoRotate);
        autoRotate->SetValue(true);
        root->Add(autoRotate, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        root->Add(
            CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxHELP), 0, wxEXPAND | wxALL, 12);
        SetSizerAndFit(root);
        clip_slicer::help::Enable(this);
    }

    SectionAxis SelectedAxis() const {
        if (axis->GetSelection() == 0)
            return SectionAxis::X;
        if (axis->GetSelection() == 1)
            return SectionAxis::Y;
        return SectionAxis::Z;
    }

    SectionClipping SelectedClipping() const {
        if (clipping->GetSelection() == 1)
            return SectionClipping::Above;
        if (clipping->GetSelection() == 2)
            return SectionClipping::Below;
        return SectionClipping::None;
    }

    wxRadioBox *axis = nullptr;
    wxRadioBox *clipping = nullptr;
    wxCheckBox *autoRotate = nullptr;
};

DocumentFrame::DocumentFrame(wxMDIParentFrame *parent, const wxString &title)
    : wxMDIChildFrame(parent, wxID_ANY, title, wxDefaultPosition, {1000, 700}) {
    clip_slicer::help::Assign(this, clip_slicer::help::documentWindow);
    clip_slicer::help::Enable(this);
    BuildMenus();
    auto *root = new wxBoxSizer(wxVERTICAL);
    toolbar_ = new wxToolBar(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_TEXT);
    const wxSize toolSize = FromDIP(wxSize(24, 24));
    toolbar_->SetToolBitmapSize(toolSize);
    toolbar_->AddTool(
        IdHide, "Hide", wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_TOOLBAR, toolSize));
    toolbar_->AddTool(
        IdShow, "Show", wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, toolSize));
    toolbar_->AddTool(IdDeleteModels,
                      "Delete",
                      wxArtProvider::GetBitmap(wxART_DELETE, wxART_TOOLBAR, toolSize),
                      "Delete selected models");
    toolbar_->AddSeparator();
    toolbar_->AddTool(IdInteractive,
                      "Section",
                      LoadEmbeddedIcon(clip_slicer::assets::planeSliceIconPng,
                                       clip_slicer::assets::planeSliceIconPngSize,
                                       wxART_LIST_VIEW,
                                       toolSize),
                      "Interactive cross-section",
                      wxITEM_CHECK);
    toolbar_->AddTool(IdGenerateSupports,
                      "Supports",
                      LoadEmbeddedIcon(clip_slicer::assets::supportGenerateIconPng,
                                       clip_slicer::assets::supportGenerateIconPngSize,
                                       wxART_PLUS,
                                       toolSize),
                      "Generate support structures");
    toolbar_->AddTool(IdStopOptimization,
                      "Stop",
                      wxArtProvider::GetBitmap(wxART_STOP, wxART_TOOLBAR, toolSize),
                      "Stop background operation");
    toolbar_->AddSeparator();
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
    clip_slicer::help::Assign(toolbar_, clip_slicer::help::documentToolbar);
    root->Add(toolbar_, 0, wxEXPAND);
    auto *split = new wxSplitterWindow(this);
    modelList_ = new wxDataViewCtrl(split,
                                    wxID_ANY,
                                    wxDefaultPosition,
                                    wxDefaultSize,
                                    wxDV_MULTIPLE | wxDV_ROW_LINES | wxDV_VERT_RULES);
    modelListModel_ =
        new ModelTreeModel(ModelGroupIcon(), MeshModelIcon(), SlicedModelIcon());
    modelList_->AssociateModel(modelListModel_);
    modelListModel_->DecRef();
    modelList_->AppendToggleColumn("Visible",
                                   ModelTreeModel::Visibility,
                                   wxDATAVIEW_CELL_ACTIVATABLE,
                                   FromDIP(58),
                                   wxALIGN_CENTER);
    modelList_->AppendIconTextColumn("Model",
                                     ModelTreeModel::Name,
                                     wxDATAVIEW_CELL_INERT,
                                     FromDIP(180),
                                     wxALIGN_LEFT);
    modelList_->AppendTextColumn("Type",
                                 ModelTreeModel::Type,
                                 wxDATAVIEW_CELL_INERT,
                                 FromDIP(72),
                                 wxALIGN_LEFT);
    modelList_->EnableDragSource(wxDF_UNICODETEXT);
    modelList_->EnableDropTarget(wxDF_UNICODETEXT);
    clip_slicer::help::Assign(modelList_, clip_slicer::help::modelList);
    auto *viewArea = new wxPanel(split);
    auto *viewSizer = new wxBoxSizer(wxVERTICAL);
    canvas_ = new ModelCanvas(viewArea, *this);
    clip_slicer::help::Assign(canvas_, clip_slicer::help::viewport);
    sectionControls_ = new wxPanel(viewArea);
    auto *sectionSizer = new wxBoxSizer(wxHORIZONTAL);
    sectionScroll_ = new wxScrollBar(
        sectionControls_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_HORIZONTAL);
    sectionScroll_->SetToolTip(
        "Section slice index (Up/Down: 1 slice, Page Up/Page Down: 10 slices)");
    sectionIndex_ = new wxSpinCtrl(sectionControls_,
                                   wxID_ANY,
                                   {},
                                   wxDefaultPosition,
                                   FromDIP(wxSize(96, -1)),
                                   wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
                                   0,
                                   0,
                                   0);
    wxSize sectionIndexSize = sectionIndex_->GetBestSize();
    sectionIndexSize.x = std::max(sectionIndexSize.x, FromDIP(96));
    sectionIndex_->SetMinSize(sectionIndexSize);
    sectionIndex_->SetToolTip("Section slice index");
    sectionSizer->Add(sectionScroll_, 1, wxALIGN_CENTER_VERTICAL);
    sectionSizer->Add(sectionIndex_,
                      0,
                      wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
                      FromDIP(6));
    sectionControls_->SetSizer(sectionSizer);
    clip_slicer::help::Assign(sectionControls_, clip_slicer::help::sectionControls);
    viewSizer->Add(sectionControls_, 0, wxEXPAND | wxALL, FromDIP(4));
    viewSizer->Add(canvas_, 1, wxEXPAND);
    viewArea->SetSizer(viewSizer);
    sectionControls_->Hide();
    split->SplitVertically(modelList_, viewArea, FromDIP(320));
    split->SetMinimumPaneSize(120);
    root->Add(split, 1, wxEXPAND);
    SetSizer(root);
    Bind(wxEVT_ACTIVATE, &DocumentFrame::OnActivate, this);
    Bind(wxEVT_CLOSE_WINDOW, &DocumentFrame::OnClose, this);
    Bind(wxEVT_DPI_CHANGED, &DocumentFrame::OnDPIChanged, this);
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
    Bind(wxEVT_THREAD,
         &DocumentFrame::OnUnsupportedAnalysisProgress,
         this,
         IdUnsupportedAnalysisProgress);
    Bind(wxEVT_MENU, &DocumentFrame::OnGenerateSupports, this, IdGenerateSupports);
    Bind(wxEVT_THREAD, &DocumentFrame::OnSupportGenerationFinished, this, IdGenerateSupports);
    Bind(wxEVT_THREAD,
         &DocumentFrame::OnSupportGenerationProgress,
         this,
         IdSupportGenerationProgress);
    Bind(wxEVT_MENU, &DocumentFrame::OnOptimizeOrientation, this, IdOptimizeOrientation);
    Bind(wxEVT_MENU, &DocumentFrame::OnStopOptimization, this, IdStopOptimization);
    Bind(wxEVT_THREAD, &DocumentFrame::OnOrientationOptimizationEvent, this, IdOptimizeOrientation);
    Bind(wxEVT_MENU, &DocumentFrame::OnShow, this, IdShow);
    Bind(wxEVT_MENU, &DocumentFrame::OnHide, this, IdHide);
    Bind(wxEVT_MENU, &DocumentFrame::OnDeleteModels, this, IdDeleteModels);
    Bind(wxEVT_MENU, &DocumentFrame::OnResetTransform, this, IdResetTransform);
    Bind(wxEVT_MENU, &DocumentFrame::OnTransformModels, this, IdTransformModels);
    Bind(wxEVT_MENU, &DocumentFrame::OnMultiplyModels, this, IdMultiplyModels);
    Bind(wxEVT_MENU, &DocumentFrame::OnNewModelGroup, this, IdNewModelGroup);
    Bind(wxEVT_MENU, &DocumentFrame::OnUngroupModels, this, IdUngroupModels);
    Bind(wxEVT_MENU, &DocumentFrame::OnMoveToOrigin, this, IdMoveToOrigin);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
            static_cast<MainFrame *>(GetMDIParent())->ShowSettingsDialog();
        },
        IdSettings);
    modelList_->Bind(
        wxEVT_DATAVIEW_SELECTION_CHANGED, &DocumentFrame::OnModelSelectionChanged, this);
    modelList_->Bind(
        wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &DocumentFrame::OnModelVisibilityChanged, this);
    modelList_->Bind(wxEVT_DATAVIEW_ITEM_EXPANDED, &DocumentFrame::OnModelGroupExpanded, this);
    modelList_->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSED, &DocumentFrame::OnModelGroupCollapsed, this);
    modelList_->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &DocumentFrame::OnModelDragBegin, this);
    modelList_->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE,
                     &DocumentFrame::OnModelDropPossible,
                     this);
    modelList_->Bind(wxEVT_DATAVIEW_ITEM_DROP, &DocumentFrame::OnModelDrop, this);
    sectionIndex_->Bind(wxEVT_SPINCTRL, &DocumentFrame::OnSectionIndexChanged, this);
    sectionIndex_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) {
        canvas_->SetSliceIndex(
            static_cast<std::size_t>(std::max(0, sectionIndex_->GetValue())));
    });
    sectionScroll_->Bind(wxEVT_SCROLL_TOP, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_BOTTOM, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_LINEUP, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_LINEDOWN, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_PAGEUP, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_PAGEDOWN, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_THUMBTRACK, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_THUMBRELEASE, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_SCROLL_CHANGED, &DocumentFrame::OnSectionScroll, this);
    sectionScroll_->Bind(wxEVT_CHAR_HOOK, &DocumentFrame::OnSectionScrollKey, this);
#ifdef __WXGTK__
    // wxGTK inserts MDI children into a GtkNotebook while they are being
    // constructed. Replacing its tab widget before the notebook has received
    // its first allocation can make GTK draw the header with negative
    // dimensions. Wait until the initial layout has completed.
    CallAfter([this, title] { AddDocumentTabCloseButton(*this, title); });
#endif
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
    deleteModelsItem_ = models->Append(IdDeleteModels, "&Delete selected");
    models->AppendSeparator();
    resetTransformItem_ = models->Append(IdResetTransform, "&Reset transformations");
    transformModelsItem_ = models->Append(IdTransformModels, "&Transform...");
    multiplyModelsItem_ = models->Append(IdMultiplyModels, "&Multiply...");
    moveToOriginItem_ = models->Append(IdMoveToOrigin, "Move to &Origin");
    models->AppendSeparator();
    newModelGroupItem_ = models->Append(IdNewModelGroup, "New &Group...");
    ungroupModelsItem_ = models->Append(IdUngroupModels, "&Ungroup selected");
    auto *slice = new wxMenu;
    slice->Append(IdSlice, "&Slice selected...");
    sectionItem_ = slice->AppendCheckItem(IdInteractive, "&Section...");
    detectUnsupportedItem_ = slice->Append(IdDetectUnsupported, "&Detect unsupported areas");
    generateSupportsItem_ = slice->Append(IdGenerateSupports, "&Generate supports");
    optimizationItem_ = slice->Append(IdOptimizeOrientation, "&Optimize orientation");
    auto *bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(models, "&Models");
    bar->Append(slice, "&Slice");
    auto *help = new wxMenu;
    help->Append(wxID_HELP, "&Topics");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
    Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { Close(); }, wxID_CLOSE);
    Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { GetMDIParent()->Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU,
         [this](wxCommandEvent &) {
             static_cast<MainFrame *>(GetMDIParent())
                 ->ShowHelpTopic(clip_slicer::help::manualTop);
         },
         wxID_HELP);
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
void DocumentFrame::InteractiveSlicePositionChanged() {
    UpdateSectionControls();
}
void DocumentFrame::UpdateSectionControls() {
    if (!sectionControls_ || !canvas_)
        return;
    const bool visible = canvas_->InteractiveSlice();
    if (sectionControls_->IsShown() != visible) {
        sectionControls_->Show(visible);
        sectionControls_->GetParent()->Layout();
    }
    if (!visible)
        return;

    constexpr std::size_t maximumControlIndex =
        static_cast<std::size_t>(std::numeric_limits<int>::max() - 1);
    const int maximum = static_cast<int>(
        std::min(canvas_->MaximumSliceIndex(), maximumControlIndex));
    const int index =
        static_cast<int>(std::min(canvas_->SliceIndex(), maximumControlIndex));
    if (sectionIndex_->GetMin() != 0 || sectionIndex_->GetMax() != maximum)
        sectionIndex_->SetRange(0, maximum);
    if (sectionIndex_->GetValue() != index)
        sectionIndex_->SetValue(index);
    if (sectionScroll_->GetRange() != maximum + 1 || sectionScroll_->GetThumbSize() != 1 ||
        sectionScroll_->GetPageSize() != 10)
        sectionScroll_->SetScrollbar(index, 1, maximum + 1, 10, true);
    else if (sectionScroll_->GetThumbPosition() != index)
        sectionScroll_->SetThumbPosition(index);
}
void DocumentFrame::OnSectionIndexChanged(wxSpinEvent &event) {
    canvas_->SetSliceIndex(static_cast<std::size_t>(std::max(0, event.GetValue())));
}
void DocumentFrame::OnSectionScroll(wxScrollEvent &event) {
    canvas_->SetSliceIndex(static_cast<std::size_t>(std::max(0, event.GetPosition())));
    if (event.GetEventType() == wxEVT_SCROLL_THUMBTRACK)
        canvas_->Update();
}
void DocumentFrame::OnSectionScrollKey(wxKeyEvent &event) {
    int change = 0;
    switch (event.GetKeyCode()) {
    case WXK_UP:
        change = -1;
        break;
    case WXK_DOWN:
        change = 1;
        break;
    case WXK_PAGEUP:
        change = -10;
        break;
    case WXK_PAGEDOWN:
        change = 10;
        break;
    default:
        event.Skip();
        return;
    }
    const long long index = std::clamp(static_cast<long long>(canvas_->SliceIndex()) + change,
                                       0LL,
                                       static_cast<long long>(canvas_->MaximumSliceIndex()));
    canvas_->SetSliceIndex(static_cast<std::size_t>(index));
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
double DocumentFrame::CrossSectionDisplayDistance() const {
    return static_cast<MainFrame *>(GetMDIParent())->Settings().crossSectionDisplayDistance;
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
    clip_slicer::help::Assign(&d, clip_slicer::help::openIntoDocumentDialog);
    clip_slicer::help::Enable(&d);
    if (d.ShowModal() == wxID_OK)
        OpenPath(d.GetPath());
}
void DocumentFrame::RefreshModelList() {
    PruneModelGroups();
    updatingModelList_ = true;
    modelListModel_->Rebuild(models_, modelGroups_);
    wxDataViewItemArray selections;
    for (const auto &model : models_) {
        if (model->selected) {
            const wxDataViewItem item = modelListModel_->ItemForModel(model.get());
            if (item.IsOk())
                selections.push_back(item);
        }
    }
    modelList_->SetSelections(selections);
    for (const DocumentModelGroup &group : modelGroups_) {
        if (!group.expanded)
            continue;
        const wxDataViewItem item = modelListModel_->ItemForGroup(group.id);
        if (item.IsOk())
            modelList_->Expand(item);
    }
    updatingModelList_ = false;
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
    if (resetTransformItem_)
        resetTransformItem_->Enable(modelSelected);
    if (transformModelsItem_)
        transformModelsItem_->Enable(modelSelected);
    if (multiplyModelsItem_)
        multiplyModelsItem_->Enable(modelSelected);
    if (newModelGroupItem_)
        newModelGroupItem_->Enable(modelSelected);
    if (ungroupModelsItem_) {
        const bool groupedModelSelected =
            std::any_of(models_.begin(), models_.end(), [this](const auto &model) {
                return model->selected && ModelGroupId(model.get()) != 0;
            });
        ungroupModelsItem_->Enable(groupedModelSelected);
    }
    if (moveToOriginItem_)
        moveToOriginItem_->Enable(modelSelected);
    if (deleteModelsItem_)
        deleteModelsItem_->Enable(modelSelected);
    if (toolbar_) {
        toolbar_->EnableTool(IdTransformModels, modelSelected);
        toolbar_->EnableTool(IdMoveToOrigin, modelSelected);
        toolbar_->EnableTool(IdDeleteModels, modelSelected);
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
        toolbar_->EnableTool(IdGenerateSupports, canAnalyze);
    if (optimizationItem_)
        optimizationItem_->Enable(canAnalyze);
    if (sectionItem_) {
        sectionItem_->Enable(modelSelected);
        sectionItem_->Check(canvas_ && canvas_->InteractiveSlice());
    }
    if (toolbar_) {
        toolbar_->EnableTool(IdInteractive, modelSelected);
        toolbar_->ToggleTool(IdInteractive, canvas_ && canvas_->InteractiveSlice());
    }
    if (toolbar_)
        toolbar_->EnableTool(IdStopOptimization,
                             optimizationRunning_ || unsupportedAnalysisRunning_ ||
                                 supportGenerationRunning_);
}
void DocumentFrame::OnModelSelectionChanged(wxDataViewEvent &) {
    if (updatingModelList_)
        return;
    for (auto &model : models_)
        model->selected = false;
    wxDataViewItemArray selections;
    modelList_->GetSelections(selections);
    for (const wxDataViewItem &item : selections) {
        if (stl_slicer::SceneModel *model = modelListModel_->Model(item)) {
            model->selected = true;
            continue;
        }
        const std::uint64_t groupId = modelListModel_->GroupId(item);
        if (!groupId)
            continue;
        for (auto &model : models_)
            if (ModelGroupId(model.get()) == groupId)
                model->selected = true;
    }
    UpdateCommandState();
    canvas_->SelectionChanged();
}
void DocumentFrame::OnModelVisibilityChanged(wxDataViewEvent &) {
    canvas_->Refresh();
    UpdateStatus();
}
void DocumentFrame::OnModelGroupExpanded(wxDataViewEvent &event) {
    const std::uint64_t groupId = modelListModel_->GroupId(event.GetItem());
    for (DocumentModelGroup &group : modelGroups_)
        if (group.id == groupId)
            group.expanded = true;
}
void DocumentFrame::OnModelGroupCollapsed(wxDataViewEvent &event) {
    const std::uint64_t groupId = modelListModel_->GroupId(event.GetItem());
    for (DocumentModelGroup &group : modelGroups_)
        if (group.id == groupId)
            group.expanded = false;
}
void DocumentFrame::PruneModelGroups() {
    std::unordered_set<const stl_slicer::SceneModel *> live;
    live.reserve(models_.size());
    for (const auto &model : models_)
        live.insert(model.get());
    std::unordered_set<const stl_slicer::SceneModel *> assigned;
    for (DocumentModelGroup &group : modelGroups_) {
        const auto end = std::remove_if(group.members.begin(),
                                        group.members.end(),
                                        [&live, &assigned](const auto *model) {
                                            return !live.count(model) || !assigned.insert(model).second;
                                        });
        group.members.erase(end, group.members.end());
    }
    modelGroups_.erase(
        std::remove_if(modelGroups_.begin(),
                       modelGroups_.end(),
                       [](const DocumentModelGroup &group) { return group.members.empty(); }),
        modelGroups_.end());
}
void DocumentFrame::RemoveModelsFromGroups(
    const std::vector<stl_slicer::SceneModel *> &models) {
    const std::unordered_set<const stl_slicer::SceneModel *> removed(models.begin(), models.end());
    for (DocumentModelGroup &group : modelGroups_) {
        const auto end = std::remove_if(group.members.begin(),
                                        group.members.end(),
                                        [&removed](const auto *model) {
                                            return removed.count(model) != 0;
                                        });
        group.members.erase(end, group.members.end());
    }
}
void DocumentFrame::CreateModelGroup(
    std::string name,
    const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &members) {
    std::vector<stl_slicer::SceneModel *> rawMembers;
    rawMembers.reserve(members.size());
    for (const auto &model : members)
        rawMembers.push_back(model.get());
    RemoveModelsFromGroups(rawMembers);
    DocumentModelGroup group;
    group.id = nextModelGroupId_++;
    group.name = std::move(name);
    group.members.assign(rawMembers.begin(), rawMembers.end());
    modelGroups_.push_back(std::move(group));
    PruneModelGroups();
}
std::uint64_t DocumentFrame::ModelGroupId(const stl_slicer::SceneModel *model) const {
    for (const DocumentModelGroup &group : modelGroups_)
        if (std::find(group.members.begin(), group.members.end(), model) != group.members.end())
            return group.id;
    return 0;
}
void DocumentFrame::OnNewModelGroup(wxCommandEvent &) {
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> selected;
    for (const auto &model : models_)
        if (model->selected)
            selected.push_back(model);
    if (selected.empty())
        return;

    const wxString defaultName = wxString::Format("Group %llu",
                                                   static_cast<unsigned long long>(
                                                       nextModelGroupId_));
    wxTextEntryDialog dialog(this, "Group name:", "New model group", defaultName);
    clip_slicer::help::Assign(&dialog, clip_slicer::help::modelList);
    clip_slicer::help::Enable(&dialog);
    if (dialog.ShowModal() != wxID_OK)
        return;
    wxString name = dialog.GetValue();
    name.Trim(true).Trim(false);
    if (name.empty())
        name = defaultName;
    CreateModelGroup(name.ToStdString(), selected);
    RefreshModelList();
}
void DocumentFrame::OnUngroupModels(wxCommandEvent &) {
    std::vector<stl_slicer::SceneModel *> selected;
    for (const auto &model : models_)
        if (model->selected)
            selected.push_back(model.get());
    if (selected.empty())
        return;
    RemoveModelsFromGroups(selected);
    PruneModelGroups();
    RefreshModelList();
}
void DocumentFrame::OnModelDragBegin(wxDataViewEvent &event) {
    if (!std::any_of(models_.begin(), models_.end(), [](const auto &model) {
            return model->selected;
        })) {
        event.Veto();
        return;
    }
    event.SetDataObject(new wxTextDataObject("clip-slicer-models"));
    event.SetDragFlags(wxDrag_AllowMove);
}
void DocumentFrame::OnModelDropPossible(wxDataViewEvent &event) {
    if (event.GetDataFormat() != wxDF_UNICODETEXT)
        event.Veto();
    else
        event.SetDropEffect(wxDragMove);
}
void DocumentFrame::OnModelDrop(wxDataViewEvent &event) {
    if (event.GetDataFormat() != wxDF_UNICODETEXT) {
        event.Veto();
        return;
    }
    wxTextDataObject data;
    data.SetData(wxDF_UNICODETEXT, event.GetDataSize(), event.GetDataBuffer());
    if (data.GetText() != "clip-slicer-models") {
        event.Veto();
        return;
    }

    std::vector<std::shared_ptr<stl_slicer::SceneModel>> selected;
    std::vector<stl_slicer::SceneModel *> selectedRaw;
    for (const auto &model : models_) {
        if (!model->selected)
            continue;
        selected.push_back(model);
        selectedRaw.push_back(model.get());
    }
    if (selected.empty()) {
        event.Veto();
        return;
    }

    stl_slicer::SceneModel *targetModel = modelListModel_->Model(event.GetItem());
    const std::uint64_t targetGroupId = modelListModel_->GroupId(event.GetItem());
    const bool targetMoves = targetModel &&
                             std::find(selectedRaw.begin(), selectedRaw.end(), targetModel) !=
                                 selectedRaw.end();
    RemoveModelsFromGroups(selectedRaw);
    if (targetGroupId) {
        const auto group = std::find_if(modelGroups_.begin(),
                                        modelGroups_.end(),
                                        [targetGroupId](const DocumentModelGroup &candidate) {
                                            return candidate.id == targetGroupId;
                                        });
        if (group != modelGroups_.end())
            group->members.insert(group->members.end(), selectedRaw.begin(), selectedRaw.end());
    }

    if (targetModel && !targetMoves) {
        const std::unordered_set<stl_slicer::SceneModel *> moving(selectedRaw.begin(),
                                                                  selectedRaw.end());
        models_.erase(std::remove_if(models_.begin(),
                                     models_.end(),
                                     [&moving](const auto &model) {
                                         return moving.count(model.get()) != 0;
                                     }),
                      models_.end());
        const auto target = std::find_if(models_.begin(),
                                         models_.end(),
                                         [targetModel](const auto &model) {
                                             return model.get() == targetModel;
                                         });
        models_.insert(target, selected.begin(), selected.end());
    }
    PruneModelGroups();
    RefreshModelList();
    canvas_->SelectionChanged();
    event.SetDropEffect(wxDragMove);
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
void DocumentFrame::OnDeleteModels(wxCommandEvent &) {
    const bool hasSelectedModel = std::any_of(
        models_.begin(), models_.end(), [](const auto &model) { return model->selected; });
    if (!hasSelectedModel)
        return;

    InvalidateUnsupportedAnalysis();
    const auto newEnd = std::remove_if(
        models_.begin(), models_.end(), [](const auto &model) { return model->selected; });
    models_.erase(newEnd, models_.end());
    RefreshModelList();
    canvas_->ModelsChanged();
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
void DocumentFrame::OnMultiplyModels(wxCommandEvent &) {
    const stl_slicer::Bounds3 bounds = SelectedBounds();
    if (!bounds.valid())
        return;

    MultiplyDialog dialog(
        this,
        {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z});
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::vector<std::shared_ptr<stl_slicer::SceneModel>> selected;
    for (const auto &model : models_)
        if (model->selected)
            selected.push_back(model);
    if (selected.empty())
        return;

    const auto copies = dialog.Copies();
    const stl_slicer::Vec3 stride = dialog.Stride();
    constexpr std::size_t maximumDataViewItems =
        std::numeric_limits<unsigned int>::max();
    std::size_t placements = 1;
    for (const unsigned int count : copies) {
        if (placements > std::numeric_limits<std::size_t>::max() / count) {
            wxMessageBox("The requested multiplication is too large for this platform.",
                         "Multiply failed",
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }
        placements *= count;
    }
    if (placements == 1)
        return;
    if (selected.size() > maximumDataViewItems / placements) {
        wxMessageBox("The requested copies exceed the wxDataViewCtrl item-count range.",
                     "Multiply failed",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    const std::size_t additional = selected.size() * (placements - 1);

    InvalidateUnsupportedAnalysis();
    models_.reserve(models_.size() + additional);
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> multiplied = selected;
    multiplied.reserve(selected.size() * placements);
    for (unsigned int z = 0; z < copies[2]; ++z) {
        for (unsigned int y = 0; y < copies[1]; ++y) {
            for (unsigned int x = 0; x < copies[0]; ++x) {
                if (x == 0 && y == 0 && z == 0)
                    continue;
                const stl_slicer::Mat4 translation =
                    stl_slicer::Mat4::translation(x * stride.x, y * stride.y, z * stride.z);
                for (const auto &source : selected) {
                    std::string copyName = source->name + " [" + std::to_string(x + 1) + "," +
                                           std::to_string(y + 1) + "," + std::to_string(z + 1) +
                                           "]";
                    auto copy = source->replica(std::move(copyName));
                    copy->transform = translation * source->transform;
                    copy->visible = source->visible;
                    copy->selected = true;
                    models_.push_back(copy);
                    multiplied.push_back(std::move(copy));
                }
            }
        }
    }
    const std::string groupName = selected.size() == 1 ? selected.front()->name + " array"
                                                       : "Multiplied models";
    CreateModelGroup(groupName, multiplied);
    RefreshModelList();
    canvas_->ModelsChanged();
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
stl_slicer::Bounds3 DocumentFrame::ModelBounds() const {
    stl_slicer::Bounds3 bounds;
    for (const auto &model : models_) {
        const auto modelBounds = model->worldBounds();
        if (modelBounds.valid()) {
            bounds.include(modelBounds.min);
            bounds.include(modelBounds.max);
        }
    }
    return bounds;
}
stl_slicer::Vec3 DocumentFrame::SelectedCenter() const {
    const stl_slicer::Bounds3 b = SelectedBounds();
    return b.valid() ? stl_slicer::Vec3{(b.min.x + b.max.x) / 2,
                                        (b.min.y + b.max.y) / 2,
                                        (b.min.z + b.max.z) / 2}
                     : stl_slicer::Vec3{};
}
stl_slicer::Bounds3 DocumentFrame::SelectedBounds() const {
    stl_slicer::Bounds3 b;
    for (const auto &m : models_)
        if (m->selected) {
            auto mb = m->worldBounds();
            if (mb.valid()) {
                b.include(mb.min);
                b.include(mb.max);
            }
        }
    return b;
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
    const char axisLetter = canvas_->SliceAxis() == SectionAxis::X
                                ? 'X'
                                : (canvas_->SliceAxis() == SectionAxis::Y ? 'Y' : 'Z');
    slicePosition << axisLetter << ": ";
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
    } else if (supportGenerationRunning_) {
        static constexpr const char *labels[] = {"Slicing...",
                                                  "Finding unsupported...",
                                                  "Contact points...",
                                                  "Volume segmentation...",
                                                  "Generating supports..."};
        for (std::size_t index = 0; index < supportProgress_.size(); ++index) {
            const SupportProgressState &stage = supportProgress_[index];
            if (!stage.started || stage.finished)
                continue;
            optimizationProgress << labels[index];
            if (stage.total > 0)
                optimizationProgress << " (" << stage.completed << " of " << stage.total << ')';
            break;
        }
    } else if (unsupportedAnalysisRunning_) {
        if (unsupportedProgressVisible_)
            optimizationProgress << "Detecting unsupported: slice "
                                 << unsupportedProgressCompleted_ << " of "
                                 << unsupportedProgressTotal_;
    } else {
        optimizationProgress << supportGenerationSummary_;
    }
    parent->SetDocumentStatus(buildVolume.str(), slicePosition.str(), optimizationProgress.str());
}
void DocumentFrame::OnActivate(wxActivateEvent &event) {
    if (event.GetActive())
        PublishStatus();
    event.Skip();
}
void DocumentFrame::OnDPIChanged(wxDPIChangedEvent &event) {
    event.Skip();
    CallAfter([this] { UpdateToolbarBitmaps(); });
}
void DocumentFrame::UpdateToolbarBitmaps() {
    if (!toolbar_)
        return;
    const wxSize toolSize = FromDIP(wxSize(24, 24));
    toolbar_->SetToolBitmapSize(toolSize);
    const auto setBitmap = [this](int id, const wxBitmap &bitmap) {
        if (auto *tool = toolbar_->FindById(id))
            tool->SetNormalBitmap(bitmap);
    };
    setBitmap(IdInteractive,
              LoadEmbeddedIcon(clip_slicer::assets::planeSliceIconPng,
                               clip_slicer::assets::planeSliceIconPngSize,
                               wxART_LIST_VIEW,
                               toolSize));
    setBitmap(IdHide, wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_TOOLBAR, toolSize));
    setBitmap(IdShow, wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, toolSize));
    setBitmap(IdDeleteModels, wxArtProvider::GetBitmap(wxART_DELETE, wxART_TOOLBAR, toolSize));
    setBitmap(IdGenerateSupports,
              LoadEmbeddedIcon(clip_slicer::assets::supportGenerateIconPng,
                               clip_slicer::assets::supportGenerateIconPngSize,
                               wxART_PLUS,
                               toolSize));
    setBitmap(IdStopOptimization,
              wxArtProvider::GetBitmap(wxART_STOP, wxART_TOOLBAR, toolSize));
    setBitmap(IdTransformModels,
              LoadEmbeddedIcon(clip_slicer::assets::transformModelsIconPng,
                               clip_slicer::assets::transformModelsIconPngSize,
                               wxART_REDO,
                               toolSize));
    setBitmap(IdMoveToOrigin,
              LoadEmbeddedIcon(clip_slicer::assets::moveToOriginIconPng,
                               clip_slicer::assets::moveToOriginIconPngSize,
                               wxART_GO_HOME,
                               toolSize));
    toolbar_->Realize();
    Layout();
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
    clip_slicer::help::Assign(&d, clip_slicer::help::exportSlicesDialog);
    clip_slicer::help::Enable(&d);
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
    clip_slicer::help::Assign(&dialog, clip_slicer::help::exportStlDialog);
    clip_slicer::help::Enable(&dialog);
    if (dialog.ShowModal() == wxID_OK) {
        try {
            stl_slicer::BinaryStlWriter{}.write(combined, dialog.GetPath().ToStdString());
        } catch (const std::exception &error) {
            wxMessageBox(error.what(), "Export failed", wxOK | wxICON_ERROR, this);
        }
    }
}
void DocumentFrame::OnInteractiveSlice(wxCommandEvent &) {
    if (canvas_->InteractiveSlice()) {
        canvas_->SetInteractiveSection(false, canvas_->SliceAxis());
    } else {
        SectionDialog dialog(this);
        if (dialog.ShowModal() == wxID_OK)
            canvas_->SetInteractiveSection(
                true,
                dialog.SelectedAxis(),
                dialog.autoRotate->GetValue(),
                dialog.SelectedClipping());
    }
    UpdateCommandState();
    UpdateStatus();
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
    unsupportedProgressVisible_ = false;
    unsupportedProgressCompleted_ = 0;
    unsupportedProgressTotal_ = 0;
    UpdateCommandState();
    UpdateStatus();
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
        const auto started = std::chrono::steady_clock::now();
        auto lastProgress = started;
        try {
            stl_slicer::TriangleMesh combined = combinedTransformedMesh(snapshots);
            const stl_slicer::SliceData slices = stl_slicer::Slicer{
                {layerThickness,
                 segmentationTolerance,
                 healingThreshold,
                 firstLayerOffset}}.slice(combined, &unsupportedAnalysisCancel_);
            if (!unsupportedAnalysisCancel_.load(std::memory_order_relaxed)) {
                stl_slicer::UnsupportedAreaResult unsupported = stl_slicer::UnsupportedAreaAnalyzer{
                    {criticalAngleDegrees,
                     overhangCoefficient}}
                    .analyze(
                        slices,
                        &unsupportedAnalysisCancel_,
                        [this, revision, started, &lastProgress](std::size_t completed,
                                                                std::size_t total) {
                            const auto now = std::chrono::steady_clock::now();
                            if (now - started < std::chrono::seconds(1) ||
                                now - lastProgress < std::chrono::seconds(1))
                                return;
                            lastProgress = now;
                            if (closing_.load(std::memory_order_relaxed))
                                return;
                            auto progress =
                                std::make_shared<UnsupportedAnalysisProgressPayload>();
                            progress->completed = completed;
                            progress->total = total;
                            progress->modelRevision = revision;
                            auto *event = new wxThreadEvent(wxEVT_THREAD,
                                                            IdUnsupportedAnalysisProgress);
                            event->SetPayload(std::move(progress));
                            wxQueueEvent(this, event);
                        });
                if (!unsupportedAnalysisCancel_.load(std::memory_order_relaxed)) {
                    payload->totalArea = unsupported.totalArea;
                    payload->visualization = BuildUnsupportedSurfaces(unsupported.unsupported);
                }
            }
            payload->cancelled = unsupportedAnalysisCancel_.load(std::memory_order_relaxed);
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
    supportGenerationSummary_.clear();
    supportProgress_ = {};
    supportProgress_[static_cast<std::size_t>(SupportProgressStage::Slicing)].started = true;
    canvas_->ClearUnsupportedVisualization();
    UpdateCommandState();
    UpdateStatus();

    supportGenerationWorker_ =
        std::thread([this, snapshots = std::move(snapshots), settings, revision]() mutable {
            auto payload = std::make_shared<SupportGenerationPayload>();
            payload->modelRevision = revision;
            const auto postProgress = [this, revision](SupportProgressStage stage,
                                                       std::size_t completed,
                                                       std::size_t total,
                                                       bool finished) {
                if (closing_.load(std::memory_order_relaxed))
                    return;
                auto progress = std::make_shared<SupportGenerationProgressPayload>();
                progress->stage = static_cast<std::size_t>(stage);
                progress->completed = completed;
                progress->total = total;
                progress->modelRevision = revision;
                progress->finished = finished;
                auto *event = new wxThreadEvent(wxEVT_THREAD, IdSupportGenerationProgress);
                event->SetPayload(std::move(progress));
                wxQueueEvent(this, event);
            };
            try {
                auto source = std::make_shared<const stl_slicer::TriangleMesh>(
                    combinedTransformedMesh(snapshots));
                auto slices = std::make_shared<const stl_slicer::SliceData>(stl_slicer::Slicer{
                    {settings.layerThickness,
                     settings.segmentationTolerance,
                     settings.contourHealingThreshold,
                     settings.firstLayerOffset}}.slice(*source, &supportGenerationCancel_));
                if (!supportGenerationCancel_.load(std::memory_order_relaxed)) {
                    postProgress(SupportProgressStage::Slicing, 0, 0, true);
                    postProgress(SupportProgressStage::FindingUnsupported, 0, 0, false);
                    stl_slicer::UnsupportedAreaResult detected =
                        stl_slicer::UnsupportedAreaAnalyzer{
                            {settings.criticalAngleDegrees, settings.overhangCoefficient}}
                            .analyze(*slices, &supportGenerationCancel_);
                    if (supportGenerationCancel_.load(std::memory_order_relaxed)) {
                        payload->cancelled = true;
                    } else {
                        const double latticeCellArea =
                            std::sqrt(3.0) * 0.5 * settings.supportSpacing *
                            settings.supportSpacing;
                        const std::size_t estimatedContacts = static_cast<std::size_t>(
                            std::max(1.0, std::ceil(detected.totalArea / latticeCellArea)));
                        postProgress(SupportProgressStage::FindingUnsupported, 0, 0, true);
                        postProgress(SupportProgressStage::ContactPoints,
                                     0,
                                     estimatedContacts,
                                     false);
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
                                  settings.criticalAngleDegrees},
                                 {settings.supportLatticeCellSize,
                                  settings.minimumSupportAngleDegrees,
                                  settings.supportBaseHeight,
                                  settings.supportBaseRadius,
                                  settings.supportPillarBottomRadius,
                                  settings.supportPillarTopRadius,
                                  settings.supportModelIsolation,
                                  static_cast<std::size_t>(settings.supportCircumferencePoints)}}}
                                .generate(
                                    {source, slices, unsupported},
                                    &supportGenerationCancel_,
                                    [&](const stl_slicer::SupportGenerationProgress &progress) {
                                        SupportProgressStage stage =
                                            SupportProgressStage::ContactPoints;
                                        switch (progress.phase) {
                                        case stl_slicer::SupportGenerationPhase::ContactPoints:
                                            stage = SupportProgressStage::ContactPoints;
                                            break;
                                        case stl_slicer::SupportGenerationPhase::VolumeSegmentation:
                                            stage = SupportProgressStage::VolumeSegmentation;
                                            break;
                                        case stl_slicer::SupportGenerationPhase::GeneratingSupports:
                                            stage = SupportProgressStage::GeneratingSupports;
                                            break;
                                        }
                                        postProgress(stage,
                                                     progress.completed,
                                                     progress.total,
                                                     progress.finished);
                                    });
                        payload->supports = std::move(generated.supports);
                        payload->contactPointCount = generated.contactPoints.size();
                        payload->processedLayerCount = generated.processedLayerCount;
                        payload->internalSupportFailureCount =
                            generated.internalSupportFailureCount;
                        payload->lowestInternalSupportFailureZ =
                            generated.lowestInternalSupportFailureZ;
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

void DocumentFrame::OnSupportGenerationProgress(wxThreadEvent &event) {
    const auto payload =
        event.GetPayload<std::shared_ptr<SupportGenerationProgressPayload>>();
    if (!supportGenerationRunning_ || payload->modelRevision != modelRevision_ ||
        payload->stage >= supportProgress_.size())
        return;

    SupportProgressState &stage = supportProgress_[payload->stage];
    stage.started = true;
    stage.completed = std::max(stage.completed, payload->completed);
    if (payload->total > 0)
        stage.total = payload->finished ? payload->total : std::max(stage.total, payload->total);
    if (!payload->finished)
        stage.total = std::max(stage.total, stage.completed);
    stage.finished = stage.finished || payload->finished;

    const std::size_t contactIndex =
        static_cast<std::size_t>(SupportProgressStage::ContactPoints);
    const std::size_t generationIndex =
        static_cast<std::size_t>(SupportProgressStage::GeneratingSupports);
    if (payload->stage == contactIndex && payload->finished) {
        supportProgress_[generationIndex].total = payload->total;
    }
    UpdateStatus();
}

void DocumentFrame::OnSupportGenerationFinished(wxThreadEvent &event) {
    if (supportGenerationWorker_.joinable())
        supportGenerationWorker_.join();
    supportGenerationRunning_ = false;
    supportGenerationCancel_.store(false, std::memory_order_relaxed);
    supportProgress_ = {};
    UpdateCommandState();
    UpdateStatus();

    const auto payload = event.GetPayload<std::shared_ptr<SupportGenerationPayload>>();
    if (!payload->error.empty()) {
        wxMessageBox(payload->error, "Support generation failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->cancelled || payload->modelRevision != modelRevision_)
        return;
    if (payload->internalSupportFailureCount > 0) {
        supportGenerationSummary_ =
            wxString::Format("%llu internal supports could not be placed. Lowest is at Z=%.2f.",
                             static_cast<unsigned long long>(
                                 payload->internalSupportFailureCount),
                             payload->lowestInternalSupportFailureZ)
                .ToStdString();
    } else {
        supportGenerationSummary_.clear();
    }
    UpdateStatus();
    if (payload->supports.triangles().empty()) {
        const wxString message =
            payload->contactPointCount == 0
                ? "No support contact points were detected."
                : wxString::Format("%llu support contact points were detected.\n"
                                   "No complete support paths were generated.",
                                   static_cast<unsigned long long>(payload->contactPointCount));
        wxMessageBox(message, "Generate supports", wxOK | wxICON_INFORMATION, this);
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
    unsupportedProgressVisible_ = false;
    unsupportedProgressCompleted_ = 0;
    unsupportedProgressTotal_ = 0;
    UpdateCommandState();
    UpdateStatus();

    const auto payload = event.GetPayload<std::shared_ptr<UnsupportedAnalysisPayload>>();
    if (!payload->error.empty()) {
        wxMessageBox(payload->error, "Unsupported-area analysis failed", wxOK | wxICON_ERROR, this);
        return;
    }
    if (payload->cancelled || payload->modelRevision != modelRevision_)
        return;
    canvas_->SetUnsupportedVisualization(std::move(payload->visualization));
}

void DocumentFrame::OnUnsupportedAnalysisProgress(wxThreadEvent &event) {
    const auto payload =
        event.GetPayload<std::shared_ptr<UnsupportedAnalysisProgressPayload>>();
    if (!unsupportedAnalysisRunning_ || payload->modelRevision != modelRevision_)
        return;
    unsupportedProgressVisible_ = true;
    unsupportedProgressCompleted_ = payload->completed;
    unsupportedProgressTotal_ = payload->total;
    UpdateStatus();
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
