#include "document_frame.hpp"
#include "gl_canvas.hpp"
#include "stl_slicer/cli_reader.hpp"
#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/slicer.hpp"
#include "stl_slicer/stl_reader.hpp"
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
#include <wx/toolbar.h>

namespace {
enum { IdOpen = wxID_HIGHEST + 20, IdExport, IdSlice, IdInteractive, IdShow, IdHide, IdSettings };
}

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
    toolbar_->AddTool(IdExport, "Export", wxArtProvider::GetBitmap(wxART_FILE_SAVE));
    toolbar_->AddTool(IdSlice, "Slice", wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE));
    toolbar_->AddTool(IdInteractive,
                      "Plane",
                      wxArtProvider::GetBitmap(wxART_FIND),
                      "Interactive slicing",
                      wxITEM_CHECK);
    toolbar_->AddTool(IdHide, "Hide", wxArtProvider::GetBitmap(wxART_MINUS));
    toolbar_->AddTool(IdShow, "Show", wxArtProvider::GetBitmap(wxART_PLUS));
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
    const int statusWidths[] = {-2, -1};
    CreateStatusBar(2);
    SetStatusWidths(2, statusWidths);
    Bind(wxEVT_MENU, &DocumentFrame::OnOpen, this, IdOpen);
    Bind(wxEVT_MENU, &DocumentFrame::OnExport, this, IdExport);
    Bind(wxEVT_MENU, &DocumentFrame::OnSlice, this, IdSlice);
    Bind(wxEVT_MENU, &DocumentFrame::OnInteractiveSlice, this, IdInteractive);
    Bind(wxEVT_MENU, &DocumentFrame::OnShow, this, IdShow);
    Bind(wxEVT_MENU, &DocumentFrame::OnHide, this, IdHide);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
            wxMessageBox("Settings will be added in a later version.",
                         "Settings",
                         wxOK | wxICON_INFORMATION,
                         this);
        },
        IdSettings);
    modelList_->Bind(wxEVT_LISTBOX, &DocumentFrame::OnListSelection, this);
    modelList_->Bind(wxEVT_CHECKLISTBOX, &DocumentFrame::OnListCheck, this);
    UpdateStatus();
    UpdateCommandState();
}
void DocumentFrame::BuildMenus() {
    auto *file = new wxMenu;
    file->Append(IdOpen, "&Open into document...");
    exportItem_ = file->Append(IdExport, "&Export Slices...");
    file->Append(wxID_CLOSE, "&Close");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto *view = new wxMenu;
    view->Append(IdShow, "&Show selected");
    view->Append(IdHide, "&Hide selected");
    auto *slice = new wxMenu;
    slice->Append(IdSlice, "&Slice selected...");
    slice->AppendCheckItem(IdInteractive, "&Interactive slicing");
    slice->Append(IdSettings, "Settings...");
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
    models_.push_back(std::move(model));
    RefreshModelList();
    canvas_->ModelsChanged();
    UpdateStatus();
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
void DocumentFrame::UpdateStatus(double z, double area, bool slicing) {
    auto b = VisibleBounds();
    std::ostringstream a;
    a << std::fixed << std::setprecision(2);
    if (b.valid())
        a << (b.max.x - b.min.x) << " x " << (b.max.y - b.min.y) << " x " << (b.max.z - b.min.z)
          << " mm";
    else
        a << "No models";
    SetStatusText(a.str(), 0);
    std::ostringstream s;
    s << std::fixed << std::setprecision(2);
    if (slicing)
        s << "Slice Z: " << z << " mm   Area: " << area << " mm2";
    SetStatusText(s.str(), 1);
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
                auto data = stl_slicer::Slicer{{dz, 1e-5, 2.0, start}}.slice(
                    stl_slicer::transformedMesh(*m));
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
        auto *child = new DocumentFrame(static_cast<wxMDIParentFrame *>(GetParent()), "Slices");
        child->Show();
        for (auto &m : made)
            child->AddModel(m);
    }
}
void DocumentFrame::OnExport(wxCommandEvent &) {
    stl_slicer::SliceData combined;
    for (const auto &m : models_)
        if (m->selected && m->isSliced()) {
            const auto *s = m->slices();
            combined.thickness = s->thickness;
            combined.layers.insert(combined.layers.end(), s->layers.begin(), s->layers.end());
        }
    if (combined.layers.empty()) {
        wxMessageBox("Select at least one sliced model.");
        return;
    }
    std::stable_sort(combined.layers.begin(),
                     combined.layers.end(),
                     [](const auto &a, const auto &b) { return a.z < b.z; });
    for (const auto &l : combined.layers)
        for (const auto &p : l.paths)
            for (const auto &q : p.points)
                combined.sourceBounds.include({q.x, q.y, l.z});
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
