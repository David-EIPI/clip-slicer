// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "main_frame.hpp"
#include <wx/app.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/utils.h>

class ClipSlicerApp final : public wxApp {
  public:
    bool OnInit() override {
#ifdef __WXGTK__
        // WebKitGTK's DMA-BUF renderer can fail on otherwise functional GBM
        // configurations and leave wxWebView blank. Help content is static, so
        // use its robust software-backed renderer without affecting OpenGL.
        if (!wxGetEnv("WEBKIT_DISABLE_DMABUF_RENDERER", nullptr))
            wxSetEnv("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
#endif
        wxImage::AddHandler(new wxPNGHandler);
        auto *frame = new MainFrame();
        frame->Show();
        if (argc > 1 && argv[1] == "--help-topics")
            frame->ShowHelpTopic("manual-top");
        else if (argc > 1)
            frame->OpenFile(argv[1]);
        return true;
    }
};
wxIMPLEMENT_APP(ClipSlicerApp);
