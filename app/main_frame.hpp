#pragma once
#include <wx/mdi.h>

class MainFrame final : public wxMDIParentFrame {
  public:
    MainFrame();
    void OpenFile(const wxString &path);

  private:
    void OnOpen(wxCommandEvent &);
    void OnExit(wxCommandEvent &);
};
