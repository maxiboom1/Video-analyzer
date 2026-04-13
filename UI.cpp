// UI.cpp  –  Video Analyzer v0.9.2
// Operator-oriented main view + tabbed Settings window.
// All core logic (VideoSource, Detection, Viz) is untouched.

#include "UI.h"
#include "Detection.h"
#include "Logger.h"
#include "Renderer.h"
#include "VideoSource.h"
#include "BlackmagicSource.h"

#include "imgui.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Settings window open/close state (module-level, not persisted)
// ---------------------------------------------------------------------------
static bool s_showSettings = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static ImVec4 ColorOk()   { return ImVec4(0.20f, 1.00f, 0.20f, 1.00f); }
static ImVec4 ColorWarn() { return ImVec4(1.00f, 0.60f, 0.00f, 1.00f); }
static ImVec4 ColorErr()  { return ImVec4(1.00f, 0.30f, 0.30f, 1.00f); }
static ImVec4 ColorDim()  { return ImVec4(0.50f, 0.50f, 0.50f, 1.00f); }

// ---------------------------------------------------------------------------
// Settings window  (Detection + Engine Connection + Templates tabs)
// ---------------------------------------------------------------------------
static void RenderSettingsWindow(AppState& state, bool& saveRequested)
{
    if (!s_showSettings) return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("Settings", &s_showSettings,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##settings_tabs"))
    {
        // ── Detection ─────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Detection"))
        {
            ImGui::Spacing();

            ImGui::Checkbox("Detection enabled", &state.detectionEnabled);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Detect Threshold");
            ImGui::SameLine(180);
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("##detect_thr", &state.detectThreshold,
                               0.10f, 0.99f, "%.2f");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Score must exceed this value to trigger a detection event.");

            ImGui::Spacing();

            ImGui::Text("Reset Threshold");
            ImGui::SameLine(180);
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("##reset_thr", &state.resetThreshold,
                               0.05f, 0.95f, "%.2f");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Score must drop below this value before another detection can fire.");

            ImGui::Spacing();

            ImGui::Text("Cooldown (ms)");
            ImGui::SameLine(180);
            ImGui::SetNextItemWidth(220);
            ImGui::SliderInt("##cooldown", &state.cooldownMs, 100, 10000);
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Minimum time (ms) between consecutive detection events.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ColorDim(),
                "Detect >= %.2f   |   Reset < %.2f   |   Cooldown %d ms",
                state.detectThreshold, state.resetThreshold, state.cooldownMs);

            ImGui::EndTabItem();
        }

        // ── Engine Connection ──────────────────────────────────────────────
        if (ImGui::BeginTabItem("Engine Connection"))
        {
            ImGui::Spacing();

            ImGui::Text("IP Address");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##viz_ip", state.vizIp, sizeof(state.vizIp));

            ImGui::Spacing();

            ImGui::Text("Port");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("##viz_port", &state.vizPort);
            state.vizPort = std::max(1, std::min(65535, state.vizPort));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("GFX ON command");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##cmd_on", state.cmdOn, sizeof(state.cmdOn));

            ImGui::Spacing();

            ImGui::Text("GFX OFF command");
            ImGui::SameLine(150);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##cmd_off", state.cmdOff, sizeof(state.cmdOff));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Status");
            ImGui::SameLine(150);
            ImGui::TextColored(
                state.lastVizOk ? ColorOk() : ColorErr(),
                "%s:%d  [%s]",
                state.vizIp, state.vizPort,
                state.lastVizOk ? "OK" : state.lastVizMsg.c_str());

            ImGui::Spacing();
            ImGui::Spacing();

            if (ImGui::Button("  Save Config  "))
                saveRequested = true;

            ImGui::EndTabItem();
        }

        // ── Templates (placeholder for v1.0) ──────────────────────────────
        if (ImGui::BeginTabItem("Templates"))
        {
            ImGui::Spacing();
            ImGui::TextColored(ColorDim(), "Template management – coming in v1.0");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped(
                "This tab will allow you to load, preview, and manage "
                "WIPER IN / WIPER OUT reference templates without "
                "restarting the application.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Main UI  –  operator view
// ---------------------------------------------------------------------------
bool UI_Render(AppState& state, bool previewTextureReady)
{
    bool saveRequested = false;

    ImGuiIO& io = ImGui::GetIO();

    // Full-screen root window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── TOP BAR ─────────────────────────────────────────────────────────────
    {
        ImGui::Text("SegevSport  Video Analyzer    v0.9.2,  2026");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
        if (ImGui::Button("  * Settings  "))
            s_showSettings = true;
        ImGui::Separator();
    }

    ImGui::Spacing();

    // ── CONTROL SECTION ─────────────────────────────────────────────────────
    const float controlH = 90.0f;
    ImGui::BeginChild("##controls", ImVec2(0, controlH), true);
    {
        // Row 1: device dropdown + Refresh
        {
            std::vector<std::string> labels;
            std::vector<const char*> names;
            labels.reserve(state.availableDevices.size());
            names.reserve(state.availableDevices.size());

            for (const auto& dev : state.availableDevices)
                labels.push_back(
                    "[" + std::string(VideoSourceKindToString(dev.kind)) + "] "
                    + dev.displayName);
            for (auto& l : labels)
                names.push_back(l.c_str());

            if (state.selectedDeviceListIndex < 0 &&
                !state.availableDevices.empty())
                state.selectedDeviceListIndex = 0;

            ImGui::Text("Video Device");
            ImGui::SameLine(110);

            if (state.availableDevices.empty())
            {
                ImGui::TextColored(ColorWarn(), "No video devices detected.");
            }
            else
            {
                ImGui::SetNextItemWidth(340);
                int prevIdx = state.selectedDeviceListIndex;
                if (ImGui::Combo("##video_device",
                                 &state.selectedDeviceListIndex,
                                 names.data(), (int)names.size()))
                {
                    if (state.selectedDeviceListIndex >= 0 &&
                        state.selectedDeviceListIndex <
                            (int)state.availableDevices.size())
                    {
                        const auto& sel =
                            state.availableDevices[state.selectedDeviceListIndex];
                        state.selectedSourceKind = sel.kind;
                        state.cameraIndex = sel.id;
                    }
                }
                if (prevIdx != state.selectedDeviceListIndex)
                    state.currentCamera = -1;
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
                state.deviceListDirty = true;
        }

        ImGui::Spacing();

        // Row 2: renderer status pill  |  Next Cue button (right-aligned)
        {
            ImGui::Text("Renderer");
            ImGui::SameLine(110);
            ImGui::TextColored(
                state.lastVizOk ? ColorOk() : ColorErr(),
                "%s:%d  [%s]",
                state.vizIp, state.vizPort,
                state.lastVizOk ? "Connected" : state.lastVizMsg.c_str());

            bool inCue   = (state.cueState == CueState::WIPER_IN);
            float btnW   = 260.0f;
            float rightX = ImGui::GetContentRegionAvail().x - btnW
                           + ImGui::GetCursorPosX() - 4.0f;
            ImGui::SameLine(rightX);

            ImGui::PushStyleColor(ImGuiCol_Button,
                inCue ? ImVec4(0.10f, 0.40f, 0.70f, 1) : ImVec4(0.70f, 0.35f, 0.05f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                inCue ? ImVec4(0.20f, 0.55f, 0.90f, 1) : ImVec4(0.85f, 0.50f, 0.10f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                inCue ? ImVec4(0.05f, 0.30f, 0.60f, 1) : ImVec4(0.60f, 0.30f, 0.05f, 1));

            if (ImGui::Button(
                    inCue ? "  [ NEXT CUE: WIPER IN ]  "
                          : "  [ NEXT CUE: WIPER OUT ]  ",
                    ImVec2(btnW, 30)))
                Detection_FlipCue(state);

            ImGui::PopStyleColor(3);
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── PREVIEW + LOG ────────────────────────────────────────────────────────
    const float availW  = ImGui::GetContentRegionAvail().x;
    const float availH  = ImGui::GetContentRegionAvail().y;
    const float previewW = availW * 0.66f;
    const float logW     = availW - previewW - 10.0f;

    // ---- Preview ----
    ImGui::BeginChild("##preview", ImVec2(previewW, availH), true);
    {
        ImGui::Checkbox("Preview", &state.previewEnabled);
        ImGui::Separator();

        if (state.previewEnabled && previewTextureReady && g_previewSRV)
        {
            float maxW  = ImGui::GetContentRegionAvail().x;
            float maxH  = ImGui::GetContentRegionAvail().y;
            float imgW  = (float)g_previewTexWidth;
            float imgH  = (float)g_previewTexHeight;
            float scale = std::max(0.01f, std::min(maxW / imgW, maxH / imgH));
            ImGui::Image((ImTextureID)g_previewSRV,
                         ImVec2(imgW * scale, imgH * scale));
        }
        else
        {
            ImGui::TextColored(ColorDim(),
                "Preview disabled or waiting for frames.");
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Log ----
    ImGui::BeginChild("##log", ImVec2(logW, availH), true);
    {
        ImGui::Checkbox("Auto-scroll", &state.autoScrollLog);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) g_logs.clear();
        ImGui::Separator();

        ImGui::BeginChild("##loglines", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : g_logs)
            ImGui::TextUnformatted(line.c_str());

        if (state.autoScrollLog &&
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End(); // ##root

    // ── SETTINGS OVERLAY ─────────────────────────────────────────────────────
    RenderSettingsWindow(state, saveRequested);

    return saveRequested;
}
