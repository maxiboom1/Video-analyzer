#pragma once
#include "AppState.h"

// Load wiper_in.png and wiper_out.png from the EXE directory.
// Resizes both to the current WORK_W x WORK_H grayscale working size.
// Returns true if at least one template loaded successfully.
bool Detection_LoadTemplates(AppState& state);

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

// Re-resize already-loaded raw templates to current WORK_W x WORK_H.
// Called automatically when DeckLink format detection changes the resolution.
void Detection_ResizeTemplates(AppState& state);
