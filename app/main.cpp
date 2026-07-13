#include "main_frame.hpp"
#include <wx/app.h>

class ClipSlicerApp final : public wxApp {
  public:
    bool OnInit() override {
        auto *frame = new MainFrame();
        frame->Show();
        if (argc > 1)
            frame->OpenFile(argv[1]);
        return true;
    }
};
wxIMPLEMENT_APP(ClipSlicerApp);
