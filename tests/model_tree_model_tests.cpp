// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "model_tree_model.hpp"
#include <iostream>
#include <stdexcept>

using namespace stl_slicer;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

TriangleMesh triangleMesh() {
    TriangleMesh mesh;
    Triangle triangle;
    triangle.vertices = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    mesh.addTriangle(triangle);
    return mesh;
}

void testHierarchyAndAttributes() {
    auto mesh = std::make_shared<MeshSceneModel>("surface", triangleMesh());
    SliceData slices;
    slices.sourceBounds.include({0, 0, 0});
    slices.sourceBounds.include({1, 1, 1});
    auto sliced = std::make_shared<SliceSceneModel>("layers", std::move(slices));
    auto ungrouped = std::make_shared<MeshSceneModel>("ungrouped", triangleMesh());
    std::vector<std::shared_ptr<SceneModel>> models = {mesh, sliced, ungrouped};
    DocumentModelGroup group{1, "Array", true, {mesh.get(), sliced.get()}};

    ModelTreeModel model({}, {}, {});
    model.Rebuild(models, {group});
    wxDataViewItemArray roots;
    require(model.GetChildren({}, roots) == 2, "unexpected data-view root count");
    const wxDataViewItem groupItem = model.ItemForGroup(1);
    require(groupItem.IsOk() && model.IsGroup(groupItem), "group node was not created");
    wxDataViewItemArray children;
    require(model.GetChildren(groupItem, children) == 2, "group members were not created");
    require(model.GetParent(model.ItemForModel(mesh.get())) == groupItem,
            "grouped model had the wrong parent");
    require(!model.GetParent(model.ItemForModel(ungrouped.get())).IsOk(),
            "ungrouped model unexpectedly had a parent");

    wxDataViewItemAttr attr;
    require(model.GetAttr(groupItem, ModelTreeModel::Name, attr) && attr.GetBold(),
            "group name was not bold");
    attr = {};
    require(model.GetAttr(model.ItemForModel(sliced.get()), ModelTreeModel::Name, attr) &&
                attr.GetItalic(),
            "sliced model name was not italic");
}

void testGroupVisibility() {
    auto first = std::make_shared<MeshSceneModel>("first", triangleMesh());
    auto second = std::make_shared<MeshSceneModel>("second", triangleMesh());
    std::vector<std::shared_ptr<SceneModel>> models = {first, second};
    DocumentModelGroup group{7, "Pair", true, {first.get(), second.get()}};
    ModelTreeModel model({}, {}, {});
    model.Rebuild(models, {group});

    wxVariant hidden(false);
    require(model.SetValue(hidden, model.ItemForModel(first.get()), ModelTreeModel::Visibility),
            "model visibility value was rejected");
    wxVariant mixedGroupVisibility;
    model.GetValue(mixedGroupVisibility, model.ItemForGroup(7), ModelTreeModel::Visibility);
    require(!mixedGroupVisibility.GetBool() && second->visible,
            "mixed group visibility was not represented");
    first->visible = true;

    require(model.SetValue(hidden, model.ItemForGroup(7), ModelTreeModel::Visibility),
            "group visibility value was rejected");
    require(!first->visible && !second->visible, "group visibility did not propagate");
    wxVariant visible;
    model.GetValue(visible, model.ItemForGroup(7), ModelTreeModel::Visibility);
    require(!visible.GetBool(), "group visibility value was not updated");
}
} // namespace

int main() {
    try {
        testHierarchyAndAttributes();
        testGroupVisibility();
        std::cout << "Model tree tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
