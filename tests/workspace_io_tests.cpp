// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "workspace_io.hpp"
#include "stl_slicer/mesh_reader.hpp"
#include "stl_slicer/stl_writer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
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

void testAsciiStlAndObjWorkspaceSources() {
    TemporaryDirectory temporary;
    const auto stlPath = temporary.path / "models" / "ascii.geometry";
    const auto objPath = temporary.path / "models" / "polygon.geometry";
    {
        std::ofstream output(stlPath);
        output << "solid part\n"
                  "facet normal 0 0 1\n"
                  "outer loop\n"
                  "vertex 0 0 0\n"
                  "vertex 1 0 0\n"
                  "vertex 0 1 0\n"
                  "endloop\n"
                  "endfacet\n"
                  "endsolid part\n";
    }
    {
        std::ofstream output(objPath);
        output << "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\n"
                  "f 1 2 3 4\n";
    }
    require(clip_slicer::DetectInputFileFormat(wxString(stlPath.string())) ==
                clip_slicer::InputFileFormat::Stl,
            "GUI did not detect ASCII STL independently of extension");
    require(clip_slicer::DetectInputFileFormat(wxString(objPath.string())) ==
                clip_slicer::InputFileFormat::Obj,
            "GUI did not detect OBJ independently of extension");

    auto stl = std::make_shared<MeshSceneModel>(
        "ASCII STL", MeshReader{}.read(stlPath));
    stl->sourcePath = stlPath.string();
    auto obj = std::make_shared<MeshSceneModel>(
        "OBJ", MeshReader{}.read(objPath));
    obj->sourcePath = objPath.string();
    const wxString linkedWorkspace((temporary.path / "linked-meshes.clipslicer").string());
    clip_slicer::SaveWorkspace(linkedWorkspace, {stl, obj}, {}, false);
    const auto linked = clip_slicer::LoadWorkspace(linkedWorkspace);
    require(linked.models.size() == 2 && linked.missingExternalFiles.empty(),
            "Linked ASCII STL and OBJ models did not reopen");

    const wxString workspace((temporary.path / "mesh-inputs.clipslicer").string());
    clip_slicer::SaveWorkspace(workspace, {stl, obj}, {}, true);
    std::filesystem::remove(stlPath);
    std::filesystem::remove(objPath);

    const auto loaded = clip_slicer::LoadWorkspace(workspace);
    require(loaded.models.size() == 2 && loaded.missingExternalFiles.empty(),
            "Embedded ASCII STL and OBJ models did not reopen");
    require(loaded.models[0]->triangleMesh().triangles().size() == 1 &&
                loaded.models[1]->triangleMesh().triangles().size() == 2,
            "Workspace changed imported mesh geometry");
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
        testAsciiStlAndObjWorkspaceSources();
        std::cout << "Workspace tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
