#pragma once
#include "AppState.h"

bool Detection_LoadTemplateCatalog(AppState& state);
bool Detection_LoadActiveTemplate(AppState& state);
bool Detection_SetActiveTemplate(AppState& state, const std::string& name);

// Returns the active template (IN or OUT) based on current cue state.
// Returns an empty Mat if the relevant template failed to load.
const cv::Mat& Detection_ActiveTemplate(const AppState& state);

// Run template matching on a single frame.
// Updates state.lastScore.
// Returns true if a detection event occurred this frame.
// Handles: threshold check, cooldown, state machine, cue flip.
// Does NOT send Viz command - caller handles that.
bool Detection_ProcessFrame(const cv::Mat& grayResized, AppState& state);

// Flip cue state (IN <-> OUT) manually (Change Cue button)
void Detection_FlipCue(AppState& state);

void Detection_RebuildRuntimeTemplateAssets(AppState& state);
