#pragma once

#include "Templates.h"

#include <string>
#include <windows.h>

struct TemplateDraft
{
    std::string templateName;
    std::string inSourcePath;
    std::string outSourcePath;
    NormalizedRoi inRoi;
    NormalizedRoi outRoi;
};

bool TemplateDialogs_ShowEditor(
    HWND owner,
    HINSTANCE instance,
    const TemplateManifest* existing,
    TemplateDraft& draft,
    std::string& error);
