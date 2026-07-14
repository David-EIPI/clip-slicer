#include "main_frame.hpp"
#include "document_frame.hpp"
#include "settings_dialog.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>

namespace {
enum { IdOpen = wxID_HIGHEST + 1, IdSettings };
}

MainFrame::MainFrame()
    : wxMDIParentFrame(nullptr, wxID_ANY, "CLIP Slicer", wxDefaultPosition, {1200, 800}) {
    settings_.Load();
    auto *file = new wxMenu;
    file->Append(IdOpen, "&Open...\tCtrl+O");
    file->Append(IdSettings, "&Settings...");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto *bar = new wxMenuBar;
    bar->Append(file, "&File");
    SetMenuBar(bar);
    wxStatusBar *statusBar = CreateStatusBar(2);
    int buildWidth = 210;
    int sliceWidth = 90;
    if (statusBar) {
        statusBar->GetTextExtent("0000.00 x 0000.00 x 0000.00", &buildWidth, nullptr);
        statusBar->GetTextExtent("Z: -0000.000", &sliceWidth, nullptr);
    }
    const int statusWidths[] = {buildWidth + 12, sliceWidth + 16};
    const int statusStyles[] = {wxSB_SUNKEN, wxSB_SUNKEN};
    SetStatusWidths(2, statusWidths);
    if (statusBar)
        statusBar->SetStatusStyles(2, statusStyles);
    ClearDocumentStatus();
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, IdOpen);
    Bind(wxEVT_MENU, &MainFrame::OnSettings, this, IdSettings);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
}
void MainFrame::OnSettings(wxCommandEvent &) {
    ShowSettingsDialog();
}
void MainFrame::ShowSettingsDialog() {
    SettingsDialog dialog(this, settings_);
    if (dialog.ShowModal() != wxID_OK)
        return;

    AppSettings updated = settings_;
    updated.contourHealingThreshold = dialog.ContourHealingThreshold();
    updated.segmentationTolerance = dialog.SegmentationTolerance();
    if (!updated.Save()) {
        wxMessageBox("Unable to save settings to:\n" + AppSettings::FilePath(),
                     "Settings",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    settings_ = updated;
    if (auto *document = dynamic_cast<DocumentFrame *>(GetActiveChild()))
        document->SettingsChanged();
}
void MainFrame::OnOpen(wxCommandEvent &) {
    OpenDialog();
}
void MainFrame::OpenDialog() {
    wxFileDialog dialog(this,
                        "Open model",
                        {},
                        {},
                        "3D model files (*.stl;*.cli)|*.stl;*.cli|STL files|*.stl|CLI files|*.cli",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK)
        OpenFile(dialog.GetPath());
}
void MainFrame::OpenFile(const wxString &path) {
    auto *child = new DocumentFrame(this, wxFileName(path).GetFullName());
    child->Show();
    child->OpenPath(path);
}
void MainFrame::SetDocumentStatus(const wxString &buildVolume, const wxString &slicePosition) {
    SetStatusText(buildVolume, 0);
    SetStatusText(slicePosition, 1);
}
void MainFrame::ClearDocumentStatus() {
    SetDocumentStatus({}, {});
}
void MainFrame::OnExit(wxCommandEvent &) {
    Close();
}
