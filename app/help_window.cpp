// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "help_window.hpp"
#include "embedded_manual.hpp"
#include "help_topics.hpp"
#include "main_frame.hpp"
#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/utils.h>
#include <wx/webview.h>

namespace {
wxString ManualHtml() {
    return wxString::FromUTF8(
        reinterpret_cast<const char *>(clip_slicer::assets::manualHtml),
        clip_slicer::assets::manualHtmlSize);
}
} // namespace

HelpWindow::HelpWindow(wxWindow *parent)
    : wxFrame(parent,
              wxID_ANY,
              "CLIP Slicer Help",
              wxDefaultPosition,
              wxSize(900, 700),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT) {
    SetSize(FromDIP(wxSize(900, 700)));
    webView_ = wxWebView::New(this, wxID_ANY);
    auto *root = new wxBoxSizer(wxVERTICAL);
    root->Add(webView_, 1, wxEXPAND);
    SetSizer(root);

    webView_->Bind(wxEVT_WEBVIEW_LOADED, &HelpWindow::OnLoaded, this);
    webView_->Bind(wxEVT_WEBVIEW_NAVIGATING, &HelpWindow::OnNavigating, this);
    Bind(wxEVT_CLOSE_WINDOW, &HelpWindow::OnClose, this);
    webView_->SetPage(ManualHtml(), "about:blank");
}

void HelpWindow::ShowTopic(const wxString &topic) {
    pendingTopic_ = topic;
    if (loaded_)
        ScrollToPendingTopic();
    Show();
    Raise();
}

void HelpWindow::ScrollToPendingTopic() {
    if (pendingTopic_.empty() || pendingTopic_ == clip_slicer::help::manualTop) {
        webView_->RunScript("window.scrollTo(0, 0);");
        return;
    }
    // Topic names are compile-time constants restricted to ASCII letters and hyphens.
    const wxString script =
        "document.getElementById('" + pendingTopic_ + "').scrollIntoView();";
    webView_->RunScript(script);
}

void HelpWindow::OnLoaded(wxWebViewEvent &event) {
    loaded_ = true;
    ScrollToPendingTopic();
    event.Skip();
}

void HelpWindow::OnNavigating(wxWebViewEvent &event) {
    const wxString url = event.GetURL();
    if (url.StartsWith("about:blank")) {
        event.Skip();
        return;
    }
    if (url.StartsWith("http://") || url.StartsWith("https://")) {
        event.Veto();
        wxLaunchDefaultBrowser(url);
        return;
    }
    event.Veto();
}

void HelpWindow::OnClose(wxCloseEvent &event) {
    if (event.CanVeto()) {
        Hide();
        event.Veto();
    } else {
        event.Skip();
    }
}

namespace clip_slicer::help {

void Assign(wxWindow *window, const char *topic) {
    window->SetHelpText(wxString::FromUTF8(topic));
    window->Bind(wxEVT_HELP, [topic](wxHelpEvent &) {
        auto *mainFrame = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
        if (mainFrame)
            mainFrame->ShowHelpTopic(wxString::FromUTF8(topic));
    });
}

} // namespace clip_slicer::help
