#pragma once

#include <string>
#include <vector>

struct AppState;

struct NormalizedRoi
{
    bool enabled = false;
    float x = 0.0f;
    float y = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
};

struct TemplateManifest
{
    std::string name;
    int version = 1;
    std::string folderName;
    std::string folderPath;
    std::string inImagePath = "in.png";
    std::string outImagePath = "out.png";
    NormalizedRoi inRoi;
    NormalizedRoi outRoi;
    std::string createdAt;
    std::string updatedAt;
};

std::string Templates_GetRootDirectory();
std::string Templates_SanitizeName(const std::string& name);
bool Templates_ScanCatalog(AppState& state);
bool Templates_SaveTemplate(
    AppState& state,
    const TemplateManifest& manifest,
    const std::string& sourceInImagePath,
    const std::string& sourceOutImagePath,
    const std::string& originalName,
    std::string& error);
bool Templates_DeleteTemplate(AppState& state, const std::string& name, std::string& error);
const TemplateManifest* Templates_FindByName(const AppState& state, const std::string& name);
TemplateManifest* Templates_FindByName(AppState& state, const std::string& name);
