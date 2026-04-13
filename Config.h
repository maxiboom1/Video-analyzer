#pragma once
#include "AppState.h"
extern int WORK_W;
extern int WORK_H;
// Reads config.ini from the EXE directory into AppState.
// Creates a default config.ini if not found.
void Config_Load(AppState& state);

// Writes current AppState Viz settings back to config.ini.
void Config_Save(const AppState& state);
