#pragma once
#include <opencv2/opencv.hpp>
#include <chrono>
#include <string>
#include <vector>
#include "Scorebug.h"
#include "Templates.h"

// -----------------------------------------------------------------------
// Fixed working size for template matching.
// -----------------------------------------------------------------------


extern int WORK_W;
extern int WORK_H;

// -----------------------------------------------------------------------
// Detection state machine
// -----------------------------------------------------------------------
enum class CueState { WIPER_IN, WIPER_OUT };
enum class DetectionState { IDLE, DETECTED, COOLDOWN };
enum class VideoSourceKind { Webcam = 0, Blackmagic = 1 };

struct VideoDeviceInfo
{
    VideoSourceKind kind = VideoSourceKind::Webcam;
    int             id = -1;
    std::string     uniqueId;
    std::string     displayName;
};

// -----------------------------------------------------------------------
// All runtime state in one place.
// Passed by reference to every subsystem - no scattered globals.
// -----------------------------------------------------------------------
struct AppState
{
    // --- Selected source ---
    VideoSourceKind selectedSourceKind = VideoSourceKind::Webcam;
    int             cameraIndex = 0;            // webcam index OR blackmagic device id
    int             currentCamera = -1;         // currently opened device id
    VideoSourceKind currentSourceKind = VideoSourceKind::Webcam;
    int             selectedDeviceListIndex = -1;
    std::vector<VideoDeviceInfo> availableDevices;
    bool            deviceListDirty = true;

    // --- Frame info ---
    int     frameWidth = 0;
    int     frameHeight = 0;

    // --- Blackmagic capture defaults ---
    int     blackmagicDisplayMode = 0;          // 0 = 1080i50 POC default

    // --- UI toggles ---
    bool    previewEnabled = false;
    bool    detectionEnabled = true;
    bool    autoScrollLog = true;

    // --- Detection thresholds ---
    float   detectThreshold = 0.75f;
    float   resetThreshold = 0.30f;
    int     cooldownMs = 2000;

    // --- Detection runtime ---
    DetectionState detectionState = DetectionState::IDLE;
    double  lastScore = 0.0;
    std::chrono::steady_clock::time_point lastDetectionTime{};

    // --- Cue ---
    CueState cueState = CueState::WIPER_IN;

    // --- Template catalog / selection ---
    std::vector<TemplateManifest> templates;
    std::string activeTemplateName;
    std::string activeTemplateFolder;
    NormalizedRoi activeInRoi;
    NormalizedRoi activeOutRoi;
    bool activeTemplateLoaded = false;

    // --- Runtime template assets ---
    cv::Mat tmplIn;
    cv::Mat tmplOut;

    // Raw originals - kept in memory so templates can be re-resized
    // when DeckLink format detection changes WORK_W/H at runtime
    cv::Mat tmplInRaw;
    cv::Mat tmplOutRaw;
    cv::Rect tmplInRect{ 0, 0, 0, 0 };
    cv::Rect tmplOutRect{ 0, 0, 0, 0 };
    int tmplWorkWidth = 0;
    int tmplWorkHeight = 0;

    // --- Viz config ---
    char    vizIp[64] = "127.0.0.1";
    int     vizPort = 6100;
    char    cmdOn[256] = "RENDERER*MAIN_LAYER*STAGE*DIRECTOR*GFX_ON GOTO_TRIO $O $A";
    char    cmdOff[256] = "RENDERER*MAIN_LAYER*STAGE*DIRECTOR*GFX_OFF GOTO_TRIO $O $A";

    // --- Last Viz send result ---
    bool    lastVizOk = true;
    std::string lastVizMsg;

    // --- Scorebug OCR ---
    std::vector<ScorebugLayoutManifest> scorebugLayouts;
    std::string activeScorebugLayoutName;
    ScorebugState lastScorebugState;
    bool scorebugOcrEnabled = true;
    float scorebugDetectThreshold = 0.70f;
    double lastScorebugPresenceScore = 0.0;
    bool scorebugOnAir = false;
};
