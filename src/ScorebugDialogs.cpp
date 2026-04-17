#include "ScorebugDialogs.h"

#include <windows.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <opencv2/opencv.hpp>

namespace
{
    constexpr int IDC_EDITOR_NAME_EDIT = 4101;
    constexpr int IDC_EDITOR_REFERENCE_EDIT = 4102;
    constexpr int IDC_EDITOR_REFERENCE_BROWSE = 4103;
    constexpr int IDC_EDITOR_ROI_BUTTON = 4104;
    constexpr int IDC_EDITOR_ROI_LABEL = 4105;
    constexpr int IDC_EDITOR_TYPE_COMBO = 4106;
    constexpr int IDC_EDITOR_SAVE = 4107;
    constexpr int IDC_EDITOR_CANCEL = 4108;
    constexpr int IDC_EDITOR_HINT = 4109;
    constexpr int IDC_EDITOR_TITLE = 4110;
    constexpr int IDC_EDITOR_NAME_LABEL = 4111;
    constexpr int IDC_EDITOR_REFERENCE_LABEL = 4112;
    constexpr int IDC_EDITOR_TYPE_LABEL = 4113;

    constexpr int IDC_ROI_OK = 4201;
    constexpr int IDC_ROI_CANCEL = 4202;
    constexpr int IDC_ROI_CLEAR = 4203;
    constexpr int IDC_ROI_INFO = 4204;
    constexpr int IDC_ROI_TITLE = 4205;
    constexpr int IDC_ROI_HINT = 4206;

    const char* kElementEditorClass = "VideoAnalyzerOcrElementEditorWindow";
    const char* kPropEditorClass = "VideoAnalyzerOcrPropEditorWindow";
    const char* kRoiEditorClass = "VideoAnalyzerOcrRoiWindow";

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

    struct ElementEditorState
    {
        HWND owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        HWND nameEdit = nullptr;
        HWND referenceEdit = nullptr;
        bool accepted = false;
        bool running = true;
        OcrElementDraft draft;
    };

    struct PropEditorState
    {
        HWND owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND window = nullptr;
        HWND nameEdit = nullptr;
        HWND typeCombo = nullptr;
        bool accepted = false;
        bool running = true;
        OcrElementManifest element;
        OcrPropDraft draft;
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

    void DrawImage(HDC hdc, const cv::Mat& image, const RECT& dest)
    {
        if (image.empty())
            return;

        cv::Mat bgraImage;
        if (image.channels() == 4)
            bgraImage = image;
        else if (image.channels() == 3)
            cv::cvtColor(image, bgraImage, cv::COLOR_BGR2BGRA);
        else if (image.channels() == 1)
            cv::cvtColor(image, bgraImage, cv::COLOR_GRAY2BGRA);
        else
            return;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgraImage.cols;
        bmi.bmiHeader.biHeight = -bgraImage.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, HALFTONE);
        StretchDIBits(hdc, dest.left, dest.top, dest.right - dest.left, dest.bottom - dest.top,
            0, 0, bgraImage.cols, bgraImage.rows, bgraImage.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    void UpdateRoiInfo(RoiEditorState* state)
    {
        if (!state || !state->infoLabel)
            return;
        const std::string text = "ROI: " + FormatRoi(state->roi);
        SetWindowTextA(state->infoLabel, text.c_str());
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

    cv::Mat LoadImage(const std::string& path)
    {
        if (path.empty())
            return {};
        return cv::imread(path, cv::IMREAD_COLOR);
    }

    cv::Mat CropElementImage(const OcrElementManifest& element)
    {
        const cv::Mat reference = cv::imread(element.folderPath + "\\" + element.referenceImagePath, cv::IMREAD_COLOR);
        if (reference.empty() || !element.frameRoi.enabled)
            return {};

        const cv::Rect rect = RoiToImageRect(element.frameRoi, reference.cols, reference.rows);
        if (rect.width <= 0 || rect.height <= 0)
            return {};
        return reference(rect).clone();
    }

    OcrPropType ComboIndexToPropType(int index)
    {
        switch (index)
        {
        case 0:
            return OcrPropType::Number;
        case 1:
            return OcrPropType::Text;
        default:
            return OcrPropType::Auto;
        }
    }

    int PropTypeToComboIndex(OcrPropType type)
    {
        switch (type)
        {
        case OcrPropType::Number:
            return 0;
        case OcrPropType::Text:
            return 1;
        default:
            return 2;
        }
    }

    void RegisterDialogClasses(HINSTANCE instance, WNDPROC elementProc, WNDPROC propProc)
    {
        static bool registered = false;
        if (registered)
            return;

        WNDCLASSEXA elementClass{};
        elementClass.cbSize = sizeof(elementClass);
        elementClass.lpfnWndProc = elementProc;
        elementClass.hInstance = instance;
        elementClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        elementClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        elementClass.lpszClassName = kElementEditorClass;
        RegisterClassExA(&elementClass);

        WNDCLASSEXA propClass{};
        propClass.cbSize = sizeof(propClass);
        propClass.lpfnWndProc = propProc;
        propClass.hInstance = instance;
        propClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        propClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        propClass.lpszClassName = kPropEditorClass;
        RegisterClassExA(&propClass);

        WNDCLASSEXA roiClass{};
        roiClass.cbSize = sizeof(roiClass);
        roiClass.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT
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
                CreateControlA(0, "STATIC", state->title.c_str(), WS_CHILD | WS_VISIBLE, IDC_ROI_TITLE, hwnd, state->instance);
                CreateControlA(0, "STATIC", "Drag to select the OCR area. Use Clear ROI for full-frame.", WS_CHILD | WS_VISIBLE, IDC_ROI_HINT, hwnd, state->instance);
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
                state->canvasRect = RECT{ 12, 76, client.right - 12, client.bottom - 56 };
                MoveWindow(GetDlgItem(hwnd, IDC_ROI_TITLE), 12, 10, client.right - 24, 18, TRUE);
                MoveWindow(GetDlgItem(hwnd, IDC_ROI_HINT), 12, 30, client.right - 24, 18, TRUE);
                MoveWindow(state->infoLabel, 12, 50, client.right - 24, 18, TRUE);
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
                RECT roiRect = state->dragging ? NormalizeRectFromPoints(state->dragStart, state->dragCurrent) : RoiToCanvasRect(state->roi, state->imageRect);
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
        };
        roiClass.hInstance = instance;
        roiClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        roiClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        roiClass.lpszClassName = kRoiEditorClass;
        RegisterClassExA(&roiClass);

        registered = true;
    }

    bool ShowRoiDialog(HWND owner, HINSTANCE instance, const char* title, const cv::Mat& image, NormalizedRoi& roi)
    {
        RoiEditorState state;
        state.owner = owner;
        state.instance = instance;
        state.image = image;
        state.roi = roi;
        state.title = title ? title : "OCR ROI";

        HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, kRoiEditorClass, state.title.c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 920, 720,
            owner, nullptr, instance, &state);
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

    void UpdateElementEditor(ElementEditorState* state)
    {
        SetWindowTextA(state->nameEdit, state->draft.name.c_str());
        SetWindowTextA(state->referenceEdit, state->draft.referenceImagePath.c_str());
        const std::string roiText = "ROI: " + FormatRoi(state->draft.frameRoi);
        SetWindowTextA(GetDlgItem(state->window, IDC_EDITOR_ROI_LABEL), roiText.c_str());
    }

    void UpdatePropEditor(PropEditorState* state)
    {
        SetWindowTextA(state->nameEdit, state->draft.name.c_str());
        SendMessageA(state->typeCombo, CB_SETCURSEL, PropTypeToComboIndex(state->draft.type), 0);
        const std::string roiText = "ROI: " + FormatRoi(state->draft.roi);
        SetWindowTextA(GetDlgItem(state->window, IDC_EDITOR_ROI_LABEL), roiText.c_str());
    }

    LRESULT CALLBACK ElementEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ElementEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
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
            state = reinterpret_cast<ElementEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (!state)
                return -1;

            state->window = hwnd;
            CreateControlA(0, "STATIC", "GFX Element Editor", WS_CHILD | WS_VISIBLE, IDC_EDITOR_TITLE, hwnd, state->instance);
            CreateControlA(0, "STATIC", "Element Name", WS_CHILD | WS_VISIBLE, IDC_EDITOR_NAME_LABEL, hwnd, state->instance);
            state->nameEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_EDITOR_NAME_EDIT, hwnd, state->instance);
            CreateControlA(0, "STATIC", "Reference Image", WS_CHILD | WS_VISIBLE, IDC_EDITOR_REFERENCE_LABEL, hwnd, state->instance);
            state->referenceEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, IDC_EDITOR_REFERENCE_EDIT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_EDITOR_REFERENCE_BROWSE, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Edit Element ROI...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_EDITOR_ROI_BUTTON, hwnd, state->instance);
            CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_EDITOR_ROI_LABEL, hwnd, state->instance);
            CreateControlA(0, "STATIC", "The element ROI is selected on the full reference image.", WS_CHILD | WS_VISIBLE, IDC_EDITOR_HINT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_EDITOR_CANCEL, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, IDC_EDITOR_SAVE, hwnd, state->instance);
            UpdateElementEditor(state);
            return 0;
        }
        case WM_SIZE:
        {
            const int width = LOWORD(lParam);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_TITLE), 16, 10, width - 32, 18, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_NAME_LABEL), 16, 32, width - 32, 18, TRUE);
            MoveWindow(state->nameEdit, 16, 52, width - 32, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_REFERENCE_LABEL), 16, 86, width - 32, 18, TRUE);
            MoveWindow(state->referenceEdit, 16, 106, width - 136, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_REFERENCE_BROWSE), width - 112, 106, 96, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_ROI_BUTTON), 16, 150, 160, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_ROI_LABEL), 186, 154, width - 202, 20, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_HINT), 16, 184, width - 32, 18, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_CANCEL), width - 196, 244, 84, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_SAVE), width - 104, 244, 84, 28, TRUE);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_EDITOR_REFERENCE_BROWSE:
                if (ShowOpenImageDialog(hwnd, state->draft.referenceImagePath))
                    UpdateElementEditor(state);
                return 0;
            case IDC_EDITOR_ROI_BUTTON:
            {
                const cv::Mat image = LoadImage(state->draft.referenceImagePath);
                if (image.empty())
                {
                    MessageBoxA(hwnd, "Select a valid reference image first.", "OCR", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ShowRoiDialog(hwnd, state->instance, "GFX Element ROI", image, state->draft.frameRoi);
                UpdateElementEditor(state);
                return 0;
            }
            case IDC_EDITOR_CANCEL:
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            case IDC_EDITOR_SAVE:
                state->draft.name = GetWindowTextString(state->nameEdit);
                if (state->draft.name.empty())
                {
                    MessageBoxA(hwnd, "Element name is required.", "OCR", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (state->draft.referenceImagePath.empty())
                {
                    MessageBoxA(hwnd, "Reference image is required.", "OCR", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (!state->draft.frameRoi.enabled)
                {
                    MessageBoxA(hwnd, "Element ROI is required.", "OCR", MB_OK | MB_ICONWARNING);
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

    LRESULT CALLBACK PropEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<PropEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
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
            state = reinterpret_cast<PropEditorState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (!state)
                return -1;

            state->window = hwnd;
            CreateControlA(0, "STATIC", "GFX Property Editor", WS_CHILD | WS_VISIBLE, IDC_EDITOR_TITLE, hwnd, state->instance);
            CreateControlA(0, "STATIC", "Property Name", WS_CHILD | WS_VISIBLE, IDC_EDITOR_NAME_LABEL, hwnd, state->instance);
            state->nameEdit = CreateControlA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, IDC_EDITOR_NAME_EDIT, hwnd, state->instance);
            CreateControlA(0, "STATIC", "Property Type", WS_CHILD | WS_VISIBLE, IDC_EDITOR_TYPE_LABEL, hwnd, state->instance);
            state->typeCombo = CreateControlA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_EDITOR_TYPE_COMBO, hwnd, state->instance);
            SendMessageA(state->typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Number"));
            SendMessageA(state->typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Text"));
            SendMessageA(state->typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Auto"));
            CreateControlA(0, "BUTTON", "Edit Property ROI...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_EDITOR_ROI_BUTTON, hwnd, state->instance);
            CreateControlA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, IDC_EDITOR_ROI_LABEL, hwnd, state->instance);
            CreateControlA(0, "STATIC", "The property ROI is selected inside the saved element ROI crop.", WS_CHILD | WS_VISIBLE, IDC_EDITOR_HINT, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, IDC_EDITOR_CANCEL, hwnd, state->instance);
            CreateControlA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, IDC_EDITOR_SAVE, hwnd, state->instance);
            UpdatePropEditor(state);
            return 0;
        }
        case WM_SIZE:
        {
            const int width = LOWORD(lParam);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_TITLE), 16, 10, width - 32, 18, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_NAME_LABEL), 16, 32, width - 32, 18, TRUE);
            MoveWindow(state->nameEdit, 16, 52, width - 32, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_TYPE_LABEL), 16, 86, width - 32, 18, TRUE);
            MoveWindow(state->typeCombo, 16, 106, 180, 240, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_ROI_BUTTON), 16, 150, 160, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_ROI_LABEL), 186, 154, width - 202, 20, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_HINT), 16, 184, width - 32, 18, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_CANCEL), width - 196, 244, 84, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_EDITOR_SAVE), width - 104, 244, 84, 28, TRUE);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_EDITOR_ROI_BUTTON:
            {
                cv::Mat crop = CropElementImage(state->element);
                if (crop.empty())
                {
                    MessageBoxA(hwnd, "The selected element does not have a valid saved ROI image crop.", "OCR", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ShowRoiDialog(hwnd, state->instance, "GFX Property ROI", crop, state->draft.roi);
                UpdatePropEditor(state);
                return 0;
            }
            case IDC_EDITOR_CANCEL:
                state->running = false;
                DestroyWindow(hwnd);
                return 0;
            case IDC_EDITOR_SAVE:
                state->draft.name = GetWindowTextString(state->nameEdit);
                state->draft.type = ComboIndexToPropType(static_cast<int>(SendMessageA(state->typeCombo, CB_GETCURSEL, 0, 0)));
                if (state->draft.name.empty())
                {
                    MessageBoxA(hwnd, "Property name is required.", "OCR", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (!state->draft.roi.enabled)
                {
                    MessageBoxA(hwnd, "Property ROI is required.", "OCR", MB_OK | MB_ICONWARNING);
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

bool ScorebugDialogs_ShowElementEditor(
    HWND owner,
    HINSTANCE instance,
    const OcrElementManifest* existing,
    OcrElementDraft& draft,
    std::string& error)
{
    error.clear();
    RegisterDialogClasses(instance, ElementEditorWndProc, PropEditorWndProc);

    ElementEditorState state;
    state.owner = owner;
    state.instance = instance;
    if (existing)
    {
        draft.name = existing->name;
        draft.referenceImagePath = existing->folderPath + "\\" + existing->referenceImagePath;
        draft.frameRoi = existing->frameRoi;
    }
    state.draft = draft;

    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, kElementEditorClass, "GFX Element Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 620, 320,
        owner, nullptr, instance, &state);
    if (!hwnd)
    {
        error = "Unable to open GFX Element Editor window.";
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

bool ScorebugDialogs_ShowPropEditor(
    HWND owner,
    HINSTANCE instance,
    const OcrElementManifest& element,
    const OcrPropManifest* existing,
    OcrPropDraft& draft,
    std::string& error)
{
    error.clear();
    RegisterDialogClasses(instance, ElementEditorWndProc, PropEditorWndProc);

    PropEditorState state;
    state.owner = owner;
    state.instance = instance;
    state.element = element;
    if (existing)
    {
        draft.name = existing->name;
        draft.type = existing->type;
        draft.roi = existing->roi;
    }
    state.draft = draft;

    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, kPropEditorClass, "GFX Property Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 620, 320,
        owner, nullptr, instance, &state);
    if (!hwnd)
    {
        error = "Unable to open GFX Property Editor window.";
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
