#include "AppState.h"

#include <windows.h>
#include <tchar.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "Renderer.h"
#include "VideoSource.h"
#include "BlackmagicSource.h"
#include "Detection.h"
#include "VizConnection.h"
#include "Config.h"
#include "UI.h"
#include "Logger.h"

#include <opencv2/opencv.hpp>
#include <iomanip>
#include <sstream>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // COM MUST be initialized first - before D3D11, DirectShow, or any DeckLink call.
    // D3D11/DirectShow may initialize COM as COINIT_APARTMENTTHREADED if we're late.
    // DeckLink requires COINIT_MULTITHREADED - calling order is critical.
    BlackmagicSource::InitCOM();

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        _T("VideoAnalyzerWindow"), nullptr
    };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName, _T("Video Analyzer v0.9.2"),
        WS_OVERLAPPEDWINDOW, 100, 100, 1500, 950,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!Renderer_CreateDevice(hwnd))
    {
        Renderer_CleanupDevice();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    AppState state;
    Config_Load(state);
    Detection_LoadTemplates(state);
    AddLog("=== Video Analyzer v0.9.2 ===");
    AddLog("Blackmagic support requires Desktop Video / driver version 16 or newer.");

    VideoSourceContext sourceCtx{};
    VideoSource_Init(sourceCtx);
    VideoSource_RefreshDeviceList(state);

    cv::Mat frame, gray, resized;
    bool previewTextureReady = false;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        VideoSource_Update(sourceCtx, state);
        frame = VideoSource_GrabFrame(sourceCtx, state);

        previewTextureReady = false;
        if (!frame.empty())
        {
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            cv::resize(gray, resized, cv::Size(WORK_W, WORK_H));

            bool triggered = Detection_ProcessFrame(resized, state);
            if (triggered)
            {
                if (state.cueState == CueState::WIPER_IN)
                    Viz_SendOff(state);
                else
                    Viz_SendOn(state);
            }

            if (state.previewEnabled)
            {
                cv::Mat overlay = frame.clone();
                std::ostringstream txt;
                txt << "Score: " << std::fixed << std::setprecision(3) << state.lastScore
                    << "  Next Cue: " << (state.cueState == CueState::WIPER_IN ? "IN" : "OUT")
                    << "  Source: " << VideoSourceKindToString(state.currentSourceKind);
                cv::putText(overlay, txt.str(), cv::Point(20, 45),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

                const char* stateStr = (state.detectionState == DetectionState::COOLDOWN) ? "COOLDOWN"
                    : (state.detectionState == DetectionState::DETECTED) ? "DETECTED"
                    : "IDLE";
                cv::putText(overlay, stateStr, cv::Point(20, 95),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 200, 255), 2);

                previewTextureReady = Renderer_UpdatePreviewTexture(overlay);
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        bool saveConfig = UI_Render(state, previewTextureReady);
        if (saveConfig)
            Config_Save(state);

        ImGui::Render();
        const float cc[4] = { 0.10f, 0.10f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    VideoSource_Release(sourceCtx, state);
    VideoSource_Shutdown(sourceCtx);
    Renderer_CleanupPreviewTexture();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    Renderer_CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
