#include "Templates.h"

#include "AppState.h"
#include "Logger.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <opencv2/opencv.hpp>

namespace
{
    namespace fs = std::filesystem;

    std::string GetExeDir()
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string path(exePath);
        const size_t pos = path.find_last_of("\\/");
        return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
    }

    std::string Trim(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string CurrentIsoTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buffer[64] = {};
        sprintf_s(
            buffer,
            "%04u-%02u-%02uT%02u:%02u:%02u",
            st.wYear,
            st.wMonth,
            st.wDay,
            st.wHour,
            st.wMinute,
            st.wSecond);
        return buffer;
    }

    bool IsFullFrame(const NormalizedRoi& roi)
    {
        return !roi.enabled ||
            (roi.x <= 0.0f && roi.y <= 0.0f && roi.w >= 0.999f && roi.h >= 0.999f);
    }

    void WriteRoi(cv::FileStorage& fs, const char* key, const NormalizedRoi& roi)
    {
        fs << key << "{";
        fs << "enabled" << (!IsFullFrame(roi));
        fs << "x" << roi.x;
        fs << "y" << roi.y;
        fs << "w" << roi.w;
        fs << "h" << roi.h;
        fs << "}";
    }

    NormalizedRoi ReadRoi(const cv::FileNode& node)
    {
        NormalizedRoi roi;
        if (node.empty())
            return roi;

        const int enabled = static_cast<int>(node["enabled"]);
        roi.enabled = enabled != 0;
        roi.x = static_cast<float>((double)node["x"]);
        roi.y = static_cast<float>((double)node["y"]);
        roi.w = static_cast<float>((double)node["w"]);
        roi.h = static_cast<float>((double)node["h"]);
        if (roi.w <= 0.0f || roi.h <= 0.0f)
            roi = {};
        return roi;
    }

    bool LoadManifest(const fs::path& jsonPath, TemplateManifest& manifest, std::string& error)
    {
        cv::FileStorage fs(jsonPath.string(), cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
        if (!fs.isOpened())
        {
            error = "unable to open template.json";
            return false;
        }

        manifest.name = static_cast<std::string>(fs["name"]);
        manifest.version = fs["version"].empty() ? 1 : static_cast<int>(fs["version"]);
        manifest.inImagePath = fs["in_image"].empty() ? "in.png" : static_cast<std::string>(fs["in_image"]);
        manifest.outImagePath = fs["out_image"].empty() ? "out.png" : static_cast<std::string>(fs["out_image"]);
        manifest.inRoi = ReadRoi(fs["in_roi"]);
        manifest.outRoi = ReadRoi(fs["out_roi"]);
        manifest.createdAt = fs["created_at"].empty() ? "" : static_cast<std::string>(fs["created_at"]);
        manifest.updatedAt = fs["updated_at"].empty() ? "" : static_cast<std::string>(fs["updated_at"]);
        manifest.folderPath = jsonPath.parent_path().string();
        manifest.folderName = jsonPath.parent_path().filename().string();

        if (Trim(manifest.name).empty())
        {
            error = "missing template name";
            return false;
        }

        if (!fs::exists(jsonPath.parent_path() / manifest.inImagePath) ||
            !fs::exists(jsonPath.parent_path() / manifest.outImagePath))
        {
            error = "missing in/out image file";
            return false;
        }

        return true;
    }

    bool SaveManifest(const TemplateManifest& manifest, std::string& error)
    {
        const fs::path jsonPath = fs::path(manifest.folderPath) / "template.json";
        cv::FileStorage fs(jsonPath.string(), cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);
        if (!fs.isOpened())
        {
            error = "unable to write template.json";
            return false;
        }

        fs << "name" << manifest.name;
        fs << "version" << manifest.version;
        fs << "in_image" << manifest.inImagePath;
        fs << "out_image" << manifest.outImagePath;
        WriteRoi(fs, "in_roi", manifest.inRoi);
        WriteRoi(fs, "out_roi", manifest.outRoi);
        fs << "created_at" << manifest.createdAt;
        fs << "updated_at" << manifest.updatedAt;
        return true;
    }
}

std::string Templates_GetRootDirectory()
{
    return GetExeDir() + "templates";
}

std::string Templates_SanitizeName(const std::string& name)
{
    std::string cleaned = Trim(name);
    for (char& ch : cleaned)
    {
        const bool valid =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == ' ';
        if (!valid)
            ch = '_';
    }

    while (!cleaned.empty() && cleaned.back() == ' ')
        cleaned.pop_back();
    while (!cleaned.empty() && cleaned.front() == ' ')
        cleaned.erase(cleaned.begin());

    for (char& ch : cleaned)
        if (ch == ' ')
            ch = '_';
    return cleaned;
}

const TemplateManifest* Templates_FindByName(const AppState& state, const std::string& name)
{
    for (const auto& manifest : state.templates)
    {
        if (manifest.name == name)
            return &manifest;
    }
    return nullptr;
}

TemplateManifest* Templates_FindByName(AppState& state, const std::string& name)
{
    for (auto& manifest : state.templates)
    {
        if (manifest.name == name)
            return &manifest;
    }
    return nullptr;
}

bool Templates_ScanCatalog(AppState& state)
{
    state.templates.clear();

    const fs::path root(Templates_GetRootDirectory());
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec)
    {
        AddLog(CurrentTimestamp() + " | ERROR: unable to create templates directory: " + root.string());
        return false;
    }

    for (const auto& entry : fs::directory_iterator(root, ec))
    {
        if (ec || !entry.is_directory())
            continue;

        const fs::path jsonPath = entry.path() / "template.json";
        if (!fs::exists(jsonPath))
            continue;

        TemplateManifest manifest;
        std::string error;
        if (LoadManifest(jsonPath, manifest, error))
        {
            state.templates.push_back(manifest);
        }
        else
        {
            AddLog(CurrentTimestamp() + " | WARNING: skipped template '" + entry.path().filename().string() + "' - " + error);
        }
    }

    std::sort(
        state.templates.begin(),
        state.templates.end(),
        [](const TemplateManifest& lhs, const TemplateManifest& rhs)
        {
            return _stricmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
        });

    AddLog(CurrentTimestamp() + " | Templates scanned: " + std::to_string(state.templates.size()));
    return !state.templates.empty();
}

bool Templates_SaveTemplate(
    AppState& state,
    const TemplateManifest& manifest,
    const std::string& sourceInImagePath,
    const std::string& sourceOutImagePath,
    const std::string& originalName,
    std::string& error)
{
    error.clear();

    const std::string trimmedName = Trim(manifest.name);
    if (trimmedName.empty())
    {
        error = "Template name is required.";
        return false;
    }

    const cv::Mat inImage = cv::imread(sourceInImagePath, cv::IMREAD_COLOR);
    if (inImage.empty())
    {
        error = "Unable to load IN image.";
        return false;
    }

    const cv::Mat outImage = cv::imread(sourceOutImagePath, cv::IMREAD_COLOR);
    if (outImage.empty())
    {
        error = "Unable to load OUT image.";
        return false;
    }

    const fs::path root(Templates_GetRootDirectory());
    fs::create_directories(root);

    const std::string folderName = Templates_SanitizeName(trimmedName);
    if (folderName.empty())
    {
        error = "Template name does not produce a valid folder name.";
        return false;
    }

    fs::path currentFolder;
    std::string createdAt = CurrentIsoTimestamp();
    if (!originalName.empty())
    {
        if (const TemplateManifest* existing = Templates_FindByName(state, originalName))
        {
            currentFolder = existing->folderPath;
            createdAt = existing->createdAt.empty() ? createdAt : existing->createdAt;
        }
    }

    const fs::path targetFolder = root / folderName;
    if (!currentFolder.empty() && fs::equivalent(currentFolder, targetFolder))
    {
        // no-op
    }
    else if (fs::exists(targetFolder))
    {
        error = "A template folder with this name already exists.";
        return false;
    }

    if (!currentFolder.empty() && currentFolder != targetFolder)
    {
        fs::rename(currentFolder, targetFolder);
    }

    fs::create_directories(targetFolder);

    const fs::path inPath = targetFolder / "in.png";
    const fs::path outPath = targetFolder / "out.png";
    if (!cv::imwrite(inPath.string(), inImage))
    {
        error = "Failed to save IN image.";
        return false;
    }
    if (!cv::imwrite(outPath.string(), outImage))
    {
        error = "Failed to save OUT image.";
        return false;
    }

    TemplateManifest persisted = manifest;
    persisted.name = trimmedName;
    persisted.folderName = folderName;
    persisted.folderPath = targetFolder.string();
    persisted.inImagePath = "in.png";
    persisted.outImagePath = "out.png";
    persisted.createdAt = createdAt;
    persisted.updatedAt = CurrentIsoTimestamp();

    if (!SaveManifest(persisted, error))
        return false;

    Templates_ScanCatalog(state);
    AddLog(CurrentTimestamp() + " | Template saved: " + persisted.name);
    return true;
}

bool Templates_DeleteTemplate(AppState& state, const std::string& name, std::string& error)
{
    error.clear();
    const TemplateManifest* manifest = Templates_FindByName(state, name);
    if (!manifest)
    {
        error = "Template not found.";
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(manifest->folderPath, ec);
    if (ec)
    {
        error = "Unable to delete template folder.";
        return false;
    }

    Templates_ScanCatalog(state);
    AddLog(CurrentTimestamp() + " | Template deleted: " + name);
    return true;
}
