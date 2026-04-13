#pragma once

#include "Scorebug.h"

#include <string>
#include <windows.h>

struct ScorebugDraft
{
    std::string layoutName;
    std::string referenceImagePath;
    NormalizedRoi frameRoi;
    ScorebugFieldDefinition teamALabel;
    ScorebugFieldDefinition teamAScore;
    ScorebugFieldDefinition teamBLabel;
    ScorebugFieldDefinition teamBScore;
    ScorebugFieldDefinition period;
    ScorebugFieldDefinition gameClock;
    ScorebugFieldDefinition shotClock;
};

bool ScorebugDialogs_ShowEditor(
    HWND owner,
    HINSTANCE instance,
    const ScorebugLayoutManifest* existing,
    ScorebugDraft& draft,
    std::string& error);
