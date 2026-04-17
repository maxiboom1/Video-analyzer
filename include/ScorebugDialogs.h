#pragma once

#include "Scorebug.h"

#include <string>
#include <windows.h>

struct OcrElementDraft
{
    std::string name;
    std::string referenceImagePath;
    NormalizedRoi frameRoi;
};

struct OcrPropDraft
{
    std::string name;
    OcrPropType type = OcrPropType::Auto;
    NormalizedRoi roi;
};

bool ScorebugDialogs_ShowElementEditor(
    HWND owner,
    HINSTANCE instance,
    const OcrElementManifest* existing,
    OcrElementDraft& draft,
    std::string& error);

bool ScorebugDialogs_ShowPropEditor(
    HWND owner,
    HINSTANCE instance,
    const OcrElementManifest& element,
    const OcrPropManifest* existing,
    OcrPropDraft& draft,
    std::string& error);
