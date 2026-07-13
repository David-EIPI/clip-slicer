#include "main_frame.hpp"
#include "document_frame.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>

namespace {
enum { IdOpen = wxID_HIGHEST + 1 };
}

MainFrame::MainFrame()
    : wxMDIParentFrame(nullptr, wxID_ANY, "CLIP Slicer", wxDefaultPosition, {1200, 800}) {
    auto *file = new wxMenu;
    file->Append(IdOpen, "&Open...\tCtrl+O");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto *bar = new wxMenuBar;
    bar->Append(file, "&File");
    SetMenuBar(bar);
    CreateStatusBar();
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, IdOpen);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
}
void MainFrame::OnOpen(wxCommandEvent &) {
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
void MainFrame::OnExit(wxCommandEvent &) {
    Close();
}
