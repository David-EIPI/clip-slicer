// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once
#include "app_settings.hpp"
#include <wx/mdi.h>

class MainFrame final : public wxMDIParentFrame {
  public:
    MainFrame();
    void OpenDialog();
    void OpenFile(const wxString &path);
    void ShowSettingsDialog();
    void ShowHelpTopic(const wxString &topic);
    const AppSettings &Settings() const {
        return settings_;
    }
    void SetDocumentStatus(const wxString &buildVolume,
                           const wxString &slicePosition,
                           const wxString &optimizationProgress);

  private:
    void ClearDocumentStatus();
    void OnOpen(wxCommandEvent &);
    void OnSettings(wxCommandEvent &);
    void OnHelpTopics(wxCommandEvent &);
    void OnExit(wxCommandEvent &);

    AppSettings settings_;
    class HelpWindow *helpWindow_ = nullptr;
};
