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
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
enum { IdOpen = wxID_HIGHEST + 1, IdSettings };
#ifdef __WXGTK__
void CloseActiveDocument(GtkButton *, gpointer data) {
    auto *frame = static_cast<MainFrame *>(data);
    if (wxMDIChildFrame *child = frame->GetActiveChild())
        child->Close();
}
#endif
}

MainFrame::MainFrame()
    : wxMDIParentFrame(nullptr, wxID_ANY, "CLIP Slicer", wxDefaultPosition, {1200, 800}) {
    settings_.Load();
#ifdef __WXGTK__
    GtkWidget *closeDocument =
        gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(closeDocument), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(closeDocument, FALSE);
    gtk_widget_set_tooltip_text(closeDocument, "Close active document");
    g_signal_connect(closeDocument,
                     "clicked",
                     G_CALLBACK(CloseActiveDocument),
                     this);
    gtk_widget_show(closeDocument);
    gtk_notebook_set_action_widget(
        GTK_NOTEBOOK(GetClientWindow()->GetHandle()), closeDocument, GTK_PACK_END);
#endif
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
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR | wxFD_MULTIPLE);
    clip_slicer::help::Assign(&dialog, clip_slicer::help::openModelDialog);
    clip_slicer::help::Enable(&dialog);
    if (dialog.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dialog.GetPaths(paths);
        OpenFiles(paths);
    }
}
void MainFrame::OpenFile(const wxString &path) {
    wxArrayString paths;
    paths.push_back(path);
    OpenFiles(paths);
}
void MainFrame::OpenFiles(const wxArrayString &paths) {
    if (paths.empty())
        return;
    wxString title = wxFileName(paths.front()).GetFullName();
    if (paths.size() > 1)
        title += wxString::Format(" (+%zu)", paths.size() - 1);
    auto *child = new DocumentFrame(this, title);
    child->Show();
    for (const wxString &path : paths)
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
