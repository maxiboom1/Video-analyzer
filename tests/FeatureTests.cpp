#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>

#include "AppState.h"
#include "Logger.h"
#include "Resources.h"
#include "StartupAuth.h"
#include "UI.h"
#include "Version.h"
#include "CueCommands.h"
#include "Detection.h"
#include "VideoSource.h"
#include "VizSocketOps.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::wstring WindowText(HWND window)
    {
        wchar_t text[512] = {};
        GetWindowTextW(window, text, static_cast<int>(std::size(text)));
        return text;
    }

    enum class AuthAction { Accept, Reject, Cancel, Close };
    AuthAction authAction = AuthAction::Cancel;
    const wchar_t* authPassword = L"";
    bool passwordMasked = false;
    bool dialogLayoutValid = false;
    bool rejectionShown = false;
    WNDPROC originalDialogProc = nullptr;
    HWND hookedDialog = nullptr;

    LRESULT CALLBACK TestDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        const LRESULT result = CallWindowProcW(originalDialogProc, dialog, message, wParam, lParam);
        if (authAction == AuthAction::Reject && message == WM_COMMAND && LOWORD(wParam) == IDOK && IsWindow(dialog))
        {
            rejectionShown = WindowText(GetDlgItem(dialog, IDC_STARTUP_PASSWORD_ERROR)) == L"Incorrect password. Please try again."
                && WindowText(GetDlgItem(dialog, IDC_STARTUP_PASSWORD)).empty();
            PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        }
        return result;
    }

    // Exercise the real modal dialog on this thread without activating a desktop window.
    LRESULT CALLBACK AuthTestHook(int code, WPARAM wParam, LPARAM lParam)
    {
        HWND dialog = reinterpret_cast<HWND>(wParam);
        if (code == HCBT_ACTIVATE && GetDlgItem(dialog, IDC_STARTUP_PASSWORD))
        {
            if (hookedDialog == dialog)
                return 1;
            hookedDialog = dialog;
            passwordMasked = (GetWindowLongPtrW(GetDlgItem(dialog, IDC_STARTUP_PASSWORD), GWL_STYLE) & ES_PASSWORD) != 0;
            RECT client{};
            GetClientRect(dialog, &client);
            dialogLayoutValid = true;
            for (int id : { IDC_STARTUP_PASSWORD, IDC_STARTUP_PASSWORD_ERROR, IDOK, IDCANCEL })
            {
                RECT control{};
                GetWindowRect(GetDlgItem(dialog, id), &control);
                MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&control), 2);
                dialogLayoutValid = dialogLayoutValid && control.left >= 0 && control.top >= 0
                    && control.right <= client.right && control.bottom <= client.bottom;
            }
            originalDialogProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(dialog, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TestDialogProc)));
            if (authAction == AuthAction::Cancel)
                PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            else if (authAction == AuthAction::Close)
                PostMessageW(dialog, WM_CLOSE, 0, 0);
            else
            {
                SetDlgItemTextW(dialog, IDC_STARTUP_PASSWORD, authPassword);
                PostMessageW(dialog, WM_COMMAND, IDOK, 0);
            }
            return 1;
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    class AuthHook
    {
        HHOOK hook_ = nullptr;
    public:
        AuthHook(AuthAction action, const wchar_t* password = L"")
        {
            authAction = action;
            authPassword = password;
            passwordMasked = false;
            dialogLayoutValid = false;
            rejectionShown = false;
            hookedDialog = nullptr;
            hook_ = SetWindowsHookExW(WH_CBT, AuthTestHook, nullptr, GetCurrentThreadId());
            Check(hook_ != nullptr, "Unable to install dialog test hook");
        }
        ~AuthHook() { UnhookWindowsHookEx(hook_); }
    };

    void TestAuthentication()
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        for (auto action : { AuthAction::Cancel, AuthAction::Close, AuthAction::Reject })
        {
            Check(!std::filesystem::exists("config.ini"), "Test directory must not contain application config");
            Logger_Clear();
            AuthHook hook(action, L"wrong-password");
            Check(WinMain(instance, nullptr, nullptr, SW_HIDE) == 0, "Denied startup did not exit cleanly");
            Check(passwordMasked, "Password input is not masked");
            Check(dialogLayoutValid, "Password form has clipped controls");
            Check(Logger_Snapshot().empty(), "Application services ran before authentication");
            Check(!std::filesystem::exists("config.ini"), "Configuration was initialized before authentication");
            Check(!std::filesystem::exists("templates"), "Template catalog was initialized before authentication");
            WNDCLASSEXA mainClass{};
            mainClass.cbSize = sizeof(mainClass);
            Check(!GetClassInfoExA(instance, "VideoAnalyzerMainWindow", &mainClass), "Main window was registered before authentication");
            if (action == AuthAction::Reject)
                Check(rejectionShown, "Invalid password did not show an error and clear the input");
        }
        for (const wchar_t* password : { L"Kisa1720!", L"SegevSportCompany" })
        {
            AuthHook hook(AuthAction::Accept, password);
            Check(StartupAuth_ShowDialog(instance), "A configured password was rejected");
            Check(passwordMasked, "Accepted password was not masked");
            Check(dialogLayoutValid, "Password form has clipped controls");
        }
        for (const wchar_t* password : { L"", L"kisa1720!", L"Kisa1720", L"SegevsportCompany", L"segevsportcompany", L"SegevSportCompany ", L" Kisa1720!" })
            Check(!StartupAuth_IsPasswordValid(password), "Password comparison accepted a case/whitespace mismatch");
        Check(std::string(kAppVersion) == "1.0.6", "Unexpected application version");
        std::cout << "PASS: password acceptance, exact comparison, masking, form layout, rejection, and gated startup/exit\n";
    }

    class LoopbackServer
    {
        SOCKET socket_ = INVALID_SOCKET;
    public:
        unsigned short port = 0;
        LoopbackServer()
        {
            socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Check(socket_ != INVALID_SOCKET, "Unable to create test receiver");
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            Check(bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Unable to bind local test receiver");
            int size = sizeof(address);
            Check(getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &size) == 0, "Unable to read test receiver port");
            port = ntohs(address.sin_port);
            Check(listen(socket_, 2) == 0, "Unable to listen on local test receiver");
        }
        ~LoopbackServer() { closesocket(socket_); }
        bool HasPendingConnection(long microseconds = 100000)
        {
            fd_set set{};
            FD_ZERO(&set);
            FD_SET(socket_, &set);
            timeval timeout{ 0, microseconds };
            return select(0, &set, nullptr, nullptr, &timeout) > 0;
        }
        std::future<std::string> Receive()
        {
            return std::async(std::launch::async, [this]() {
                fd_set set{};
                FD_ZERO(&set);
                FD_SET(socket_, &set);
                timeval timeout{ 3, 0 };
                Check(select(0, &set, nullptr, nullptr, &timeout) > 0, "No event command reached the test receiver");
                SOCKET client = accept(socket_, nullptr, nullptr);
                Check(client != INVALID_SOCKET, "Unable to accept event command");
                DWORD receiveTimeout = 2000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
                std::string payload;
                char buffer[7];
                int count;
                while ((count = recv(client, buffer, sizeof(buffer), 0)) > 0)
                    payload.append(buffer, count);
                closesocket(client);
                Check(count == 0, "Command connection did not close normally");
                return payload;
            });
        }
    };

    bool uiReady = false;
    int heartbeatCount = 0;
    LRESULT CALLBACK TestMainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (uiReady && state)
        {
            if (message == WM_TIMER) { ++heartbeatCount; return 0; }
            if (message == WM_COMMAND && UI_HandleMainCommand(wParam, lParam, *state))
                return 0;
            if (message == WM_DRAWITEM && UI_HandleDrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), *state))
                return TRUE;
            if (message == WM_SIZE)
                UI_OnSize(LOWORD(lParam), HIWORD(lParam));
            if (message == WM_PRINTCLIENT)
            {
                UI_PaintMain(reinterpret_cast<HDC>(wParam));
                return 0;
            }
            if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)
            {
                HBRUSH brush = UI_HandleCtlColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
                if (brush)
                    return reinterpret_cast<LRESULT>(brush);
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HWND FindChild(HWND parent, const wchar_t* caption, const wchar_t* className = nullptr)
    {
        struct Search { const wchar_t* caption; const wchar_t* className; HWND found; } search{ caption, className, nullptr };
        EnumChildWindows(parent, [](HWND child, LPARAM param) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(param);
            wchar_t name[100] = {};
            GetClassNameW(child, name, static_cast<int>(std::size(name)));
            if ((!search.caption || WindowText(child) == search.caption) && (!search.className || std::wstring(name) == search.className))
            {
                search.found = child;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    void ClickTestButton(HWND parent, HWND button)
    {
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED), reinterpret_cast<LPARAM>(button));
    }

    template<typename Predicate>
    void PumpUntil(Predicate finished, int timeoutMs = 3000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!finished())
        {
            Check(std::chrono::steady_clock::now() < deadline, "Timed out waiting for background work");
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(1);
        }
    }

    void FinishSend(CueCommandContext& commands, AppState& state)
    {
        PumpUntil([&] { Cue_PollCommand(commands, state); return !state.vizSendPending; });
    }

    int WSAAPI PendingConnect(SOCKET, const sockaddr*, int)
    {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }

    int WSAAPI NeverWritable(int, fd_set*, fd_set*, fd_set*, const timeval* wait)
    {
        std::this_thread::sleep_for(std::chrono::seconds(wait->tv_sec) + std::chrono::microseconds(wait->tv_usec));
        return 0;
    }

    int WSAAPI ChunkedSend(SOCKET socket, const char* data, int size, int flags)
    {
        return send(socket, data, std::min(size, 3), flags);
    }

    int WSAAPI DroppedSend(SOCKET, const char*, int, int)
    {
        WSASetLastError(WSAECONNRESET);
        return SOCKET_ERROR;
    }

    int WSAAPI DelayedConnect(SOCKET socket, const sockaddr* address, int size)
    {
        Sleep(150);
        return connect(socket, address, size);
    }

    HWND FindThreadWindow(const wchar_t* className)
    {
        struct Search { const wchar_t* name; HWND result = nullptr; } search{ className };
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            wchar_t name[100]{};
            GetClassNameW(window, name, 100);
            if (lstrcmpiW(name, search.name) == 0) { search.result = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.result;
    }

    void TestDetectionTooltips()
    {
        HWND settings = FindThreadWindow(L"VideoAnalyzerSettingsWindow");
        HWND tooltip = FindThreadWindow(TOOLTIPS_CLASSW);
        Check(settings && tooltip, "Detection tooltip window is missing");
        Check(SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0) == 8, "Not every detection control has a tooltip");
        Check(SendMessageW(tooltip, TTM_GETMAXTIPWIDTH, 0, 0) == 420, "Tooltip text does not wrap");
        for (int i = 0; i < 8; ++i)
        {
            wchar_t text[1024]{};
            TOOLINFOW tool{};
            tool.cbSize = TTTOOLINFOW_V2_SIZE;
            tool.lpszText = text;
            Check(SendMessageW(tooltip, TTM_ENUMTOOLSW, i, reinterpret_cast<LPARAM>(&tool)) != 0, "Tooltip registration is invalid");
            tool.lpszText = text;
            SendMessageW(tooltip, TTM_GETTEXTW, std::size(text), reinterpret_cast<LPARAM>(&tool));
            Check(wcslen(text) > 20, "Tooltip explanation is empty");
            HWND control = reinterpret_cast<HWND>(tool.uId);
            if (WindowText(control) != L"Save Config")
                Check(std::wstring(text).find(L"Save Config") != std::wstring::npos, "Tooltip does not explain how to apply changes");
        }
        Check(!FindChild(settings, L"Reset Threshold"), "Inactive Reset Threshold is still exposed");
        ClickTestButton(settings, FindChild(settings, L"Engine"));
        Check(!(GetWindowLongPtrW(FindChild(settings, L"Detect Threshold"), GWL_STYLE) & WS_VISIBLE), "Detection controls remain on Engine tab");
        ClickTestButton(settings, FindChild(settings, L"Detection"));
        Check((GetWindowLongPtrW(FindChild(settings, L"Detect Threshold"), GWL_STYLE) & WS_VISIBLE) != 0, "Detection controls did not return");
        SendMessageW(settings, WM_CLOSE, 0, 0);
        Check(IsWindow(tooltip), "Closing Settings prematurely destroyed its reusable tooltips");
        std::cout << "PASS: Detection tooltip coverage, hidden Reset Threshold, wrapping, and tab lifecycle\n";
    }

    std::atomic_int waitCalls{ 0 };
    int WSAAPI ConnectThenStall(int n, fd_set* read, fd_set* write, fd_set* error, const timeval* timeout)
    {
        if (waitCalls.fetch_add(1) == 0) { Sleep(600); return 1; }
        return NeverWritable(n, read, write, error, timeout);
    }
    int WSAAPI BlockedSend(SOCKET, const char*, int, int)
    {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }

    void TestTransport()
    {
        WSADATA winsock{};
        Check(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "Winsock test initialization failed");
        std::atomic_bool cancelled{ false };
        {
            LoopbackServer receiver;
            VizRequest request{ "127.0.0.1", receiver.port, "PARTIAL_WRITE_TEST" };
            auto received = receiver.Receive();
            VizSocketOps ops;
            ops.send = ChunkedSend;
            Check(Viz_SendRequest(request, cancelled, &ops).success, "Partial writes did not complete");
            Check(received.get() == request.command + std::string(1, '\0'), "Partial writes corrupted the payload or terminator");
            auto disconnected = receiver.Receive();
            ops.send = DroppedSend;
            Check(!Viz_SendRequest(request, cancelled, &ops).success, "Dropped connection was reported as successful");
            Check(disconnected.get().empty(), "Injected disconnect unexpectedly sent a command");
        }
        SOCKET reserved = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(reserved, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Cannot reserve closed test port");
        int length = sizeof(address);
        getsockname(reserved, reinterpret_cast<sockaddr*>(&address), &length);
        Check(!Viz_SendRequest({ "127.0.0.1", ntohs(address.sin_port), "REFUSED" }, cancelled).success,
            "A refused connection was marked successful");
        closesocket(reserved);

        VizSocketOps stalled;
        stalled.connect = PendingConnect;
        stalled.select = ConnectThenStall;
        stalled.send = BlockedSend;
        waitCalls = 0;
        auto started = std::chrono::steady_clock::now();
        const auto result = Viz_SendRequest({ "127.0.0.1", 6100, "STALL" }, cancelled, &stalled);
        const auto duration = std::chrono::steady_clock::now() - started;
        Check(!result.success && result.message.find("timed out") != std::string::npos, "Send stall did not time out");
        Check(duration >= std::chrono::milliseconds(1900) && duration < std::chrono::milliseconds(2500),
            "Connection and send did not share one two-second deadline");

        stalled.select = NeverWritable;
        std::atomic_bool entered{ false };
        VizSender sender([&](const VizRequest& request, const std::atomic_bool& stopping) {
            entered = true;
            return Viz_SendRequest(request, stopping, &stalled);
        });
        Check(sender.Submit({ "127.0.0.1", 6100, "CANCEL" }), "Could not start cancellable send");
        PumpUntil([&] { return entered.load(); });
        Check(!sender.Submit({ "127.0.0.1", 6100, "DUPLICATE" }), "Worker queued a second request");
        started = std::chrono::steady_clock::now();
        sender.Stop();
        Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500), "Shutdown waited for connection timeout");
        Check(!sender.Submit({ "127.0.0.1", 6100, "AFTER_STOP" }), "Stopped sender restarted unexpectedly");
        VizResult ignored;
        Check(!sender.Poll(ignored), "Shutdown retained a stale completion");
        WSACleanup();
        std::cout << "PASS: partial writes, disconnect/refusal, total deadline, cancellation, and no queued replay\n";
    }

    void TestLogsAndRetry()
    {
        Logger_Clear();
        std::atomic_int completed{ 0 };
        std::vector<std::future<void>> writers;
        for (int i = 0; i < 3; ++i)
            writers.push_back(std::async(std::launch::async, [&, i] {
                for (int j = 0; j < 4000; ++j) AddLog("writer " + std::to_string(i) + " line " + std::to_string(j));
                ++completed;
            }));
        while (completed != 3)
        {
            Check(Logger_Snapshot().size() <= 500, "Concurrent logs exceeded the buffer limit");
            Logger_Clear();
            std::this_thread::yield();
        }
        for (auto& writer : writers) writer.get();
        Logger_Clear();
        for (int i = 0; i < 600; ++i) AddLog(std::to_string(i));
        const auto snapshot = Logger_Snapshot();
        Check(snapshot.size() == 500 && snapshot.front() == "100" && snapshot.back() == "599", "Log retention order is wrong");
        Logger_Clear();
        Check(snapshot.size() == 500 && Logger_Snapshot().empty(), "Snapshots are not independent of clearing");

        AppState state;
        VideoSourceContext source;
        state.deviceListDirty = false;
        state.cameraIndex = 999;
        state.availableDevices.push_back({ VideoSourceKind::Webcam, 0, "test", "test" });
        const auto start = std::chrono::steady_clock::now();
        VideoSource_Update(source, state, start);
        Check(Logger_Snapshot().size() == 1, "Initial missing-device attempt was not made");
        for (int ms = 33; ms < 2000; ms += 33)
            VideoSource_Update(source, state, start + std::chrono::milliseconds(ms));
        Check(Logger_Snapshot().size() == 1, "Missing device retried on each frame");
        VideoSource_Update(source, state, start + std::chrono::milliseconds(2000));
        Check(Logger_Snapshot().size() == 2, "Device was not retried after two seconds");
        state.cameraIndex = 998;
        VideoSource_Update(source, state, start + std::chrono::milliseconds(2010));
        Check(Logger_Snapshot().size() == 3, "Changing device did not reset backoff");
        state.availableDevices.clear();
        VideoSource_Update(source, state, start + std::chrono::milliseconds(2020));
        Check(Logger_Snapshot().size() == 3, "Empty device list bypassed the retry delay");
        Logger_Clear();
        std::cout << "PASS: concurrent log append/snapshot/clear and capture retry timing\n";
    }

    void TestCueControls()
    {
        WSADATA winsock{};
        Check(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "Unable to initialize local socket test");
        LoopbackServer receiver;
        AppState state;
        VizSocketOps ops;
        CueCommandContext commands([&](const VizRequest& request, const std::atomic_bool& cancelled) {
            return Viz_SendRequest(request, cancelled, &ops);
        });
        state.vizPort = receiver.port;
        state.detectionEnabled = false; // Manual events must work without live detection or a camera.
        strcpy_s(state.cmdOn, "TEST_GFX_ON");
        strcpy_s(state.cmdOff, "TEST_GFX_OFF");
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&controls);
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"VideoAnalyzerFeatureTestWindow";
        klass.lpfnWndProc = TestMainProc;
        Check(RegisterClassW(&klass) != 0, "Unable to register hidden test window");
        HWND window = CreateWindowW(klass.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 660, 960, nullptr, nullptr, klass.hInstance, &state);
        Check(window && UI_Create(window, klass.hInstance, state, commands), "Unable to construct main controls");
        uiReady = true;
        HWND next = FindChild(window, L"NEXT CUE\nWIPER IN");
        HWND send = FindChild(window, L"SEND EVENT COMMAND");
        HWND preview = FindChild(window, nullptr, L"VideoAnalyzerCuePreviewWindow");
        Check(next && send && preview, "Expected cue buttons/preview were not created");
        TestDetectionTooltips();
        for (int width : { 660, 1000 })
        {
            MoveWindow(window, 0, 0, width, 960, FALSE);
            RECT nextRect{}, sendRect{}, previewRect{};
            GetWindowRect(next, &nextRect);
            GetWindowRect(send, &sendRect);
            GetWindowRect(preview, &previewRect);
            Check(nextRect.right - nextRect.left == 96 && nextRect.bottom - nextRect.top == 96, "Next Cue changed size");
            Check(sendRect.right - sendRect.left == 96 && sendRect.bottom - sendRect.top == 96, "Send button size differs from Next Cue");
            Check(nextRect.top == sendRect.top && sendRect.left == nextRect.right + 12, "Send button is not immediately to the right of Next Cue");
            Check(previewRect.left == sendRect.right + 12 && previewRect.right > previewRect.left, "Cue preview does not fit after the second button");
        }
        MoveWindow(window, 0, 0, 660, 960, FALSE);
        ClickTestButton(window, next);
        Check(state.cueState == CueState::WIPER_OUT && WindowText(next) == L"NEXT CUE\nWIPER OUT", "Next Cue did not advance");
        Check(!receiver.HasPendingConnection(), "Next Cue unexpectedly sent a command");
        ClickTestButton(window, next);
        for (bool sendingIn : { true, false })
        {
            auto payload = receiver.Receive();
            const auto before = std::chrono::steady_clock::now();
            ClickTestButton(window, send);
            Check(state.vizSendPending, "Send did not enter pending state");
            Check(!IsWindowEnabled(next) && !IsWindowEnabled(send), "Cue buttons were not disabled during sending");
            FinishSend(commands, state);
            UI_SyncState(state);
            std::string expected = sendingIn ? state.cmdOn : state.cmdOff;
            expected.push_back('\0');
            Check(payload.get() == expected, "Wrong command or missing NUL terminator");
            Check(state.vizStatus == VizSendStatus::Succeeded, "Successful send was marked failed");
            Check(state.cueState == (sendingIn ? CueState::WIPER_OUT : CueState::WIPER_IN), "Successful send did not advance cue");
            Check(WindowText(next) == (sendingIn ? L"NEXT CUE\nWIPER OUT" : L"NEXT CUE\nWIPER IN"), "Next Cue label did not update after send");
            Check(state.detectionState == DetectionState::COOLDOWN && state.lastDetectionTime >= before, "Manual event did not start detection cooldown");
            Check(!receiver.HasPendingConnection(), "A single click sent multiple commands");
        }
        strcpy_s(state.vizIp, "invalid-address");
        for (CueState cue : { CueState::WIPER_IN, CueState::WIPER_OUT })
        {
            state.cueState = cue;
            UI_SyncState(state);
            const auto label = WindowText(next);
            const auto lastDetectionTime = state.lastDetectionTime;
            ClickTestButton(window, send);
            FinishSend(commands, state);
            UI_SyncState(state);
            Check(state.vizStatus == VizSendStatus::Failed, "Failed send was marked successful");
            Check(state.cueState == cue && WindowText(next) == label, "Failed send changed the cue/label");
            Check(state.lastDetectionTime == lastDetectionTime, "Failed send changed the cooldown");
        }
        // A black-holed connection must not block the UI or permit extra commands.
        state.cueState = CueState::WIPER_IN;
        strcpy_s(state.vizIp, "127.0.0.1");
        ops.connect = PendingConnect;
        ops.select = NeverWritable;
        UI_SyncState(state);
        heartbeatCount = 0;
        SetTimer(window, 2, 10, nullptr);
        const auto beforeStall = std::chrono::steady_clock::now();
        const auto previousCooldown = state.lastDetectionTime;
        ClickTestButton(window, send);
        Check(std::chrono::steady_clock::now() - beforeStall < std::chrono::milliseconds(150), "Sending blocked the UI thread");
        ClickTestButton(window, send);
        ClickTestButton(window, next);
        Check(state.cueState == CueState::WIPER_IN && state.vizSendPending, "Pending send allowed a duplicate or cue change");
        FinishSend(commands, state);
        KillTimer(window, 2);
        Check(heartbeatCount > 20, "UI messages were starved during offline connection");
        Check(state.vizStatus == VizSendStatus::Failed && state.cueState == CueState::WIPER_IN, "Offline manual send changed cue");
        Check(state.lastDetectionTime == previousCooldown, "Offline manual send changed cooldown");
        UI_SyncState(state);
        Check(IsWindowEnabled(next) && IsWindowEnabled(send), "Cue buttons were not reenabled after timeout");
        Check(!receiver.HasPendingConnection(), "Failed commands were replayed to the renderer");

        // The worker uses a snapshot, and an old completion cannot label a new endpoint successful.
        ops = {};
        ops.connect = DelayedConnect;
        auto captured = receiver.Receive();
        const std::string capturedCommand = state.cmdOn;
        ClickTestButton(window, send);
        strcpy_s(state.cmdOn, "CHANGED_WHILE_SENDING");
        strcpy_s(state.vizIp, "127.0.0.2");
        Cue_ResetRendererStatus(state);
        strcpy_s(state.vizIp, "127.0.0.1");
        Cue_ResetRendererStatus(state);
        FinishSend(commands, state);
        Check(captured.get() == capturedCommand + std::string(1, '\0'), "Pending request was changed by settings edits");
        Check(state.vizStatus == VizSendStatus::NotTested, "Stale completion overwrote reset destination status");
        Check(state.cueState == CueState::WIPER_OUT, "Successful captured manual send did not advance");

        // Exercise the actual automatic detection path, including its pending-send guard.
        const int oldWidth = WORK_W, oldHeight = WORK_H;
        WORK_W = WORK_H = 8;
        cv::Mat pattern(8, 8, CV_8U);
        cv::RNG random(123);
        random.fill(pattern, cv::RNG::UNIFORM, 0, 255);
        state.tmplIn = pattern.clone();
        state.tmplOut = pattern.clone();
        state.tmplInRect = state.tmplOutRect = cv::Rect(0, 0, 8, 8);
        state.tmplWorkWidth = state.tmplWorkHeight = 8;
        state.activeTemplateLoaded = true;
        state.detectionEnabled = true;
        state.detectionState = DetectionState::IDLE;
        state.cueState = CueState::WIPER_IN;
        strcpy_s(state.vizIp, "invalid-address");
        Cue_ProcessFrame(commands, state, pattern);
        Check(state.cueState == CueState::WIPER_OUT && state.vizSendPending, "Automatic event did not advance immediately");
        state.detectionState = DetectionState::IDLE;
        Cue_ProcessFrame(commands, state, pattern);
        Check(state.cueState == CueState::WIPER_OUT, "Pending send allowed another automatic event");
        FinishSend(commands, state);
        Check(state.vizStatus == VizSendStatus::Failed && state.cueState == CueState::WIPER_OUT, "Failed automatic send reverted its cue");
        strcpy_s(state.vizIp, "127.0.0.1");
        ops = {};
        auto automatic = receiver.Receive();
        Cue_ProcessFrame(commands, state, pattern);
        FinishSend(commands, state);
        Check(automatic.get() == std::string(state.cmdOff) + std::string(1, '\0'), "Automatic event sent the next cue instead of detected cue");
        Check(state.cueState == CueState::WIPER_IN && state.detectionState == DetectionState::COOLDOWN, "Automatic success changed cue/cooldown behavior");
        WORK_W = oldWidth;
        WORK_H = oldHeight;
        state.cueState = CueState::WIPER_IN;
        strcpy_s(state.vizIp, "127.0.0.1");
        state.vizStatus = VizSendStatus::NotTested;
        Logger_Clear();
        UI_SyncState(state);
        uiReady = false;
        UI_Destroy();
        DestroyWindow(window);
        UnregisterClassW(klass.lpszClassName, klass.hInstance);
        WSACleanup();
        std::cout << "PASS: button layout, cue-only switching, ON/OFF payloads, cue labels, cooldown, and failed-send state\n";
    }
}

int main()
{
    try
    {
        TestAuthentication();
        TestCueControls();
        TestTransport();
        TestLogsAndRetry();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
