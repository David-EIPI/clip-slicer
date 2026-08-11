// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "stl_slicer/scene_model.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <wx/dataview.h>

struct DocumentModelGroup {
    std::uint64_t id = 0;
    std::string name;
    bool expanded = true;
    std::vector<const stl_slicer::SceneModel *> members;
};

class ModelTreeModel final : public wxDataViewModel {
  public:
    enum Column : unsigned int { Visibility, Name, Count };

    ModelTreeModel(wxBitmapBundle groupIcon,
                   wxBitmapBundle meshIcon,
                   wxBitmapBundle slicesIcon);

    void Rebuild(const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &models,
                 const std::vector<DocumentModelGroup> &groups);

    stl_slicer::SceneModel *Model(const wxDataViewItem &item) const;
    std::uint64_t GroupId(const wxDataViewItem &item) const;
    bool IsGroup(const wxDataViewItem &item) const;
    wxDataViewItem ItemForModel(const stl_slicer::SceneModel *model) const;
    wxDataViewItem ItemForGroup(std::uint64_t groupId) const;

    unsigned int GetColumnCount() const override;
    wxString GetColumnType(unsigned int column) const override;
    void GetValue(wxVariant &value,
                  const wxDataViewItem &item,
                  unsigned int column) const override;
    bool SetValue(const wxVariant &value,
                  const wxDataViewItem &item,
                  unsigned int column) override;
    bool GetAttr(const wxDataViewItem &item,
                 unsigned int column,
                 wxDataViewItemAttr &attr) const override;
    wxDataViewItem GetParent(const wxDataViewItem &item) const override;
    bool IsContainer(const wxDataViewItem &item) const override;
    bool HasContainerColumns(const wxDataViewItem &item) const override;
    unsigned int GetChildren(const wxDataViewItem &parent,
                             wxDataViewItemArray &children) const override;

  private:
    struct Node {
        Node *parent = nullptr;
        std::uint64_t groupId = 0;
        std::string groupName;
        std::shared_ptr<stl_slicer::SceneModel> model;
        std::vector<std::unique_ptr<Node>> children;
    };

    static Node *NodeFromItem(const wxDataViewItem &item);
    static wxDataViewItem ItemFromNode(const Node *node);
    void NotifyVisibilityChanged(Node &node);

    std::vector<std::unique_ptr<Node>> roots_;
    wxBitmapBundle groupIcon_;
    wxBitmapBundle meshIcon_;
    wxBitmapBundle slicesIcon_;
};
