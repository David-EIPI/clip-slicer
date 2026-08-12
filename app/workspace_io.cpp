// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "workspace_io.hpp"
#include "stl_slicer/cli_reader.hpp"
#include "stl_slicer/cli_writer.hpp"
#include "stl_slicer/stl_reader.hpp"
#include "stl_slicer/stl_writer.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>
#include <wx/xml/xml.h>
#include <wx/zipstrm.h>

namespace clip_slicer {
namespace {

constexpr const char *manifestName = "workspace.xml";
constexpr std::size_t maximumManifestSize = 16 * 1024 * 1024;

std::filesystem::path FileSystemPath(const wxString &path) {
#ifdef _WIN32
    return std::filesystem::path(path.ToStdWstring());
#else
    return std::filesystem::path(path.ToStdString());
#endif
}

std::string Utf8(const wxString &text) {
    const wxScopedCharBuffer buffer = text.ToUTF8();
    return buffer ? std::string(buffer.data(), buffer.length()) : std::string();
}

wxString FromUtf8(const std::string &text) {
    return wxString::FromUTF8(text.data(), text.size());
}

wxString AbsolutePath(const wxString &path) {
    wxFileName name(path);
    name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return name.GetFullPath();
}

std::vector<unsigned char> ReadCurrentEntry(wxZipInputStream &zip,
                                            std::size_t maximumSize = 0) {
    std::vector<unsigned char> result;
    std::array<unsigned char, 1024 * 1024> buffer{};
    while (true) {
        zip.Read(buffer.data(), buffer.size());
        const std::size_t count = zip.LastRead();
        if (count == 0)
            break;
        if (maximumSize && result.size() + count > maximumSize)
            throw std::runtime_error("Workspace XML manifest is too large");
        result.insert(result.end(), buffer.begin(), buffer.begin() + count);
    }
    if (zip.GetLastError() != wxSTREAM_EOF && zip.GetLastError() != wxSTREAM_NO_ERROR)
        throw std::runtime_error("Unable to read workspace archive entry");
    return result;
}

wxXmlDocument ReadManifest(const wxString &path) {
    wxFFileInputStream file(path);
    if (!file.IsOk())
        throw std::runtime_error("Cannot open workspace: " + Utf8(path));
    wxZipInputStream zip(file);
    while (std::unique_ptr<wxZipEntry> entry{zip.GetNextEntry()}) {
        if (entry->GetName() != manifestName)
            continue;
        const std::vector<unsigned char> bytes = ReadCurrentEntry(zip, maximumManifestSize);
        wxMemoryInputStream input(bytes.data(), bytes.size());
        wxXmlDocument document;
        if (!document.Load(input))
            throw std::runtime_error("Workspace XML manifest is invalid");
        const wxXmlNode *root = document.GetRoot();
        if (!root || root->GetName() != "clipslicer-workspace")
            throw std::runtime_error("Archive is not a CLIP Slicer workspace");
        return document;
    }
    throw std::runtime_error("Archive has no CLIP Slicer workspace manifest");
}

bool HasZipMagic(const unsigned char *header, std::size_t count) {
    return count >= 4 && header[0] == 'P' && header[1] == 'K' &&
           ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
            (header[2] == 7 && header[3] == 8));
}

std::uint32_t LittleEndian32(const unsigned char *value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8) |
           (static_cast<std::uint32_t>(value[2]) << 16) |
           (static_cast<std::uint32_t>(value[3]) << 24);
}

InputFileFormat DetectBytes(const unsigned char *data, std::size_t size) {
    static constexpr char cliMagic[] = "$$HEADERSTART";
    if (size >= sizeof(cliMagic) - 1 &&
        std::memcmp(data, cliMagic, sizeof(cliMagic) - 1) == 0)
        return InputFileFormat::Cli;
    if (size >= 84) {
        const std::uint64_t triangles = LittleEndian32(data + 80);
        if (84ULL + triangles * 50ULL == size)
            return InputFileFormat::Stl;
    }
    return InputFileFormat::Unknown;
}

std::shared_ptr<stl_slicer::SceneModel>
LoadModelStream(InputFileFormat format, const std::string &bytes) {
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    if (format == InputFileFormat::Stl)
        return std::make_shared<stl_slicer::MeshSceneModel>(
            "source", stl_slicer::BinaryStlReader{}.read(input));
    if (format == InputFileFormat::Cli)
        return std::make_shared<stl_slicer::SliceSceneModel>(
            "source", stl_slicer::CliReader{}.read(input));
    throw std::runtime_error("Unsupported embedded model format");
}

std::shared_ptr<stl_slicer::SceneModel> LoadModelFile(const wxString &path) {
    const InputFileFormat format = DetectInputFileFormat(path);
    if (format == InputFileFormat::Stl)
        return std::make_shared<stl_slicer::MeshSceneModel>(
            "source", stl_slicer::BinaryStlReader{}.read(FileSystemPath(path)));
    if (format == InputFileFormat::Cli)
        return std::make_shared<stl_slicer::SliceSceneModel>(
            "source", stl_slicer::CliReader{}.read(FileSystemPath(path)));
    throw std::runtime_error("Linked file has an unsupported or invalid format: " + Utf8(path));
}

wxXmlNode *Element(const wxString &name) {
    return new wxXmlNode(wxXML_ELEMENT_NODE, name);
}

wxString Boolean(bool value) {
    return value ? "true" : "false";
}

bool ParseBoolean(const wxString &value, bool fallback = false) {
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    return fallback;
}

std::uint64_t ParseId(const wxString &value, const char *description) {
    unsigned long long result = 0;
    if (!value.ToULongLong(&result))
        throw std::runtime_error(std::string("Invalid workspace ") + description);
    return static_cast<std::uint64_t>(result);
}

stl_slicer::Mat4 ParseTransform(const wxXmlNode *model) {
    const wxXmlNode *transform = model->GetChildren();
    while (transform && transform->GetName() != "transform")
        transform = transform->GetNext();
    if (!transform)
        throw std::runtime_error("Workspace model has no transformation matrix");
    std::istringstream input(Utf8(transform->GetNodeContent()));
    std::array<double, 16> values{};
    for (double &value : values)
        if (!(input >> value))
            throw std::runtime_error("Workspace model has an invalid transformation matrix");
    std::string extra;
    if (input >> extra)
        throw std::runtime_error("Workspace transformation matrix has too many values");
    return stl_slicer::Mat4(values);
}

wxString TransformText(const stl_slicer::Mat4 &transform) {
    std::ostringstream output;
    output << std::setprecision(17);
    const auto &values = transform.values();
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index)
            output << ' ';
        output << values[index];
    }
    return FromUtf8(output.str());
}

wxString ExternalReference(const wxString &sourcePath, const wxString &workspacePath) {
    wxFileName source(AbsolutePath(sourcePath));
    wxFileName workspace(AbsolutePath(workspacePath));
    const wxString workspaceDirectory = workspace.GetPath();
    wxFileName relative(source);
    if (relative.MakeRelativeTo(workspaceDirectory)) {
        wxString value = relative.GetFullPath();
        const wxString parentPrefix = ".." + wxString(wxFILE_SEP_PATH);
        if (value != ".." && !value.StartsWith(parentPrefix)) {
            value.Replace("\\", "/");
            return value;
        }
    }
    return source.GetFullPath();
}

wxString ResolveReference(const wxString &location, const wxString &workspacePath) {
    wxFileName reference(location);
    if (reference.IsRelative()) {
        wxFileName workspace(AbsolutePath(workspacePath));
        reference.MakeAbsolute(workspace.GetPath());
    }
    return reference.GetFullPath();
}

struct SaveSource {
    wxString id;
    wxString kind;
    wxString location;
    wxString sourcePath;
    bool embedded = false;
    std::shared_ptr<stl_slicer::SceneModel> representative;
    std::string generatedBytes;
    std::uint64_t size = 0;
};

std::string SerializeGenerated(const stl_slicer::SceneModel &model) {
    std::ostringstream output(std::ios::out | std::ios::binary);
    if (model.isSliced()) {
        const stl_slicer::SliceData *slices = model.slices();
        if (!slices)
            throw std::runtime_error("Sliced model has no slice data");
        stl_slicer::CliWriter{}.write(*slices, output);
    } else {
        stl_slicer::BinaryStlWriter{}.write(model.triangleMesh(), output);
    }
    return output.str();
}

void WriteBytes(wxZipOutputStream &zip,
                const void *data,
                std::size_t size,
                std::uint64_t &completed,
                std::uint64_t total,
                const WorkspaceProgress &progress) {
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    constexpr std::size_t chunkSize = 1024 * 1024;
    while (size) {
        const std::size_t count = std::min(size, chunkSize);
        zip.Write(bytes, count);
        if (zip.GetLastError() != wxSTREAM_NO_ERROR)
            throw std::runtime_error("Unable to write workspace archive");
        bytes += count;
        size -= count;
        completed += count;
        if (progress)
            progress(completed, total);
    }
}

void WriteFile(wxZipOutputStream &zip,
               const wxString &path,
               std::uint64_t &completed,
               std::uint64_t total,
               const WorkspaceProgress &progress) {
    wxFFileInputStream input(path);
    if (!input.IsOk())
        throw std::runtime_error("Cannot read model file for embedding: " + Utf8(path));
    std::array<unsigned char, 1024 * 1024> buffer{};
    while (true) {
        input.Read(buffer.data(), buffer.size());
        const std::size_t count = input.LastRead();
        if (!count)
            break;
        WriteBytes(zip, buffer.data(), count, completed, total, progress);
    }
    if (input.GetLastError() != wxSTREAM_EOF && input.GetLastError() != wxSTREAM_NO_ERROR)
        throw std::runtime_error("Unable to read model file for embedding: " + Utf8(path));
}

struct SourceDescription {
    wxString id;
    wxString kind;
    wxString location;
    bool embedded = false;
};

struct ModelDescription {
    wxString name;
    wxString source;
    stl_slicer::Mat4 transform;
    bool visible = true;
    bool selected = false;
    std::uint64_t groupId = 0;
};

struct GroupDescription {
    std::uint64_t id = 0;
    std::string name;
    bool expanded = true;
};

void ParseModelNodes(const wxXmlNode *parent,
                     std::uint64_t groupId,
                     std::vector<ModelDescription> &models) {
    for (const wxXmlNode *node = parent->GetChildren(); node; node = node->GetNext()) {
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != "model")
            continue;
        ModelDescription model;
        model.name = node->GetAttribute("name");
        model.source = node->GetAttribute("source");
        if (model.source.empty())
            throw std::runtime_error("Workspace model has no source");
        model.transform = ParseTransform(node);
        model.visible = ParseBoolean(node->GetAttribute("visible", "true"), true);
        model.selected = ParseBoolean(node->GetAttribute("selected", "false"));
        model.groupId = groupId;
        models.push_back(std::move(model));
    }
}

} // namespace

InputFileFormat DetectInputFileFormat(const wxString &path) {
    wxFFileInputStream input(path);
    if (!input.IsOk())
        return InputFileFormat::Unknown;
    const wxFileOffset length = input.GetLength();
    std::array<unsigned char, 4096> header{};
    input.Read(header.data(), header.size());
    const std::size_t count = input.LastRead();
    if (HasZipMagic(header.data(), count)) {
        try {
            (void)ReadManifest(path);
            return InputFileFormat::Workspace;
        } catch (...) {
            return InputFileFormat::Unknown;
        }
    }
    static constexpr char cliMagic[] = "$$HEADERSTART";
    if (count >= sizeof(cliMagic) - 1 &&
        std::memcmp(header.data(), cliMagic, sizeof(cliMagic) - 1) == 0)
        return InputFileFormat::Cli;
    if (length >= 84 && count >= 84) {
        const std::uint64_t triangles = LittleEndian32(header.data() + 80);
        if (84ULL + triangles * 50ULL == static_cast<std::uint64_t>(length))
            return InputFileFormat::Stl;
    }
    return InputFileFormat::Unknown;
}

void SaveWorkspace(const wxString &path,
                   const std::vector<std::shared_ptr<stl_slicer::SceneModel>> &models,
                   const std::vector<DocumentModelGroup> &groups,
                   bool embedModelFiles,
                   const WorkspaceProgress &progress) {
    std::vector<SaveSource> sources;
    std::unordered_map<std::string, std::size_t> sourceIndices;
    std::unordered_map<const stl_slicer::SceneModel *, std::size_t> modelSources;

    for (const auto &model : models) {
        const wxString sourcePath = FromUtf8(model->sourcePath);
        std::ostringstream key;
        key << (model->isSliced() ? "cli:" : "stl:");
        if (!sourcePath.empty())
            key << Utf8(AbsolutePath(sourcePath));
        else
            key << "geometry@" << model->geometryIdentity();
        auto [entry, inserted] = sourceIndices.emplace(key.str(), sources.size());
        if (inserted) {
            SaveSource source;
            source.id = wxString::Format("source-%zu", sources.size() + 1);
            source.kind = model->isSliced() ? "cli" : "stl";
            source.sourcePath = sourcePath.empty() ? wxString() : AbsolutePath(sourcePath);
            source.embedded = embedModelFiles || source.sourcePath.empty();
            source.representative = model;
            if (source.embedded) {
                source.location =
                    wxString::Format("models/source-%04zu.%s",
                                     sources.size() + 1,
                                     source.kind);
                if (source.sourcePath.empty()) {
                    source.generatedBytes = SerializeGenerated(*model);
                    source.size = source.generatedBytes.size();
                } else {
                    wxFileName file(source.sourcePath);
                    if (!file.FileExists())
                        throw std::runtime_error("Cannot embed missing model file: " +
                                                 Utf8(source.sourcePath));
                    source.size = static_cast<std::uint64_t>(file.GetSize().GetValue());
                }
            } else {
                source.location = ExternalReference(source.sourcePath, path);
            }
            sources.push_back(std::move(source));
        }
        modelSources.emplace(model.get(), entry->second);
    }

    std::uint64_t total = 0;
    for (const SaveSource &source : sources)
        if (source.embedded)
            total += source.size;
    if (progress && total)
        progress(0, total);

    wxXmlDocument document;
    auto *root = Element("clipslicer-workspace");
    root->AddAttribute("version", "1");
    document.SetRoot(root);
    auto *sourceList = Element("sources");
    root->AddChild(sourceList);
    for (const SaveSource &source : sources) {
        auto *node = Element("source");
        node->AddAttribute("id", source.id);
        node->AddAttribute("kind", source.kind);
        node->AddAttribute("embedded", Boolean(source.embedded));
        node->AddAttribute("location", source.location);
        sourceList->AddChild(node);
    }

    auto *scene = Element("scene");
    root->AddChild(scene);
    const auto addModel = [&](wxXmlNode *parent,
                              const std::shared_ptr<stl_slicer::SceneModel> &model) {
        auto *node = Element("model");
        node->AddAttribute("name", FromUtf8(model->name));
        node->AddAttribute("source", sources.at(modelSources.at(model.get())).id);
        node->AddAttribute("visible", Boolean(model->visible));
        node->AddAttribute("selected", Boolean(model->selected));
        auto *transform = Element("transform");
        transform->AddChild(new wxXmlNode(wxXML_TEXT_NODE, {}, TransformText(model->transform)));
        node->AddChild(transform);
        parent->AddChild(node);
    };

    std::unordered_set<const stl_slicer::SceneModel *> grouped;
    for (const DocumentModelGroup &group : groups) {
        auto *groupNode = Element("group");
        groupNode->AddAttribute("id", wxString::Format("%llu",
                                                        static_cast<unsigned long long>(group.id)));
        groupNode->AddAttribute("name", FromUtf8(group.name));
        groupNode->AddAttribute("expanded", Boolean(group.expanded));
        for (const stl_slicer::SceneModel *member : group.members) {
            const auto model = std::find_if(models.begin(), models.end(), [member](const auto &item) {
                return item.get() == member;
            });
            if (model == models.end())
                continue;
            addModel(groupNode, *model);
            grouped.insert(member);
        }
        scene->AddChild(groupNode);
    }
    for (const auto &model : models)
        if (!grouped.count(model.get()))
            addModel(scene, model);

    wxTempFileOutputStream output(path);
    if (!output.IsOk())
        throw std::runtime_error("Cannot create workspace: " + Utf8(path));
    {
        wxZipOutputStream zip(output, 6);
        zip.SetComment("CLIP Slicer workspace");
        if (!zip.PutNextEntry(manifestName) || !document.Save(zip) || !zip.CloseEntry())
            throw std::runtime_error("Unable to write workspace XML manifest");

        std::uint64_t completed = 0;
        for (const SaveSource &source : sources) {
            if (!source.embedded)
                continue;
            if (!zip.PutNextEntry(source.location, wxDateTime::Now(), source.size))
                throw std::runtime_error("Unable to create embedded model entry");
            if (source.sourcePath.empty())
                WriteBytes(zip,
                           source.generatedBytes.data(),
                           source.generatedBytes.size(),
                           completed,
                           total,
                           progress);
            else
                WriteFile(zip, source.sourcePath, completed, total, progress);
            if (!zip.CloseEntry())
                throw std::runtime_error("Unable to finish embedded model entry");
        }
        if (!zip.Close())
            throw std::runtime_error("Unable to finish workspace archive");
    }
    if (!output.Commit())
        throw std::runtime_error("Unable to replace workspace file: " + Utf8(path));
    if (progress && total)
        progress(total, total);
}

WorkspaceData LoadWorkspace(const wxString &path) {
    const wxXmlDocument document = ReadManifest(path);
    const wxXmlNode *root = document.GetRoot();
    const wxString version = root->GetAttribute("version");
    if (version != "1")
        throw std::runtime_error("Unsupported workspace version: " + Utf8(version));

    std::vector<SourceDescription> sources;
    const wxXmlNode *sourceList = root->GetChildren();
    while (sourceList && sourceList->GetName() != "sources")
        sourceList = sourceList->GetNext();
    if (!sourceList)
        throw std::runtime_error("Workspace has no source list");
    std::unordered_set<std::string> sourceIds;
    for (const wxXmlNode *node = sourceList->GetChildren(); node; node = node->GetNext()) {
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != "source")
            continue;
        SourceDescription source;
        source.id = node->GetAttribute("id");
        source.kind = node->GetAttribute("kind");
        source.location = node->GetAttribute("location");
        source.embedded = ParseBoolean(node->GetAttribute("embedded"));
        if (source.id.empty() || source.location.empty() ||
            (source.kind != "stl" && source.kind != "cli"))
            throw std::runtime_error("Workspace contains an invalid source entry");
        if (!sourceIds.insert(Utf8(source.id)).second)
            throw std::runtime_error("Workspace contains duplicate source identifiers");
        sources.push_back(std::move(source));
    }

    std::vector<ModelDescription> modelDescriptions;
    std::vector<GroupDescription> groupDescriptions;
    const wxXmlNode *scene = root->GetChildren();
    while (scene && scene->GetName() != "scene")
        scene = scene->GetNext();
    if (!scene)
        throw std::runtime_error("Workspace has no scene");
    ParseModelNodes(scene, 0, modelDescriptions);
    for (const wxXmlNode *node = scene->GetChildren(); node; node = node->GetNext()) {
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != "group")
            continue;
        GroupDescription group;
        group.id = ParseId(node->GetAttribute("id"), "group identifier");
        group.name = Utf8(node->GetAttribute("name"));
        group.expanded = ParseBoolean(node->GetAttribute("expanded", "true"), true);
        groupDescriptions.push_back(group);
        ParseModelNodes(node, group.id, modelDescriptions);
    }

    std::unordered_map<std::string, std::shared_ptr<stl_slicer::SceneModel>> loadedSources;
    WorkspaceData result;
    std::unordered_map<std::string, SourceDescription> embeddedByLocation;
    std::unordered_set<std::string> missingPaths;
    for (const SourceDescription &source : sources) {
        if (source.embedded) {
            embeddedByLocation.emplace(Utf8(source.location), source);
            continue;
        }
        const wxString resolved = ResolveReference(source.location, path);
        if (!wxFileName::FileExists(resolved)) {
            if (missingPaths.insert(Utf8(resolved)).second)
                result.missingExternalFiles.push_back(resolved);
            continue;
        }
        auto model = LoadModelFile(resolved);
        model->sourcePath = Utf8(AbsolutePath(resolved));
        loadedSources.emplace(Utf8(source.id), std::move(model));
    }

    if (!embeddedByLocation.empty()) {
        wxFFileInputStream file(path);
        wxZipInputStream zip(file);
        while (std::unique_ptr<wxZipEntry> entry{zip.GetNextEntry()}) {
            const auto wanted = embeddedByLocation.find(Utf8(entry->GetName()));
            if (wanted == embeddedByLocation.end())
                continue;
            const std::vector<unsigned char> bytes = ReadCurrentEntry(zip);
            const InputFileFormat detected = DetectBytes(bytes.data(), bytes.size());
            const InputFileFormat expected =
                wanted->second.kind == "stl" ? InputFileFormat::Stl : InputFileFormat::Cli;
            if (detected != expected)
                throw std::runtime_error("Embedded model content does not match its workspace type");
            std::string content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            loadedSources.emplace(Utf8(wanted->second.id), LoadModelStream(expected, content));
            embeddedByLocation.erase(wanted);
        }
        if (!embeddedByLocation.empty())
            throw std::runtime_error("Workspace is missing one or more embedded model entries");
    }

    std::unordered_map<std::uint64_t, std::size_t> groupIndices;
    for (const GroupDescription &description : groupDescriptions) {
        DocumentModelGroup group;
        group.id = description.id;
        group.name = description.name;
        group.expanded = description.expanded;
        groupIndices.emplace(group.id, result.groups.size());
        result.groups.push_back(std::move(group));
    }

    for (const ModelDescription &description : modelDescriptions) {
        const auto source = loadedSources.find(Utf8(description.source));
        if (source == loadedSources.end())
            continue;
        auto model = source->second->replica(Utf8(description.name));
        model->transform = description.transform;
        model->visible = description.visible;
        model->selected = description.selected;
        result.models.push_back(model);
        if (description.groupId) {
            const auto group = groupIndices.find(description.groupId);
            if (group == groupIndices.end())
                throw std::runtime_error("Workspace model refers to an unknown group");
            result.groups[group->second].members.push_back(model.get());
        }
    }
    result.groups.erase(
        std::remove_if(result.groups.begin(),
                       result.groups.end(),
                       [](const DocumentModelGroup &group) { return group.members.empty(); }),
        result.groups.end());
    return result;
}

} // namespace clip_slicer
