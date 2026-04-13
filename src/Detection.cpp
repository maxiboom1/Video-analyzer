#include "Detection.h"

#include "Logger.h"
#include "Templates.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace
{
    cv::Rect RoiToWorkRect(const NormalizedRoi& roi)
    {
        if (!roi.enabled)
            return cv::Rect(0, 0, WORK_W, WORK_H);

        const int x = std::clamp(static_cast<int>(roi.x * WORK_W + 0.5f), 0, std::max(0, WORK_W - 1));
        const int y = std::clamp(static_cast<int>(roi.y * WORK_H + 0.5f), 0, std::max(0, WORK_H - 1));
        const int w = std::clamp(static_cast<int>(roi.w * WORK_W + 0.5f), 1, WORK_W - x);
        const int h = std::clamp(static_cast<int>(roi.h * WORK_H + 0.5f), 1, WORK_H - y);
        return cv::Rect(x, y, w, h);
    }

    void ClearActiveTemplate(AppState& state)
    {
        state.activeTemplateLoaded = false;
        state.activeTemplateFolder.clear();
        state.activeTemplateName.clear();
        state.activeInRoi = {};
        state.activeOutRoi = {};
        state.tmplIn.release();
        state.tmplOut.release();
        state.tmplInRaw.release();
        state.tmplOutRaw.release();
        state.tmplInRect = cv::Rect();
        state.tmplOutRect = cv::Rect();
        state.tmplWorkWidth = 0;
        state.tmplWorkHeight = 0;
        state.lastScore = 0.0;
    }

    const cv::Rect& ActiveTemplateRect(const AppState& state)
    {
        return (state.cueState == CueState::WIPER_IN) ? state.tmplInRect : state.tmplOutRect;
    }
}

bool Detection_LoadTemplateCatalog(AppState& state)
{
    Templates_ScanCatalog(state);
    if (state.templates.empty())
    {
        ClearActiveTemplate(state);
        AddLog(CurrentTimestamp() + " | WARNING: no templates found in " + Templates_GetRootDirectory());
        return false;
    }

    if (state.activeTemplateName.empty() || !Templates_FindByName(state, state.activeTemplateName))
    {
        state.activeTemplateName = state.templates.front().name;
        AddLog(CurrentTimestamp() + " | Active template fallback: " + state.activeTemplateName);
    }

    return Detection_LoadActiveTemplate(state);
}

bool Detection_LoadActiveTemplate(AppState& state)
{
    if (state.templates.empty())
    {
        ClearActiveTemplate(state);
        return false;
    }

    const TemplateManifest* manifest = Templates_FindByName(state, state.activeTemplateName);
    if (!manifest)
    {
        state.activeTemplateName = state.templates.front().name;
        manifest = Templates_FindByName(state, state.activeTemplateName);
    }

    if (!manifest)
    {
        ClearActiveTemplate(state);
        return false;
    }

    const std::string inPath = manifest->folderPath + "\\" + manifest->inImagePath;
    const std::string outPath = manifest->folderPath + "\\" + manifest->outImagePath;

    const cv::Mat inRaw = cv::imread(inPath, cv::IMREAD_GRAYSCALE);
    const cv::Mat outRaw = cv::imread(outPath, cv::IMREAD_GRAYSCALE);
    if (inRaw.empty() || outRaw.empty())
    {
        ClearActiveTemplate(state);
        AddLog(CurrentTimestamp() + " | ERROR: failed to load active template assets for " + manifest->name);
        return false;
    }

    state.activeTemplateFolder = manifest->folderPath;
    state.activeInRoi = manifest->inRoi;
    state.activeOutRoi = manifest->outRoi;
    state.tmplInRaw = inRaw;
    state.tmplOutRaw = outRaw;
    state.activeTemplateLoaded = true;
    Detection_RebuildRuntimeTemplateAssets(state);

    AddLog(CurrentTimestamp() + " | Active template loaded: " + state.activeTemplateName);
    return !state.tmplIn.empty() && !state.tmplOut.empty();
}

bool Detection_SetActiveTemplate(AppState& state, const std::string& name)
{
    if (name.empty())
    {
        ClearActiveTemplate(state);
        return false;
    }

    if (!Templates_FindByName(state, name))
    {
        AddLog(CurrentTimestamp() + " | WARNING: requested template not found: " + name);
        return false;
    }

    state.activeTemplateName = name;
    return Detection_LoadActiveTemplate(state);
}

void Detection_RebuildRuntimeTemplateAssets(AppState& state)
{
    if (state.tmplInRaw.empty() || state.tmplOutRaw.empty())
    {
        state.tmplIn.release();
        state.tmplOut.release();
        state.tmplInRect = cv::Rect();
        state.tmplOutRect = cv::Rect();
        state.tmplWorkWidth = 0;
        state.tmplWorkHeight = 0;
        return;
    }

    cv::Mat inResized;
    cv::Mat outResized;
    cv::resize(state.tmplInRaw, inResized, cv::Size(WORK_W, WORK_H));
    cv::resize(state.tmplOutRaw, outResized, cv::Size(WORK_W, WORK_H));

    state.tmplInRect = RoiToWorkRect(state.activeInRoi);
    state.tmplOutRect = RoiToWorkRect(state.activeOutRoi);
    state.tmplIn = inResized(state.tmplInRect).clone();
    state.tmplOut = outResized(state.tmplOutRect).clone();
    state.tmplWorkWidth = WORK_W;
    state.tmplWorkHeight = WORK_H;

    AddLog(CurrentTimestamp() + " | Active template assets rebuilt for " +
           std::to_string(WORK_W) + "x" + std::to_string(WORK_H));
}

const cv::Mat& Detection_ActiveTemplate(const AppState& state)
{
    return (state.cueState == CueState::WIPER_IN) ? state.tmplIn : state.tmplOut;
}

bool Detection_ProcessFrame(const cv::Mat& grayResized, AppState& state)
{
    if (!state.activeTemplateLoaded || grayResized.empty())
        return false;

    if (state.tmplWorkWidth != WORK_W || state.tmplWorkHeight != WORK_H)
    {
        Detection_RebuildRuntimeTemplateAssets(state);
        return false;
    }

    const cv::Mat& tmpl = Detection_ActiveTemplate(state);
    const cv::Rect& roi = ActiveTemplateRect(state);
    if (tmpl.empty() || roi.width <= 0 || roi.height <= 0)
        return false;

    const cv::Rect frameBounds(0, 0, grayResized.cols, grayResized.rows);
    const cv::Rect clipped = roi & frameBounds;
    if (clipped.width != roi.width || clipped.height != roi.height)
        return false;

    cv::Mat frameRoi = grayResized(roi);
    if (frameRoi.size() != tmpl.size())
        return false;

    cv::Mat result;
    cv::matchTemplate(frameRoi, tmpl, result, cv::TM_CCOEFF_NORMED);

    double maxVal = 0.0;
    cv::minMaxLoc(result, nullptr, &maxVal);
    state.lastScore = maxVal;

    auto now = std::chrono::steady_clock::now();
    if (state.detectionState == DetectionState::COOLDOWN)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.lastDetectionTime).count();
        if (elapsed >= state.cooldownMs)
            state.detectionState = DetectionState::IDLE;
        else
            return false;
    }

    if (state.detectionState == DetectionState::DETECTED && maxVal < state.resetThreshold)
        state.detectionState = DetectionState::IDLE;

    if (!state.detectionEnabled)
        return false;

    if (state.detectionState == DetectionState::IDLE && maxVal >= state.detectThreshold)
    {
        state.detectionState = DetectionState::DETECTED;
        state.lastDetectionTime = now;

        std::ostringstream oss;
        oss << CurrentTimestamp()
            << " | DETECTED ["
            << (state.cueState == CueState::WIPER_IN ? "WIPER IN" : "WIPER OUT")
            << "] Template: " << state.activeTemplateName
            << " Score: " << std::fixed << std::setprecision(3) << maxVal;
        AddLog(oss.str());

        Detection_FlipCue(state);
        state.detectionState = DetectionState::COOLDOWN;
        return true;
    }

    return false;
}

void Detection_FlipCue(AppState& state)
{
    state.cueState = (state.cueState == CueState::WIPER_IN)
                   ? CueState::WIPER_OUT
                   : CueState::WIPER_IN;

    AddLog(CurrentTimestamp() + " | Cue changed to: " +
           (state.cueState == CueState::WIPER_IN ? "WIPER IN" : "WIPER OUT"));
}
