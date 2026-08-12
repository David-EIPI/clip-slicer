// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "workspace_io.hpp"
#include "stl_slicer/stl_writer.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <wx/init.h>

using namespace stl_slicer;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

TriangleMesh triangleMesh(double offset = 0.0) {
    TriangleMesh mesh;
    Triangle triangle;
    triangle.vertices = {{{offset, 0, 0}, {offset + 1, 0, 0}, {offset, 1, 0}}};
    mesh.addTriangle(triangle);
    return mesh;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("clip-slicer-workspace-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(path / "models");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void testExternalRoundTripAndMissingSummaryData() {
    TemporaryDirectory temporary;
    const auto firstPath = temporary.path / "models" / "first.model";
    const auto secondPath = temporary.path / "second.model";
    BinaryStlWriter{}.write(triangleMesh(), firstPath);
    BinaryStlWriter{}.write(triangleMesh(2.0), secondPath);

    auto first = std::make_shared<MeshSceneModel>("first", triangleMesh());
    first->sourcePath = firstPath.string();
    first->transform = Mat4::translation(2, 3, 4);
    first->selected = true;
    auto replica = first->replica("first copy");
    replica->transform = Mat4::translation(8, 9, 10);
    replica->visible = false;
    auto second = std::make_shared<MeshSceneModel>("second", triangleMesh(2.0));
    second->sourcePath = secondPath.string();

    std::vector<std::shared_ptr<SceneModel>> models = {first, replica, second};
    DocumentModelGroup group{17, "Parts", false, {first.get(), replica.get(), second.get()}};
    const wxString workspace((temporary.path / "layout.unusual").string());
    clip_slicer::SaveWorkspace(workspace, models, {group}, false);

    require(clip_slicer::DetectInputFileFormat(workspace) ==
                clip_slicer::InputFileFormat::Workspace,
            "workspace detection depended on the extension");
    auto loaded = clip_slicer::LoadWorkspace(workspace);
    require(loaded.missingExternalFiles.empty(), "existing links were reported missing");
    require(loaded.models.size() == 3, "external workspace model count changed");
    require(loaded.groups.size() == 1 && loaded.groups.front().members.size() == 3,
            "workspace group hierarchy changed");
    require(!loaded.groups.front().expanded, "group expansion state changed");
    require(loaded.models[0]->selected && !loaded.models[1]->visible,
            "model display state changed");
    require(loaded.models[0]->geometryIdentity() == loaded.models[1]->geometryIdentity(),
            "replicas no longer share their geometry");
    require(loaded.models[0]->transform.values() == first->transform.values() &&
                loaded.models[1]->transform.values() == replica->transform.values(),
            "model transformations changed");

    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
    loaded = clip_slicer::LoadWorkspace(workspace);
    require(loaded.missingExternalFiles.size() == 2,
            "missing links were not counted once per referenced source");
    require(loaded.models.empty(), "models with missing sources were not skipped");
}

void testEmbeddedWorkspaceIsSelfContained() {
    TemporaryDirectory temporary;
    const auto sourcePath = temporary.path / "source.stl";
    BinaryStlWriter{}.write(triangleMesh(), sourcePath);
    auto model = std::make_shared<MeshSceneModel>("embedded", triangleMesh());
    model->sourcePath = sourcePath.string();

    const wxString workspace((temporary.path / "embedded.clipslicer").string());
    std::uint64_t lastCompleted = 0;
    std::uint64_t lastTotal = 0;
    clip_slicer::SaveWorkspace(
        workspace,
        {model},
        {},
        true,
        [&](std::uint64_t completed, std::uint64_t total) {
            lastCompleted = completed;
            lastTotal = total;
        });
    require(lastTotal > 0 && lastCompleted == lastTotal,
            "embedded save progress did not reach completion");

    std::filesystem::remove(sourcePath);
    const auto loaded = clip_slicer::LoadWorkspace(workspace);
    require(loaded.missingExternalFiles.empty(), "embedded source was treated as external");
    require(loaded.models.size() == 1 && loaded.models.front()->name == "embedded",
            "embedded model did not round trip");
}
} // namespace

int main() {
    wxInitializer wx;
    if (!wx.IsOk()) {
        std::cerr << "Unable to initialize wxWidgets\n";
        return 1;
    }
    try {
        testExternalRoundTripAndMissingSummaryData();
        testEmbeddedWorkspaceIsSelfContained();
        std::cout << "Workspace tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
