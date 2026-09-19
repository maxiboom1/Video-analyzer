#pragma once
#include "AppState.h"
#include "VizConnection.h"
#include <utility>

struct CueCommandContext
{
    explicit CueCommandContext(VizSender::Transport transport = {}) : sender(std::move(transport)) {}
    VizSender sender;
    bool manual = false;
    VizRequest pendingRequest;
    unsigned long long destinationRevision = 0;
};

bool Cue_SendEventCommand(CueCommandContext& commands, AppState& state);
// Called after Detection_ProcessFrame has advanced the automatic cue.
bool Cue_SendDetectedCommand(CueCommandContext& commands, AppState& state);
void Cue_ProcessFrame(CueCommandContext& commands, AppState& state, const cv::Mat& grayResized);
void Cue_PollCommand(CueCommandContext& commands, AppState& state);
void Cue_ResetRendererStatus(AppState& state);
