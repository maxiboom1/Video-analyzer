#include "ScorebugDialogs.h"

#include <windows.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <opencv2/opencv.hpp>

namespace
{
    constexpr int IDC_LAYOUT_NAME_EDIT = 4101;
    constexpr int IDC_REFERENCE_EDIT = 4102;
    constexpr int IDC_REFERENCE_BROWSE = 4103;
    constexpr int IDC_FRAME_ROI_BUTTON = 4104;
    constexpr int IDC_FRAME_ROI_LABEL = 4105;
    constexpr int IDC_TEAM_A_LABEL_BUTTON = 4106;
    constexpr int IDC_TEAM_A_LABEL_LABEL = 4107;
    constexpr int IDC_TEAM_A_SCORE_BUTTON = 4108;
    constexpr int IDC_TEAM_A_SCORE_LABEL = 4109;
    constexpr int IDC_TEAM_B_LABEL_BUTTON = 4110;
    constexpr int IDC_TEAM_B_LABEL_LABEL = 4111;
    constexpr int IDC_TEAM_B_SCORE_BUTTON = 4112;
    constexpr int IDC_TEAM_B_SCORE_LABEL = 4113;
    constexpr int IDC_PERIOD_BUTTON = 4114;
    constexpr int IDC_PERIOD_LABEL = 4115;
    constexpr int IDC_GAME_CLOCK_BUTTON = 4116;
    constexpr int IDC_GAME_CLOCK_LABEL = 4117;
    constexpr int IDC_SHOT_CLOCK_BUTTON = 4118;
    constexpr int IDC_SHOT_CLOCK_LABEL = 4119;
    constexpr int IDC_LAYOUT_SAVE = 4120;
    constexpr int IDC_LAYOUT_CANCEL = 4121;
    constexpr int IDC_LAYOUT_HINT = 4122;

    constexpr int IDC_ROI_OK = 4201;
    constexpr int IDC_ROI_CANCEL = 4202;
    constexpr int IDC_ROI_CLEAR = 4203;
    constexpr int IDC_ROI_INFO = 4204;

    const char* kScorebugEditorClass = "VideoAnalyzerScorebugEditorWindow";
    const char* kScorebugRoiClass = "VideoAnalyzerScorebugRoiWindow";

    struct ScorebugEditorState
    {
        HWND owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        HWND nameEdit = nullptr;
        HWND referenceEdit = nullptr;
        bool accepted = false;
        bool running = true;
        ScorebugDraft draft;
        std::string error;
        std::array<HWND, 8> roiLabels{};
    };

    struct RoiEditorState
    {
        HWND owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        HWND infoLabel = nullptr;
        bool accepted = false;
        bool running = true;
        bool dragging = false;
        cv::Mat image;
        NormalizedRoi roi;
        RECT canvasRect{ 0, 0, 0, 0 };
        RECT imageRect{ 0, 0, 0, 0 };
        POINT dragStart{ 0, 0 };
        POINT dragCurrent{ 0, 0 };
        std::string title;
    };

    std::string GetWindowTextString(HWND hwnd)
    {
        const int length = GetWindowTextLengthA(hwnd);
        std::string value(length + 1, '\0');
        if (length > 0)
            GetWindowTextA(hwnd, &value[0], length + 1);
        value.resize(length);
        return value;
    }

    void SetControlFont(HWND hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }

    HWND CreateControlA(
        DWORD exStyle,
        const char* className,
        const char* text,
        DWORD style,
        int id,
        HWND parent,
        HINSTANCE instance)
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
            instance,
            nullptr);
        if (hwnd)
            SetControlFont(hwnd);
        return hwnd;
    }

    std::string FormatRoi(const NormalizedRoi& roi)
    {
        if (!roi.enabled)
            return "Not set";
        char buffer[128] = {};
        sprintf_s(buffer, "x=%.3f y=%.3f w=%.3f h=%.3f", roi.x, roi.y, roi.w, roi.h);
        return buffer;
    }

    bool ShowOpenImageDialog(HWND owner, std::string& path)
    {
        char filePath[MAX_PATH] = {};
        if (!path.empty())
            strncpy_s(filePath, path.c_str(), _TRUNCATE);

        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetOpenFileNameA(&ofn))
            return false;

        path = filePath;
        return true;
    }

    RECT FitRectPreservingAspect(const RECT& bounds, int srcW, int srcH)
    {
        RECT dest = bounds;
        if (srcW <= 0 || srcH <= 0)
            return dest;

        const int dstW = bounds.right - bounds.left;
        const int dstH = bounds.bottom - bounds.top;
        if (dstW <= 0 || dstH <= 0)
            return dest;

        const double srcAspect = static_cast<double>(srcW) / static_cast<double>(srcH);
        const double dstAspect = static_cast<double>(dstW) / static_cast<double>(dstH);
        int drawW = dstW;
        int drawH = dstH;
        if (srcAspect > dstAspect)
            drawH = static_cast<int>(drawW / srcAspect);
        else
            drawW = static_cast<int>(drawH * srcAspect);

        dest.left = bounds.left + (dstW - drawW) / 2;
        dest.top = bounds.top + (dstH - drawH) / 2;
        dest.right = dest.left + drawW;
        dest.bottom = dest.top + drawH;
        return dest;
    }

    POINT ClampPointToRect(POINT pt, const RECT& rect)
    {
        pt.x = std::max(rect.left, std::min(rect.right - 1, pt.x));
        pt.y = std::max(rect.top, std::min(rect.bottom - 1, pt.y));
        return pt;
    }

    RECT NormalizeRectFromPoints(POINT a, POINT b)
    {
        RECT rect{};
        rect.left = std::min(a.x, b.x);
        rect.right = std::max(a.x, b.x);
        rect.top = std::min(a.y, b.y);
        rect.bottom = std::max(a.y, b.y);
        return rect;
    }

    RECT RoiToCanvasRect(const NormalizedRoi& roi, const RECT& imageRect)
    {
        if (!roi.enabled)
            return RECT{ imageRect.left, imageRect.top, imageRect.left, imageRect.top };

        RECT rect{};
        const int width = imageRect.right - imageRect.left;
        const int height = imageRect.bottom - imageRect.top;
        rect.left = imageRect.left + static_cast<int>(roi.x * width + 0.5f);
        rect.top = imageRect.top + static_cast<int>(roi.y * height + 0.5f);
        rect.right = rect.left + static_cast<int>(roi.w * width + 0.5f);
        rect.bottom = rect.top + static_cast<int>(roi.h * height + 0.5f);
        rect.right = std::max(rect.left + 1, std::min(imageRect.right, rect.right));
        rect.bottom = std::max(rect.top + 1, std::min(imageRect.bottom, rect.bottom));
        return rect;
    }

    NormalizedRoi CanvasRectToRoi(const RECT& selection, const RECT& imageRect)
    {
        NormalizedRoi roi;
        const int width = imageRect.right - imageRect.left;
        const int height = imageRect.bottom - imageRect.top;
        const int selW = selection.right - selection.left;
        const int selH = selection.bottom - selection.top;
        if (width <= 0 || height <= 0 || selW <= 0 || selH <= 0)
            return roi;

        roi.enabled = true;
        roi.x = static_cast<float>(selection.left - imageRect.left) / static_cast<float>(width);
        roi.y = static_cast<float>(selection.top - imageRect.top) / static_cast<float>(height);
        roi.w = static_cast<float>(selW) / static_cast<float>(width);
        roi.h = static_cast<float>(selH) / static_cast<float>(height);
        return roi;
    }

    void UpdateRoiInfo(RoiEditorState* state)
    {
        if (!state || !state->infoLabel)
            return;
        const std::string text = "ROI: " + FormatRoi(state->roi);
        SetWindowTextA(state->infoLabel, text.c_str());
    }

    void DrawImage(HDC hdc, const cv::Mat& image, const RECT& dest)
    {
        if (image.empty())
            return;

        cv::Mat bgraImage;
        switch (image.channels())
        {
        case 4:
            bgraImage = image;
            break;
        case 3:
            cv::cvtColor(image, bgraImage, cv::COLOR_BGR2BGRA);
            break;
        case 1:
            cv::cvtColor(image, bgraImage, cv::COLOR_GRAY2BGRA);
            break;
        default:
            return;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgraImage.cols;
        bmi.bmiHeader.biHeight = -bgraImage.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, HALFTONE);
        StretchDIBits(
            hdc,
            dest.left,
            dest.top,
            dest.right - dest.left,
            dest.bottom - dest.top,
            0,
            0,
            bgraImage.cols,
            bgraImage.rows,
            bgraImage.data,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    LRESULT CALLBACK RoiEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<RoiEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_NCCREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE:
            state = reinterpret_cast<RoiEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (!state)
                return -1;
            state->window = hwnd;
            state->infoLabel = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_ROI_INFO, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Clear ROI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_ROI_CLEAR, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_ROI_CANCEL, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, IDC_ROI_OK, hwnd, state->instance);
            UpdateRoiInfo(state);
            return 0;

        case WM_SIZE:
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            state->canvasRect = RECT{ 12, 40, client.right - 12, client.bottom - 56 };
            MoveWindow(state->infoLabel, 12, 12, client.right - 24, 18, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_ROI_CLEAR), 12, client.bottom - 38, 90, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_ROI_CANCEL), client.right - 188, client.bottom - 38, 84, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_ROI_OK), client.right - 96, client.bottom - 38, 84, 26, TRUE);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (state && PtInRect(&state->imageRect, pt))
            {
                state->dragging = true;
                state->dragStart = ClampPointToRect(pt, state->imageRect);
                state->dragCurrent = state->dragStart;
                SetCapture(hwnd);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (state && state->dragging)
            {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->dragCurrent = ClampPointToRect(pt, state->imageRect);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (state && state->dragging)
            {
                ReleaseCapture();
                state->dragging = false;
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->dragCurrent = ClampPointToRect(pt, state->imageRect);
                RECT selection = NormalizeRectFromPoints(state->dragStart, state->dragCurrent);
                if ((selection.right - selection.left) > 2 && (selection.bottom - selection.top) > 2)
                    state->roi = CanvasRectToRoi(selection, state->imageRect);
                UpdateRoiInfo(state);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_ROI_CLEAR:
                state->roi = {};
                UpdateRoiInfo(state);
                InvalidateRect(hwnd, &state->canvasRect, FALSE);
                return 0;
            case IDC_ROI_CANCEL:
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            case IDC_ROI_OK:
                state->accepted = true;
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HDC memDc = CreateCompatibleDC(hdc);
            HBITMAP backBuffer = CreateCompatibleBitmap(hdc, client.right - client.left, client.bottom - client.top);
            HGDIOBJ oldBitmap = SelectObject(memDc, backBuffer);

            FillRect(memDc, &client, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

            HBRUSH canvasBrush = CreateSolidBrush(RGB(20, 20, 20));
            FillRect(memDc, &state->canvasRect, canvasBrush);
            DeleteObject(canvasBrush);
            FrameRect(memDc, &state->canvasRect, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

            state->imageRect = FitRectPreservingAspect(state->canvasRect, state->image.cols, state->image.rows);
            DrawImage(memDc, state->image, state->imageRect);

            RECT roiRect = state->dragging
                ? NormalizeRectFromPoints(state->dragStart, state->dragCurrent)
                : RoiToCanvasRect(state->roi, state->imageRect);
            if ((roiRect.right - roiRect.left) > 1 && (roiRect.bottom - roiRect.top) > 1)
            {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(27, 94, 181));
                HGDIOBJ oldPen = SelectObject(memDc, pen);
                HGDIOBJ oldBrush = SelectObject(memDc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(memDc, roiRect.left, roiRect.top, roiRect.right, roiRect.bottom);
                SelectObject(memDc, oldBrush);
                SelectObject(memDc, oldPen);
                DeleteObject(pen);
            }

            BitBlt(hdc, 0, 0, client.right - client.left, client.bottom - client.top, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBitmap);
            DeleteObject(backBuffer);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            if (state)
                state->running = false;
            DestroyWindow(hwnd);
            return 0;
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    bool ShowRoiDialog(HWND owner, HINSTANCE instance, const char* title, const cv::Mat& image, NormalizedRoi& roi)
    {
        RoiEditorState state;
        state.owner = owner;
        state.instance = instance;
        state.image = image;
        state.roi = roi;
        state.title = title ? title : "Scorebug ROI";

        HWND hwnd = CreateWindowExA(
            WS_EX_DLGMODALFRAME,
            kScorebugRoiClass,
            state.title.c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            920,
            720,
            owner,
            nullptr,
            instance,
            &state);
        if (!hwnd)
            return false;

        EnableWindow(owner, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg{};
        while (state.running && IsWindow(hwnd) && GetMessageA(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
        if (state.accepted)
            roi = state.roi;
        return state.accepted;
    }

    cv::Rect RoiToImageRect(const NormalizedRoi& roi, int width, int height)
    {
        if (!roi.enabled || width <= 0 || height <= 0)
            return cv::Rect();

        const int x = std::clamp(static_cast<int>(roi.x * width + 0.5f), 0, std::max(0, width - 1));
        const int y = std::clamp(static_cast<int>(roi.y * height + 0.5f), 0, std::max(0, height - 1));
        const int w = std::clamp(static_cast<int>(roi.w * width + 0.5f), 1, width - x);
        const int h = std::clamp(static_cast<int>(roi.h * height + 0.5f), 1, height - y);
        return cv::Rect(x, y, w, h);
    }

    void RegisterDialogClasses(HINSTANCE instance, WNDPROC editorProc)
    {
        static bool registered = false;
        if (registered)
            return;

        WNDCLASSEXA editorClass{};
        editorClass.cbSize = sizeof(editorClass);
        editorClass.lpfnWndProc = editorProc;
        editorClass.hInstance = instance;
        editorClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        editorClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        editorClass.lpszClassName = kScorebugEditorClass;
        RegisterClassExA(&editorClass);

        WNDCLASSEXA roiClass{};
        roiClass.cbSize = sizeof(roiClass);
        roiClass.lpfnWndProc = RoiEditorWndProc;
        roiClass.hInstance = instance;
        roiClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        roiClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        roiClass.lpszClassName = kScorebugRoiClass;
        RegisterClassExA(&roiClass);

        registered = true;
    }

    cv::Mat LoadReferenceImage(const ScorebugDraft& draft)
    {
        if (draft.referenceImagePath.empty())
            return {};
        return cv::imread(draft.referenceImagePath, cv::IMREAD_COLOR);
    }

    cv::Mat CropFrameRegion(const ScorebugDraft& draft, const cv::Mat& reference)
    {
        if (reference.empty() || !draft.frameRoi.enabled)
            return {};

        const cv::Rect rect = RoiToImageRect(draft.frameRoi, reference.cols, reference.rows);
        if (rect.width <= 0 || rect.height <= 0)
            return {};
        return reference(rect).clone();
    }

    void UpdateEditorLabels(ScorebugEditorState* state)
    {
        SetWindowTextA(state->nameEdit, state->draft.layoutName.c_str());
        SetWindowTextA(state->referenceEdit, state->draft.referenceImagePath.c_str());

        const NormalizedRoi rois[] = {
            state->draft.frameRoi,
            state->draft.teamALabel.roi,
            state->draft.teamAScore.roi,
            state->draft.teamBLabel.roi,
            state->draft.teamBScore.roi,
            state->draft.period.roi,
            state->draft.gameClock.roi,
            state->draft.shotClock.roi
        };

        for (size_t i = 0; i < state->roiLabels.size(); ++i)
        {
            if (state->roiLabels[i])
            {
                const std::string text = "ROI: " + FormatRoi(rois[i]);
                SetWindowTextA(state->roiLabels[i], text.c_str());
            }
        }
    }

    ScorebugFieldDefinition* ResolveFieldByButton(ScorebugDraft& draft, int controlId)
    {
        switch (controlId)
        {
        case IDC_TEAM_A_LABEL_BUTTON:
            return &draft.teamALabel;
        case IDC_TEAM_A_SCORE_BUTTON:
            return &draft.teamAScore;
        case IDC_TEAM_B_LABEL_BUTTON:
            return &draft.teamBLabel;
        case IDC_TEAM_B_SCORE_BUTTON:
            return &draft.teamBScore;
        case IDC_PERIOD_BUTTON:
            return &draft.period;
        case IDC_GAME_CLOCK_BUTTON:
            return &draft.gameClock;
        case IDC_SHOT_CLOCK_BUTTON:
            return &draft.shotClock;
        default:
            return nullptr;
        }
    }

    bool EditFieldRoi(ScorebugEditorState* state, int controlId, const char* title)
    {
        const cv::Mat reference = LoadReferenceImage(state->draft);
        if (reference.empty())
        {
            MessageBoxA(state->window, "Select a valid reference image first.", "Scorebug", MB_OK | MB_ICONWARNING);
            return false;
        }

        if (!state->draft.frameRoi.enabled)
        {
            MessageBoxA(state->window, "Set the scorebug frame ROI first.", "Scorebug", MB_OK | MB_ICONWARNING);
            return false;
        }

        ScorebugFieldDefinition* field = ResolveFieldByButton(state->draft, controlId);
        if (!field)
            return false;

        cv::Mat frameCrop = CropFrameRegion(state->draft, reference);
        if (frameCrop.empty())
        {
            MessageBoxA(state->window, "Unable to crop the scorebug frame ROI.", "Scorebug", MB_OK | MB_ICONWARNING);
            return false;
        }

        ShowRoiDialog(state->window, state->instance, title, frameCrop, field->roi);
        UpdateEditorLabels(state);
        return true;
    }

    LRESULT CALLBACK ScorebugEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ScorebugEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_NCCREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE:
        {
            state = reinterpret_cast<ScorebugEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (!state)
                return -1;

            state->window = hwnd;
            CreateControlA(0, "STATIC", "Layout Name", WS_CHILD | WS_VISIBLE, 0, hwnd, state->instance);
            state->nameEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_LAYOUT_NAME_EDIT, hwnd, state->instance);

            CreateControlA(0, "STATIC", "Reference Image", WS_CHILD | WS_VISIBLE, 0, hwnd, state->instance);
            state->referenceEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, IDC_REFERENCE_EDIT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_REFERENCE_BROWSE, hwnd, state->instance);

            CreateControlA(0, "BUTTON", "Edit Scorebug ROI...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_FRAME_ROI_BUTTON, hwnd, state->instance);
            state->roiLabels[0] = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_FRAME_ROI_LABEL, hwnd, state->instance);
            CreateControlA(0, "STATIC", "Field ROI editors below open the cropped scorebug area selected above.", WS_CHILD | WS_VISIBLE, IDC_LAYOUT_HINT, hwnd, state->instance);

            const struct
            {
                const char* text;
                int buttonId;
                int labelId;
                size_t index;
            } fieldRows[] = {
                { "Team A Label ROI...", IDC_TEAM_A_LABEL_BUTTON, IDC_TEAM_A_LABEL_LABEL, 1 },
                { "Team A Score ROI...", IDC_TEAM_A_SCORE_BUTTON, IDC_TEAM_A_SCORE_LABEL, 2 },
                { "Team B Label ROI...", IDC_TEAM_B_LABEL_BUTTON, IDC_TEAM_B_LABEL_LABEL, 3 },
                { "Team B Score ROI...", IDC_TEAM_B_SCORE_BUTTON, IDC_TEAM_B_SCORE_LABEL, 4 },
                { "Period ROI...", IDC_PERIOD_BUTTON, IDC_PERIOD_LABEL, 5 },
                { "Game Clock ROI...", IDC_GAME_CLOCK_BUTTON, IDC_GAME_CLOCK_LABEL, 6 },
                { "Shot Clock ROI...", IDC_SHOT_CLOCK_BUTTON, IDC_SHOT_CLOCK_LABEL, 7 }
            };

            for (const auto& row : fieldRows)
            {
                CreateControlA(0, "BUTTON", row.text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, row.buttonId, hwnd, state->instance);
                state->roiLabels[row.index] = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, row.labelId, hwnd, state->instance);
            }

            CreateControlA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_LAYOUT_CANCEL, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, IDC_LAYOUT_SAVE, hwnd, state->instance);
            UpdateEditorLabels(state);
            return 0;
        }

        case WM_SIZE:
        {
            const int width = LOWORD(lParam);
            MoveWindow(state->nameEdit, 16, 34, width - 32, 24, TRUE);
            MoveWindow(state->referenceEdit, 16, 88, width - 136, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_REFERENCE_BROWSE), width - 112, 88, 96, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_FRAME_ROI_BUTTON), 16, 132, 170, 26, TRUE);
            MoveWindow(state->roiLabels[0], 196, 136, width - 212, 20, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_LAYOUT_HINT), 16, 162, width - 32, 18, TRUE);

            HWND firstStatic = GetWindow(hwnd, GW_CHILD);
            int staticIndex = 0;
            for (HWND child = firstStatic; child; child = GetWindow(child, GW_HWNDNEXT))
            {
                char className[32] = {};
                GetClassNameA(child, className, static_cast<int>(std::size(className)));
                if (strcmp(className, "Static") == 0 && child != state->roiLabels[0] &&
                    std::find(state->roiLabels.begin() + 1, state->roiLabels.end(), child) == state->roiLabels.end())
                {
                    const int top = staticIndex == 0 ? 12 : 66;
                    MoveWindow(child, 16, top, width - 32, 18, TRUE);
                    ++staticIndex;
                }
            }

            const int buttonLeft = 16;
            const int labelLeft = 196;
            const int rowTop = 194;
            for (size_t i = 1; i < state->roiLabels.size(); ++i)
            {
                const int y = rowTop + static_cast<int>(i - 1) * 34;
                MoveWindow(GetDlgItem(hwnd, IDC_TEAM_A_LABEL_BUTTON + static_cast<int>((i - 1) * 2)), buttonLeft, y, 170, 26, TRUE);
                MoveWindow(state->roiLabels[i], labelLeft, y + 4, width - labelLeft - 16, 20, TRUE);
            }

            MoveWindow(GetDlgItem(hwnd, IDC_LAYOUT_CANCEL), width - 196, 446, 84, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_LAYOUT_SAVE), width - 104, 446, 84, 28, TRUE);
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_REFERENCE_BROWSE:
                if (ShowOpenImageDialog(hwnd, state->draft.referenceImagePath))
                    UpdateEditorLabels(state);
                return 0;
            case IDC_FRAME_ROI_BUTTON:
            {
                const cv::Mat reference = LoadReferenceImage(state->draft);
                if (reference.empty())
                {
                    MessageBoxA(hwnd, "Select a valid reference image first.", "Scorebug", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ShowRoiDialog(hwnd, state->instance, "Scorebug ROI", reference, state->draft.frameRoi);
                UpdateEditorLabels(state);
                return 0;
            }
            case IDC_TEAM_A_LABEL_BUTTON:
                EditFieldRoi(state, IDC_TEAM_A_LABEL_BUTTON, "Team A Label ROI (inside scorebug crop)");
                return 0;
            case IDC_TEAM_A_SCORE_BUTTON:
                EditFieldRoi(state, IDC_TEAM_A_SCORE_BUTTON, "Team A Score ROI (inside scorebug crop)");
                return 0;
            case IDC_TEAM_B_LABEL_BUTTON:
                EditFieldRoi(state, IDC_TEAM_B_LABEL_BUTTON, "Team B Label ROI (inside scorebug crop)");
                return 0;
            case IDC_TEAM_B_SCORE_BUTTON:
                EditFieldRoi(state, IDC_TEAM_B_SCORE_BUTTON, "Team B Score ROI (inside scorebug crop)");
                return 0;
            case IDC_PERIOD_BUTTON:
                EditFieldRoi(state, IDC_PERIOD_BUTTON, "Period ROI (inside scorebug crop)");
                return 0;
            case IDC_GAME_CLOCK_BUTTON:
                EditFieldRoi(state, IDC_GAME_CLOCK_BUTTON, "Game Clock ROI (inside scorebug crop)");
                return 0;
            case IDC_SHOT_CLOCK_BUTTON:
                EditFieldRoi(state, IDC_SHOT_CLOCK_BUTTON, "Shot Clock ROI (inside scorebug crop)");
                return 0;
            case IDC_LAYOUT_CANCEL:
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            case IDC_LAYOUT_SAVE:
                state->draft.layoutName = GetWindowTextString(state->nameEdit);
                if (state->draft.layoutName.empty())
                {
                    MessageBoxA(hwnd, "Layout name is required.", "Scorebug", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (state->draft.referenceImagePath.empty())
                {
                    MessageBoxA(hwnd, "Reference image is required.", "Scorebug", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (!state->draft.frameRoi.enabled)
                {
                    MessageBoxA(hwnd, "Scorebug frame ROI is required.", "Scorebug", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                state->accepted = true;
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            state->running = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(hdc, &client, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
            EndPaint(hwnd, &ps);
            return 0;
        }
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

bool ScorebugDialogs_ShowEditor(
    HWND owner,
    HINSTANCE instance,
    const ScorebugLayoutManifest* existing,
    ScorebugDraft& draft,
    std::string& error)
{
    error.clear();
    RegisterDialogClasses(instance, ScorebugEditorWndProc);

    ScorebugEditorState state;
    state.owner = owner;
    state.instance = instance;
    if (existing)
    {
        draft.layoutName = existing->name;
        draft.referenceImagePath = existing->folderPath + "\\" + existing->referenceImagePath;
        draft.frameRoi = existing->frameRoi;
        draft.teamALabel = existing->teamALabel;
        draft.teamAScore = existing->teamAScore;
        draft.teamBLabel = existing->teamBLabel;
        draft.teamBScore = existing->teamBScore;
        draft.period = existing->period;
        draft.gameClock = existing->gameClock;
        draft.shotClock = existing->shotClock;
    }
    else
    {
        draft.teamALabel.type = ScorebugFieldType::Text;
        draft.teamALabel.whitelist = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        draft.teamALabel.preprocess = "auto";
        draft.teamAScore.type = ScorebugFieldType::Integer;
        draft.teamAScore.whitelist = "0123456789";
        draft.teamAScore.preprocess = "auto";
        draft.teamBLabel = draft.teamALabel;
        draft.teamBScore = draft.teamAScore;
        draft.period.type = ScorebugFieldType::PeriodText;
        draft.period.whitelist = "1234OTNDRHST";
        draft.period.preprocess = "auto";
        draft.gameClock.type = ScorebugFieldType::Clock;
        draft.gameClock.whitelist = "0123456789:";
        draft.gameClock.preprocess = "auto";
        draft.shotClock.type = ScorebugFieldType::DecimalClock;
        draft.shotClock.whitelist = "0123456789.";
        draft.shotClock.preprocess = "auto";
    }
    state.draft = draft;

    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        kScorebugEditorClass,
        "Scorebug Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        760,
        540,
        owner,
        nullptr,
        instance,
        &state);
    if (!hwnd)
    {
        error = "Unable to open scorebug editor window.";
        return false;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (state.running && IsWindow(hwnd) && GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (state.accepted)
    {
        draft = state.draft;
        return true;
    }

    return false;
}
