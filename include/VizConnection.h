#pragma once
#include "AppState.h"

// Fire-and-forget TCP send to Viz.
// Opens socket, sends command string + trailing NUL, closes socket.
// Returns true on success.
// On failure, logs the error and sets state.lastVizOk = false.
bool Viz_SendCommand(const std::string& command, AppState& state);

// Convenience wrappers that pick cmd_on / cmd_off from state
bool Viz_SendOn(AppState& state);
bool Viz_SendOff(AppState& state);
