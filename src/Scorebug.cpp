#include "Scorebug.h"

#include "AppState.h"
#include "Logger.h"

#include <windows.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    struct FieldBinding
    {
        const char* storageKey;
        const char* label;
        ScorebugFieldDefinition ScorebugLayoutManifest::*layoutMember;
        ScorebugFieldResult ScorebugState::*stateMember;
        ScorebugFieldType defaultType;
        const char* defaultWhitelist;
    };

    constexpr std::array<FieldBinding, 7> kFields = {{
        { "team_a_label", "Team A Label", &ScorebugLayoutManifest::teamALabel, &ScorebugState::teamALabel, ScorebugFieldType::Text, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" },
        { "team_a_score", "Team A Score", &ScorebugLayoutManifest::teamAScore, &ScorebugState::teamAScore, ScorebugFieldType::Integer, "0123456789" },
        { "team_b_label", "Team B Label", &ScorebugLayoutManifest::teamBLabel, &ScorebugState::teamBLabel, ScorebugFieldType::Text, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" },
        { "team_b_score", "Team B Score", &ScorebugLayoutManifest::teamBScore, &ScorebugState::teamBScore, ScorebugFieldType::Integer, "0123456789" },
        { "period", "Period", &ScorebugLayoutManifest::period, &ScorebugState::period, ScorebugFieldType::PeriodText, "1234OTNDRHST" },
        { "game_clock", "Game Clock", &ScorebugLayoutManifest::gameClock, &ScorebugState::gameClock, ScorebugFieldType::Clock, "0123456789:" },
        { "shot_clock", "Shot Clock", &ScorebugLayoutManifest::shotClock, &ScorebugState::shotClock, ScorebugFieldType::DecimalClock, "0123456789." }
    }};

    struct FieldStabilizer
    {
        bool hasCommitted = false;
        std::string committedValue;
        double committedConfidence = 0.0;
        std::string pendingValue;
        double pendingConfidence = 0.0;
        int consistentCount = 0;
    };

    struct RuntimeContext
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool running = false;
        bool stopRequested = false;
        bool hasPendingFrame = false;
        cv::Mat pendingFrame;
        ScorebugLayoutManifest activeLayout;
        bool activeLayoutLoaded = false;
        cv::Mat referenceScorebugGray;
        double detectThreshold = 0.70;
        double lastPresenceScore = 0.0;
        bool onAir = false;
        Clock::time_point lastSubmitTime{};
        std::thread worker;

        std::array<FieldStabilizer, kFields.size()> stabilizers{};
        ScorebugState lastState;
        std::string lastPublishedSignature;
        Clock::time_point lastHeartbeatTime{};

        std::vector<std::string> pendingJsonLogs;
        bool tesseractAvailable = false;
        std::string tesseractPath;
        std::string lastError;
        bool missingBinaryLogged = false;
    };

    RuntimeContext g_runtime;

    std::string GetExeDir()
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string path(exePath);
        const size_t pos = path.find_last_of("\\/");
        return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
    }

    std::string Trim(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string CurrentIsoTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buffer[64] = {};
        sprintf_s(
            buffer,
            "%04u-%02u-%02uT%02u:%02u:%02u",
            st.wYear,
            st.wMonth,
            st.wDay,
            st.wHour,
            st.wMinute,
            st.wSecond);
        return buffer;
    }

    std::string SanitizeLayoutName(const std::string& name)
    {
        std::string cleaned = Trim(name);
        for (char& ch : cleaned)
        {
            const bool valid =
                (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == ' ';
            if (!valid)
                ch = '_';
        }

        while (!cleaned.empty() && cleaned.front() == ' ')
            cleaned.erase(cleaned.begin());
        while (!cleaned.empty() && cleaned.back() == ' ')
            cleaned.pop_back();
        for (char& ch : cleaned)
        {
            if (ch == ' ')
                ch = '_';
        }
        return cleaned;
    }

    bool IsFullFrame(const NormalizedRoi& roi)
    {
        return !roi.enabled ||
            (roi.x <= 0.0f && roi.y <= 0.0f && roi.w >= 0.999f && roi.h >= 0.999f);
    }

    void WriteRoi(cv::FileStorage& storage, const char* key, const NormalizedRoi& roi)
    {
        storage << key << "{";
        storage << "enabled" << (!IsFullFrame(roi));
        storage << "x" << roi.x;
        storage << "y" << roi.y;
        storage << "w" << roi.w;
        storage << "h" << roi.h;
        storage << "}";
    }

    NormalizedRoi ReadRoi(const cv::FileNode& node)
    {
        NormalizedRoi roi;
        if (node.empty())
            return roi;

        roi.enabled = static_cast<int>(node["enabled"]) != 0;
        roi.x = static_cast<float>((double)node["x"]);
        roi.y = static_cast<float>((double)node["y"]);
        roi.w = static_cast<float>((double)node["w"]);
        roi.h = static_cast<float>((double)node["h"]);
        if (roi.w <= 0.0f || roi.h <= 0.0f)
            roi = {};
        return roi;
    }

    const char* FieldTypeToString(ScorebugFieldType type)
    {
        switch (type)
        {
        case ScorebugFieldType::Integer:
            return "integer";
        case ScorebugFieldType::PeriodText:
            return "periodText";
        case ScorebugFieldType::Clock:
            return "clock";
        case ScorebugFieldType::DecimalClock:
            return "decimalClock";
        default:
            return "text";
        }
    }

    ScorebugFieldType FieldTypeFromString(const std::string& text)
    {
        if (_stricmp(text.c_str(), "integer") == 0)
            return ScorebugFieldType::Integer;
        if (_stricmp(text.c_str(), "periodText") == 0)
            return ScorebugFieldType::PeriodText;
        if (_stricmp(text.c_str(), "clock") == 0)
            return ScorebugFieldType::Clock;
        if (_stricmp(text.c_str(), "decimalClock") == 0)
            return ScorebugFieldType::DecimalClock;
        return ScorebugFieldType::Text;
    }

    ScorebugFieldDefinition DefaultFieldDefinition(const FieldBinding& binding)
    {
        ScorebugFieldDefinition def;
        def.type = binding.defaultType;
        def.whitelist = binding.defaultWhitelist;
        def.preprocess = "auto";
        return def;
    }

    void NormalizeFieldDefinition(ScorebugFieldDefinition& def, const FieldBinding& binding)
    {
        if (def.whitelist.empty())
            def.whitelist = binding.defaultWhitelist;
        if (def.preprocess.empty())
            def.preprocess = "auto";
    }

    void WriteField(cv::FileStorage& storage, const FieldBinding& binding, const ScorebugFieldDefinition& def)
    {
        storage << binding.storageKey << "{";
        WriteRoi(storage, "roi", def.roi);
        storage << "type" << FieldTypeToString(def.type);
        storage << "whitelist" << def.whitelist;
        storage << "preprocess" << def.preprocess;
        storage << "}";
    }

    ScorebugFieldDefinition ReadField(const cv::FileNode& node, const FieldBinding& binding)
    {
        ScorebugFieldDefinition def = DefaultFieldDefinition(binding);
        if (node.empty())
            return def;

        def.roi = ReadRoi(node["roi"]);
        def.type = node["type"].empty() ? binding.defaultType : FieldTypeFromString(static_cast<std::string>(node["type"]));
        def.whitelist = node["whitelist"].empty() ? binding.defaultWhitelist : static_cast<std::string>(node["whitelist"]);
        def.preprocess = node["preprocess"].empty() ? "auto" : static_cast<std::string>(node["preprocess"]);
        NormalizeFieldDefinition(def, binding);
        return def;
    }

    cv::Rect RoiToRect(const NormalizedRoi& roi, int width, int height, bool fullIfDisabled)
    {
        if (width <= 0 || height <= 0)
            return cv::Rect();
        if (!roi.enabled)
            return fullIfDisabled ? cv::Rect(0, 0, width, height) : cv::Rect();

        const int x = std::clamp(static_cast<int>(roi.x * width + 0.5f), 0, std::max(0, width - 1));
        const int y = std::clamp(static_cast<int>(roi.y * height + 0.5f), 0, std::max(0, height - 1));
        const int w = std::clamp(static_cast<int>(roi.w * width + 0.5f), 1, width - x);
        const int h = std::clamp(static_cast<int>(roi.h * height + 0.5f), 1, height - y);
        return cv::Rect(x, y, w, h);
    }

    std::string FindTesseractBinary()
    {
        char resolved[MAX_PATH] = {};
        const DWORD found = SearchPathA(nullptr, "tesseract.exe", nullptr, MAX_PATH, resolved, nullptr);
        if (found > 0 && found < MAX_PATH)
            return resolved;

        const char* commonPaths[] = {
            "C:\\Program Files\\Tesseract-OCR\\tesseract.exe",
            "C:\\Program Files (x86)\\Tesseract-OCR\\tesseract.exe"
        };

        for (const char* path : commonPaths)
        {
            DWORD attrs = GetFileAttributesA(path);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
                return path;
        }

        return {};
    }

    std::string MakeTempPngPath()
    {
        char tempDir[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, tempDir);
        char tempBase[MAX_PATH] = {};
        GetTempFileNameA(tempDir, "sbg", 0, tempBase);
        DeleteFileA(tempBase);
        std::string path = tempBase;
        path += ".png";
        return path;
    }

    cv::Mat PreprocessField(const cv::Mat& input, const ScorebugFieldDefinition& def)
    {
        if (input.empty())
            return {};

        cv::Mat gray;
        if (input.channels() == 1)
            gray = input.clone();
        else
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

        cv::Mat normalized;
        cv::resize(gray, normalized, cv::Size(), 3.0, 3.0, cv::INTER_CUBIC);
        cv::normalize(normalized, normalized, 0, 255, cv::NORM_MINMAX);

        const std::string preprocess = Trim(def.preprocess);
        if (_stricmp(preprocess.c_str(), "invert") == 0)
            cv::bitwise_not(normalized, normalized);

        cv::copyMakeBorder(normalized, normalized, 12, 12, 12, 12, cv::BORDER_CONSTANT, cv::Scalar(255));
        return normalized;
    }

    bool RunHiddenProcess(const std::string& commandLine, std::string& stdoutText, std::string& error)
    {
        stdoutText.clear();
        error.clear();

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        {
            error = "Unable to create OCR output pipe.";
            return false;
        }

        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi{};
        std::string mutableCommand = commandLine;
        const BOOL ok = CreateProcessA(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi);

        CloseHandle(writePipe);
        writePipe = nullptr;

        if (!ok)
        {
            CloseHandle(readPipe);
            error = "Unable to start tesseract.exe.";
            return false;
        }

        char buffer[512] = {};
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer, static_cast<DWORD>(std::size(buffer) - 1), &bytesRead, nullptr) && bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            stdoutText.append(buffer, bytesRead);
        }

        WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(readPipe);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (exitCode != 0)
        {
            error = "Tesseract returned a non-zero exit code.";
            return false;
        }

        return true;
    }

    ScorebugFieldResult RunTesseractOnField(
        const cv::Mat& fieldImage,
        const ScorebugFieldDefinition& def,
        const std::string& tesseractPath,
        std::string& error)
    {
        ScorebugFieldResult result;
        error.clear();

        const std::string tempPath = MakeTempPngPath();
        if (!cv::imwrite(tempPath, fieldImage))
        {
            error = "Failed to write temporary OCR image.";
            return result;
        }

        const int psm = 7;
        std::ostringstream command;
        command << "\"" << tesseractPath << "\" \"" << tempPath << "\" stdout"
                << " --oem 1 --psm " << psm
                << " -l eng";
        const bool allowWhitelist =
            def.type == ScorebugFieldType::Integer ||
            def.type == ScorebugFieldType::Clock ||
            def.type == ScorebugFieldType::DecimalClock;
        if (allowWhitelist && !def.whitelist.empty())
            command << " -c tessedit_char_whitelist=" << def.whitelist;

        std::string output;
        if (!RunHiddenProcess(command.str(), output, error))
        {
            DeleteFileA(tempPath.c_str());
            return result;
        }
        DeleteFileA(tempPath.c_str());

        output = Trim(output);
        if (output.empty())
        {
            error = "Tesseract returned no OCR data.";
            return result;
        }

        result.valid = true;
        result.value = output;
        result.confidence = 1.0;
        return result;
    }

    std::string SanitizeByType(const std::string& text, ScorebugFieldType type)
    {
        std::string sanitized;
        sanitized.reserve(text.size());
        for (char ch : text)
        {
            const char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            switch (type)
            {
            case ScorebugFieldType::Text:
                if (upper >= 'A' && upper <= 'Z')
                    sanitized.push_back(upper);
                break;
            case ScorebugFieldType::Integer:
                if (upper >= '0' && upper <= '9')
                    sanitized.push_back(upper);
                break;
            case ScorebugFieldType::PeriodText:
                if ((upper >= '0' && upper <= '9') || (upper >= 'A' && upper <= 'Z'))
                    sanitized.push_back(upper);
                break;
            case ScorebugFieldType::Clock:
                if ((upper >= '0' && upper <= '9') || upper == ':')
                    sanitized.push_back(upper);
                break;
            case ScorebugFieldType::DecimalClock:
                if ((upper >= '0' && upper <= '9') || upper == '.')
                    sanitized.push_back(upper);
                break;
            }
        }
        return sanitized;
    }

    bool IsValidForType(const std::string& value, ScorebugFieldType type)
    {
        if (value.empty())
            return false;

        switch (type)
        {
        case ScorebugFieldType::Text:
            return value.size() >= 2 && value.size() <= 6;
        case ScorebugFieldType::Integer:
            return value.find_first_not_of("0123456789") == std::string::npos;
        case ScorebugFieldType::PeriodText:
            return value == "OT" || value == "1" || value == "2" || value == "3" || value == "4" ||
                value == "1ST" || value == "2ND" || value == "3RD" || value == "4TH";
        case ScorebugFieldType::Clock:
        {
            const size_t colon = value.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 3 != value.size())
                return false;
            return std::isdigit(static_cast<unsigned char>(value[0])) &&
                std::isdigit(static_cast<unsigned char>(value[colon + 1])) &&
                std::isdigit(static_cast<unsigned char>(value[colon + 2]));
        }
        case ScorebugFieldType::DecimalClock:
        {
            const size_t dot = value.find('.');
            if (dot == std::string::npos)
                return value.find_first_not_of("0123456789") == std::string::npos;
            return dot > 0 &&
                dot + 2 == value.size() &&
                value.substr(0, dot).find_first_not_of("0123456789") == std::string::npos &&
                std::isdigit(static_cast<unsigned char>(value[dot + 1]));
        }
        }

        return false;
    }

    std::string FormatPeriodValue(const std::string& value)
    {
        if (value == "OT")
            return value;
        if (value == "1")
            return "1st";
        if (value == "2")
            return "2nd";
        if (value == "3")
            return "3rd";
        if (value == "4")
            return "4th";
        if (value == "1ST")
            return "1st";
        if (value == "2ND")
            return "2nd";
        if (value == "3RD")
            return "3rd";
        if (value == "4TH")
            return "4th";
        return value;
    }

    ScorebugFieldResult NormalizeFieldResult(const ScorebugFieldResult& raw, const ScorebugFieldDefinition& def)
    {
        ScorebugFieldResult normalized;
        if (!raw.valid)
            return normalized;

        normalized.value = SanitizeByType(raw.value, def.type);
        normalized.confidence = raw.confidence;
        if (!IsValidForType(normalized.value, def.type))
            return {};

        if (def.type == ScorebugFieldType::PeriodText)
            normalized.value = FormatPeriodValue(normalized.value);
        normalized.valid = true;
        return normalized;
    }

    std::string BuildStateSignature(const ScorebugState& state)
    {
        std::ostringstream oss;
        oss << state.layoutName << "|"
            << state.teamALabel.value << "|" << state.teamAScore.value << "|"
            << state.teamBLabel.value << "|" << state.teamBScore.value << "|"
            << state.period.value << "|" << state.gameClock.value << "|"
            << state.shotClock.value << "|"
            << state.detected;
        return oss.str();
    }

    ScorebugFieldResult GetCommittedResult(const FieldStabilizer& stabilizer)
    {
        ScorebugFieldResult result;
        if (!stabilizer.hasCommitted)
            return result;
        result.valid = true;
        result.value = stabilizer.committedValue;
        result.confidence = stabilizer.committedConfidence;
        return result;
    }

    void ApplyStableField(FieldStabilizer& stabilizer, const ScorebugFieldResult& incoming)
    {
        if (!incoming.valid)
            return;

        if (stabilizer.hasCommitted && stabilizer.committedValue == incoming.value)
        {
            stabilizer.committedConfidence = incoming.confidence;
            stabilizer.pendingValue.clear();
            stabilizer.pendingConfidence = 0.0;
            stabilizer.consistentCount = 0;
            return;
        }

        if (stabilizer.pendingValue == incoming.value)
        {
            ++stabilizer.consistentCount;
            stabilizer.pendingConfidence = incoming.confidence;
        }
        else
        {
            stabilizer.pendingValue = incoming.value;
            stabilizer.pendingConfidence = incoming.confidence;
            stabilizer.consistentCount = 1;
        }

        const int commitThreshold = stabilizer.hasCommitted ? 2 : 1;
        if (stabilizer.consistentCount >= commitThreshold)
        {
            stabilizer.hasCommitted = true;
            stabilizer.committedValue = stabilizer.pendingValue;
            stabilizer.committedConfidence = stabilizer.pendingConfidence;
            stabilizer.pendingValue.clear();
            stabilizer.pendingConfidence = 0.0;
            stabilizer.consistentCount = 0;
        }
    }

    ScorebugState BuildCommittedState(const ScorebugLayoutManifest& layout, const std::array<FieldStabilizer, kFields.size()>& stabilizers)
    {
        ScorebugState state;
        state.layoutName = layout.name;
        state.teamALabel = GetCommittedResult(stabilizers[0]);
        state.teamAScore = GetCommittedResult(stabilizers[1]);
        state.teamBLabel = GetCommittedResult(stabilizers[2]);
        state.teamBScore = GetCommittedResult(stabilizers[3]);
        state.period = GetCommittedResult(stabilizers[4]);
        state.gameClock = GetCommittedResult(stabilizers[5]);
        state.shotClock = GetCommittedResult(stabilizers[6]);
        state.detected =
            state.teamALabel.valid &&
            state.teamAScore.valid &&
            state.teamBLabel.valid &&
            state.teamBScore.valid &&
            state.period.valid &&
            state.gameClock.valid &&
            state.shotClock.valid;
        return state;
    }

    double CalculatePresenceScore(const cv::Mat& frameCropBgr, const cv::Mat& referenceGray)
    {
        if (frameCropBgr.empty() || referenceGray.empty())
            return 0.0;

        cv::Mat frameGray;
        if (frameCropBgr.channels() == 1)
            frameGray = frameCropBgr;
        else
            cv::cvtColor(frameCropBgr, frameGray, cv::COLOR_BGR2GRAY);

        cv::Mat resizedReference;
        if (referenceGray.size() != frameGray.size())
            cv::resize(referenceGray, resizedReference, frameGray.size(), 0.0, 0.0, cv::INTER_LINEAR);
        else
            resizedReference = referenceGray;

        cv::Mat result;
        cv::matchTemplate(frameGray, resizedReference, result, cv::TM_CCOEFF_NORMED);
        double maxValue = 0.0;
        cv::minMaxLoc(result, nullptr, &maxValue);
        return maxValue;
    }

    bool LoadManifest(const fs::path& jsonPath, ScorebugLayoutManifest& manifest, std::string& error)
    {
        cv::FileStorage storage(jsonPath.string(), cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
        if (!storage.isOpened())
        {
            error = "unable to open layout.json";
            return false;
        }

        manifest.name = static_cast<std::string>(storage["name"]);
        manifest.version = storage["version"].empty() ? 1 : static_cast<int>(storage["version"]);
        manifest.referenceImagePath = storage["reference_image"].empty() ? "reference.png" : static_cast<std::string>(storage["reference_image"]);
        manifest.frameRoi = ReadRoi(storage["frame_roi"]);
        manifest.createdAt = storage["created_at"].empty() ? "" : static_cast<std::string>(storage["created_at"]);
        manifest.updatedAt = storage["updated_at"].empty() ? "" : static_cast<std::string>(storage["updated_at"]);
        manifest.folderPath = jsonPath.parent_path().string();
        manifest.folderName = jsonPath.parent_path().filename().string();

        for (const auto& binding : kFields)
            manifest.*(binding.layoutMember) = ReadField(storage[binding.storageKey], binding);

        if (Trim(manifest.name).empty())
        {
            error = "missing layout name";
            return false;
        }

        if (!fs::exists(jsonPath.parent_path() / manifest.referenceImagePath))
        {
            error = "missing reference image";
            return false;
        }

        return true;
    }

    bool SaveManifest(const ScorebugLayoutManifest& manifest, std::string& error)
    {
        const fs::path jsonPath = fs::path(manifest.folderPath) / "layout.json";
        cv::FileStorage storage(jsonPath.string(), cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);
        if (!storage.isOpened())
        {
            error = "unable to write layout.json";
            return false;
        }

        storage << "name" << manifest.name;
        storage << "version" << manifest.version;
        storage << "reference_image" << manifest.referenceImagePath;
        WriteRoi(storage, "frame_roi", manifest.frameRoi);
        for (const auto& binding : kFields)
            WriteField(storage, binding, manifest.*(binding.layoutMember));
        storage << "created_at" << manifest.createdAt;
        storage << "updated_at" << manifest.updatedAt;
        return true;
    }

    void ResetRuntimeForLayout(const ScorebugLayoutManifest* layout)
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.activeLayoutLoaded = layout != nullptr;
        if (layout)
            g_runtime.activeLayout = *layout;
        else
            g_runtime.activeLayout = {};
        g_runtime.referenceScorebugGray.release();
        g_runtime.stabilizers = {};
        g_runtime.lastState = {};
        g_runtime.lastPublishedSignature.clear();
        g_runtime.lastHeartbeatTime = Clock::now();
        g_runtime.hasPendingFrame = false;
        g_runtime.lastPresenceScore = 0.0;
        g_runtime.onAir = false;
    }

    bool BuildReferenceScorebug(const ScorebugLayoutManifest& layout, cv::Mat& scorebugGray)
    {
        const std::string referencePath = layout.folderPath + "\\" + layout.referenceImagePath;
        const cv::Mat reference = cv::imread(referencePath, cv::IMREAD_COLOR);
        if (reference.empty())
            return false;

        const cv::Rect roi = RoiToRect(layout.frameRoi, reference.cols, reference.rows, true);
        if (roi.width <= 0 || roi.height <= 0)
            return false;

        cv::Mat cropped = reference(roi).clone();
        cv::cvtColor(cropped, scorebugGray, cv::COLOR_BGR2GRAY);
        return !scorebugGray.empty();
    }

    void EnsureWorker()
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        if (g_runtime.running)
            return;

        g_runtime.tesseractPath = FindTesseractBinary();
        g_runtime.tesseractAvailable = !g_runtime.tesseractPath.empty();
        g_runtime.stopRequested = false;
        g_runtime.running = true;
        g_runtime.worker = std::thread([]()
        {
            for (;;)
            {
                cv::Mat frame;
                ScorebugLayoutManifest layout;
                cv::Mat referenceScorebugGray;
                double detectThreshold = 0.70;
                {
                    std::unique_lock<std::mutex> lock(g_runtime.mutex);
                    g_runtime.cv.wait(lock, []()
                    {
                        return g_runtime.stopRequested || g_runtime.hasPendingFrame;
                    });

                    if (g_runtime.stopRequested)
                        break;

                    frame = g_runtime.pendingFrame.clone();
                    layout = g_runtime.activeLayout;
                    referenceScorebugGray = g_runtime.referenceScorebugGray.clone();
                    detectThreshold = g_runtime.detectThreshold;
                    g_runtime.hasPendingFrame = false;
                }

                if (frame.empty() || !layout.frameRoi.enabled || referenceScorebugGray.empty())
                    continue;

                if (!g_runtime.tesseractAvailable)
                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    if (!g_runtime.missingBinaryLogged)
                    {
                        g_runtime.pendingJsonLogs.push_back("ERROR: Tesseract OCR is not installed. Install tesseract.exe to enable scorebug OCR.");
                        g_runtime.missingBinaryLogged = true;
                    }
                    g_runtime.lastError = "tesseract.exe not found";
                    continue;
                }

                const cv::Rect frameRect = RoiToRect(layout.frameRoi, frame.cols, frame.rows, true);
                if (frameRect.width <= 0 || frameRect.height <= 0)
                    continue;

                const cv::Mat scorebugRegion = frame(frameRect).clone();
                const double presenceScore = CalculatePresenceScore(scorebugRegion, referenceScorebugGray);
                const bool detectedNow = presenceScore >= detectThreshold;

                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    g_runtime.lastPresenceScore = presenceScore;
                    if (detectedNow != g_runtime.onAir)
                    {
                        g_runtime.onAir = detectedNow;
                        g_runtime.pendingJsonLogs.push_back(detectedNow ? "STATE:scoreboard onair" : "STATE:scoreboard offair");
                        if (!detectedNow)
                            g_runtime.lastPublishedSignature.clear();
                    }
                    if (!detectedNow)
                        g_runtime.lastState.detected = false;
                }

                if (!detectedNow)
                    continue;

                std::array<ScorebugFieldResult, kFields.size()> readResults{};

                for (size_t i = 0; i < kFields.size(); ++i)
                {
                    const auto& binding = kFields[i];
                    const ScorebugFieldDefinition& def = layout.*(binding.layoutMember);
                    if (!def.roi.enabled)
                        continue;

                    const cv::Rect fieldRect = RoiToRect(def.roi, scorebugRegion.cols, scorebugRegion.rows, false);
                    if (fieldRect.width <= 0 || fieldRect.height <= 0)
                        continue;

                    cv::Mat fieldImage = scorebugRegion(fieldRect).clone();
                    fieldImage = PreprocessField(fieldImage, def);
                    std::string error;
                    const ScorebugFieldResult raw = RunTesseractOnField(fieldImage, def, g_runtime.tesseractPath, error);
                    if (!error.empty())
                    {
                        std::lock_guard<std::mutex> lock(g_runtime.mutex);
                        g_runtime.lastError = error;
                    }
                    readResults[i] = NormalizeFieldResult(raw, def);
                }

                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    for (size_t i = 0; i < readResults.size(); ++i)
                        ApplyStableField(g_runtime.stabilizers[i], readResults[i]);

                    ScorebugState nextState = BuildCommittedState(layout, g_runtime.stabilizers);
                    const std::string signature = BuildStateSignature(nextState);
                    const bool changed = signature != g_runtime.lastPublishedSignature;

                    g_runtime.lastState = nextState;
                    if (!signature.empty() && changed)
                    {
                        nextState.publishedAtIso = CurrentIsoTimestamp();
                        g_runtime.lastState = nextState;
                        g_runtime.lastPublishedSignature = signature;
                    }
                }
            }
        });
    }
}

std::string Scorebug_GetRootDirectory()
{
    return GetExeDir() + "scorebugs";
}

const ScorebugLayoutManifest* Scorebug_FindLayoutByName(const AppState& state, const std::string& name)
{
    for (const auto& layout : state.scorebugLayouts)
    {
        if (layout.name == name)
            return &layout;
    }
    return nullptr;
}

ScorebugLayoutManifest* Scorebug_FindLayoutByName(AppState& state, const std::string& name)
{
    for (auto& layout : state.scorebugLayouts)
    {
        if (layout.name == name)
            return &layout;
    }
    return nullptr;
}

bool Scorebug_LoadLayoutCatalog(AppState& state)
{
    state.scorebugLayouts.clear();

    const fs::path root(Scorebug_GetRootDirectory());
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec)
    {
        AddLog(CurrentTimestamp() + " | ERROR: unable to create scorebugs directory: " + root.string());
        return false;
    }

    for (const auto& entry : fs::directory_iterator(root, ec))
    {
        if (ec || !entry.is_directory())
            continue;

        const fs::path jsonPath = entry.path() / "layout.json";
        if (!fs::exists(jsonPath))
            continue;

        ScorebugLayoutManifest manifest;
        std::string error;
        if (LoadManifest(jsonPath, manifest, error))
        {
            state.scorebugLayouts.push_back(manifest);
        }
        else
        {
            AddLog(CurrentTimestamp() + " | WARNING: skipped scorebug layout '" + entry.path().filename().string() + "' - " + error);
        }
    }

    std::sort(
        state.scorebugLayouts.begin(),
        state.scorebugLayouts.end(),
        [](const ScorebugLayoutManifest& lhs, const ScorebugLayoutManifest& rhs)
        {
            return _stricmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
        });

    if (state.scorebugLayouts.empty())
    {
        state.activeScorebugLayoutName.clear();
        state.lastScorebugState = {};
        ResetRuntimeForLayout(nullptr);
        AddLog(CurrentTimestamp() + " | WARNING: no scorebug layouts found in " + Scorebug_GetRootDirectory());
        return false;
    }

    if (state.activeScorebugLayoutName.empty() || !Scorebug_FindLayoutByName(state, state.activeScorebugLayoutName))
    {
        state.activeScorebugLayoutName = state.scorebugLayouts.front().name;
        AddLog(CurrentTimestamp() + " | Active scorebug layout fallback: " + state.activeScorebugLayoutName);
    }

    return Scorebug_SetActiveLayout(state, state.activeScorebugLayoutName);
}

bool Scorebug_SetActiveLayout(AppState& state, const std::string& name)
{
    if (name.empty())
    {
        state.activeScorebugLayoutName.clear();
        state.lastScorebugState = {};
        ResetRuntimeForLayout(nullptr);
        return false;
    }

    const ScorebugLayoutManifest* manifest = Scorebug_FindLayoutByName(state, name);
    if (!manifest)
    {
        AddLog(CurrentTimestamp() + " | WARNING: requested scorebug layout not found: " + name);
        return false;
    }

    state.activeScorebugLayoutName = manifest->name;
    state.lastScorebugState = {};
    ResetRuntimeForLayout(manifest);
    cv::Mat referenceGray;
    if (!BuildReferenceScorebug(*manifest, referenceGray))
    {
        AddLog(CurrentTimestamp() + " | ERROR: failed to prepare scorebug reference crop for " + state.activeScorebugLayoutName);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.referenceScorebugGray = referenceGray;
        g_runtime.detectThreshold = state.scorebugDetectThreshold;
    }
    EnsureWorker();
    AddLog(CurrentTimestamp() + " | Active scorebug layout loaded: " + state.activeScorebugLayoutName);
    return true;
}

bool Scorebug_SaveLayout(
    AppState& state,
    const ScorebugLayoutManifest& manifest,
    const std::string& sourceReferenceImagePath,
    const std::string& originalName,
    std::string& error)
{
    error.clear();
    const std::string trimmedName = Trim(manifest.name);
    if (trimmedName.empty())
    {
        error = "Layout name is required.";
        return false;
    }

    const cv::Mat reference = cv::imread(sourceReferenceImagePath, cv::IMREAD_COLOR);
    if (reference.empty())
    {
        error = "Unable to load reference image.";
        return false;
    }

    if (!manifest.frameRoi.enabled)
    {
        error = "Scorebug frame ROI is required.";
        return false;
    }

    const fs::path root(Scorebug_GetRootDirectory());
    fs::create_directories(root);

    const std::string folderName = SanitizeLayoutName(trimmedName);
    if (folderName.empty())
    {
        error = "Layout name does not produce a valid folder name.";
        return false;
    }

    fs::path currentFolder;
    std::string createdAt = CurrentIsoTimestamp();
    if (!originalName.empty())
    {
        if (const ScorebugLayoutManifest* existing = Scorebug_FindLayoutByName(state, originalName))
        {
            currentFolder = existing->folderPath;
            createdAt = existing->createdAt.empty() ? createdAt : existing->createdAt;
        }
    }

    const fs::path targetFolder = root / folderName;
    if (!currentFolder.empty() && fs::exists(currentFolder) && fs::exists(targetFolder))
    {
        std::error_code equivError;
        if (!fs::equivalent(currentFolder, targetFolder, equivError) && !equivError)
        {
            error = "A scorebug layout folder with this name already exists.";
            return false;
        }
    }
    else if (currentFolder.empty() && fs::exists(targetFolder))
    {
        error = "A scorebug layout folder with this name already exists.";
        return false;
    }

    if (!currentFolder.empty() && currentFolder != targetFolder)
        fs::rename(currentFolder, targetFolder);

    fs::create_directories(targetFolder);

    const fs::path referencePath = targetFolder / "reference.png";
    if (!cv::imwrite(referencePath.string(), reference))
    {
        error = "Failed to save reference image.";
        return false;
    }

    ScorebugLayoutManifest persisted = manifest;
    persisted.name = trimmedName;
    persisted.folderName = folderName;
    persisted.folderPath = targetFolder.string();
    persisted.referenceImagePath = "reference.png";
    persisted.createdAt = createdAt;
    persisted.updatedAt = CurrentIsoTimestamp();

    for (const auto& binding : kFields)
        NormalizeFieldDefinition(persisted.*(binding.layoutMember), binding);

    if (!SaveManifest(persisted, error))
        return false;

    Scorebug_LoadLayoutCatalog(state);
    AddLog(CurrentTimestamp() + " | Scorebug layout saved: " + persisted.name);
    return true;
}

bool Scorebug_DeleteLayout(AppState& state, const std::string& name, std::string& error)
{
    error.clear();
    const ScorebugLayoutManifest* manifest = Scorebug_FindLayoutByName(state, name);
    if (!manifest)
    {
        error = "Scorebug layout not found.";
        return false;
    }

    std::error_code ec;
    fs::remove_all(manifest->folderPath, ec);
    if (ec)
    {
        error = "Unable to delete scorebug layout folder.";
        return false;
    }

    Scorebug_LoadLayoutCatalog(state);
    AddLog(CurrentTimestamp() + " | Scorebug layout deleted: " + name);
    return true;
}

void Scorebug_ProcessFrame(const cv::Mat& bgrFrame, AppState& state)
{
    EnsureWorker();

    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        state.lastScorebugState = g_runtime.lastState;
        state.lastScorebugPresenceScore = g_runtime.lastPresenceScore;
        state.scorebugOnAir = g_runtime.onAir;
        for (const std::string& entry : g_runtime.pendingJsonLogs)
        {
            if (entry == "STATE:scoreboard onair")
                AddLog(CurrentTimestamp() + " | scoreboard ONAIR");
            else if (entry == "STATE:scoreboard offair")
                AddLog(CurrentTimestamp() + " | Scoreboard turned off");
            else if (entry.rfind("ERROR:", 0) == 0)
                AddLog(CurrentTimestamp() + " | " + entry);
        }
        g_runtime.pendingJsonLogs.clear();
    }

    if (!state.scorebugOcrEnabled)
    {
        state.scorebugOnAir = false;
        state.lastScorebugPresenceScore = 0.0;
        state.lastScorebugState.detected = false;
        return;
    }

    if (bgrFrame.empty())
        return;

    const ScorebugLayoutManifest* manifest = Scorebug_FindLayoutByName(state, state.activeScorebugLayoutName);
    if (!manifest)
    {
        state.scorebugOnAir = false;
        state.lastScorebugPresenceScore = 0.0;
        state.lastScorebugState.detected = false;
        return;
    }

    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.detectThreshold = state.scorebugDetectThreshold;
    if (g_runtime.lastSubmitTime.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_runtime.lastSubmitTime).count() < 350)
    {
        return;
    }

    g_runtime.pendingFrame = bgrFrame.clone();
    g_runtime.hasPendingFrame = true;
    g_runtime.lastSubmitTime = now;
    g_runtime.cv.notify_one();
}

ScorebugState Scorebug_GetLastState()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return g_runtime.lastState;
}

ScorebugSubmissionStatus Scorebug_GetStatus()
{
    ScorebugSubmissionStatus status;
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    status.tesseractAvailable = g_runtime.tesseractAvailable;
    status.tesseractPath = g_runtime.tesseractPath;
    status.lastError = g_runtime.lastError;
    return status;
}

void Scorebug_Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        if (!g_runtime.running)
            return;
        g_runtime.stopRequested = true;
        g_runtime.cv.notify_one();
    }

    if (g_runtime.worker.joinable())
        g_runtime.worker.join();

    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.running = false;
    g_runtime.stopRequested = false;
    g_runtime.hasPendingFrame = false;
    g_runtime.pendingFrame.release();
    g_runtime.activeLayout = {};
    g_runtime.activeLayoutLoaded = false;
    g_runtime.lastSubmitTime = {};
    g_runtime.stabilizers = {};
    g_runtime.lastState = {};
    g_runtime.lastPublishedSignature.clear();
    g_runtime.lastHeartbeatTime = {};
    g_runtime.pendingJsonLogs.clear();
    g_runtime.tesseractAvailable = false;
    g_runtime.tesseractPath.clear();
    g_runtime.lastError.clear();
    g_runtime.missingBinaryLogged = false;
}
