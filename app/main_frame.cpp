// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "main_frame.hpp"
#include "document_frame.hpp"
#include "help_topics.hpp"
#include "help_window.hpp"
#include "settings_dialog.hpp"
#include <wx/dialog.h>
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
    auto *help = new wxMenu;
    help->Append(wxID_HELP, "&Topics");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
    clip_slicer::help::Assign(this, clip_slicer::help::manualTop);
    clip_slicer::help::Enable(this);
    wxStatusBar *statusBar = CreateStatusBar(3);
    int buildWidth = 210;
    int sliceWidth = 90;
    if (statusBar) {
        statusBar->GetTextExtent("0000.00 x 0000.00 x 0000.00", &buildWidth, nullptr);
        statusBar->GetTextExtent("Z: -000000000.000", &sliceWidth, nullptr);
    }
    const int statusWidths[] = {buildWidth + 12, sliceWidth + 16, -1};
    const int statusStyles[] = {wxSB_SUNKEN, wxSB_SUNKEN, wxSB_SUNKEN};
    SetStatusWidths(3, statusWidths);
    if (statusBar)
        statusBar->SetStatusStyles(3, statusStyles);
    ClearDocumentStatus();
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, IdOpen);
    Bind(wxEVT_MENU, &MainFrame::OnSettings, this, IdSettings);
    Bind(wxEVT_MENU, &MainFrame::OnHelpTopics, this, wxID_HELP);
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
    updated.layerThickness = dialog.LayerThickness();
    updated.firstLayerOffset = dialog.FirstLayerOffset();
    updated.contourHealingThreshold = dialog.ContourHealingThreshold();
    updated.segmentationTolerance = dialog.SegmentationTolerance();
    updated.criticalAngleDegrees = dialog.CriticalAngleDegrees();
    updated.overhangCoefficient = dialog.OverhangCoefficient();
    updated.optimizationAttempts = dialog.OptimizationAttempts();
    updated.optimizationWorkers = dialog.OptimizationWorkers();
    updated.optimizationTolerance = dialog.OptimizationTolerance();
    updated.supportSpacing = dialog.SupportSpacing();
    updated.supportTipTopRadius = dialog.SupportTipTopRadius();
    updated.supportTipBottomRadius = dialog.SupportTipBottomRadius();
    updated.supportTipHeight = dialog.SupportTipHeight();
    updated.supportLatticeCellSize = dialog.SupportLatticeCellSize();
    updated.supportModelIsolation = dialog.SupportModelIsolation();
    updated.minimumSupportAngleDegrees = dialog.MinimumSupportAngleDegrees();
    updated.supportBaseHeight = dialog.SupportBaseHeight();
    updated.supportBaseRadius = dialog.SupportBaseRadius();
    updated.supportPillarBottomRadius = dialog.SupportPillarBottomRadius();
    updated.supportPillarTopRadius = dialog.SupportPillarTopRadius();
    updated.supportCircumferencePoints = dialog.SupportCircumferencePoints();
    updated.crossSectionDisplayDistance = dialog.CrossSectionDisplayDistance();
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
void MainFrame::OnHelpTopics(wxCommandEvent &) {
    ShowHelpTopic(clip_slicer::help::manualTop);
}
void MainFrame::ShowHelpTopic(const wxString &topic, wxWindow *context) {
    wxWindow *helpParent = this;
    if (context) {
        wxWindow *topLevel = wxGetTopLevelParent(context);
        if (auto *dialog = dynamic_cast<wxDialog *>(topLevel); dialog && dialog->IsModal())
            helpParent = dialog;
    }

    if (helpWindow_ && helpWindow_->GetParent() != helpParent) {
        helpWindow_->Destroy();
        helpWindow_ = nullptr;
    }
    if (!helpWindow_) {
        auto *window = new HelpWindow(helpParent);
        helpWindow_ = window;
        window->Bind(wxEVT_DESTROY, [this, window](wxWindowDestroyEvent &event) {
            if (helpWindow_ == window)
                helpWindow_ = nullptr;
            event.Skip();
        });
    }
    helpWindow_->ShowTopic(topic);
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
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
    clip_slicer::help::Assign(&dialog, clip_slicer::help::openModelDialog);
    clip_slicer::help::Enable(&dialog);
    if (dialog.ShowModal() == wxID_OK)
        OpenFile(dialog.GetPath());
}
void MainFrame::OpenFile(const wxString &path) {
    auto *child = new DocumentFrame(this, wxFileName(path).GetFullName());
    child->Show();
    child->OpenPath(path);
}
void MainFrame::SetDocumentStatus(const wxString &buildVolume,
                                  const wxString &slicePosition,
                                  const wxString &optimizationProgress) {
    SetStatusText(buildVolume, 0);
    SetStatusText(slicePosition, 1);
    SetStatusText(optimizationProgress, 2);
}
void MainFrame::ClearDocumentStatus() {
    SetDocumentStatus({}, {}, {});
}
void MainFrame::OnExit(wxCommandEvent &) {
    Close();
}
