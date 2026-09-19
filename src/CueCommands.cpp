#include "CueCommands.h"
#include "Detection.h"
#include "Logger.h"

namespace
{
    bool Submit(CueCommandContext& commands, AppState& state, bool manual)
    {
        if (state.vizSendPending) return false;
        const bool sendOn = manual ? state.cueState == CueState::WIPER_IN : state.cueState == CueState::WIPER_OUT;
        VizRequest request{ state.vizIp, state.vizPort, sendOn ? state.cmdOn : state.cmdOff };
        if (!commands.sender.Submit(request)) return false;
        commands.manual = manual;
        commands.pendingRequest = std::move(request);
        commands.destinationRevision = state.vizDestinationRevision;
        state.vizSendPending = true;
        state.vizStatus = VizSendStatus::Sending;
        state.lastVizMsg.clear();
        return true;
    }
}

bool Cue_SendEventCommand(CueCommandContext& commands, AppState& state) { return Submit(commands, state, true); }
bool Cue_SendDetectedCommand(CueCommandContext& commands, AppState& state) { return Submit(commands, state, false); }

void Cue_ProcessFrame(CueCommandContext& commands, AppState& state, const cv::Mat& grayResized)
{
    if (!state.vizSendPending && Detection_ProcessFrame(grayResized, state))
        Cue_SendDetectedCommand(commands, state);
}

void Cue_PollCommand(CueCommandContext& commands, AppState& state)
{
    VizResult result;
    if (!commands.sender.Poll(result)) return;
    state.vizSendPending = false;
    const auto& request = commands.pendingRequest;
    if (commands.destinationRevision == state.vizDestinationRevision && request.ip == state.vizIp && request.port == state.vizPort)
    {
        state.vizStatus = result.success ? VizSendStatus::Succeeded : VizSendStatus::Failed;
        state.lastVizMsg = result.message;
    }
    AddLog(CurrentTimestamp() + " | VIZ " + request.ip + ":" + std::to_string(request.port) +
        (result.success ? " SENT: " + request.command : " ERROR: " + result.message));
    if (commands.manual && result.success)
    {
        Detection_FlipCue(state);
        state.lastDetectionTime = std::chrono::steady_clock::now();
        state.detectionState = DetectionState::COOLDOWN;
    }
}

void Cue_ResetRendererStatus(AppState& state)
{
    ++state.vizDestinationRevision;
    state.vizStatus = VizSendStatus::NotTested;
    state.lastVizMsg.clear();
}
