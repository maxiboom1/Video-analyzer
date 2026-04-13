#include "Detection.h"
#include "Logger.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
#include "Config.h"

static std::string GetExeDir()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path(exePath);
    size_t pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

bool Detection_LoadTemplates(AppState& state)
{
    std::string dir = GetExeDir();
    bool ok = false;

    // --- Load wiper_in ---
    cv::Mat rawIn = cv::imread(dir + "wiper_in.png", cv::IMREAD_GRAYSCALE);
    if (rawIn.empty())
    {
        AddLog(CurrentTimestamp() + " | WARNING: wiper_in.png not found in " + dir);
    }
    else
    {
        state.tmplInRaw = rawIn;  // keep original for re-resize on resolution change
        cv::resize(rawIn, state.tmplIn, cv::Size(WORK_W, WORK_H));
        AddLog(CurrentTimestamp() + " | Template loaded: wiper_in.png (" +
               std::to_string(rawIn.cols) + "x" + std::to_string(rawIn.rows) + ")");
        ok = true;
    }

    // --- Load wiper_out ---
    cv::Mat rawOut = cv::imread(dir + "wiper_out.png", cv::IMREAD_GRAYSCALE);
    if (rawOut.empty())
    {
        AddLog(CurrentTimestamp() + " | WARNING: wiper_out.png not found in " + dir);
    }
    else
    {
        state.tmplOutRaw = rawOut;  // keep original for re-resize on resolution change
        cv::resize(rawOut, state.tmplOut, cv::Size(WORK_W, WORK_H));
        AddLog(CurrentTimestamp() + " | Template loaded: wiper_out.png (" +
               std::to_string(rawOut.cols) + "x" + std::to_string(rawOut.rows) + ")");
        ok = true;
    }

    return ok;
}

void Detection_ResizeTemplates(AppState& state)
{
    if (!state.tmplInRaw.empty())
        cv::resize(state.tmplInRaw, state.tmplIn, cv::Size(WORK_W, WORK_H));

    if (!state.tmplOutRaw.empty())
        cv::resize(state.tmplOutRaw, state.tmplOut, cv::Size(WORK_W, WORK_H));

    AddLog(CurrentTimestamp() + " | Templates resized to " +
           std::to_string(WORK_W) + "x" + std::to_string(WORK_H));
}

const cv::Mat& Detection_ActiveTemplate(const AppState& state)
{
    return (state.cueState == CueState::WIPER_IN) ? state.tmplIn : state.tmplOut;
}

bool Detection_ProcessFrame(const cv::Mat& grayResized, AppState& state)
{
    const cv::Mat& tmpl = Detection_ActiveTemplate(state);

    if (tmpl.empty() || grayResized.empty())
        return false;

    // If template size doesn't match working resolution, re-resize templates.
    // This handles DeckLink format detection changing WORK_W/H after initial load.
    if (tmpl.cols != WORK_W || tmpl.rows != WORK_H)
    {
        Detection_ResizeTemplates(state);
        return false; // skip this frame, templates ready next frame
    }

    // Template must exactly match working resolution for full-frame matching
    if (grayResized.size() != tmpl.size())
        return false;

    // --- Template matching ---
    cv::Mat result;
    cv::matchTemplate(grayResized, tmpl, result, cv::TM_CCOEFF_NORMED);

    double maxVal = 0.0;
    cv::minMaxLoc(result, nullptr, &maxVal);
    state.lastScore = maxVal;

    auto now = std::chrono::steady_clock::now();

    // --- Cooldown check ---
    if (state.detectionState == DetectionState::COOLDOWN)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.lastDetectionTime).count();

        if (elapsed >= state.cooldownMs)
            state.detectionState = DetectionState::IDLE;
        else
            return false;  // still in cooldown
    }

    // --- Reset logic ---
    if (state.detectionState == DetectionState::DETECTED && maxVal < state.resetThreshold)
        state.detectionState = DetectionState::IDLE;

    // --- Detection trigger ---
    if (!state.detectionEnabled)
        return false;

    if (state.detectionState == DetectionState::IDLE && maxVal >= state.detectThreshold)
    {
        state.detectionState    = DetectionState::DETECTED;
        state.lastDetectionTime = now;

        std::ostringstream oss;
        oss << CurrentTimestamp()
            << " | DETECTED ["
            << (state.cueState == CueState::WIPER_IN ? "WIPER IN" : "WIPER OUT")
            << "] Score: " << std::fixed << std::setprecision(3) << maxVal;
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
