// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "help_window.hpp"
#include "embedded_manual.hpp"
#include "help_topics.hpp"
#include "main_frame.hpp"
#include <memory>
#include <unordered_map>
#include <wx/app.h>
#include <wx/bookctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/utils.h>
#include <wx/webview.h>

namespace {
std::unordered_map<wxWindow *, wxString> assignedTopics;

wxString HelpTextFor(wxWindow *window) {
    const auto assigned = assignedTopics.find(window);
    if (assigned != assignedTopics.end())
        return assigned->second;
#if wxUSE_HELP
    return window->GetHelpText();
#else
    return {};
#endif
}

wxString ManualHtml() {
    return wxString::FromUTF8(
        reinterpret_cast<const char *>(clip_slicer::assets::manualHtml),
        clip_slicer::assets::manualHtmlSize);
}

wxString TopicFor(wxWindow *window, wxWindow *root) {
    for (wxWindow *candidate = window; candidate; candidate = candidate->GetParent()) {
        if (auto *book = dynamic_cast<wxBookCtrlBase *>(candidate)) {
            if (wxWindow *page = book->GetCurrentPage()) {
                const wxString topic = HelpTextFor(page);
                if (!topic.empty())
                    return topic;
            }
        }
        const wxString topic = HelpTextFor(candidate);
        if (!topic.empty())
            return topic;
        if (candidate == root)
            break;
    }
    return HelpTextFor(root);
}

MainFrame *FindMainFrame(wxWindow *context) {
    for (wxWindow *candidate = context; candidate; candidate = candidate->GetParent()) {
        if (auto *mainFrame = dynamic_cast<MainFrame *>(candidate))
            return mainFrame;
    }
    return dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
}

void OpenTopic(wxWindow *context, const wxString &topic) {
    auto *mainFrame = FindMainFrame(context);
    if (mainFrame && !topic.empty())
        mainFrame->ShowHelpTopic(topic, context);
}
} // namespace

HelpWindow::HelpWindow(wxWindow *parent)
    : wxDialog(parent,
               wxID_ANY,
               "CLIP Slicer Help",
               wxDefaultPosition,
               wxSize(900, 700),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX) {
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
    if (auto *parentDialog = dynamic_cast<wxDialog *>(GetParent());
        parentDialog && parentDialog->IsModal()) {
        ShowModal();
    } else {
        Show();
        Raise();
    }
}

void HelpWindow::ScrollToPendingTopic() {
    if (pendingTopic_.empty() || pendingTopic_ == clip_slicer::help::manualTop) {
        webView_->RunScript("window.scrollTo(0, 0);");
        return;
    }
    // Topic names are compile-time constants restricted to ASCII letters and hyphens.
    const wxString script = "{const target=document.getElementById('" + pendingTopic_ +
                            "');if(target)target.scrollIntoView();else window.scrollTo(0,0);}";
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
    if (IsModal()) {
        EndModal(wxID_CLOSE);
        return;
    }
    if (event.CanVeto()) {
        Hide();
        event.Veto();
    } else {
        event.Skip();
    }
}

namespace clip_slicer::help {

void Assign(wxWindow *window, const char *topic) {
    const wxString value = wxString::FromUTF8(topic);
#if wxUSE_HELP
    window->SetHelpText(value);
#endif
    assignedTopics[window] = value;
    window->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent &event) {
        assignedTopics.erase(static_cast<wxWindow *>(event.GetEventObject()));
        event.Skip();
    });
}

void Enable(wxWindow *root) {
    struct State {
        wxString lastFocusedTopic;
    };
    const auto state = std::make_shared<State>();
    state->lastFocusedTopic = HelpTextFor(root);

    root->Bind(wxEVT_CHILD_FOCUS, [root, state](wxChildFocusEvent &event) {
        wxWindow *focused = event.GetWindow();
        if (focused && focused->GetId() != wxID_HELP) {
            const wxString topic = TopicFor(focused, root);
            if (!topic.empty())
                state->lastFocusedTopic = topic;
        }
        event.Skip();
    });
    root->Bind(wxEVT_CHAR_HOOK, [root](wxKeyEvent &event) {
        if (event.GetKeyCode() != WXK_F1) {
            event.Skip();
            return;
        }
        OpenTopic(root, TopicFor(wxWindow::FindFocus(), root));
    });
    root->Bind(wxEVT_HELP, [root](wxHelpEvent &) {
        OpenTopic(root, TopicFor(wxWindow::FindFocus(), root));
    });
    const auto showFocusedTopic = [root, state]() {
        wxString topic = state->lastFocusedTopic;
        if (topic.empty())
            topic = TopicFor(wxWindow::FindFocus(), root);
        OpenTopic(root, topic);
    };
    root->Bind(wxEVT_BUTTON, [showFocusedTopic](wxCommandEvent &) { showFocusedTopic(); },
               wxID_HELP);

    // Native dialog handling may consume wxID_HELP before a command event can
    // propagate to the dialog. Bind the actual standard button as well.
    if (wxWindow *helpButton = root->FindWindow(wxID_HELP)) {
        helpButton->Bind(wxEVT_BUTTON,
                         [showFocusedTopic](wxCommandEvent &) { showFocusedTopic(); });
    }
}

} // namespace clip_slicer::help
