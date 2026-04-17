#include "UI.h"

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "Config.h"
#include "Detection.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scorebug.h"
#include "ScorebugDialogs.h"
#include "TemplateDialogs.h"
#include "Version.h"
#include "VideoSource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK CuePreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr int kMargin = 12;
    constexpr int kGap = 8;
    constexpr int kTopBarHeight = 42;
    constexpr int kControlHeight = 68;
    constexpr int kLogToolbarHeight = 28;

    enum ControlId
    {
        IDC_SETTINGS_BUTTON = 1001,
        IDC_DEVICE_COMBO,
        IDC_REFRESH_BUTTON,
        IDC_TEMPLATE_COMBO,
        IDC_RENDERER_STATUS,
        IDC_NEXT_CUE_BUTTON,
        IDC_CUE_PREVIEW_WINDOW,
        IDC_PREVIEW_CHECK,
        IDC_PREVIEW_WINDOW,
        IDC_AUTOSCROLL_CHECK,
        IDC_CLEAR_LOG_BUTTON,
        IDC_LOG_EDIT,
        IDC_SETTINGS_TAB,
        IDC_SETTINGS_TAB_DETECTION,
        IDC_SETTINGS_TAB_ENGINE,
        IDC_SETTINGS_TAB_TEMPLATES,
        IDC_SETTINGS_TAB_SCOREBUG,
        IDC_DETECTION_ENABLED_CHECK,
        IDC_DETECT_THRESHOLD_SLIDER,
        IDC_RESET_THRESHOLD_SLIDER,
        IDC_COOLDOWN_SLIDER,
        IDC_DETECT_THRESHOLD_VALUE,
        IDC_RESET_THRESHOLD_VALUE,
        IDC_COOLDOWN_VALUE,
        IDC_VIZ_IP_EDIT,
        IDC_VIZ_PORT_EDIT,
        IDC_CMD_ON_EDIT,
        IDC_CMD_OFF_EDIT,
        IDC_SETTINGS_SAVE_BUTTON,
        IDC_SETTINGS_STATUS,
        IDC_TEMPLATES_LIST,
        IDC_TEMPLATE_NEW_BUTTON,
        IDC_TEMPLATE_EDIT_BUTTON,
        IDC_TEMPLATE_DELETE_BUTTON,
        IDC_TEMPLATE_ACTIVATE_BUTTON,
        IDC_TEMPLATE_DETAILS,
        IDC_SCOREBUG_ENABLED_CHECK,
        IDC_SCOREBUG_THRESHOLD_SLIDER,
        IDC_SCOREBUG_THRESHOLD_VALUE,
        IDC_SCOREBUGS_LIST,
        IDC_SCOREBUG_PROPS_LIST,
        IDC_SCOREBUG_SEPARATOR,
        IDC_SCOREBUG_NEW_BUTTON,
        IDC_SCOREBUG_EDIT_BUTTON,
        IDC_SCOREBUG_DELETE_BUTTON,
        IDC_SCOREBUG_ACTIVATE_BUTTON,
        IDC_SCOREBUG_DETAILS
    };

    struct UIContext
    {
        AppState* state = nullptr;
        HINSTANCE instance = nullptr;
        HWND mainWindow = nullptr;
        HWND settingsWindow = nullptr;

        HFONT font = nullptr;
        HFONT sectionFont = nullptr;
        HFONT titleFont = nullptr;

        HBRUSH appBrush = nullptr;
        HBRUSH headerBrush = nullptr;
        HBRUSH cardBrush = nullptr;
        HBRUSH panelBrush = nullptr;
        HBRUSH previewBrush = nullptr;

        HWND headerLabel = nullptr;
        HWND settingsButton = nullptr;
        HWND deviceLabel = nullptr;
        HWND deviceCombo = nullptr;
        HWND templateLabel = nullptr;
        HWND templateCombo = nullptr;
        HWND rendererLabel = nullptr;
        HWND rendererStatus = nullptr;
        HWND nextCueButton = nullptr;
        HWND cuePreviewWindow = nullptr;
        HWND previewCheck = nullptr;
        HWND previewWindow = nullptr;
        HWND ocrStatusLabel = nullptr;
        HWND autoScrollCheck = nullptr;
        HWND clearLogButton = nullptr;
        HWND logEdit = nullptr;

        HWND settingsTab = nullptr;
        HWND settingsTabDetection = nullptr;
        HWND settingsTabEngine = nullptr;
        HWND settingsTabTemplates = nullptr;
        HWND settingsTabScorebug = nullptr;
        HWND detectionEnabledCheck = nullptr;
        HWND detectThresholdLabel = nullptr;
        HWND detectThresholdSlider = nullptr;
        HWND detectThresholdValue = nullptr;
        HWND resetThresholdLabel = nullptr;
        HWND resetThresholdSlider = nullptr;
        HWND resetThresholdValue = nullptr;
        HWND cooldownLabel = nullptr;
        HWND cooldownSlider = nullptr;
        HWND cooldownValue = nullptr;

        HWND vizIpLabel = nullptr;
        HWND vizIpEdit = nullptr;
        HWND vizPortLabel = nullptr;
        HWND vizPortEdit = nullptr;
        HWND cmdOnLabel = nullptr;
        HWND cmdOnEdit = nullptr;
        HWND cmdOffLabel = nullptr;
        HWND cmdOffEdit = nullptr;
        HWND settingsStatus = nullptr;
        HWND saveButton = nullptr;
        HWND templatePresetsLabel = nullptr;
        HWND templatesList = nullptr;
        HWND templateNewButton = nullptr;
        HWND templateEditButton = nullptr;
        HWND templateDeleteButton = nullptr;
        HWND templateActivateButton = nullptr;
        HWND templateDetails = nullptr;
        HWND scorebugEnabledCheck = nullptr;
        HWND scorebugThresholdLabel = nullptr;
        HWND scorebugThresholdSlider = nullptr;
        HWND scorebugThresholdValue = nullptr;
        HWND scorebugPresetsLabel = nullptr;
        HWND scorebugsList = nullptr;
        HWND scorebugPropsList = nullptr;
        HWND scorebugSeparator = nullptr;
        HWND scorebugNewButton = nullptr;
        HWND scorebugEditButton = nullptr;
        HWND scorebugDeleteButton = nullptr;
        HWND scorebugActivateButton = nullptr;
        HWND scorebugDetails = nullptr;

        bool lastVizOk = true;
        int currentSettingsTab = 0;
        size_t lastLogCount = 0;
        std::string lastLogTail;
        std::string deviceSignature;
        std::string templateSignature;
        std::string templateSelectionName;
        std::string scorebugSignature;
        std::string scorebugPropsSignature;
        std::string scorebugSelectionName;
        std::string scorebugPropSelectionName;
        std::string cuePreviewSignature;

        RECT headerBand{};
        RECT controlCard{};
        RECT previewCard{};
        RECT logCard{};
        RECT settingsPanel{};
    };

    UIContext g_ui;
    void SyncSettingsFromState(const AppState& state);
    void SetControlFont(HWND hwnd)
    {
        if (hwnd && g_ui.font)
            SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.font), TRUE);
    }

    void SetControlFont(HWND hwnd, HFONT font)
    {
        if (hwnd && font)
            SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    HWND CreateControlA(
        DWORD exStyle,
        const char* className,
        const char* text,
        DWORD style,
        int id,
        HWND parent)
    {
        HWND hwnd = CreateWindowExA(
            exStyle,
            className,
            text,
            style,
            0,
            0,
            0,
            0,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            g_ui.instance,
            nullptr);
        SetControlFont(hwnd);
        return hwnd;
    }

    HWND CreateButtonA(const char* text, int id, HWND parent, bool ownerDraw)
    {
        return CreateControlA(
            0,
            "BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | (ownerDraw ? BS_OWNERDRAW : BS_PUSHBUTTON),
            id,
            parent);
    }

    COLORREF AppBgColor() { return RGB(241, 245, 249); }
    COLORREF HeaderBgColor() { return RGB(16, 28, 43); }
    COLORREF CardBgColor() { return RGB(255, 255, 255); }
    COLORREF CardBorderColor() { return RGB(214, 223, 233); }
    COLORREF AccentColor() { return RGB(27, 94, 181); }
    COLORREF AccentHoverColor() { return RGB(18, 78, 156); }
    COLORREF SuccessColor() { return RGB(21, 128, 61); }
    COLORREF DangerColor() { return RGB(185, 28, 28); }
    COLORREF MutedTextColor() { return RGB(89, 100, 116); }
    COLORREF BodyTextColor() { return RGB(30, 41, 59); }
    COLORREF PreviewFrameColor() { return RGB(11, 17, 25); }

    void FillRoundedRect(HDC hdc, const RECT& rect, COLORREF fillColor, COLORREF borderColor)
    {
        HBRUSH brush = CreateSolidBrush(fillColor);
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    void DrawSectionLabel(HDC hdc, const RECT& rect, const wchar_t* text)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, BodyTextColor());
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, g_ui.sectionFont ? g_ui.sectionFont : g_ui.font));
        DrawTextW(hdc, text, -1, const_cast<RECT*>(&rect), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }

    std::string FormatFloat(float value)
    {
        char buffer[32] = {};
        sprintf_s(buffer, "%.2f", value);
        return buffer;
    }

    int ClampInt(int value, int minValue, int maxValue)
    {
        return std::max(minValue, std::min(maxValue, value));
    }

    int ThresholdToSlider(float value, int minValue, int maxValue)
    {
        return ClampInt(static_cast<int>(value * 100.0f + 0.5f), minValue, maxValue);
    }

    float SliderToThreshold(HWND hwnd, int minValue, int maxValue)
    {
        const int pos = ClampInt(static_cast<int>(SendMessageA(hwnd, TBM_GETPOS, 0, 0)), minValue, maxValue);
        return static_cast<float>(pos) / 100.0f;
    }

    int SliderToInt(HWND hwnd, int minValue, int maxValue)
    {
        return ClampInt(static_cast<int>(SendMessageA(hwnd, TBM_GETPOS, 0, 0)), minValue, maxValue);
    }

    std::string GetWindowTextString(HWND hwnd)
    {
        const int length = GetWindowTextLengthA(hwnd);
        std::string text(length + 1, '\0');
        if (length > 0)
            GetWindowTextA(hwnd, &text[0], length + 1);
        text.resize(length);
        return text;
    }

    void SetCheckState(HWND hwnd, bool checked)
    {
        SendMessageA(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetCheckState(HWND hwnd)
    {
        return SendMessageA(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    int ParseIntOrDefault(HWND hwnd, int fallback, int minValue, int maxValue)
    {
        std::string text = GetWindowTextString(hwnd);
        if (text.empty())
            return fallback;

        char* end = nullptr;
        const long parsed = strtol(text.c_str(), &end, 10);
        if (end == text.c_str())
            return fallback;
        return std::max(minValue, std::min(maxValue, static_cast<int>(parsed)));
    }

    void SetEditText(HWND hwnd, const std::string& text)
    {
        SetWindowTextA(hwnd, text.c_str());
    }

    void SetEditText(HWND hwnd, const char* text)
    {
        SetWindowTextA(hwnd, text);
    }

    void SetWindowTextIfChanged(HWND hwnd, const std::string& text)
    {
        if (!hwnd)
            return;
        if (GetWindowTextString(hwnd) != text)
            SetWindowTextA(hwnd, text.c_str());
    }

    void UpdateDetectionSliderLabels()
    {
        SetWindowTextA(g_ui.detectThresholdValue, FormatFloat(SliderToThreshold(g_ui.detectThresholdSlider, 10, 99)).c_str());
        SetWindowTextA(g_ui.resetThresholdValue, FormatFloat(SliderToThreshold(g_ui.resetThresholdSlider, 5, 95)).c_str());
        SetWindowTextA(g_ui.cooldownValue, std::to_string(SliderToInt(g_ui.cooldownSlider, 100, 10000)).c_str());
    }

    void UpdateScorebugSliderLabel()
    {
        SetWindowTextA(g_ui.scorebugThresholdValue, FormatFloat(SliderToThreshold(g_ui.scorebugThresholdSlider, 10, 99)).c_str());
    }

    std::string BuildDeviceSignature(const AppState& state)
    {
        std::ostringstream oss;
        oss << state.selectedDeviceListIndex << "|";
        for (const auto& dev : state.availableDevices)
            oss << static_cast<int>(dev.kind) << ":" << dev.id << ":" << dev.displayName << "|";
        return oss.str();
    }

    void UpdateCuePreview(const AppState& state)
    {
        const TemplateManifest* activeManifest = Templates_FindByName(state, state.activeTemplateName);
        std::ostringstream signature;
        signature << state.activeTemplateName << "|"
                  << static_cast<int>(state.cueState) << "|"
                  << state.activeTemplateLoaded << "|";
        if (activeManifest)
            signature << activeManifest->updatedAt;

        if (signature.str() == g_ui.cuePreviewSignature)
            return;

        if (state.activeTemplateLoaded)
            Renderer_SetCueFrame(Detection_ActiveTemplate(state));
        else
            Renderer_SetCueFrame(cv::Mat());

        g_ui.cuePreviewSignature = signature.str();
        if (g_ui.cuePreviewWindow)
            InvalidateRect(g_ui.cuePreviewWindow, nullptr, FALSE);
    }

    std::string FormatRoiSummary(const NormalizedRoi& roi)
    {
        if (!roi.enabled)
            return "Full frame";

        char buffer[96] = {};
        sprintf_s(buffer, "x=%.3f y=%.3f w=%.3f h=%.3f", roi.x, roi.y, roi.w, roi.h);
        return buffer;
    }

    std::string BuildTemplateSignature(const AppState& state)
    {
        std::ostringstream oss;
        oss << state.activeTemplateName << "|" << state.templates.size() << "|";
        for (const auto& manifest : state.templates)
        {
            oss << manifest.name << "|"
                << manifest.updatedAt << "|"
                << manifest.inImagePath << "|"
                << manifest.outImagePath << "|";
        }
        return oss.str();
    }

    std::string BuildMainOcrStatusText(const AppState& state)
    {
        if (!state.ocrEnabled)
            return "[OCR Disabled]";

        if (!state.ocrOnAir)
            return "[OCR] Element not detected";

        std::ostringstream oss;
        oss << "[OCR] " << (state.lastOcrState.elementName.empty() ? state.activeOcrElementName : state.lastOcrState.elementName);
        for (const auto& prop : state.lastOcrState.props)
            oss << " || " << prop.name << ": " << (prop.valid && !prop.value.empty() ? prop.value : "-");
        return oss.str();
    }

    std::string GetSelectedTemplateNameFromList()
    {
        const int selection = static_cast<int>(SendMessageA(g_ui.templatesList, LB_GETCURSEL, 0, 0));
        if (selection == LB_ERR || !g_ui.state)
            return {};
        if (selection < 0 || selection >= static_cast<int>(g_ui.state->templates.size()))
            return {};
        return g_ui.state->templates[selection].name;
    }

    void SelectTemplateListByName(const std::string& name)
    {
        if (!g_ui.templatesList || !g_ui.state)
            return;

        int index = LB_ERR;
        for (size_t i = 0; i < g_ui.state->templates.size(); ++i)
        {
            if (g_ui.state->templates[i].name == name)
            {
                index = static_cast<int>(i);
                break;
            }
        }

        if (index == LB_ERR && !g_ui.state->templates.empty())
            index = 0;

        SendMessageA(g_ui.templatesList, LB_SETCURSEL, index, 0);
        g_ui.templateSelectionName = (index != LB_ERR) ? g_ui.state->templates[static_cast<size_t>(index)].name : std::string();
    }

    void UpdateTemplateDetails(const AppState& state)
    {
        std::string selectedName = GetSelectedTemplateNameFromList();
        if (selectedName.empty())
            selectedName = !g_ui.templateSelectionName.empty() ? g_ui.templateSelectionName : state.activeTemplateName;

        const TemplateManifest* manifest = Templates_FindByName(state, selectedName);
        std::ostringstream details;
        if (!manifest)
        {
            details << "No templates available.\r\nCreate a template to enable runtime matching.";
        }
        else
        {
            details << "Selected: " << manifest->name << "\r\n";
            details << "Active: " << (state.activeTemplateName.empty() ? "None" : state.activeTemplateName) << "\r\n";
            details << "IN ROI: " << FormatRoiSummary(manifest->inRoi) << "\r\n";
            details << "OUT ROI: " << FormatRoiSummary(manifest->outRoi);
        }

        SetWindowTextA(g_ui.templateDetails, details.str().c_str());

        const bool hasSelection = manifest != nullptr;
        EnableWindow(g_ui.templateEditButton, hasSelection);
        EnableWindow(g_ui.templateDeleteButton, hasSelection);
        EnableWindow(g_ui.templateActivateButton, hasSelection && manifest->name != state.activeTemplateName);
    }

    void UpdateTemplateControls(const AppState& state)
    {
        const bool comboDropped = g_ui.templateCombo &&
            SendMessageA(g_ui.templateCombo, CB_GETDROPPEDSTATE, 0, 0) != 0;
        if (comboDropped)
        {
            UpdateTemplateDetails(state);
            return;
        }

        const std::string signature = BuildTemplateSignature(state);
        if (signature != g_ui.templateSignature)
        {
            SendMessageA(g_ui.templateCombo, CB_RESETCONTENT, 0, 0);
            SendMessageA(g_ui.templatesList, LB_RESETCONTENT, 0, 0);

            if (state.templates.empty())
            {
                SendMessageA(g_ui.templateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("<No templates>"));
                SendMessageA(g_ui.templateCombo, CB_SETCURSEL, 0, 0);
                EnableWindow(g_ui.templateCombo, FALSE);
                g_ui.templateSelectionName.clear();
            }
            else
            {
                EnableWindow(g_ui.templateCombo, TRUE);
                std::string selectedName = g_ui.templateSelectionName;
                if (selectedName.empty())
                    selectedName = state.activeTemplateName;

                int activeIndex = 0;
                int selectedIndex = LB_ERR;
                for (size_t i = 0; i < state.templates.size(); ++i)
                {
                    const char* name = state.templates[i].name.c_str();
                    SendMessageA(g_ui.templateCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                    SendMessageA(g_ui.templatesList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                    if (state.templates[i].name == state.activeTemplateName)
                        activeIndex = static_cast<int>(i);
                    if (state.templates[i].name == selectedName)
                        selectedIndex = static_cast<int>(i);
                }

                SendMessageA(g_ui.templateCombo, CB_SETCURSEL, activeIndex, 0);
                if (selectedIndex == LB_ERR)
                    selectedIndex = activeIndex;
                SendMessageA(g_ui.templatesList, LB_SETCURSEL, selectedIndex, 0);
                g_ui.templateSelectionName = state.templates[static_cast<size_t>(selectedIndex)].name;
            }

            g_ui.templateSignature = signature;
        }
        else if (!state.templates.empty())
        {
            int activeIndex = 0;
            for (size_t i = 0; i < state.templates.size(); ++i)
            {
                if (state.templates[i].name == state.activeTemplateName)
                {
                    activeIndex = static_cast<int>(i);
                    break;
                }
            }
            SendMessageA(g_ui.templateCombo, CB_SETCURSEL, activeIndex, 0);
        }

        UpdateTemplateDetails(state);
    }

    std::string BuildScorebugSignature(const AppState& state)
    {
        std::ostringstream oss;
        oss << state.activeOcrElementName << "|" << state.ocrEnabled << "|"
            << static_cast<int>(state.ocrDetectThreshold * 1000.0f) << "|"
            << state.ocrElements.size() << "|";
        for (const auto& element : state.ocrElements)
            oss << element.name << "|" << element.updatedAt << "|" << element.referenceImagePath << "|" << element.props.size() << "|";
        return oss.str();
    }

    std::string GetSelectedScorebugNameFromList()
    {
        const int selection = static_cast<int>(SendMessageA(g_ui.scorebugsList, LB_GETCURSEL, 0, 0));
        if (selection == LB_ERR || !g_ui.state)
            return {};
        if (selection < 0 || selection >= static_cast<int>(g_ui.state->ocrElements.size()))
            return {};
        return g_ui.state->ocrElements[selection].name;
    }

    std::string GetSelectedScorebugPropNameFromList()
    {
        const int selection = static_cast<int>(SendMessageA(g_ui.scorebugPropsList, LB_GETCURSEL, 0, 0));
        if (selection == LB_ERR)
            return {};

        std::string elementName = GetSelectedScorebugNameFromList();
        if (elementName.empty() || !g_ui.state)
            return {};
        const OcrElementManifest* element = Scorebug_FindLayoutByName(*g_ui.state, elementName);
        if (!element || selection < 0 || selection >= static_cast<int>(element->props.size()))
            return {};
        return element->props[selection].name;
    }

    std::string BuildScorebugPropsSignature(const AppState& state, const std::string& elementName)
    {
        std::ostringstream oss;
        oss << elementName << "|";
        if (const OcrElementManifest* element = Scorebug_FindLayoutByName(state, elementName))
        {
            oss << element->updatedAt << "|" << element->props.size() << "|";
            for (const auto& prop : element->props)
                oss << prop.name << "|" << static_cast<int>(prop.type) << "|";
        }
        return oss.str();
    }

    void UpdateScorebugControls(const AppState& state)
    {
        const std::string signature = BuildScorebugSignature(state);
        if (signature != g_ui.scorebugSignature)
        {
            const std::string liveSelectedName = GetSelectedScorebugNameFromList();
            SendMessageA(g_ui.scorebugsList, LB_RESETCONTENT, 0, 0);
            if (state.ocrElements.empty())
            {
                g_ui.scorebugSelectionName.clear();
            }
            else
            {
                std::string selectedName = !liveSelectedName.empty()
                    ? liveSelectedName
                    : (g_ui.scorebugSelectionName.empty() ? state.activeOcrElementName : g_ui.scorebugSelectionName);
                int selectedIndex = LB_ERR;
                for (size_t i = 0; i < state.ocrElements.size(); ++i)
                {
                    const char* name = state.ocrElements[i].name.c_str();
                    SendMessageA(g_ui.scorebugsList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                    if (state.ocrElements[i].name == selectedName)
                        selectedIndex = static_cast<int>(i);
                }
                if (selectedIndex == LB_ERR)
                    selectedIndex = 0;
                SendMessageA(g_ui.scorebugsList, LB_SETCURSEL, selectedIndex, 0);
                g_ui.scorebugSelectionName = state.ocrElements[static_cast<size_t>(selectedIndex)].name;
            }

            SetCheckState(g_ui.scorebugEnabledCheck, state.ocrEnabled);
            g_ui.scorebugSignature = signature;
        }

        const OcrElementManifest* element = Scorebug_FindLayoutByName(state, g_ui.scorebugSelectionName);
        const std::string propsSignature = BuildScorebugPropsSignature(state, g_ui.scorebugSelectionName);
        if (propsSignature != g_ui.scorebugPropsSignature)
        {
            const std::string liveSelectedPropName = GetSelectedScorebugPropNameFromList();
            SendMessageA(g_ui.scorebugPropsList, LB_RESETCONTENT, 0, 0);
            if (element)
            {
                std::string selectedPropName = !liveSelectedPropName.empty()
                    ? liveSelectedPropName
                    : g_ui.scorebugPropSelectionName;
                int selectedPropIndex = LB_ERR;
                for (size_t i = 0; i < element->props.size(); ++i)
                {
                    const char* name = element->props[i].name.c_str();
                    SendMessageA(g_ui.scorebugPropsList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                    if (element->props[i].name == selectedPropName)
                        selectedPropIndex = static_cast<int>(i);
                }
                if (selectedPropIndex == LB_ERR && !element->props.empty())
                    selectedPropIndex = 0;
                SendMessageA(g_ui.scorebugPropsList, LB_SETCURSEL, selectedPropIndex, 0);
                g_ui.scorebugPropSelectionName = (selectedPropIndex != LB_ERR) ? element->props[static_cast<size_t>(selectedPropIndex)].name : std::string();
            }
            else
            {
                g_ui.scorebugPropSelectionName.clear();
            }
            g_ui.scorebugPropsSignature = propsSignature;
        }

        EnableWindow(g_ui.scorebugNewButton, TRUE);
        EnableWindow(g_ui.scorebugDeleteButton, element != nullptr);
        EnableWindow(g_ui.scorebugEditButton, element != nullptr && element->props.size() < 12);
        EnableWindow(g_ui.scorebugActivateButton, !GetSelectedScorebugPropNameFromList().empty());
    }

    TemplateManifest DraftToManifest(const TemplateDraft& draft)
    {
        TemplateManifest manifest;
        manifest.name = draft.templateName;
        manifest.inRoi = draft.inRoi;
        manifest.outRoi = draft.outRoi;
        return manifest;
    }

    OcrElementManifest DraftToScorebugManifest(const OcrElementDraft& draft, const OcrElementManifest* existing)
    {
        OcrElementManifest manifest;
        manifest.name = draft.name;
        manifest.frameRoi = draft.frameRoi;
        if (existing)
            manifest.props = existing->props;
        return manifest;
    }

    void ShowSettingsError(HWND owner, const std::string& error)
    {
        MessageBoxA(owner, error.c_str(), "Settings", MB_OK | MB_ICONERROR);
    }

    void SaveConfigAndRefresh(AppState& state)
    {
        Config_Save(state);
        UI_SyncState(state);
        SyncSettingsFromState(state);
    }

    void SyncSettingsFromState(const AppState& state)
    {
        SetCheckState(g_ui.detectionEnabledCheck, state.detectionEnabled);
        SetCheckState(g_ui.scorebugEnabledCheck, state.ocrEnabled);
        SendMessageA(g_ui.scorebugThresholdSlider, TBM_SETPOS, TRUE, ThresholdToSlider(state.ocrDetectThreshold, 10, 99));
        UpdateScorebugSliderLabel();
        SendMessageA(g_ui.detectThresholdSlider, TBM_SETPOS, TRUE, ThresholdToSlider(state.detectThreshold, 10, 99));
        SendMessageA(g_ui.resetThresholdSlider, TBM_SETPOS, TRUE, ThresholdToSlider(state.resetThreshold, 5, 95));
        SendMessageA(g_ui.cooldownSlider, TBM_SETPOS, TRUE, ClampInt(state.cooldownMs, 100, 10000));
        UpdateDetectionSliderLabels();
        SetEditText(g_ui.vizIpEdit, state.vizIp);
        SetEditText(g_ui.vizPortEdit, std::to_string(state.vizPort));
        SetEditText(g_ui.cmdOnEdit, state.cmdOn);
        SetEditText(g_ui.cmdOffEdit, state.cmdOff);
    }

    void SaveSettingsToState(AppState& state)
    {
        state.detectionEnabled = GetCheckState(g_ui.detectionEnabledCheck);
        state.ocrEnabled = GetCheckState(g_ui.scorebugEnabledCheck);
        state.ocrDetectThreshold = SliderToThreshold(g_ui.scorebugThresholdSlider, 10, 99);
        state.activeOcrElementName = GetSelectedScorebugNameFromList();
        state.detectThreshold = SliderToThreshold(g_ui.detectThresholdSlider, 10, 99);
        state.resetThreshold = SliderToThreshold(g_ui.resetThresholdSlider, 5, 95);
        state.cooldownMs = SliderToInt(g_ui.cooldownSlider, 100, 10000);
        state.vizPort = ParseIntOrDefault(g_ui.vizPortEdit, state.vizPort, 1, 65535);

        strncpy_s(state.vizIp, GetWindowTextString(g_ui.vizIpEdit).c_str(), _TRUNCATE);
        strncpy_s(state.cmdOn, GetWindowTextString(g_ui.cmdOnEdit).c_str(), _TRUNCATE);
        strncpy_s(state.cmdOff, GetWindowTextString(g_ui.cmdOffEdit).c_str(), _TRUNCATE);
    }

    bool RunTemplateEditor(AppState& state, const TemplateManifest* existing, const std::string& originalName)
    {
        TemplateDraft draft;
        std::string error;
        if (!TemplateDialogs_ShowEditor(g_ui.settingsWindow, g_ui.instance, existing, draft, error))
        {
            if (!error.empty())
                ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        TemplateManifest manifest = DraftToManifest(draft);
        if (!Templates_SaveTemplate(state, manifest, draft.inSourcePath, draft.outSourcePath, originalName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        const bool shouldActivate = originalName.empty() || state.activeTemplateName == originalName;
        if (shouldActivate)
            Detection_SetActiveTemplate(state, manifest.name);
        else
            Templates_ScanCatalog(state);

        g_ui.templateSignature.clear();
        g_ui.templateSelectionName = manifest.name;
        SaveConfigAndRefresh(state);
        return true;
    }

    bool RunScorebugEditor(AppState& state, const OcrElementManifest* existing, const std::string& originalName)
    {
        OcrElementDraft draft;
        std::string error;
        if (!ScorebugDialogs_ShowElementEditor(g_ui.settingsWindow, g_ui.instance, existing, draft, error))
        {
            if (!error.empty())
                ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        OcrElementManifest manifest = DraftToScorebugManifest(draft, existing);
        if (!Scorebug_SaveLayout(state, manifest, draft.referenceImagePath, originalName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        g_ui.scorebugSignature.clear();
        g_ui.scorebugPropsSignature.clear();
        g_ui.scorebugSelectionName = manifest.name;
        g_ui.scorebugPropSelectionName.clear();
        UI_SyncState(state);
        SyncSettingsFromState(state);
        return true;
    }

    bool RunScorebugPropEditor(AppState& state, const OcrElementManifest& element, const OcrPropManifest* existing, const std::string& originalName)
    {
        OcrPropDraft draft;
        std::string error;
        if (!ScorebugDialogs_ShowPropEditor(g_ui.settingsWindow, g_ui.instance, element, existing, draft, error))
        {
            if (!error.empty())
                ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        OcrPropManifest prop;
        prop.name = draft.name;
        prop.roi = draft.roi;
        prop.type = draft.type;
        if (!Scorebug_SaveProp(state, element.name, prop, originalName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return false;
        }

        g_ui.scorebugSignature.clear();
        g_ui.scorebugPropsSignature.clear();
        g_ui.scorebugSelectionName = element.name;
        g_ui.scorebugPropSelectionName = prop.name;
        UI_SyncState(state);
        SyncSettingsFromState(state);
        return true;
    }

    void DeleteSelectedTemplate(AppState& state)
    {
        const std::string selectedName = GetSelectedTemplateNameFromList();
        if (selectedName.empty())
            return;

        std::string prompt = "Delete template '" + selectedName + "'?";
        if (MessageBoxA(g_ui.settingsWindow, prompt.c_str(), "Delete Template", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;

        const bool deletingActive = selectedName == state.activeTemplateName;
        std::string error;
        if (!Templates_DeleteTemplate(state, selectedName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return;
        }

        if (deletingActive)
        {
            if (!state.templates.empty())
                Detection_SetActiveTemplate(state, state.templates.front().name);
            else
                Detection_SetActiveTemplate(state, "");
        }

        g_ui.templateSignature.clear();
        g_ui.templateSelectionName = state.activeTemplateName;
        SaveConfigAndRefresh(state);
    }

    void DeleteSelectedScorebug(AppState& state)
    {
        const std::string selectedName = GetSelectedScorebugNameFromList();
        if (selectedName.empty())
            return;

        std::string prompt = "Delete OCR element '" + selectedName + "'?";
        if (MessageBoxA(g_ui.settingsWindow, prompt.c_str(), "Delete OCR Element", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;
        std::string error;
        if (!Scorebug_DeleteLayout(state, selectedName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return;
        }

        g_ui.scorebugSignature.clear();
        g_ui.scorebugPropsSignature.clear();
        g_ui.scorebugSelectionName.clear();
        g_ui.scorebugPropSelectionName.clear();
        UI_SyncState(state);
        SyncSettingsFromState(state);
    }

    void DeleteSelectedScorebugProp(AppState& state)
    {
        const std::string elementName = GetSelectedScorebugNameFromList();
        const std::string propName = GetSelectedScorebugPropNameFromList();
        if (elementName.empty() || propName.empty())
            return;

        std::string prompt = "Delete OCR property '" + propName + "'?";
        if (MessageBoxA(g_ui.settingsWindow, prompt.c_str(), "Delete OCR Property", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;

        std::string error;
        if (!Scorebug_DeleteProp(state, elementName, propName, error))
        {
            ShowSettingsError(g_ui.settingsWindow, error);
            return;
        }

        g_ui.scorebugSignature.clear();
        g_ui.scorebugPropsSignature.clear();
        g_ui.scorebugSelectionName = elementName;
        g_ui.scorebugPropSelectionName.clear();
        UI_SyncState(state);
        SyncSettingsFromState(state);
    }

    void ShowSettingsWindow(const AppState& state)
    {
        SyncSettingsFromState(state);
        UpdateTemplateControls(state);
        UpdateScorebugControls(state);

        RECT mainRect{};
        GetWindowRect(g_ui.mainWindow, &mainRect);
        SetWindowPos(
            g_ui.settingsWindow,
            nullptr,
            mainRect.left + 40,
            mainRect.top + 40,
            720,
            470,
            SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(g_ui.settingsWindow, SW_SHOW);
        SetForegroundWindow(g_ui.settingsWindow);
    }

    void UpdateRendererStatus(const AppState& state)
    {
        std::ostringstream statusText;
        statusText << state.vizIp << ":" << state.vizPort << " ["
                   << (state.lastVizOk ? "Connected" : state.lastVizMsg) << "]";
        SetWindowTextIfChanged(g_ui.rendererStatus, statusText.str());

        std::ostringstream settingsText;
        settingsText << "Status: " << state.vizIp << ":" << state.vizPort << " ["
                     << (state.lastVizOk ? "OK" : state.lastVizMsg) << "]";
        SetWindowTextIfChanged(g_ui.settingsStatus, settingsText.str());

        g_ui.lastVizOk = state.lastVizOk;
    }

    void UpdateNextCueButton(const AppState& state)
    {
        SetWindowTextIfChanged(
            g_ui.nextCueButton,
            state.cueState == CueState::WIPER_IN ? "NEXT CUE\nWIPER IN" : "NEXT CUE\nWIPER OUT");
    }

    void UpdateLogView(const AppState& state)
    {
        const size_t logCount = g_logs.size();
        const std::string tail = logCount > 0 ? g_logs.back() : std::string();
        if (logCount == g_ui.lastLogCount && tail == g_ui.lastLogTail)
            return;

        std::string joined;
        for (size_t i = 0; i < g_logs.size(); ++i)
        {
            joined += g_logs[i];
            if (i + 1 < g_logs.size())
                joined += "\r\n";
        }

        SetWindowTextA(g_ui.logEdit, joined.c_str());
        if (state.autoScrollLog)
        {
            SendMessageA(g_ui.logEdit, EM_SETSEL, static_cast<WPARAM>(joined.size()), static_cast<LPARAM>(joined.size()));
            SendMessageA(g_ui.logEdit, EM_SCROLLCARET, 0, 0);
        }

        g_ui.lastLogCount = logCount;
        g_ui.lastLogTail = tail;
    }

    void UpdateDeviceList(const AppState& state)
    {
        const std::string signature = BuildDeviceSignature(state);
        if (signature == g_ui.deviceSignature)
            return;

        SendMessageA(g_ui.deviceCombo, CB_RESETCONTENT, 0, 0);
        for (const auto& dev : state.availableDevices)
        {
            std::string label = "[" + std::string(VideoSourceKindToString(dev.kind)) + "] " + dev.displayName;
            SendMessageA(g_ui.deviceCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }

        if (state.selectedDeviceListIndex >= 0 &&
            state.selectedDeviceListIndex < static_cast<int>(state.availableDevices.size()))
        {
            SendMessageA(g_ui.deviceCombo, CB_SETCURSEL, state.selectedDeviceListIndex, 0);
        }
        else
        {
            SendMessageA(g_ui.deviceCombo, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        }

        g_ui.deviceSignature = signature;
    }

    void LayoutMainWindow(int width, int height)
    {
        const int contentLeft = kMargin;
        const int contentTop = kMargin;
        const int contentWidth = std::max(0, width - (kMargin * 2));
        g_ui.headerBand = { 0, 0, width, 88 };

        MoveWindow(g_ui.headerLabel, contentLeft + 8, contentTop + 6, std::max(0, contentWidth - 180), 28, TRUE);
        MoveWindow(g_ui.settingsButton, width - kMargin - 126, contentTop + 2, 118, 34, TRUE);

        const int controlTop = contentTop + 54;
        g_ui.controlCard = { contentLeft, controlTop, width - kMargin, controlTop + 202 };
        MoveWindow(g_ui.deviceLabel, contentLeft + 20, controlTop + 18, 100, 22, TRUE);
        MoveWindow(g_ui.deviceCombo, contentLeft + 126, controlTop + 14, std::max(220, contentWidth - 146), 240, TRUE);

        MoveWindow(g_ui.templateLabel, contentLeft + 20, controlTop + 52, 100, 22, TRUE);
        MoveWindow(g_ui.templateCombo, contentLeft + 126, controlTop + 48, contentWidth - 146, 240, TRUE);

        const int cueTop = controlTop + 88;
        const int cueHeight = 96;
        const int cueButtonSize = cueHeight;
        const int cuePreviewLeft = contentLeft + 20 + cueButtonSize + 12;
        const int cuePreviewWidth = std::max(120, contentWidth - 52 - cueButtonSize - 12);
        MoveWindow(g_ui.nextCueButton, contentLeft + 20, cueTop, cueButtonSize, cueHeight, TRUE);
        MoveWindow(g_ui.cuePreviewWindow, cuePreviewLeft, cueTop, cuePreviewWidth, cueHeight, TRUE);

        const int previewTop = controlTop + 214;
        const int footerTop = std::max(previewTop + 140, height - kMargin - 20);
        const int availableHeight = std::max(120, footerTop - previewTop - 12);
        const int previewSectionHeight = std::max(250, static_cast<int>(availableHeight * 0.50f));
        const int logTop = previewTop + previewSectionHeight + 12;
        const int logHeight = std::max(120, footerTop - logTop - 10);
        g_ui.previewCard = { contentLeft, previewTop, width - kMargin, previewTop + previewSectionHeight };
        g_ui.logCard = { contentLeft, logTop, width - kMargin, logTop + logHeight };

        MoveWindow(g_ui.previewCheck, contentLeft + 20, previewTop + 14, 140, 24, TRUE);
        const int previewWindowTop = previewTop + 48;
        const int previewStatusTop = previewTop + previewSectionHeight - 30;
        MoveWindow(g_ui.previewWindow, contentLeft + 20, previewWindowTop, contentWidth - 40, std::max(80, previewStatusTop - previewWindowTop - 8), TRUE);
        MoveWindow(g_ui.ocrStatusLabel, contentLeft + 20, previewStatusTop, contentWidth - 40, 18, TRUE);

        MoveWindow(g_ui.autoScrollCheck, contentLeft + 20, logTop + 14, 140, 24, TRUE);
        MoveWindow(g_ui.clearLogButton, width - kMargin - 112, logTop + 10, 92, 32, TRUE);
        MoveWindow(g_ui.logEdit, contentLeft + 20, logTop + 52, contentWidth - 40, std::max(80, logHeight - 72), TRUE);
        MoveWindow(g_ui.rendererLabel, contentLeft + 8, footerTop, 96, 22, TRUE);
        MoveWindow(g_ui.rendererStatus, contentLeft + 100, footerTop, std::max(180, contentWidth - 108), 22, TRUE);

        InvalidateRect(g_ui.mainWindow, nullptr, TRUE);
    }

    void ShowSettingsTab(int tabIndex)
    {
        g_ui.currentSettingsTab = tabIndex;
        const bool detectionTab = (tabIndex == 0);
        const bool engineTab = (tabIndex == 1);
        const bool templatesTab = (tabIndex == 2);
        const bool scorebugTab = (tabIndex == 3);

        const HWND detectionControls[] = {
            g_ui.detectionEnabledCheck,
            g_ui.detectThresholdLabel,
            g_ui.detectThresholdSlider,
            g_ui.detectThresholdValue,
            g_ui.resetThresholdLabel,
            g_ui.resetThresholdSlider,
            g_ui.resetThresholdValue,
            g_ui.cooldownLabel,
            g_ui.cooldownSlider,
            g_ui.cooldownValue
        };
        const HWND engineControls[] = {
            g_ui.vizIpLabel,
            g_ui.vizIpEdit,
            g_ui.vizPortLabel,
            g_ui.vizPortEdit,
            g_ui.cmdOnLabel,
            g_ui.cmdOnEdit,
            g_ui.cmdOffLabel,
            g_ui.cmdOffEdit,
            g_ui.settingsStatus
        };
        const HWND templatesControls[] = {
            g_ui.templatePresetsLabel,
            g_ui.templatesList,
            g_ui.templateNewButton,
            g_ui.templateEditButton,
            g_ui.templateDeleteButton,
            g_ui.templateActivateButton,
            g_ui.templateDetails
        };
        const HWND scorebugControls[] = {
            g_ui.scorebugEnabledCheck,
            g_ui.scorebugThresholdLabel,
            g_ui.scorebugThresholdSlider,
            g_ui.scorebugThresholdValue,
            g_ui.scorebugPresetsLabel,
            g_ui.scorebugsList,
            g_ui.scorebugPropsList,
            g_ui.scorebugNewButton,
            g_ui.scorebugEditButton,
            g_ui.scorebugDeleteButton,
            g_ui.scorebugActivateButton,
            g_ui.scorebugDetails
        };

        for (HWND hwnd : detectionControls)
            ShowWindow(hwnd, detectionTab ? SW_SHOW : SW_HIDE);
        for (HWND hwnd : engineControls)
            ShowWindow(hwnd, engineTab ? SW_SHOW : SW_HIDE);
        for (HWND hwnd : templatesControls)
            ShowWindow(hwnd, templatesTab ? SW_SHOW : SW_HIDE);
        for (HWND hwnd : scorebugControls)
            ShowWindow(hwnd, scorebugTab ? SW_SHOW : SW_HIDE);
        if (g_ui.scorebugSeparator)
            ShowWindow(g_ui.scorebugSeparator, SW_HIDE);
        if (g_ui.settingsTabDetection)
            InvalidateRect(g_ui.settingsTabDetection, nullptr, TRUE);
        if (g_ui.settingsTabEngine)
            InvalidateRect(g_ui.settingsTabEngine, nullptr, TRUE);
        if (g_ui.settingsTabTemplates)
            InvalidateRect(g_ui.settingsTabTemplates, nullptr, TRUE);
        if (g_ui.settingsTabScorebug)
            InvalidateRect(g_ui.settingsTabScorebug, nullptr, TRUE);
    }

    void LayoutSettingsWindow(int width, int height)
    {
        const int tabsTop = kMargin;
        const int tabsLeft = kMargin;
        const int tabWidth = 86;
        const int tabHeight = 24;
        MoveWindow(g_ui.settingsTabDetection, tabsLeft, tabsTop, tabWidth, tabHeight, TRUE);
        MoveWindow(g_ui.settingsTabEngine, tabsLeft + tabWidth + 6, tabsTop, tabWidth, tabHeight, TRUE);
        MoveWindow(g_ui.settingsTabTemplates, tabsLeft + (tabWidth + 6) * 2, tabsTop, tabWidth + 10, tabHeight, TRUE);
        MoveWindow(g_ui.settingsTabScorebug, tabsLeft + (tabWidth + 6) * 3 + 10, tabsTop, tabWidth + 8, tabHeight, TRUE);

        RECT panelRect{ kMargin, tabsTop + tabHeight + 8, width - kMargin, std::max(tabsTop + tabHeight + 120, height - 58) };
        g_ui.settingsPanel = panelRect;
        const int left = panelRect.left + 16;
        const int labelW = 160;
        const int fieldLeft = left + labelW;
        const int valueW = 76;
        const int fieldW = std::max(160, static_cast<int>(panelRect.right) - fieldLeft - valueW - 28);

        MoveWindow(g_ui.detectionEnabledCheck, left, panelRect.top + 8, 180, 24, TRUE);
        MoveWindow(g_ui.detectThresholdLabel, left, panelRect.top + 48, labelW, 20, TRUE);
        MoveWindow(g_ui.detectThresholdSlider, fieldLeft, panelRect.top + 42, fieldW, 28, TRUE);
        MoveWindow(g_ui.detectThresholdValue, fieldLeft + fieldW + 10, panelRect.top + 46, valueW, 22, TRUE);
        MoveWindow(g_ui.resetThresholdLabel, left, panelRect.top + 84, labelW, 20, TRUE);
        MoveWindow(g_ui.resetThresholdSlider, fieldLeft, panelRect.top + 78, fieldW, 28, TRUE);
        MoveWindow(g_ui.resetThresholdValue, fieldLeft + fieldW + 10, panelRect.top + 82, valueW, 22, TRUE);
        MoveWindow(g_ui.cooldownLabel, left, panelRect.top + 120, labelW, 20, TRUE);
        MoveWindow(g_ui.cooldownSlider, fieldLeft, panelRect.top + 114, fieldW, 28, TRUE);
        MoveWindow(g_ui.cooldownValue, fieldLeft + fieldW + 10, panelRect.top + 118, valueW, 22, TRUE);

        MoveWindow(g_ui.vizIpLabel, left, panelRect.top + 12, labelW, 20, TRUE);
        MoveWindow(g_ui.vizIpEdit, fieldLeft, panelRect.top + 8, 220, 24, TRUE);
        MoveWindow(g_ui.vizPortLabel, left, panelRect.top + 48, labelW, 20, TRUE);
        MoveWindow(g_ui.vizPortEdit, fieldLeft, panelRect.top + 44, 100, 24, TRUE);
        MoveWindow(g_ui.cmdOnLabel, left, panelRect.top + 88, labelW, 20, TRUE);
        MoveWindow(g_ui.cmdOnEdit, fieldLeft, panelRect.top + 84, fieldW, 24, TRUE);
        MoveWindow(g_ui.cmdOffLabel, left, panelRect.top + 124, labelW, 20, TRUE);
        MoveWindow(g_ui.cmdOffEdit, fieldLeft, panelRect.top + 120, fieldW, 24, TRUE);
        MoveWindow(g_ui.settingsStatus, left, panelRect.top + 164, std::max(240, fieldW + labelW), 20, TRUE);

        const int panelWidth = static_cast<int>(panelRect.right - panelRect.left);
        const int listWidth = std::max(220, panelWidth / 2 - 24);
        const int rightLeft = left + listWidth + 18;
        const int rightWidth = std::max(180, static_cast<int>(panelRect.right) - rightLeft - 16);
        const int buttonTop = panelRect.bottom - 46;
        const int buttonHeight = 30;
        const int buttonWidth = 104;
        const int buttonGap = 10;
        const int activateWidth = 120;
        const int listTop = panelRect.top + 40;
        const int listHeight = std::max(180, buttonTop - listTop - 12);

        MoveWindow(g_ui.templatePresetsLabel, left, panelRect.top + 12, listWidth, 20, TRUE);
        MoveWindow(g_ui.templatesList, left, listTop, listWidth, listHeight, TRUE);
        MoveWindow(g_ui.templateDetails, rightLeft, panelRect.top + 12, rightWidth, 112, TRUE);
        MoveWindow(g_ui.templateNewButton, left, buttonTop, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_ui.templateEditButton, left + buttonWidth + buttonGap, buttonTop, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_ui.templateDeleteButton, left + (buttonWidth + buttonGap) * 2, buttonTop, buttonWidth, buttonHeight, TRUE);
        MoveWindow(g_ui.templateActivateButton, left + (buttonWidth + buttonGap) * 3, buttonTop, activateWidth, buttonHeight, TRUE);

        const int topBandTop = panelRect.top + 12;
        MoveWindow(g_ui.scorebugEnabledCheck, left, topBandTop, 140, 24, TRUE);
        MoveWindow(g_ui.scorebugThresholdLabel, left + 170, topBandTop + 2, 122, 20, TRUE);
        MoveWindow(
            g_ui.scorebugThresholdSlider,
            left + 298,
            topBandTop - 4,
            std::max(140, static_cast<int>(panelRect.right) - (left + 370) - 16),
            28,
            TRUE);
        MoveWindow(g_ui.scorebugThresholdValue, panelRect.right - 70, topBandTop + 2, 54, 22, TRUE);
        MoveWindow(g_ui.scorebugSeparator, left, panelRect.top + 46, panelWidth - 32, 10, TRUE);

        const int ocrContentTop = panelRect.top + 64;
        const int ocrButtonTop = panelRect.bottom - 46;
        const int ocrListHeight = std::max(140, ocrButtonTop - ocrContentTop - 36);
        const int paneGap = 18;
        const int ocrListWidth = std::max(180, (panelWidth - 32 - paneGap) / 2);
        const int rightPaneLeft = left + ocrListWidth + paneGap;
        const int rightPaneWidth = std::max(180, static_cast<int>(panelRect.right) - rightPaneLeft - 16);

        MoveWindow(g_ui.scorebugPresetsLabel, left, ocrContentTop, ocrListWidth, 20, TRUE);
        MoveWindow(g_ui.scorebugDetails, rightPaneLeft, ocrContentTop, rightPaneWidth, 20, TRUE);
        MoveWindow(g_ui.scorebugsList, left, ocrContentTop + 28, ocrListWidth, ocrListHeight, TRUE);
        MoveWindow(g_ui.scorebugPropsList, rightPaneLeft, ocrContentTop + 28, rightPaneWidth, ocrListHeight, TRUE);

        MoveWindow(g_ui.scorebugNewButton, left, ocrButtonTop, buttonWidth, 30, TRUE);
        MoveWindow(g_ui.scorebugDeleteButton, left + buttonWidth + buttonGap, ocrButtonTop, buttonWidth, 30, TRUE);
        MoveWindow(g_ui.scorebugEditButton, rightPaneLeft, ocrButtonTop, buttonWidth, 30, TRUE);
        MoveWindow(g_ui.scorebugActivateButton, rightPaneLeft + buttonWidth + buttonGap, ocrButtonTop, buttonWidth, 30, TRUE);
        MoveWindow(g_ui.saveButton, width - kMargin - 120, height - kMargin - 30, 120, 30, TRUE);

        InvalidateRect(g_ui.settingsWindow, nullptr, TRUE);
    }

    void CreateSettingsWindowControls()
    {
        g_ui.settingsTabDetection = CreateButtonA("Detection", IDC_SETTINGS_TAB_DETECTION, g_ui.settingsWindow, true);
        g_ui.settingsTabEngine = CreateButtonA("Engine", IDC_SETTINGS_TAB_ENGINE, g_ui.settingsWindow, true);
        g_ui.settingsTabTemplates = CreateButtonA("Templates", IDC_SETTINGS_TAB_TEMPLATES, g_ui.settingsWindow, true);
        g_ui.settingsTabScorebug = CreateButtonA("OCR", IDC_SETTINGS_TAB_SCOREBUG, g_ui.settingsWindow, true);

        g_ui.detectionEnabledCheck = CreateControlA(0, "BUTTON", "Detection enabled", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, IDC_DETECTION_ENABLED_CHECK, g_ui.settingsWindow);
        g_ui.detectThresholdLabel = CreateControlA(0, "STATIC", "Detect Threshold", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.detectThresholdSlider = CreateWindowExA(
            0,
            TRACKBAR_CLASSA,
            "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            0, 0, 0, 0,
            g_ui.settingsWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETECT_THRESHOLD_SLIDER)),
            g_ui.instance,
            nullptr);
        g_ui.detectThresholdValue = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT, IDC_DETECT_THRESHOLD_VALUE, g_ui.settingsWindow);
        g_ui.resetThresholdLabel = CreateControlA(0, "STATIC", "Reset Threshold", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.resetThresholdSlider = CreateWindowExA(
            0,
            TRACKBAR_CLASSA,
            "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            0, 0, 0, 0,
            g_ui.settingsWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RESET_THRESHOLD_SLIDER)),
            g_ui.instance,
            nullptr);
        g_ui.resetThresholdValue = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT, IDC_RESET_THRESHOLD_VALUE, g_ui.settingsWindow);
        g_ui.cooldownLabel = CreateControlA(0, "STATIC", "Cooldown (ms)", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.cooldownSlider = CreateWindowExA(
            0,
            TRACKBAR_CLASSA,
            "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            0, 0, 0, 0,
            g_ui.settingsWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COOLDOWN_SLIDER)),
            g_ui.instance,
            nullptr);
        g_ui.cooldownValue = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT, IDC_COOLDOWN_VALUE, g_ui.settingsWindow);

        g_ui.vizIpLabel = CreateControlA(0, "STATIC", "IP Address", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.vizIpEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_VIZ_IP_EDIT, g_ui.settingsWindow);
        g_ui.vizPortLabel = CreateControlA(0, "STATIC", "Port", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.vizPortEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_VIZ_PORT_EDIT, g_ui.settingsWindow);
        g_ui.cmdOnLabel = CreateControlA(0, "STATIC", "GFX ON command", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.cmdOnEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_CMD_ON_EDIT, g_ui.settingsWindow);
        g_ui.cmdOffLabel = CreateControlA(0, "STATIC", "GFX OFF command", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.cmdOffEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_CMD_OFF_EDIT, g_ui.settingsWindow);
        g_ui.settingsStatus = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_SETTINGS_STATUS, g_ui.settingsWindow);
        g_ui.saveButton = CreateButtonA("Save Config", IDC_SETTINGS_SAVE_BUTTON, g_ui.settingsWindow, true);
        g_ui.templatePresetsLabel = CreateControlA(0, "STATIC", "Detection Presets", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.templatesList = CreateControlA(
            WS_EX_CLIENTEDGE,
            "LISTBOX",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            IDC_TEMPLATES_LIST,
            g_ui.settingsWindow);
        g_ui.templateNewButton = CreateButtonA("New", IDC_TEMPLATE_NEW_BUTTON, g_ui.settingsWindow, true);
        g_ui.templateEditButton = CreateButtonA("Edit", IDC_TEMPLATE_EDIT_BUTTON, g_ui.settingsWindow, true);
        g_ui.templateDeleteButton = CreateButtonA("Delete", IDC_TEMPLATE_DELETE_BUTTON, g_ui.settingsWindow, true);
        g_ui.templateActivateButton = CreateButtonA("Set Active", IDC_TEMPLATE_ACTIVATE_BUTTON, g_ui.settingsWindow, true);
        g_ui.templateDetails = CreateControlA(
            0,
            "STATIC",
            "",
            WS_CHILD | WS_VISIBLE,
            IDC_TEMPLATE_DETAILS,
            g_ui.settingsWindow);
        g_ui.scorebugEnabledCheck = CreateControlA(0, "BUTTON", "Enable OCR", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, IDC_SCOREBUG_ENABLED_CHECK, g_ui.settingsWindow);
        g_ui.scorebugThresholdLabel = CreateControlA(0, "STATIC", "Detect Threshold", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.scorebugThresholdSlider = CreateWindowExA(
            0,
            TRACKBAR_CLASSA,
            "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            0, 0, 0, 0,
            g_ui.settingsWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCOREBUG_THRESHOLD_SLIDER)),
            g_ui.instance,
            nullptr);
        g_ui.scorebugThresholdValue = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT, IDC_SCOREBUG_THRESHOLD_VALUE, g_ui.settingsWindow);
        g_ui.scorebugPresetsLabel = CreateControlA(0, "STATIC", "GFX Element List", WS_CHILD | WS_VISIBLE, 0, g_ui.settingsWindow);
        g_ui.scorebugSeparator = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, IDC_SCOREBUG_SEPARATOR, g_ui.settingsWindow);
        g_ui.scorebugsList = CreateControlA(
            WS_EX_CLIENTEDGE,
            "LISTBOX",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            IDC_SCOREBUGS_LIST,
            g_ui.settingsWindow);
        g_ui.scorebugPropsList = CreateControlA(
            WS_EX_CLIENTEDGE,
            "LISTBOX",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            IDC_SCOREBUG_PROPS_LIST,
            g_ui.settingsWindow);
        g_ui.scorebugNewButton = CreateButtonA("New", IDC_SCOREBUG_NEW_BUTTON, g_ui.settingsWindow, true);
        g_ui.scorebugEditButton = CreateButtonA("New", IDC_SCOREBUG_EDIT_BUTTON, g_ui.settingsWindow, true);
        g_ui.scorebugDeleteButton = CreateButtonA("Delete", IDC_SCOREBUG_DELETE_BUTTON, g_ui.settingsWindow, true);
        g_ui.scorebugActivateButton = CreateButtonA("Delete", IDC_SCOREBUG_ACTIVATE_BUTTON, g_ui.settingsWindow, true);
        g_ui.scorebugDetails = CreateControlA(
            0,
            "STATIC",
            "GFX Properties",
            WS_CHILD | WS_VISIBLE,
            IDC_SCOREBUG_DETAILS,
            g_ui.settingsWindow);

        SetControlFont(g_ui.detectThresholdLabel, g_ui.sectionFont);
        SetControlFont(g_ui.detectThresholdValue, g_ui.sectionFont);
        SetControlFont(g_ui.resetThresholdLabel, g_ui.sectionFont);
        SetControlFont(g_ui.resetThresholdValue, g_ui.sectionFont);
        SetControlFont(g_ui.cooldownLabel, g_ui.sectionFont);
        SetControlFont(g_ui.cooldownValue, g_ui.sectionFont);
        SetControlFont(g_ui.vizIpLabel, g_ui.sectionFont);
        SetControlFont(g_ui.vizPortLabel, g_ui.sectionFont);
        SetControlFont(g_ui.cmdOnLabel, g_ui.sectionFont);
        SetControlFont(g_ui.cmdOffLabel, g_ui.sectionFont);
        SetControlFont(g_ui.settingsStatus, g_ui.sectionFont);
        SetControlFont(g_ui.templatePresetsLabel, g_ui.sectionFont);
        SetControlFont(g_ui.templateDetails, g_ui.sectionFont);
        SetControlFont(g_ui.scorebugThresholdLabel, g_ui.sectionFont);
        SetControlFont(g_ui.scorebugThresholdValue, g_ui.sectionFont);
        SetControlFont(g_ui.scorebugPresetsLabel, g_ui.sectionFont);
        SetControlFont(g_ui.scorebugDetails, g_ui.sectionFont);

        SetWindowTheme(g_ui.detectThresholdSlider, L"Explorer", nullptr);
        SetWindowTheme(g_ui.resetThresholdSlider, L"Explorer", nullptr);
        SetWindowTheme(g_ui.cooldownSlider, L"Explorer", nullptr);
        SetWindowTheme(g_ui.scorebugThresholdSlider, L"Explorer", nullptr);
        SetWindowTheme(g_ui.vizIpEdit, L"Explorer", nullptr);
        SetWindowTheme(g_ui.vizPortEdit, L"Explorer", nullptr);
        SetWindowTheme(g_ui.cmdOnEdit, L"Explorer", nullptr);
        SetWindowTheme(g_ui.cmdOffEdit, L"Explorer", nullptr);
        SetWindowTheme(g_ui.templatesList, L"Explorer", nullptr);
        SetWindowTheme(g_ui.scorebugsList, L"Explorer", nullptr);
        SetWindowTheme(g_ui.scorebugPropsList, L"Explorer", nullptr);

        SendMessageA(g_ui.detectThresholdSlider, TBM_SETRANGEMIN, FALSE, 10);
        SendMessageA(g_ui.detectThresholdSlider, TBM_SETRANGEMAX, TRUE, 99);
        SendMessageA(g_ui.resetThresholdSlider, TBM_SETRANGEMIN, FALSE, 5);
        SendMessageA(g_ui.resetThresholdSlider, TBM_SETRANGEMAX, TRUE, 95);
        SendMessageA(g_ui.cooldownSlider, TBM_SETRANGEMIN, FALSE, 100);
        SendMessageA(g_ui.cooldownSlider, TBM_SETRANGEMAX, TRUE, 10000);
        SendMessageA(g_ui.scorebugThresholdSlider, TBM_SETRANGEMIN, FALSE, 10);
        SendMessageA(g_ui.scorebugThresholdSlider, TBM_SETRANGEMAX, TRUE, 99);
    }

    void RegisterWindowClasses(HINSTANCE instance)
    {
        static bool registered = false;
        if (registered)
            return;

        WNDCLASSEXA previewClass = {};
        previewClass.cbSize = sizeof(previewClass);
        previewClass.lpfnWndProc = PreviewWndProc;
        previewClass.hInstance = instance;
        previewClass.lpszClassName = "VideoAnalyzerPreviewWindow";
        previewClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        previewClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExA(&previewClass);

        WNDCLASSEXA cueClass = {};
        cueClass.cbSize = sizeof(cueClass);
        cueClass.lpfnWndProc = CuePreviewWndProc;
        cueClass.hInstance = instance;
        cueClass.lpszClassName = "VideoAnalyzerCuePreviewWindow";
        cueClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        cueClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExA(&cueClass);

        WNDCLASSEXA settingsClass = {};
        settingsClass.cbSize = sizeof(settingsClass);
        settingsClass.lpfnWndProc = SettingsWndProc;
        settingsClass.hInstance = instance;
        settingsClass.lpszClassName = "VideoAnalyzerSettingsWindow";
        settingsClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        settingsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassExA(&settingsClass);

        registered = true;
    }

    void PaintSettingsWindow(HDC hdc)
    {
        RECT client{};
        GetClientRect(g_ui.settingsWindow, &client);
        FillRect(hdc, &client, g_ui.appBrush);

        RECT footer = client;
        footer.top = footer.bottom - 56;
        FillRect(hdc, &footer, g_ui.appBrush);

        if (g_ui.settingsPanel.right > g_ui.settingsPanel.left)
            FillRoundedRect(hdc, g_ui.settingsPanel, AppBgColor(), CardBorderColor());

        if (g_ui.currentSettingsTab == 3 && g_ui.settingsPanel.right > g_ui.settingsPanel.left)
        {
            RECT separator{
                g_ui.settingsPanel.left + 16,
                g_ui.settingsPanel.top + 48,
                g_ui.settingsPanel.right - 16,
                g_ui.settingsPanel.top + 49
            };
            HBRUSH brush = CreateSolidBrush(RGB(157, 169, 185));
            FillRect(hdc, &separator, brush);
            DeleteObject(brush);
        }
    }
}

bool UI_Create(HWND hwnd, HINSTANCE instance, AppState& state)
{
    g_ui = {};
    g_ui.mainWindow = hwnd;
    g_ui.instance = instance;
    g_ui.state = &state;
    g_ui.lastVizOk = state.lastVizOk;

    RegisterWindowClasses(instance);

    NONCLIENTMETRICSA metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    g_ui.font = CreateFontIndirectA(&metrics.lfMessageFont);
    LOGFONTA titleLf = metrics.lfMessageFont;
    titleLf.lfHeight = -22;
    titleLf.lfWeight = FW_SEMIBOLD;
    strcpy_s(titleLf.lfFaceName, "Segoe UI");
    g_ui.titleFont = CreateFontIndirectA(&titleLf);
    LOGFONTA sectionLf = metrics.lfMessageFont;
    sectionLf.lfHeight = -13;
    sectionLf.lfWeight = FW_SEMIBOLD;
    strcpy_s(sectionLf.lfFaceName, "Segoe UI");
    g_ui.sectionFont = CreateFontIndirectA(&sectionLf);

    g_ui.appBrush = CreateSolidBrush(AppBgColor());
    g_ui.headerBrush = CreateSolidBrush(HeaderBgColor());
    g_ui.cardBrush = CreateSolidBrush(CardBgColor());
    g_ui.panelBrush = CreateSolidBrush(AppBgColor());
    g_ui.previewBrush = CreateSolidBrush(PreviewFrameColor());

    g_ui.headerLabel = CreateControlA(0, "STATIC", kAppHeaderTextA, WS_CHILD | WS_VISIBLE, 0, hwnd);
    SetControlFont(g_ui.headerLabel, g_ui.titleFont);
    SetWindowTextA(hwnd, kAppWindowTitleA);
    g_ui.settingsButton = CreateButtonA("Settings", IDC_SETTINGS_BUTTON, hwnd, true);
    g_ui.deviceLabel = CreateControlA(0, "STATIC", "Video Device", WS_CHILD | WS_VISIBLE, 0, hwnd);
    g_ui.deviceCombo = CreateControlA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_DEVICE_COMBO, hwnd);
    g_ui.templateLabel = CreateControlA(0, "STATIC", "Template", WS_CHILD | WS_VISIBLE, 0, hwnd);
    g_ui.templateCombo = CreateControlA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_TEMPLATE_COMBO, hwnd);
    g_ui.rendererLabel = CreateControlA(0, "STATIC", "Connection:", WS_CHILD | WS_VISIBLE, 0, hwnd);
    g_ui.rendererStatus = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_RENDERER_STATUS, hwnd);
    g_ui.nextCueButton = CreateButtonA("", IDC_NEXT_CUE_BUTTON, hwnd, true);
    g_ui.cuePreviewWindow = CreateControlA(0, "VideoAnalyzerCuePreviewWindow", "", WS_CHILD | WS_VISIBLE, IDC_CUE_PREVIEW_WINDOW, hwnd);
    g_ui.previewCheck = CreateControlA(0, "BUTTON", "Preview", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, IDC_PREVIEW_CHECK, hwnd);
    g_ui.previewWindow = CreateControlA(0, "VideoAnalyzerPreviewWindow", "", WS_CHILD | WS_VISIBLE, IDC_PREVIEW_WINDOW, hwnd);
    g_ui.ocrStatusLabel = CreateControlA(0, "STATIC", "[OCR Disabled]", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 0, hwnd);
    g_ui.autoScrollCheck = CreateControlA(0, "BUTTON", "Auto-scroll", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, IDC_AUTOSCROLL_CHECK, hwnd);
    g_ui.clearLogButton = CreateButtonA("Clear", IDC_CLEAR_LOG_BUTTON, hwnd, true);
    g_ui.logEdit = CreateControlA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
        IDC_LOG_EDIT,
        hwnd);

    SetControlFont(g_ui.deviceLabel, g_ui.sectionFont);
    SetControlFont(g_ui.templateLabel, g_ui.sectionFont);
    SetControlFont(g_ui.rendererLabel, g_ui.sectionFont);
    SetControlFont(g_ui.rendererStatus, g_ui.sectionFont);
    SetControlFont(g_ui.ocrStatusLabel, g_ui.sectionFont);

    g_ui.settingsWindow = CreateWindowExA(
        WS_EX_APPWINDOW,
        "VideoAnalyzerSettingsWindow",
        kAppSettingsWindowTitleA,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        470,
        hwnd,
        nullptr,
        instance,
        nullptr);

    if (!g_ui.settingsWindow)
        return false;

    SendMessageA(g_ui.logEdit, EM_SETLIMITTEXT, 0, 0);
    SetWindowTheme(g_ui.deviceCombo, L"Explorer", nullptr);
    SetWindowTheme(g_ui.templateCombo, L"Explorer", nullptr);
    SetWindowTheme(g_ui.logEdit, L"Explorer", nullptr);
    CreateSettingsWindowControls();
    SyncSettingsFromState(state);

    SetCheckState(g_ui.previewCheck, state.previewEnabled);
    SetCheckState(g_ui.autoScrollCheck, state.autoScrollLog);
    UpdateRendererStatus(state);
    UpdateNextCueButton(state);
    UpdateCuePreview(state);
    UpdateDeviceList(state);
    UpdateTemplateControls(state);
    UpdateLogView(state);
    ShowSettingsTab(0);

    RECT client{};
    GetClientRect(hwnd, &client);
    LayoutMainWindow(client.right - client.left, client.bottom - client.top);

    RECT settingsClient{};
    GetClientRect(g_ui.settingsWindow, &settingsClient);
    LayoutSettingsWindow(settingsClient.right - settingsClient.left, settingsClient.bottom - settingsClient.top);

    return true;
}

void UI_Destroy()
{
    Renderer_ClearPreview();

    if (g_ui.settingsWindow)
    {
        DestroyWindow(g_ui.settingsWindow);
        g_ui.settingsWindow = nullptr;
    }

    if (g_ui.font)
    {
        DeleteObject(g_ui.font);
        g_ui.font = nullptr;
    }
    if (g_ui.sectionFont)
    {
        DeleteObject(g_ui.sectionFont);
        g_ui.sectionFont = nullptr;
    }
    if (g_ui.titleFont)
    {
        DeleteObject(g_ui.titleFont);
        g_ui.titleFont = nullptr;
    }
    if (g_ui.appBrush)
    {
        DeleteObject(g_ui.appBrush);
        g_ui.appBrush = nullptr;
    }
    if (g_ui.headerBrush)
    {
        DeleteObject(g_ui.headerBrush);
        g_ui.headerBrush = nullptr;
    }
    if (g_ui.cardBrush)
    {
        DeleteObject(g_ui.cardBrush);
        g_ui.cardBrush = nullptr;
    }
    if (g_ui.panelBrush)
    {
        DeleteObject(g_ui.panelBrush);
        g_ui.panelBrush = nullptr;
    }
    if (g_ui.previewBrush)
    {
        DeleteObject(g_ui.previewBrush);
        g_ui.previewBrush = nullptr;
    }

    g_ui = {};
}

void UI_OnSize(int width, int height)
{
    LayoutMainWindow(width, height);
}

bool UI_HandleMainCommand(WPARAM wParam, LPARAM, AppState& state)
{
    switch (LOWORD(wParam))
    {
    case IDC_SETTINGS_BUTTON:
        ShowSettingsWindow(state);
        return true;

    case IDC_DEVICE_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            const int selectedIndex = static_cast<int>(SendMessageA(g_ui.deviceCombo, CB_GETCURSEL, 0, 0));
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(state.availableDevices.size()))
            {
                state.selectedDeviceListIndex = selectedIndex;
                state.selectedSourceKind = state.availableDevices[selectedIndex].kind;
                state.cameraIndex = state.availableDevices[selectedIndex].id;
                state.currentCamera = -1;
                g_ui.deviceSignature.clear();
            }
            return true;
        }
        break;

    case IDC_TEMPLATE_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            const int selectedIndex = static_cast<int>(SendMessageA(g_ui.templateCombo, CB_GETCURSEL, 0, 0));
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(state.templates.size()))
            {
                Detection_SetActiveTemplate(state, state.templates[static_cast<size_t>(selectedIndex)].name);
                g_ui.templateSignature.clear();
                SaveConfigAndRefresh(state);
            }
            return true;
        }
        break;

    case IDC_NEXT_CUE_BUTTON:
        Detection_FlipCue(state);
        UpdateNextCueButton(state);
        UpdateCuePreview(state);
        return true;

    case IDC_PREVIEW_CHECK:
        state.previewEnabled = GetCheckState(g_ui.previewCheck);
        if (!state.previewEnabled)
            Renderer_ClearPreview();
        InvalidateRect(g_ui.previewWindow, nullptr, FALSE);
        return true;

    case IDC_AUTOSCROLL_CHECK:
        state.autoScrollLog = GetCheckState(g_ui.autoScrollCheck);
        return true;

    case IDC_CLEAR_LOG_BUTTON:
        g_logs.clear();
        g_ui.lastLogCount = 0;
        g_ui.lastLogTail.clear();
        SetWindowTextA(g_ui.logEdit, "");
        return true;
    }

    return false;
}

HBRUSH UI_HandleCtlColor(HDC hdc, HWND control)
{
    if (control == g_ui.headerLabel)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        return g_ui.headerBrush;
    }

    if (control == g_ui.vizIpEdit ||
        control == g_ui.vizPortEdit ||
        control == g_ui.cmdOnEdit ||
        control == g_ui.cmdOffEdit ||
        control == g_ui.templatesList ||
        control == g_ui.scorebugsList ||
        control == g_ui.scorebugPropsList)
    {
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, AppBgColor());
        SetTextColor(hdc, BodyTextColor());
        return g_ui.panelBrush;
    }

    if (control == g_ui.rendererStatus || control == g_ui.settingsStatus)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_ui.lastVizOk ? SuccessColor() : DangerColor());
        return (control == g_ui.settingsStatus) ? g_ui.panelBrush : g_ui.appBrush;
    }

    if (control == g_ui.previewCheck || control == g_ui.autoScrollCheck)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, BodyTextColor());
        return g_ui.cardBrush;
    }

    if (control == g_ui.detectionEnabledCheck || control == g_ui.scorebugEnabledCheck)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, BodyTextColor());
        return g_ui.panelBrush;
    }

    if (control == g_ui.deviceLabel ||
        control == g_ui.templateLabel ||
        control == g_ui.ocrStatusLabel ||
        control == g_ui.rendererLabel ||
        control == g_ui.detectThresholdLabel ||
        control == g_ui.detectThresholdValue ||
        control == g_ui.resetThresholdLabel ||
        control == g_ui.resetThresholdValue ||
        control == g_ui.cooldownLabel ||
        control == g_ui.cooldownValue ||
        control == g_ui.templatePresetsLabel ||
        control == g_ui.scorebugThresholdLabel ||
        control == g_ui.scorebugThresholdValue ||
        control == g_ui.scorebugPresetsLabel ||
        control == g_ui.vizIpLabel ||
        control == g_ui.vizPortLabel ||
        control == g_ui.cmdOnLabel ||
        control == g_ui.cmdOffLabel ||
        control == g_ui.templateDetails ||
        control == g_ui.scorebugDetails)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, BodyTextColor());
        if (control == g_ui.deviceLabel || control == g_ui.templateLabel || control == g_ui.ocrStatusLabel)
            return g_ui.cardBrush;
        if (control == g_ui.rendererLabel)
            return g_ui.appBrush;
        return g_ui.panelBrush;
    }

    return g_ui.appBrush;
}

bool UI_HandleDrawItem(const DRAWITEMSTRUCT& drawItem, const AppState& state)
{
    if (drawItem.CtlType != ODT_BUTTON)
        return false;

    const bool hot = (drawItem.itemState & ODS_HOTLIGHT) != 0;
    const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
    const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;

    COLORREF fill = CardBgColor();
    COLORREF border = CardBorderColor();
    COLORREF text = BodyTextColor();

    switch (drawItem.CtlID)
    {
    case IDC_SETTINGS_TAB_DETECTION:
    case IDC_SETTINGS_TAB_ENGINE:
    case IDC_SETTINGS_TAB_TEMPLATES:
    case IDC_SETTINGS_TAB_SCOREBUG:
    {
        const int tabIndex =
            drawItem.CtlID == IDC_SETTINGS_TAB_DETECTION ? 0 :
            drawItem.CtlID == IDC_SETTINGS_TAB_ENGINE ? 1 :
            drawItem.CtlID == IDC_SETTINGS_TAB_TEMPLATES ? 2 : 3;
        const bool activeTab = g_ui.currentSettingsTab == tabIndex;
        fill = activeTab ? CardBgColor() : AppBgColor();
        border = activeTab ? CardBorderColor() : RGB(206, 216, 227);
        text = activeTab ? BodyTextColor() : MutedTextColor();
        break;
    }
    case IDC_SETTINGS_BUTTON:
        fill = hot ? RGB(244, 248, 252) : RGB(255, 255, 255);
        border = hot ? RGB(194, 206, 221) : RGB(214, 223, 233);
        text = AccentColor();
        break;
    case IDC_CLEAR_LOG_BUTTON:
    case IDC_TEMPLATE_NEW_BUTTON:
    case IDC_TEMPLATE_EDIT_BUTTON:
    case IDC_TEMPLATE_DELETE_BUTTON:
    case IDC_TEMPLATE_ACTIVATE_BUTTON:
    case IDC_SCOREBUG_NEW_BUTTON:
    case IDC_SCOREBUG_EDIT_BUTTON:
    case IDC_SCOREBUG_DELETE_BUTTON:
    case IDC_SCOREBUG_ACTIVATE_BUTTON:
        fill = hot ? RGB(241, 245, 249) : RGB(255, 255, 255);
        border = hot ? RGB(180, 192, 208) : RGB(203, 213, 225);
        text = BodyTextColor();
        break;
    case IDC_SETTINGS_SAVE_BUTTON:
        fill = hot ? AccentHoverColor() : AccentColor();
        border = fill;
        text = RGB(255, 255, 255);
        break;
    case IDC_NEXT_CUE_BUTTON:
        if (state.cueState == CueState::WIPER_IN)
        {
            fill = hot ? RGB(22, 101, 192) : AccentColor();
            border = fill;
            text = RGB(255, 255, 255);
        }
        else
        {
            fill = hot ? RGB(194, 65, 12) : RGB(234, 88, 12);
            border = fill;
            text = RGB(255, 255, 255);
        }
        break;
    default:
        return false;
    }

    if (disabled)
    {
        fill = RGB(226, 232, 240);
        border = RGB(203, 213, 225);
        text = RGB(148, 163, 184);
    }

    RECT rect = drawItem.rcItem;
    HDC hdc = drawItem.hDC;
    FillRoundedRect(hdc, rect, fill, border);

    if (selected)
        OffsetRect(&rect, 0, 1);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(
        hdc,
        (drawItem.CtlID == IDC_SETTINGS_BUTTON || drawItem.CtlID == IDC_SETTINGS_TAB_DETECTION ||
         drawItem.CtlID == IDC_SETTINGS_TAB_ENGINE || drawItem.CtlID == IDC_SETTINGS_TAB_TEMPLATES ||
         drawItem.CtlID == IDC_SETTINGS_TAB_SCOREBUG)
            ? g_ui.font
            : (g_ui.sectionFont ? g_ui.sectionFont : g_ui.font)));

    wchar_t caption[256] = {};
    GetWindowTextW(drawItem.hwndItem, caption, static_cast<int>(std::size(caption)));
    if (drawItem.CtlID == IDC_NEXT_CUE_BUTTON)
    {
        RECT textRect = rect;
        InflateRect(&textRect, -8, -8);

        RECT measureRect = textRect;
        DrawTextW(hdc, caption, -1, &measureRect, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

        const int availableHeight = textRect.bottom - textRect.top;
        const int textHeight = measureRect.bottom - measureRect.top;
        if (textHeight < availableHeight)
        {
            textRect.top += (availableHeight - textHeight) / 2;
            textRect.bottom = textRect.top + textHeight;
        }

        DrawTextW(hdc, caption, -1, &textRect, DT_CENTER | DT_WORDBREAK);
    }
    else
    {
        DrawTextW(hdc, caption, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (drawItem.itemState & ODS_FOCUS)
    {
        RECT focusRect = drawItem.rcItem;
        InflateRect(&focusRect, -4, -4);
        DrawFocusRect(hdc, &focusRect);
    }

    SelectObject(hdc, oldFont);
    return true;
}

void UI_PaintMain(HDC hdc)
{
    RECT client{};
    GetClientRect(g_ui.mainWindow, &client);
    FillRect(hdc, &client, g_ui.appBrush);

    FillRect(hdc, &g_ui.headerBand, g_ui.headerBrush);

    FillRoundedRect(hdc, g_ui.controlCard, CardBgColor(), CardBorderColor());
    FillRoundedRect(hdc, g_ui.previewCard, CardBgColor(), CardBorderColor());
    FillRoundedRect(hdc, g_ui.logCard, CardBgColor(), CardBorderColor());

}

void UI_SyncState(const AppState& state)
{
    SetCheckState(g_ui.previewCheck, state.previewEnabled);
    SetCheckState(g_ui.autoScrollCheck, state.autoScrollLog);
    SetWindowTextIfChanged(g_ui.ocrStatusLabel, BuildMainOcrStatusText(state));
    UpdateRendererStatus(state);
    UpdateNextCueButton(state);
    UpdateCuePreview(state);
    UpdateDeviceList(state);
    UpdateTemplateControls(state);
    if (g_ui.settingsWindow && IsWindowVisible(g_ui.settingsWindow))
        UpdateScorebugControls(state);
    UpdateLogView(state);
}

void UI_UpdatePreview(const cv::Mat& bgrFrame)
{
    Renderer_SetPreviewFrame(bgrFrame);
    InvalidateRect(g_ui.previewWindow, nullptr, FALSE);
}

void UI_ClearPreview()
{
    Renderer_ClearPreview();
    InvalidateRect(g_ui.previewWindow, nullptr, FALSE);
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        Renderer_PaintPreview(hdc, rect, g_ui.state ? g_ui.state->previewEnabled : false);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CuePreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        Renderer_PaintCuePreview(hdc, rect, g_ui.state ? g_ui.state->activeTemplateLoaded : false);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintSettingsWindow(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SHOWWINDOW:
        if (wParam && g_ui.state)
        {
            SyncSettingsFromState(*g_ui.state);
            UpdateTemplateControls(*g_ui.state);
            UpdateScorebugControls(*g_ui.state);
        }
        break;

    case WM_SIZE:
        LayoutSettingsWindow(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DRAWITEM:
        if (g_ui.state && UI_HandleDrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), *g_ui.state))
            return TRUE;
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_SETTINGS_TAB_DETECTION:
            ShowSettingsTab(0);
            return 0;
        case IDC_SETTINGS_TAB_ENGINE:
            ShowSettingsTab(1);
            return 0;
        case IDC_SETTINGS_TAB_TEMPLATES:
            ShowSettingsTab(2);
            return 0;
        case IDC_SETTINGS_TAB_SCOREBUG:
            ShowSettingsTab(3);
            return 0;
        case IDC_TEMPLATES_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE && g_ui.state)
            {
                g_ui.templateSelectionName = GetSelectedTemplateNameFromList();
                UpdateTemplateDetails(*g_ui.state);
                return 0;
            }
            break;
        case IDC_SCOREBUGS_LIST:
            if (!g_ui.state)
                break;
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                g_ui.scorebugSelectionName = GetSelectedScorebugNameFromList();
                g_ui.scorebugPropSelectionName.clear();
                UpdateScorebugControls(*g_ui.state);
                return 0;
            }
            if (HIWORD(wParam) == LBN_DBLCLK)
            {
                const std::string selectedName = GetSelectedScorebugNameFromList();
                if (const OcrElementManifest* element = Scorebug_FindLayoutByName(*g_ui.state, selectedName))
                    RunScorebugEditor(*g_ui.state, element, selectedName);
                return 0;
            }
            break;
        case IDC_SCOREBUG_PROPS_LIST:
            if (!g_ui.state)
                break;
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                g_ui.scorebugPropSelectionName = GetSelectedScorebugPropNameFromList();
                UpdateScorebugControls(*g_ui.state);
                return 0;
            }
            if (HIWORD(wParam) == LBN_DBLCLK)
            {
                const std::string selectedName = GetSelectedScorebugNameFromList();
                const std::string propName = GetSelectedScorebugPropNameFromList();
                if (const OcrElementManifest* element = Scorebug_FindLayoutByName(*g_ui.state, selectedName))
                {
                    for (const auto& prop : element->props)
                    {
                        if (prop.name == propName)
                        {
                            RunScorebugPropEditor(*g_ui.state, *element, &prop, propName);
                            break;
                        }
                    }
                }
                return 0;
            }
            break;
        case IDC_TEMPLATE_NEW_BUTTON:
            if (g_ui.state)
            {
                RunTemplateEditor(*g_ui.state, nullptr, "");
                return 0;
            }
            break;
        case IDC_TEMPLATE_EDIT_BUTTON:
            if (g_ui.state)
            {
                const std::string selectedName = GetSelectedTemplateNameFromList();
                if (const TemplateManifest* manifest = Templates_FindByName(*g_ui.state, selectedName))
                {
                    RunTemplateEditor(*g_ui.state, manifest, selectedName);
                    return 0;
                }
            }
            break;
        case IDC_TEMPLATE_DELETE_BUTTON:
            if (g_ui.state)
            {
                DeleteSelectedTemplate(*g_ui.state);
                return 0;
            }
            break;
        case IDC_TEMPLATE_ACTIVATE_BUTTON:
            if (g_ui.state)
            {
                const std::string selectedName = GetSelectedTemplateNameFromList();
                if (!selectedName.empty())
                {
                    Detection_SetActiveTemplate(*g_ui.state, selectedName);
                    g_ui.templateSignature.clear();
                    g_ui.templateSelectionName = selectedName;
                    SaveConfigAndRefresh(*g_ui.state);
                    return 0;
                }
            }
            break;
        case IDC_SCOREBUG_NEW_BUTTON:
            if (g_ui.state)
            {
                RunScorebugEditor(*g_ui.state, nullptr, "");
                return 0;
            }
            break;
        case IDC_SCOREBUG_EDIT_BUTTON:
            if (g_ui.state)
            {
                const std::string elementName = GetSelectedScorebugNameFromList();
                if (const OcrElementManifest* element = Scorebug_FindLayoutByName(*g_ui.state, elementName))
                    RunScorebugPropEditor(*g_ui.state, *element, nullptr, "");
                return 0;
            }
            break;
        case IDC_SCOREBUG_DELETE_BUTTON:
            if (g_ui.state)
            {
                DeleteSelectedScorebug(*g_ui.state);
                return 0;
            }
            break;
        case IDC_SCOREBUG_ACTIVATE_BUTTON:
            if (g_ui.state)
            {
                DeleteSelectedScorebugProp(*g_ui.state);
                return 0;
            }
            break;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_SAVE_BUTTON && g_ui.state)
        {
            SaveSettingsToState(*g_ui.state);
            Scorebug_SetActiveLayout(*g_ui.state, g_ui.state->activeOcrElementName);
            Config_Save(*g_ui.state);
            SyncSettingsFromState(*g_ui.state);
            UI_SyncState(*g_ui.state);
            return 0;
        }
        break;

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_ui.detectThresholdSlider ||
            reinterpret_cast<HWND>(lParam) == g_ui.resetThresholdSlider ||
            reinterpret_cast<HWND>(lParam) == g_ui.cooldownSlider)
        {
            UpdateDetectionSliderLabels();
            return 0;
        }
        if (reinterpret_cast<HWND>(lParam) == g_ui.scorebugThresholdSlider)
        {
            UpdateScorebugSliderLabel();
            return 0;
        }
        break;

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HBRUSH brush = UI_HandleCtlColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
        if (brush)
            return reinterpret_cast<LRESULT>(brush);
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
