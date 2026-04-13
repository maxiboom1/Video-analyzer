#include "Config.h"
#include "Logger.h"
#include <windows.h>
#include <string>

int WORK_W = 1920;
int WORK_H = 1080;

static std::string GetConfigPath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    std::string path(exePath);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);

    return path + "config.ini";
}

static std::string IniGetString(const char* section, const char* key,
                                const char* defaultVal, const char* iniPath)
{
    char buf[512] = {};
    GetPrivateProfileStringA(section, key, defaultVal, buf, sizeof(buf), iniPath);
    return std::string(buf);
}

static int IniGetInt(const char* section, const char* key,
                     int defaultVal, const char* iniPath)
{
    return (int)GetPrivateProfileIntA(section, key, defaultVal, iniPath);
}

void Config_Load(AppState& state)
{
    std::string iniPath = GetConfigPath();
    DWORD attr = GetFileAttributesA(iniPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        AddLog(CurrentTimestamp() + " | config.ini not found - creating defaults");
        Config_Save(state);
        return;
    }

    const char* ip = iniPath.c_str();

    auto vizIp = IniGetString("Viz", "ip", state.vizIp, ip);
    auto vizPort = IniGetInt("Viz", "port", state.vizPort, ip);
    auto cmdOn = IniGetString("Viz", "cmd_on", state.cmdOn, ip);
    auto cmdOff = IniGetString("Viz", "cmd_off", state.cmdOff, ip);

    strncpy_s(state.vizIp, vizIp.c_str(), _TRUNCATE);
    state.vizPort = vizPort;
    strncpy_s(state.cmdOn, cmdOn.c_str(), _TRUNCATE);
    strncpy_s(state.cmdOff, cmdOff.c_str(), _TRUNCATE);

    state.detectThreshold = static_cast<float>(
        IniGetInt("Detection", "detect_threshold_pct", (int)(state.detectThreshold * 100), ip)) / 100.0f;
    state.resetThreshold = static_cast<float>(
        IniGetInt("Detection", "reset_threshold_pct", (int)(state.resetThreshold * 100), ip)) / 100.0f;
    state.cooldownMs = IniGetInt("Detection", "cooldown_ms", state.cooldownMs, ip);

    const auto sourceType = IniGetString("Video", "source_type", "webcam", ip);
    state.selectedSourceKind = (sourceType == "blackmagic") ? VideoSourceKind::Blackmagic : VideoSourceKind::Webcam;
    state.cameraIndex = IniGetInt("Video", "device_id", state.cameraIndex, ip);
    state.blackmagicDisplayMode = IniGetInt("Video", "blackmagic_display_mode", state.blackmagicDisplayMode, ip);

    AddLog(CurrentTimestamp() + " | config.ini loaded from: " + iniPath);
}

void Config_Save(const AppState& state)
{
    std::string iniPath = GetConfigPath();
    const char* ip = iniPath.c_str();

    WritePrivateProfileStringA("Viz", "ip", state.vizIp, ip);
    WritePrivateProfileStringA("Viz", "port", std::to_string(state.vizPort).c_str(), ip);
    WritePrivateProfileStringA("Viz", "cmd_on", state.cmdOn, ip);
    WritePrivateProfileStringA("Viz", "cmd_off", state.cmdOff, ip);

    WritePrivateProfileStringA("Detection", "detect_threshold_pct",
        std::to_string((int)(state.detectThreshold * 100)).c_str(), ip);
    WritePrivateProfileStringA("Detection", "reset_threshold_pct",
        std::to_string((int)(state.resetThreshold * 100)).c_str(), ip);
    WritePrivateProfileStringA("Detection", "cooldown_ms",
        std::to_string(state.cooldownMs).c_str(), ip);

    WritePrivateProfileStringA("Video", "source_type",
        state.selectedSourceKind == VideoSourceKind::Blackmagic ? "blackmagic" : "webcam", ip);
    WritePrivateProfileStringA("Video", "device_id", std::to_string(state.cameraIndex).c_str(), ip);
    WritePrivateProfileStringA("Video", "blackmagic_display_mode", std::to_string(state.blackmagicDisplayMode).c_str(), ip);

    AddLog(CurrentTimestamp() + " | config.ini saved");
}
