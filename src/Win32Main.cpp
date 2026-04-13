#include "AppState.h"

#include <windows.h>
#include <commctrl.h>

#include "BlackmagicSource.h"
#include "Config.h"
#include "Detection.h"
#include "Logger.h"
#include "UI.h"
#include "Version.h"
#include "VideoSource.h"
#include "VizConnection.h"

#include <opencv2/opencv.hpp>
#include <iomanip>
#include <sstream>

namespace
{
    constexpr UINT_PTR kFrameTimerId = 1;
    constexpr UINT kFrameTimerMs = 33;

    struct AppContext
    {
        AppState state;
        VideoSourceContext sourceCtx{};
    };

    const char* DetectionStateToString(DetectionState state)
    {
        switch (state)
        {
        case DetectionState::COOLDOWN:
            return "COOLDOWN";
        case DetectionState::DETECTED:
            return "DETECTED";
        default:
            return "IDLE";
        }
    }

    void DrawOverlayPanel(cv::Mat& frame, const AppState& state)
    {
        if (frame.empty())
            return;

        const cv::Rect panelRect(16, 16, std::min(frame.cols - 32, 520), 82);
        if (panelRect.width <= 0 || panelRect.height <= 0)
            return;

        cv::Mat roi = frame(panelRect).clone();
        cv::rectangle(frame, panelRect, cv::Scalar(24, 30, 38), cv::FILLED);
        cv::addWeighted(frame(panelRect), 0.72, roi, 0.28, 0.0, frame(panelRect));

        std::ostringstream topLine;
        topLine << "Score " << std::fixed << std::setprecision(3) << state.lastScore
                << "   Cue " << (state.cueState == CueState::WIPER_IN ? "IN" : "OUT")
                << "   Source " << VideoSourceKindToString(state.currentSourceKind);

        cv::putText(frame, topLine.str(), cv::Point(panelRect.x + 14, panelRect.y + 28),
            cv::FONT_HERSHEY_SIMPLEX, 0.62, cv::Scalar(241, 245, 249), 1, cv::LINE_AA);
        cv::putText(frame, DetectionStateToString(state.detectionState), cv::Point(panelRect.x + 14, panelRect.y + 58),
            cv::FONT_HERSHEY_SIMPLEX, 0.72,
            state.detectionState == DetectionState::COOLDOWN ? cv::Scalar(96, 165, 250) :
            state.detectionState == DetectionState::DETECTED ? cv::Scalar(34, 197, 94) :
            cv::Scalar(248, 190, 68),
            2, cv::LINE_AA);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* app = reinterpret_cast<AppContext*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_NCCREATE:
    {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    case WM_CREATE:
    {
        app = reinterpret_cast<AppContext*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
        if (!app)
            return -1;

        if (!UI_Create(hWnd, reinterpret_cast<LPCREATESTRUCTA>(lParam)->hInstance, app->state))
            return -1;

        SetTimer(hWnd, kFrameTimerId, kFrameTimerMs, nullptr);
        return 0;
    }

    case WM_SIZE:
        UI_OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        if (app && UI_HandleMainCommand(wParam, lParam, app->state))
            return 0;
        break;

    case WM_DRAWITEM:
        if (app && UI_HandleDrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), app->state))
            return TRUE;
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hWnd, &ps);
        UI_PaintMain(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

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

    case WM_TIMER:
        if (wParam == kFrameTimerId && app)
        {
            VideoSource_Update(app->sourceCtx, app->state);
            cv::Mat frame = VideoSource_GrabFrame(app->sourceCtx, app->state);
            cv::Mat previewFrame;

            if (!frame.empty())
            {
                cv::Mat gray;
                cv::Mat resized;
                cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
                cv::resize(gray, resized, cv::Size(WORK_W, WORK_H));

                const bool triggered = Detection_ProcessFrame(resized, app->state);
                if (triggered)
                {
                    if (app->state.cueState == CueState::WIPER_IN)
                        Viz_SendOff(app->state);
                    else
                        Viz_SendOn(app->state);
                }

                if (app->state.previewEnabled)
                {
                    previewFrame = frame.clone();
                    DrawOverlayPanel(previewFrame, app->state);
                }
            }

            if (app->state.previewEnabled && !previewFrame.empty())
                UI_UpdatePreview(previewFrame);
            else if (!frame.empty())
                UI_UpdatePreview(frame);
            else if (!app->state.previewEnabled)
                UI_ClearPreview();

            UI_SyncState(app->state);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, kFrameTimerId);
        if (app)
        {
            VideoSource_Release(app->sourceCtx, app->state);
            VideoSource_Shutdown(app->sourceCtx);
        }
        UI_Destroy();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    BlackmagicSource::InitCOM();

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    AppContext app;
    Config_Load(app.state);
    Detection_LoadTemplateCatalog(app.state);
    AddLog(kAppLogBannerA);
    AddLog("Blackmagic support requires Desktop Video / driver version 16 or newer.");

    VideoSource_Init(app.sourceCtx);
    VideoSource_RefreshDeviceList(app.state);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = "VideoAnalyzerMainWindow";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        kAppWindowTitleA,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        120,
        70,
        660,
        960,
        nullptr,
        nullptr,
        hInstance,
        &app);

    if (!hwnd)
    {
        VideoSource_Shutdown(app.sourceCtx);
        UnregisterClassA(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow ? nCmdShow : SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    UnregisterClassA(wc.lpszClassName, hInstance);
    return 0;
}
