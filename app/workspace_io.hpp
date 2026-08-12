// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include "model_tree_model.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <wx/string.h>

namespace clip_slicer {

enum class InputFileFormat { Workspace, Stl, Cli, Unknown };

struct WorkspaceData {
    std::vector<std::shared_ptr<stl_slicer::SceneModel>> models;
    std::vector<DocumentModelGroup> groups;
    std::vector<wxString> missingExternalFiles;
};

using WorkspaceProgress = std::function<void(std::uint64_t completed, std::uint64_t total)>;

InputFileFormat DetectInputFileFormat(const wxString &path);

void SaveWorkspace(const wxString &path,
                   const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &models,
                   const std::vector<DocumentModelGroup> &groups,
                   bool embedModelFiles,
                   const WorkspaceProgress &progress = {});

WorkspaceData LoadWorkspace(const wxString &path);

} // namespace clip_slicer
