// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_tree_model.hpp"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>
#include <wx/dvrenderers.h>

ModelTreeModel::ModelTreeModel(wxBitmapBundle groupIcon,
                               wxBitmapBundle meshIcon,
                               wxBitmapBundle slicesIcon)
    : groupIcon_(std::move(groupIcon)), meshIcon_(std::move(meshIcon)),
      slicesIcon_(std::move(slicesIcon)) {}

void ModelTreeModel::Rebuild(
    const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &models,
    const std::vector<DocumentModelGroup> &groups) {
    roots_.clear();
    std::unordered_map<const stl_slicer::SceneModel *, Node *> grouped;
    grouped.reserve(models.size());

    for (const DocumentModelGroup &group : groups) {
        auto node = std::make_unique<Node>();
        node->groupId = group.id;
        node->groupName = group.name;
        Node *groupNode = node.get();
        roots_.push_back(std::move(node));
        for (const stl_slicer::SceneModel *member : group.members)
            grouped.emplace(member, groupNode);
    }

    for (const auto &model : models) {
        auto node = std::make_unique<Node>();
        node->model = model;
        if (const auto found = grouped.find(model.get()); found != grouped.end()) {
            node->parent = found->second;
            found->second->children.push_back(std::move(node));
        } else {
            roots_.push_back(std::move(node));
        }
    }
    Cleared();
}

stl_slicer::SceneModel *ModelTreeModel::Model(const wxDataViewItem &item) const {
    Node *node = NodeFromItem(item);
    return node && node->model ? node->model.get() : nullptr;
}

std::uint64_t ModelTreeModel::GroupId(const wxDataViewItem &item) const {
    Node *node = NodeFromItem(item);
    if (!node)
        return 0;
    if (node->groupId)
        return node->groupId;
    return node->parent ? node->parent->groupId : 0;
}

bool ModelTreeModel::IsGroup(const wxDataViewItem &item) const {
    Node *node = NodeFromItem(item);
    return node && node->groupId != 0;
}

wxDataViewItem ModelTreeModel::ItemForModel(const stl_slicer::SceneModel *model) const {
    for (const auto &root : roots_) {
        if (root->model.get() == model)
            return ItemFromNode(root.get());
        for (const auto &child : root->children)
            if (child->model.get() == model)
                return ItemFromNode(child.get());
    }
    return {};
}

wxDataViewItem ModelTreeModel::ItemForGroup(std::uint64_t groupId) const {
    for (const auto &root : roots_)
        if (root->groupId == groupId)
            return ItemFromNode(root.get());
    return {};
}

unsigned int ModelTreeModel::GetColumnCount() const {
    return Count;
}

wxString ModelTreeModel::GetColumnType(unsigned int column) const {
    switch (column) {
    case Visibility:
        return "bool";
    case Name:
        return "wxDataViewIconText";
    default:
        return "string";
    }
}

void ModelTreeModel::GetValue(wxVariant &value,
                              const wxDataViewItem &item,
                              unsigned int column) const {
    const Node *node = NodeFromItem(item);
    if (!node)
        return;
    if (column == Visibility) {
        if (node->model) {
            value = node->model->visible;
        } else {
            value = !node->children.empty() &&
                    std::all_of(node->children.begin(), node->children.end(), [](const auto &child) {
                        return child->model->visible;
                    });
        }
        return;
    }
    if (column == Name) {
        if (node->model)
            value << wxDataViewIconText(node->model->name,
                                        node->model->isSliced() ? slicesIcon_ : meshIcon_);
        else {
            const wxString label = wxString::FromUTF8(node->groupName) +
                                   wxString::Format(" (%zu)", node->children.size());
            value << wxDataViewIconText(label, groupIcon_);
        }
        return;
    }
}

bool ModelTreeModel::SetValue(const wxVariant &value,
                              const wxDataViewItem &item,
                              unsigned int column) {
    Node *node = NodeFromItem(item);
    if (!node || column != Visibility)
        return false;
    const bool visible = value.GetBool();
    if (node->model) {
        node->model->visible = visible;
        if (node->parent)
            ValueChanged(ItemFromNode(node->parent), Visibility);
    } else {
        for (auto &child : node->children)
            child->model->visible = visible;
        NotifyVisibilityChanged(*node);
    }
    return true;
}

bool ModelTreeModel::GetAttr(const wxDataViewItem &item,
                             unsigned int column,
                             wxDataViewItemAttr &attr) const {
    if (column != Name)
        return false;
    const Node *node = NodeFromItem(item);
    if (!node)
        return false;
    if (node->groupId) {
        attr.SetBold(true);
        return true;
    }
    if (node->model && node->model->isSliced()) {
        attr.SetItalic(true);
        return true;
    }
    return false;
}

wxDataViewItem ModelTreeModel::GetParent(const wxDataViewItem &item) const {
    const Node *node = NodeFromItem(item);
    return node ? ItemFromNode(node->parent) : wxDataViewItem{};
}

bool ModelTreeModel::IsContainer(const wxDataViewItem &item) const {
    if (!item.IsOk())
        return true;
    const Node *node = NodeFromItem(item);
    return node && node->groupId != 0;
}

bool ModelTreeModel::HasContainerColumns(const wxDataViewItem &) const {
    return true;
}

unsigned int ModelTreeModel::GetChildren(const wxDataViewItem &parent,
                                         wxDataViewItemArray &children) const {
    if (!parent.IsOk()) {
        const std::size_t count = std::min<std::size_t>(
            roots_.size(), std::numeric_limits<unsigned int>::max());
        children.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            children.push_back(ItemFromNode(roots_[index].get()));
        return static_cast<unsigned int>(count);
    }
    const Node *node = NodeFromItem(parent);
    if (!node || !node->groupId)
        return 0;
    const std::size_t count = std::min<std::size_t>(
        node->children.size(), std::numeric_limits<unsigned int>::max());
    children.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        children.push_back(ItemFromNode(node->children[index].get()));
    return static_cast<unsigned int>(count);
}

ModelTreeModel::Node *ModelTreeModel::NodeFromItem(const wxDataViewItem &item) {
    return static_cast<Node *>(item.GetID());
}

wxDataViewItem ModelTreeModel::ItemFromNode(const Node *node) {
    return node ? wxDataViewItem(const_cast<Node *>(node)) : wxDataViewItem{};
}

void ModelTreeModel::NotifyVisibilityChanged(Node &node) {
    for (const auto &child : node.children)
        ValueChanged(ItemFromNode(child.get()), Visibility);
}
