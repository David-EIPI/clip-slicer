// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <wx/dialog.h>

class wxCloseEvent;
class wxWebView;
class wxWebViewEvent;

class HelpWindow final : public wxDialog {
  public:
    explicit HelpWindow(wxWindow *parent);
    void ShowTopic(const wxString &topic);

  private:
    void ScrollToPendingTopic();
    void OnLoaded(wxWebViewEvent &event);
    void OnNavigating(wxWebViewEvent &event);
    void OnClose(wxCloseEvent &event);

    wxWebView *webView_ = nullptr;
    wxString pendingTopic_;
    bool loaded_ = false;
};
