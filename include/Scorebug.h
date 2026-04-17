#pragma once

#include "Templates.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

struct AppState;

enum class OcrPropType
{
    Number = 0,
    Text = 1,
    Auto = 2
};

struct OcrPropManifest
{
    std::string name;
    NormalizedRoi roi;
    OcrPropType type = OcrPropType::Auto;
};

struct OcrElementManifest
{
    std::string name;
    int version = 1;
    std::string folderName;
    std::string folderPath;
    std::string referenceImagePath = "reference.png";
    NormalizedRoi frameRoi;
    std::vector<OcrPropManifest> props;
    std::string createdAt;
    std::string updatedAt;
};

struct OcrPropResult
{
    std::string name;
    std::string value;
    double confidence = 0.0;
    bool valid = false;
    OcrPropType type = OcrPropType::Auto;
};

struct OcrElementState
{
    bool detected = false;
    std::string elementName;
    std::vector<OcrPropResult> props;
    std::string publishedAtIso;
};

struct ScorebugSubmissionStatus
{
    bool tesseractAvailable = false;
    std::string tesseractPath;
    std::string lastError;
};

std::string Scorebug_GetRootDirectory();
bool Scorebug_LoadLayoutCatalog(AppState& state);
bool Scorebug_SetActiveLayout(AppState& state, const std::string& name);
const OcrElementManifest* Scorebug_FindLayoutByName(const AppState& state, const std::string& name);
OcrElementManifest* Scorebug_FindLayoutByName(AppState& state, const std::string& name);
bool Scorebug_SaveLayout(
    AppState& state,
    const OcrElementManifest& manifest,
    const std::string& sourceReferenceImagePath,
    const std::string& originalName,
    std::string& error);
bool Scorebug_DeleteLayout(AppState& state, const std::string& name, std::string& error);
bool Scorebug_SaveProp(
    AppState& state,
    const std::string& elementName,
    const OcrPropManifest& prop,
    const std::string& originalPropName,
    std::string& error);
bool Scorebug_DeleteProp(
    AppState& state,
    const std::string& elementName,
    const std::string& propName,
    std::string& error);
void Scorebug_ProcessFrame(const cv::Mat& bgrFrame, AppState& state);
void Scorebug_Shutdown();
OcrElementState Scorebug_GetLastState();
ScorebugSubmissionStatus Scorebug_GetStatus();
std::string Scorebug_BuildStateJson(const OcrElementState& state);
