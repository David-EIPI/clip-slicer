#pragma once

#include "stl_slicer/scene_model.hpp"
#include <memory>
#include <vector>
#include <wx/checklst.h>
#include <wx/mdi.h>

class ModelCanvas;
class wxActivateEvent;
class wxCloseEvent;
class wxMenuItem;
class wxToolBar;

class DocumentFrame final : public wxMDIChildFrame {
  public:
    DocumentFrame(wxMDIParentFrame *parent, const wxString &title);
    const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &Models() const {
        return models_;
    }
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> &Models() {
        return models_;
    }
    void AddModel(std::shared_ptr<stl_slicer::SceneModel> model);
    stl_slicer::Bounds3 VisibleBounds() const;
    stl_slicer::Vec3 SelectedCenter() const;
    void RefreshModelList();
    void UpdateStatus();
    void OpenPath(const wxString &path);

  private:
    void BuildMenus();
    void OnOpen(wxCommandEvent &);
    void OnActivate(wxActivateEvent &);
    void OnClose(wxCloseEvent &);
    void OnExport(wxCommandEvent &);
    void OnSlice(wxCommandEvent &);
    void OnInteractiveSlice(wxCommandEvent &);
    void OnShow(wxCommandEvent &);
    void OnHide(wxCommandEvent &);
    void OnListSelection(wxCommandEvent &);
    void OnListCheck(wxCommandEvent &);
    void PublishStatus();
    void UpdateCommandState();

    std::vector<std::shared_ptr<stl_slicer::SceneModel>> models_;
    wxCheckListBox *modelList_ = nullptr;
    ModelCanvas *canvas_ = nullptr;
    wxToolBar *toolbar_ = nullptr;
    wxMenuItem *exportItem_ = nullptr;
};
