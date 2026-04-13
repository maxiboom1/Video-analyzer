#include "TemplateDialogs.h"

#include <windows.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>

#include <opencv2/opencv.hpp>

namespace
{
    constexpr int IDC_TEMPLATE_NAME_EDIT = 3001;
    constexpr int IDC_TEMPLATE_IN_EDIT = 3002;
    constexpr int IDC_TEMPLATE_IN_BROWSE = 3003;
    constexpr int IDC_TEMPLATE_IN_ROI = 3004;
    constexpr int IDC_TEMPLATE_OUT_EDIT = 3005;
    constexpr int IDC_TEMPLATE_OUT_BROWSE = 3006;
    constexpr int IDC_TEMPLATE_OUT_ROI = 3007;
    constexpr int IDC_TEMPLATE_SAVE = 3008;
    constexpr int IDC_TEMPLATE_CANCEL = 3009;
    constexpr int IDC_TEMPLATE_IN_ROI_LABEL = 3010;
    constexpr int IDC_TEMPLATE_OUT_ROI_LABEL = 3011;

    constexpr int IDC_ROI_OK = 3101;
    constexpr int IDC_ROI_CANCEL = 3102;
    constexpr int IDC_ROI_CLEAR = 3103;
    constexpr int IDC_ROI_INFO = 3104;

    const char* kTemplateEditorClass = "VideoAnalyzerTemplateEditorWindow";
    const char* kRoiEditorClass = "VideoAnalyzerRoiEditorWindow";

    struct TemplateEditorState
    {
        HWND owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        HWND nameEdit = nullptr;
        HWND inPathEdit = nullptr;
        HWND outPathEdit = nullptr;
        HWND inRoiLabel = nullptr;
        HWND outRoiLabel = nullptr;
        bool accepted = false;
        bool running = true;
        bool editMode = false;
        TemplateDraft draft;
        std::string error;
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
            return "Full frame";

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

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = image.cols;
        bmi.bmiHeader.biHeight = -image.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
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
            image.cols,
            image.rows,
            image.data,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    void RegisterDialogClasses(HINSTANCE instance, WNDPROC editorProc, WNDPROC roiProc)
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
        editorClass.lpszClassName = kTemplateEditorClass;
        RegisterClassExA(&editorClass);

        WNDCLASSEXA roiClass{};
        roiClass.cbSize = sizeof(roiClass);
        roiClass.lpfnWndProc = roiProc;
        roiClass.hInstance = instance;
        roiClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        roiClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        roiClass.lpszClassName = kRoiEditorClass;
        RegisterClassExA(&roiClass);

        registered = true;
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
            if (!state)
                break;
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&state->imageRect, pt))
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
        state.title = title ? title : "ROI Designer";

        RegisterDialogClasses(instance, nullptr, RoiEditorWndProc);
        HWND hwnd = CreateWindowExA(
            WS_EX_DLGMODALFRAME,
            kRoiEditorClass,
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

    void UpdateTemplateEditorLabels(TemplateEditorState* state)
    {
        SetWindowTextA(state->nameEdit, state->draft.templateName.c_str());
        SetWindowTextA(state->inPathEdit, state->draft.inSourcePath.c_str());
        SetWindowTextA(state->outPathEdit, state->draft.outSourcePath.c_str());
        const std::string inText = "ROI: " + FormatRoi(state->draft.inRoi);
        const std::string outText = "ROI: " + FormatRoi(state->draft.outRoi);
        SetWindowTextA(state->inRoiLabel, inText.c_str());
        SetWindowTextA(state->outRoiLabel, outText.c_str());
    }

    LRESULT CALLBACK TemplateEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TemplateEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
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
            state = reinterpret_cast<TemplateEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (!state)
                return -1;

            state->window = hwnd;
            CreateControlA(0, "STATIC", "1. Template Name", WS_CHILD | WS_VISIBLE, 0, hwnd, state->instance);
            state->nameEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_TEMPLATE_NAME_EDIT, hwnd, state->instance);

            CreateControlA(0, "STATIC", "2. IN Image", WS_CHILD | WS_VISIBLE, 0, hwnd, state->instance);
            state->inPathEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, IDC_TEMPLATE_IN_EDIT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_TEMPLATE_IN_BROWSE, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Edit ROI...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_TEMPLATE_IN_ROI, hwnd, state->instance);
            state->inRoiLabel = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_TEMPLATE_IN_ROI_LABEL, hwnd, state->instance);

            CreateControlA(0, "STATIC", "3. OUT Image", WS_CHILD | WS_VISIBLE, 0, hwnd, state->instance);
            state->outPathEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, IDC_TEMPLATE_OUT_EDIT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_TEMPLATE_OUT_BROWSE, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Edit ROI...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_TEMPLATE_OUT_ROI, hwnd, state->instance);
            state->outRoiLabel = CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_TEMPLATE_OUT_ROI_LABEL, hwnd, state->instance);

            CreateControlA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_TEMPLATE_CANCEL, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, IDC_TEMPLATE_SAVE, hwnd, state->instance);
            UpdateTemplateEditorLabels(state);
            return 0;
        }

        case WM_SIZE:
        {
            const int width = LOWORD(lParam);
            MoveWindow(state->nameEdit, 16, 38, width - 32, 24, TRUE);

            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_IN_EDIT), 16, 98, width - 220, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_IN_BROWSE), width - 194, 98, 82, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_IN_ROI), width - 104, 98, 88, 24, TRUE);
            MoveWindow(state->inRoiLabel, 16, 128, width - 32, 20, TRUE);

            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_OUT_EDIT), 16, 188, width - 220, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_OUT_BROWSE), width - 194, 188, 82, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_OUT_ROI), width - 104, 188, 88, 24, TRUE);
            MoveWindow(state->outRoiLabel, 16, 218, width - 32, 20, TRUE);

            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_CANCEL), width - 196, 264, 84, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_TEMPLATE_SAVE), width - 104, 264, 84, 28, TRUE);

            HWND firstStatic = GetWindow(hwnd, GW_CHILD);
            int staticIndex = 0;
            for (HWND child = firstStatic; child; child = GetWindow(child, GW_HWNDNEXT))
            {
                char className[32] = {};
                GetClassNameA(child, className, static_cast<int>(std::size(className)));
                if (strcmp(className, "Static") == 0 && child != state->inRoiLabel && child != state->outRoiLabel)
                {
                    const int top = staticIndex == 0 ? 16 : (staticIndex == 1 ? 76 : 166);
                    MoveWindow(child, 16, top, width - 32, 18, TRUE);
                    ++staticIndex;
                }
            }
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_TEMPLATE_IN_BROWSE:
                if (ShowOpenImageDialog(hwnd, state->draft.inSourcePath))
                    UpdateTemplateEditorLabels(state);
                return 0;
            case IDC_TEMPLATE_OUT_BROWSE:
                if (ShowOpenImageDialog(hwnd, state->draft.outSourcePath))
                    UpdateTemplateEditorLabels(state);
                return 0;
            case IDC_TEMPLATE_IN_ROI:
            {
                cv::Mat image = cv::imread(state->draft.inSourcePath, cv::IMREAD_COLOR);
                if (image.empty())
                {
                    MessageBoxA(hwnd, "Select a valid IN image before editing ROI.", "Template", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ShowRoiDialog(hwnd, state->instance, "ROI Designer - IN", image, state->draft.inRoi);
                UpdateTemplateEditorLabels(state);
                return 0;
            }
            case IDC_TEMPLATE_OUT_ROI:
            {
                cv::Mat image = cv::imread(state->draft.outSourcePath, cv::IMREAD_COLOR);
                if (image.empty())
                {
                    MessageBoxA(hwnd, "Select a valid OUT image before editing ROI.", "Template", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ShowRoiDialog(hwnd, state->instance, "ROI Designer - OUT", image, state->draft.outRoi);
                UpdateTemplateEditorLabels(state);
                return 0;
            }
            case IDC_TEMPLATE_CANCEL:
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            case IDC_TEMPLATE_SAVE:
                state->draft.templateName = GetWindowTextString(state->nameEdit);
                if (state->draft.templateName.empty())
                {
                    MessageBoxA(hwnd, "Template name is required.", "Template", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (state->draft.inSourcePath.empty() || state->draft.outSourcePath.empty())
                {
                    MessageBoxA(hwnd, "Both IN and OUT images are required.", "Template", MB_OK | MB_ICONWARNING);
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

bool TemplateDialogs_ShowEditor(
    HWND owner,
    HINSTANCE instance,
    const TemplateManifest* existing,
    TemplateDraft& draft,
    std::string& error)
{
    error.clear();
    RegisterDialogClasses(instance, TemplateEditorWndProc, RoiEditorWndProc);

    TemplateEditorState state;
    state.owner = owner;
    state.instance = instance;
    state.editMode = existing != nullptr;
    if (existing)
    {
        draft.templateName = existing->name;
        draft.inSourcePath = existing->folderPath + "\\" + existing->inImagePath;
        draft.outSourcePath = existing->folderPath + "\\" + existing->outImagePath;
        draft.inRoi = existing->inRoi;
        draft.outRoi = existing->outRoi;
    }
    state.draft = draft;

    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        kTemplateEditorClass,
        "Template Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        360,
        owner,
        nullptr,
        instance,
        &state);
    if (!hwnd)
    {
        error = "Unable to open template editor window.";
        return false;
    }

    if (existing)
        SetWindowTextA(hwnd, "Template Editor - Edit");
    else
        SetWindowTextA(hwnd, "Template Editor - Create");

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
