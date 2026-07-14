#pragma once
#include "app_settings.hpp"
#include <wx/mdi.h>

class MainFrame final : public wxMDIParentFrame {
  public:
    MainFrame();
    void OpenDialog();
    void OpenFile(const wxString &path);
    void ShowSettingsDialog();
    const AppSettings &Settings() const {
        return settings_;
    }
    void SetDocumentStatus(const wxString &buildVolume, const wxString &slicePosition);

  private:
    void ClearDocumentStatus();
    void OnOpen(wxCommandEvent &);
    void OnSettings(wxCommandEvent &);
    void OnExit(wxCommandEvent &);

    AppSettings settings_;
};
