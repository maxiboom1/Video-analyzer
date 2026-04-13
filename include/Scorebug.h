#pragma once

#include "Templates.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

struct AppState;

enum class ScorebugFieldType
{
    Text = 0,
    Integer = 1,
    PeriodText = 2,
    Clock = 3,
    DecimalClock = 4
};

struct ScorebugFieldDefinition
{
    NormalizedRoi roi;
    ScorebugFieldType type = ScorebugFieldType::Text;
    std::string whitelist;
    std::string preprocess = "auto";
};

struct ScorebugLayoutManifest
{
    std::string name;
    int version = 1;
    std::string folderName;
    std::string folderPath;
    std::string referenceImagePath = "reference.png";
    NormalizedRoi frameRoi;

    ScorebugFieldDefinition teamALabel;
    ScorebugFieldDefinition teamAScore;
    ScorebugFieldDefinition teamBLabel;
    ScorebugFieldDefinition teamBScore;
    ScorebugFieldDefinition period;
    ScorebugFieldDefinition gameClock;
    ScorebugFieldDefinition shotClock;

    std::string createdAt;
    std::string updatedAt;
};

struct ScorebugFieldResult
{
    bool valid = false;
    std::string value;
    double confidence = 0.0;
};

struct ScorebugState
{
    bool detected = false;
    std::string layoutName;
    ScorebugFieldResult teamALabel;
    ScorebugFieldResult teamAScore;
    ScorebugFieldResult teamBLabel;
    ScorebugFieldResult teamBScore;
    ScorebugFieldResult period;
    ScorebugFieldResult gameClock;
    ScorebugFieldResult shotClock;
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
const ScorebugLayoutManifest* Scorebug_FindLayoutByName(const AppState& state, const std::string& name);
ScorebugLayoutManifest* Scorebug_FindLayoutByName(AppState& state, const std::string& name);
bool Scorebug_SaveLayout(
    AppState& state,
    const ScorebugLayoutManifest& manifest,
    const std::string& sourceReferenceImagePath,
    const std::string& originalName,
    std::string& error);
bool Scorebug_DeleteLayout(AppState& state, const std::string& name, std::string& error);
void Scorebug_ProcessFrame(const cv::Mat& bgrFrame, AppState& state);
void Scorebug_Shutdown();
ScorebugState Scorebug_GetLastState();
ScorebugSubmissionStatus Scorebug_GetStatus();
