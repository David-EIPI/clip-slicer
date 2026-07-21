// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "main_frame.hpp"
#include <wx/app.h>
#include <wx/image.h>
#include <wx/imagpng.h>

class ClipSlicerApp final : public wxApp {
  public:
    bool OnInit() override {
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
