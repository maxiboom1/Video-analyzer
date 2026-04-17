#include "Scorebug.h"

#include "AppState.h"
#include "Logger.h"

#include <windows.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
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

    struct PropStabilizer
    {
        bool hasCommitted = false;
        std::string committedValue;
        double committedConfidence = 0.0;
        std::string pendingValue;
        double pendingConfidence = 0.0;
        int consistentCount = 0;
        int missCount = 0;
    };

    struct RuntimeContext
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool running = false;
        bool stopRequested = false;
        bool hasPendingFrame = false;
        cv::Mat pendingFrame;
        OcrElementManifest activeElement;
        bool activeElementLoaded = false;
        cv::Mat referenceElementGray;
        double detectThreshold = 0.70;
        double lastPresenceScore = 0.0;
        bool onAir = false;
        Clock::time_point lastSubmitTime{};
        std::thread worker;

        std::vector<PropStabilizer> stabilizers;
        OcrElementState lastState;
        std::string lastPublishedSignature;

        std::vector<std::string> pendingLogs;
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

    std::string CollapseWhitespace(const std::string& value)
    {
        std::string out;
        bool inSpace = false;
        for (unsigned char ch : value)
        {
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ')
            {
                if (!out.empty())
                    inSpace = true;
                continue;
            }

            if (inSpace)
            {
                out.push_back(' ');
                inSpace = false;
            }
            out.push_back(static_cast<char>(ch));
        }
        return Trim(out);
    }

    std::string CurrentIsoTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buffer[64] = {};
        sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

    std::string SanitizeFolderName(const std::string& name)
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
            if (ch == ' ')
                ch = '_';
        }
        return cleaned;
    }

    bool IsFullFrame(const NormalizedRoi& roi)
    {
        return !roi.enabled || (roi.x <= 0.0f && roi.y <= 0.0f && roi.w >= 0.999f && roi.h >= 0.999f);
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

    const char* OcrPropTypeToStorageString(OcrPropType type)
    {
        switch (type)
        {
        case OcrPropType::Number:
            return "number";
        case OcrPropType::Text:
            return "text";
        default:
            return "auto";
        }
    }

    OcrPropType OcrPropTypeFromString(const std::string& text)
    {
        if (_stricmp(text.c_str(), "number") == 0)
            return OcrPropType::Number;
        if (_stricmp(text.c_str(), "text") == 0)
            return OcrPropType::Text;
        return OcrPropType::Auto;
    }

    std::string ToUpperAscii(const std::string& value)
    {
        std::string out = value;
        for (char& ch : out)
            ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        return out;
    }

    bool EqualsIgnoreCase(const std::string& lhs, const std::string& rhs)
    {
        return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
    }

    bool HasDuplicatePropName(const std::vector<OcrPropManifest>& props, const std::string& name, const std::string& ignoreName)
    {
        bool foundIgnored = false;
        for (const auto& prop : props)
        {
            if (!ignoreName.empty() && EqualsIgnoreCase(prop.name, ignoreName) && !foundIgnored)
            {
                foundIgnored = true;
                continue;
            }
            if (EqualsIgnoreCase(prop.name, name))
                return true;
        }
        return false;
    }

    bool ValidateElementManifest(const AppState& state, const OcrElementManifest& manifest, const std::string& originalName, std::string& error)
    {
        if (Trim(manifest.name).empty())
        {
            error = "Element name is required.";
            return false;
        }
        if (!manifest.frameRoi.enabled)
        {
            error = "Element ROI is required.";
            return false;
        }
        if (manifest.props.size() > 12)
        {
            error = "An OCR element supports up to 12 properties.";
            return false;
        }

        for (const auto& existing : state.ocrElements)
        {
            if (!originalName.empty() && EqualsIgnoreCase(existing.name, originalName))
                continue;
            if (EqualsIgnoreCase(existing.name, manifest.name))
            {
                error = "An OCR element with this name already exists.";
                return false;
            }
        }

        for (const auto& prop : manifest.props)
        {
            if (Trim(prop.name).empty())
            {
                error = "Property names cannot be empty.";
                return false;
            }
            if (!prop.roi.enabled)
            {
                error = "Every property must have an ROI.";
                return false;
            }
            int sameNameCount = 0;
            for (const auto& other : manifest.props)
            {
                if (EqualsIgnoreCase(other.name, prop.name))
                    ++sameNameCount;
            }
            if (sameNameCount > 1)
            {
                error = "Property names must be unique inside the same element.";
                return false;
            }
        }

        return true;
    }

    bool LoadManifest(const fs::path& jsonPath, OcrElementManifest& manifest, std::string& error)
    {
        cv::FileStorage storage(jsonPath.string(), cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
        if (!storage.isOpened())
        {
            error = "unable to open element.json";
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
        manifest.props.clear();

        const cv::FileNode propsNode = storage["props"];
        if (!propsNode.empty() && propsNode.isSeq())
        {
            for (const auto& node : propsNode)
            {
                OcrPropManifest prop;
                prop.name = node["name"].empty() ? "" : static_cast<std::string>(node["name"]);
                prop.roi = ReadRoi(node["roi"]);
                prop.type = node["type"].empty() ? OcrPropType::Auto : OcrPropTypeFromString(static_cast<std::string>(node["type"]));
                manifest.props.push_back(prop);
            }
        }

        if (Trim(manifest.name).empty())
        {
            error = "missing element name";
            return false;
        }
        if (!fs::exists(jsonPath.parent_path() / manifest.referenceImagePath))
        {
            error = "missing reference image";
            return false;
        }
        return true;
    }

    bool SaveManifest(const OcrElementManifest& manifest, std::string& error)
    {
        const fs::path jsonPath = fs::path(manifest.folderPath) / "element.json";
        cv::FileStorage storage(jsonPath.string(), cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);
        if (!storage.isOpened())
        {
            error = "unable to write element.json";
            return false;
        }

        storage << "name" << manifest.name;
        storage << "version" << manifest.version;
        storage << "reference_image" << manifest.referenceImagePath;
        WriteRoi(storage, "frame_roi", manifest.frameRoi);
        storage << "props" << "[";
        for (const auto& prop : manifest.props)
        {
            storage << "{";
            storage << "name" << prop.name;
            WriteRoi(storage, "roi", prop.roi);
            storage << "type" << OcrPropTypeToStorageString(prop.type);
            storage << "}";
        }
        storage << "]";
        storage << "created_at" << manifest.createdAt;
        storage << "updated_at" << manifest.updatedAt;
        return true;
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
        GetTempFileNameA(tempDir, "ocr", 0, tempBase);
        DeleteFileA(tempBase);
        return std::string(tempBase) + ".png";
    }

    cv::Mat PreprocessField(const cv::Mat& input)
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
        const BOOL ok = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi);

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

    std::string NumberWhitelist()
    {
        return "0123456789:.-";
    }

    std::string TextWhitelist()
    {
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_/";
    }

    std::string AutoWhitelist()
    {
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:.-_/ ";
    }

    int PsmForType(OcrPropType type)
    {
        switch (type)
        {
        case OcrPropType::Text:
            return 8;
        case OcrPropType::Number:
            return 7;
        default:
            return 7;
        }
    }

    std::string SanitizeByType(const std::string& text, OcrPropType type);
    bool IsValidForType(const std::string& value, OcrPropType type);

    std::vector<cv::Mat> BuildPreprocessVariants(const cv::Mat& input, OcrPropType type)
    {
        std::vector<cv::Mat> variants;
        if (input.empty())
            return variants;

        cv::Mat gray;
        if (input.channels() == 1)
            gray = input.clone();
        else
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(), 4.0, 4.0, cv::INTER_CUBIC);
        cv::normalize(resized, resized, 0, 255, cv::NORM_MINMAX);

        cv::Mat blurred;
        cv::GaussianBlur(resized, blurred, cv::Size(3, 3), 0.0);

        cv::Mat otsu;
        cv::threshold(blurred, otsu, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        cv::Mat otsuInv;
        cv::bitwise_not(otsu, otsuInv);

        cv::Mat adaptive;
        cv::adaptiveThreshold(blurred, adaptive, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 31, 11);

        cv::Mat adaptiveInv;
        cv::bitwise_not(adaptive, adaptiveInv);

        auto addBorder = [](const cv::Mat& src)
        {
            cv::Mat bordered;
            cv::copyMakeBorder(src, bordered, 14, 14, 14, 14, cv::BORDER_CONSTANT, cv::Scalar(255));
            return bordered;
        };

        variants.push_back(addBorder(blurred));
        variants.push_back(addBorder(otsu));
        variants.push_back(addBorder(otsuInv));
        if (type != OcrPropType::Number)
            variants.push_back(addBorder(adaptive));
        if (type == OcrPropType::Auto)
            variants.push_back(addBorder(adaptiveInv));
        return variants;
    }

    int ScoreSanitizedCandidate(const std::string& value, OcrPropType type)
    {
        if (!IsValidForType(value, type))
            return 0;

        int score = static_cast<int>(value.size()) * 10;
        for (unsigned char ch : value)
        {
            if (ch >= '0' && ch <= '9')
                score += (type == OcrPropType::Number) ? 8 : 4;
            else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                score += (type == OcrPropType::Text) ? 8 : 4;
            else if (ch == ':' || ch == '.' || ch == '-')
                score += 3;
        }
        return score;
    }

    OcrPropResult RunTesseractOnField(
        const cv::Mat& fieldImage,
        const OcrPropManifest& prop,
        const std::string& tesseractPath,
        std::string& error)
    {
        OcrPropResult result;
        result.name = prop.name;
        result.type = prop.type;
        error.clear();

        const std::vector<cv::Mat> variants = BuildPreprocessVariants(fieldImage, prop.type);
        int bestScore = 0;
        std::string bestRaw;
        std::string lastError;
        for (const auto& variant : variants)
        {
            const std::string tempPath = MakeTempPngPath();
            if (!cv::imwrite(tempPath, variant))
            {
                lastError = "Failed to write temporary OCR image.";
                continue;
            }

            std::ostringstream command;
            command << "\"" << tesseractPath << "\" \"" << tempPath << "\" stdout --oem 1 --psm " << PsmForType(prop.type) << " -l eng";
            switch (prop.type)
            {
            case OcrPropType::Number:
                command << " -c tessedit_char_whitelist=" << NumberWhitelist();
                break;
            case OcrPropType::Text:
                command << " -c tessedit_char_whitelist=" << TextWhitelist();
                break;
            case OcrPropType::Auto:
                command << " -c tessedit_char_whitelist=" << AutoWhitelist();
                break;
            }

            std::string output;
            std::string processError;
            const bool ok = RunHiddenProcess(command.str(), output, processError);
            DeleteFileA(tempPath.c_str());
            if (!ok)
            {
                lastError = processError;
                continue;
            }

            output = Trim(output);
            const std::string sanitized = SanitizeByType(output, prop.type);
            const int candidateScore = ScoreSanitizedCandidate(sanitized, prop.type);
            if (candidateScore > bestScore)
            {
                bestScore = candidateScore;
                bestRaw = output;
            }
        }

        if (bestScore <= 0 || bestRaw.empty())
        {
            error = lastError.empty() ? "Tesseract returned no OCR data." : lastError;
            return result;
        }

        result.value = bestRaw;
        result.confidence = std::min(0.99, 0.55 + (static_cast<double>(bestScore) / 100.0));
        result.valid = true;
        return result;
    }

    std::string SanitizeByType(const std::string& text, OcrPropType type)
    {
        std::string sanitized;
        sanitized.reserve(text.size());
        const std::string collapsed = CollapseWhitespace(text);
        for (unsigned char ch : collapsed)
        {
            switch (type)
            {
            case OcrPropType::Number:
                if ((ch >= '0' && ch <= '9') || ch == ':' || ch == '.' || ch == '-')
                    sanitized.push_back(static_cast<char>(ch));
                break;
            case OcrPropType::Text:
                if (ch >= 32 && ch <= 126)
                    sanitized.push_back(static_cast<char>(ch));
                break;
            case OcrPropType::Auto:
                if (ch >= 32 && ch <= 126)
                    sanitized.push_back(static_cast<char>(ch));
                break;
            }
        }

        if (type == OcrPropType::Text)
            sanitized = ToUpperAscii(sanitized);
        return Trim(sanitized);
    }

    bool IsValidForType(const std::string& value, OcrPropType type)
    {
        if (value.empty())
            return false;
        if (type != OcrPropType::Number)
            return true;

        return value.find_first_of("0123456789") != std::string::npos;
    }

    OcrPropResult NormalizeFieldResult(const OcrPropResult& raw)
    {
        OcrPropResult normalized = raw;
        if (!raw.valid)
            return normalized;

        normalized.value = SanitizeByType(raw.value, raw.type);
        normalized.valid = IsValidForType(normalized.value, raw.type);
        if (!normalized.valid)
            normalized.confidence = 0.0;
        return normalized;
    }

    OcrPropResult GetCommittedResult(const PropStabilizer& stabilizer, const OcrPropManifest& prop)
    {
        OcrPropResult result;
        result.name = prop.name;
        result.type = prop.type;
        if (!stabilizer.hasCommitted)
            return result;

        result.valid = true;
        result.value = stabilizer.committedValue;
        result.confidence = stabilizer.committedConfidence;
        return result;
    }

    void ApplyStableProp(PropStabilizer& stabilizer, const OcrPropResult& incoming)
    {
        if (!incoming.valid)
        {
            stabilizer.pendingValue.clear();
            stabilizer.pendingConfidence = 0.0;
            stabilizer.consistentCount = 0;
            if (stabilizer.hasCommitted)
            {
                ++stabilizer.missCount;
                if (stabilizer.missCount >= 2)
                {
                    stabilizer = {};
                }
            }
            return;
        }

        stabilizer.missCount = 0;
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

    OcrElementState BuildCommittedState(const OcrElementManifest& element, const std::vector<PropStabilizer>& stabilizers)
    {
        OcrElementState state;
        state.elementName = element.name;
        state.props.reserve(element.props.size());
        state.detected = false;
        for (size_t i = 0; i < element.props.size(); ++i)
        {
            OcrPropResult result = GetCommittedResult(stabilizers[i], element.props[i]);
            state.props.push_back(result);
            state.detected = state.detected || result.valid;
        }
        return state;
    }

    std::string BuildStateSignature(const OcrElementState& state)
    {
        std::ostringstream oss;
        oss << state.elementName << "|" << state.detected << "|";
        for (const auto& prop : state.props)
            oss << prop.name << "=" << prop.value << ":" << prop.valid << "|";
        return oss.str();
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

    bool BuildReferenceElement(const OcrElementManifest& element, cv::Mat& elementGray)
    {
        const cv::Mat reference = cv::imread(element.folderPath + "\\" + element.referenceImagePath, cv::IMREAD_COLOR);
        if (reference.empty())
            return false;

        const cv::Rect roi = RoiToRect(element.frameRoi, reference.cols, reference.rows, true);
        if (roi.width <= 0 || roi.height <= 0)
            return false;

        cv::Mat cropped = reference(roi).clone();
        cv::cvtColor(cropped, elementGray, cv::COLOR_BGR2GRAY);
        return !elementGray.empty();
    }
    
    void ResetRuntimeForElement(const OcrElementManifest* element)
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.activeElementLoaded = element != nullptr;
        if (element)
        {
            g_runtime.activeElement = *element;
            g_runtime.stabilizers.assign(element->props.size(), PropStabilizer{});
        }
        else
        {
            g_runtime.activeElement = {};
            g_runtime.stabilizers.clear();
        }
        g_runtime.referenceElementGray.release();
        g_runtime.lastState = {};
        g_runtime.lastPublishedSignature.clear();
        g_runtime.hasPendingFrame = false;
        g_runtime.lastPresenceScore = 0.0;
        g_runtime.onAir = false;
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
                OcrElementManifest element;
                cv::Mat referenceElementGray;
                double detectThreshold = 0.70;
                {
                    std::unique_lock<std::mutex> lock(g_runtime.mutex);
                    g_runtime.cv.wait(lock, []() { return g_runtime.stopRequested || g_runtime.hasPendingFrame; });
                    if (g_runtime.stopRequested)
                        break;

                    frame = g_runtime.pendingFrame.clone();
                    element = g_runtime.activeElement;
                    referenceElementGray = g_runtime.referenceElementGray.clone();
                    detectThreshold = g_runtime.detectThreshold;
                    g_runtime.hasPendingFrame = false;
                }

                if (frame.empty() || !element.frameRoi.enabled || referenceElementGray.empty())
                    continue;

                if (!g_runtime.tesseractAvailable)
                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    if (!g_runtime.missingBinaryLogged)
                    {
                        g_runtime.pendingLogs.push_back("ERROR: Tesseract OCR is not installed. Install tesseract.exe to enable OCR.");
                        g_runtime.missingBinaryLogged = true;
                    }
                    g_runtime.lastError = "tesseract.exe not found";
                    continue;
                }

                const cv::Rect frameRect = RoiToRect(element.frameRoi, frame.cols, frame.rows, true);
                if (frameRect.width <= 0 || frameRect.height <= 0)
                    continue;

                const cv::Mat elementRegion = frame(frameRect).clone();
                const double presenceScore = CalculatePresenceScore(elementRegion, referenceElementGray);
                const bool detectedNow = presenceScore >= detectThreshold;

                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    g_runtime.lastPresenceScore = presenceScore;
                    if (detectedNow != g_runtime.onAir)
                    {
                        g_runtime.onAir = detectedNow;
                        g_runtime.pendingLogs.push_back(detectedNow ? "STATE:element onair" : "STATE:element offair");
                        if (!detectedNow)
                            g_runtime.lastPublishedSignature.clear();
                    }
                    if (!detectedNow)
                        g_runtime.lastState.detected = false;
                }

                if (!detectedNow)
                    continue;

                std::vector<OcrPropResult> readResults(element.props.size());
                for (size_t i = 0; i < element.props.size(); ++i)
                {
                    const auto& prop = element.props[i];
                    readResults[i].name = prop.name;
                    readResults[i].type = prop.type;
                    if (!prop.roi.enabled)
                        continue;

                    const cv::Rect propRect = RoiToRect(prop.roi, elementRegion.cols, elementRegion.rows, false);
                    if (propRect.width <= 0 || propRect.height <= 0)
                        continue;

                    cv::Mat fieldImage = elementRegion(propRect).clone();
                    std::string error;
                    const OcrPropResult raw = RunTesseractOnField(fieldImage, prop, g_runtime.tesseractPath, error);
                    if (!error.empty())
                    {
                        std::lock_guard<std::mutex> lock(g_runtime.mutex);
                        g_runtime.lastError = error;
                    }
                    readResults[i] = NormalizeFieldResult(raw);
                    readResults[i].name = prop.name;
                    readResults[i].type = prop.type;
                }

                {
                    std::lock_guard<std::mutex> lock(g_runtime.mutex);
                    if (g_runtime.stabilizers.size() != element.props.size())
                        g_runtime.stabilizers.assign(element.props.size(), PropStabilizer{});
                    for (size_t i = 0; i < readResults.size(); ++i)
                        ApplyStableProp(g_runtime.stabilizers[i], readResults[i]);

                    OcrElementState nextState = BuildCommittedState(element, g_runtime.stabilizers);
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
    return GetExeDir() + "ocr_elements";
}

const OcrElementManifest* Scorebug_FindLayoutByName(const AppState& state, const std::string& name)
{
    for (const auto& element : state.ocrElements)
    {
        if (element.name == name)
            return &element;
    }
    return nullptr;
}

OcrElementManifest* Scorebug_FindLayoutByName(AppState& state, const std::string& name)
{
    for (auto& element : state.ocrElements)
    {
        if (element.name == name)
            return &element;
    }
    return nullptr;
}

bool Scorebug_LoadLayoutCatalog(AppState& state)
{
    state.ocrElements.clear();

    const fs::path root(Scorebug_GetRootDirectory());
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec)
    {
        AddLog(CurrentTimestamp() + " | ERROR: unable to create OCR elements directory: " + root.string());
        return false;
    }

    for (const auto& entry : fs::directory_iterator(root, ec))
    {
        if (ec || !entry.is_directory())
            continue;

        const fs::path jsonPath = entry.path() / "element.json";
        if (!fs::exists(jsonPath))
            continue;

        OcrElementManifest manifest;
        std::string error;
        if (LoadManifest(jsonPath, manifest, error))
            state.ocrElements.push_back(manifest);
        else
            AddLog(CurrentTimestamp() + " | WARNING: skipped OCR element '" + entry.path().filename().string() + "' - " + error);
    }

    std::sort(state.ocrElements.begin(), state.ocrElements.end(),
        [](const OcrElementManifest& lhs, const OcrElementManifest& rhs)
        {
            return _stricmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
        });

    if (state.ocrElements.empty())
        AddLog(CurrentTimestamp() + " | WARNING: no OCR elements found in " + Scorebug_GetRootDirectory());
    return !state.ocrElements.empty();
}

bool Scorebug_SetActiveLayout(AppState& state, const std::string& name)
{
    EnsureWorker();

    if (name.empty())
    {
        state.activeOcrElementName.clear();
        state.lastOcrState = {};
        ResetRuntimeForElement(nullptr);
        return false;
    }

    const OcrElementManifest* manifest = Scorebug_FindLayoutByName(state, name);
    if (!manifest)
    {
        AddLog(CurrentTimestamp() + " | WARNING: requested OCR element not found: " + name);
        state.activeOcrElementName.clear();
        state.lastOcrState = {};
        ResetRuntimeForElement(nullptr);
        return false;
    }

    state.activeOcrElementName = manifest->name;
    state.lastOcrState = {};
    ResetRuntimeForElement(manifest);
    cv::Mat referenceGray;
    if (!BuildReferenceElement(*manifest, referenceGray))
    {
        AddLog(CurrentTimestamp() + " | ERROR: failed to prepare OCR element reference crop for " + state.activeOcrElementName);
        ResetRuntimeForElement(nullptr);
        state.activeOcrElementName.clear();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.referenceElementGray = referenceGray;
        g_runtime.detectThreshold = state.ocrDetectThreshold;
    }

    AddLog(CurrentTimestamp() + " | Active OCR element loaded: " + state.activeOcrElementName);
    return true;
}

bool Scorebug_SaveLayout(AppState& state, const OcrElementManifest& manifest, const std::string& sourceReferenceImagePath, const std::string& originalName, std::string& error)
{
    error.clear();
    OcrElementManifest persisted = manifest;
    persisted.name = Trim(persisted.name);
    for (auto& prop : persisted.props)
        prop.name = Trim(prop.name);

    if (!ValidateElementManifest(state, persisted, originalName, error))
        return false;

    const cv::Mat reference = cv::imread(sourceReferenceImagePath, cv::IMREAD_COLOR);
    if (reference.empty())
    {
        error = "Unable to load reference image.";
        return false;
    }

    const fs::path root(Scorebug_GetRootDirectory());
    fs::create_directories(root);
    const std::string folderName = SanitizeFolderName(persisted.name);
    if (folderName.empty())
    {
        error = "Element name does not produce a valid folder name.";
        return false;
    }

    fs::path currentFolder;
    persisted.createdAt = CurrentIsoTimestamp();
    if (!originalName.empty())
    {
        if (const OcrElementManifest* existing = Scorebug_FindLayoutByName(state, originalName))
        {
            currentFolder = existing->folderPath;
            persisted.createdAt = existing->createdAt.empty() ? persisted.createdAt : existing->createdAt;
        }
    }

    const fs::path targetFolder = root / folderName;
    if (!currentFolder.empty() && currentFolder != targetFolder)
        fs::rename(currentFolder, targetFolder);
    else if (currentFolder.empty() && fs::exists(targetFolder))
    {
        error = "An OCR element folder with this name already exists.";
        return false;
    }

    fs::create_directories(targetFolder);
    const fs::path referencePath = targetFolder / "reference.png";
    if (!cv::imwrite(referencePath.string(), reference))
    {
        error = "Failed to save reference image.";
        return false;
    }

    persisted.folderName = folderName;
    persisted.folderPath = targetFolder.string();
    persisted.referenceImagePath = "reference.png";
    persisted.updatedAt = CurrentIsoTimestamp();

    if (!SaveManifest(persisted, error))
        return false;

    Scorebug_LoadLayoutCatalog(state);
    AddLog(CurrentTimestamp() + " | OCR element saved: " + persisted.name);
    return true;
}

bool Scorebug_DeleteLayout(AppState& state, const std::string& name, std::string& error)
{
    error.clear();
    const OcrElementManifest* manifest = Scorebug_FindLayoutByName(state, name);
    if (!manifest)
    {
        error = "OCR element not found.";
        return false;
    }

    std::error_code ec;
    fs::remove_all(manifest->folderPath, ec);
    if (ec)
    {
        error = "Unable to delete OCR element folder.";
        return false;
    }

    Scorebug_LoadLayoutCatalog(state);
    AddLog(CurrentTimestamp() + " | OCR element deleted: " + name);
    return true;
}

bool Scorebug_SaveProp(AppState& state, const std::string& elementName, const OcrPropManifest& prop, const std::string& originalPropName, std::string& error)
{
    error.clear();
    const OcrElementManifest* existing = Scorebug_FindLayoutByName(state, elementName);
    if (!existing)
    {
        error = "OCR element not found.";
        return false;
    }

    OcrElementManifest updated = *existing;
    OcrPropManifest normalized = prop;
    normalized.name = Trim(normalized.name);
    if (normalized.name.empty())
    {
        error = "Property name is required.";
        return false;
    }
    if (!normalized.roi.enabled)
    {
        error = "Property ROI is required.";
        return false;
    }
    if (HasDuplicatePropName(updated.props, normalized.name, originalPropName))
    {
        error = "Property names must be unique inside the same element.";
        return false;
    }

    bool replaced = false;
    for (auto& existingProp : updated.props)
    {
        if (!originalPropName.empty() && EqualsIgnoreCase(existingProp.name, originalPropName))
        {
            existingProp = normalized;
            replaced = true;
            break;
        }
    }

    if (!replaced)
    {
        if (updated.props.size() >= 12)
        {
            error = "An OCR element supports up to 12 properties.";
            return false;
        }
        updated.props.push_back(normalized);
    }

    return Scorebug_SaveLayout(state, updated, existing->folderPath + "\\" + existing->referenceImagePath, existing->name, error);
}

bool Scorebug_DeleteProp(AppState& state, const std::string& elementName, const std::string& propName, std::string& error)
{
    error.clear();
    const OcrElementManifest* existing = Scorebug_FindLayoutByName(state, elementName);
    if (!existing)
    {
        error = "OCR element not found.";
        return false;
    }

    OcrElementManifest updated = *existing;
    const size_t originalCount = updated.props.size();
    updated.props.erase(std::remove_if(updated.props.begin(), updated.props.end(),
        [&](const OcrPropManifest& prop) { return EqualsIgnoreCase(prop.name, propName); }), updated.props.end());
    if (updated.props.size() == originalCount)
    {
        error = "OCR property not found.";
        return false;
    }

    return Scorebug_SaveLayout(state, updated, existing->folderPath + "\\" + existing->referenceImagePath, existing->name, error);
}

void Scorebug_ProcessFrame(const cv::Mat& bgrFrame, AppState& state)
{
    EnsureWorker();

    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        state.lastOcrState = g_runtime.lastState;
        state.lastOcrPresenceScore = g_runtime.lastPresenceScore;
        state.ocrOnAir = g_runtime.onAir;
        for (const std::string& entry : g_runtime.pendingLogs)
        {
            if (entry == "STATE:element onair")
                AddLog(CurrentTimestamp() + " | OCR element ONAIR");
            else if (entry == "STATE:element offair")
                AddLog(CurrentTimestamp() + " | OCR element turned off");
            else if (entry.rfind("ERROR:", 0) == 0)
                AddLog(CurrentTimestamp() + " | " + entry);
        }
        g_runtime.pendingLogs.clear();
    }

    if (!state.ocrEnabled)
    {
        state.ocrOnAir = false;
        state.lastOcrPresenceScore = 0.0;
        state.lastOcrState.detected = false;
        return;
    }

    if (bgrFrame.empty())
        return;

    if (!Scorebug_FindLayoutByName(state, state.activeOcrElementName))
    {
        state.ocrOnAir = false;
        state.lastOcrPresenceScore = 0.0;
        state.lastOcrState.detected = false;
        return;
    }

    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.detectThreshold = state.ocrDetectThreshold;
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

OcrElementState Scorebug_GetLastState()
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

std::string Scorebug_BuildStateJson(const OcrElementState& state)
{
    std::ostringstream oss;
    oss << "{\"type\":\"ocr_element_state\",\"element\":\"" << state.elementName << "\",\"detected\":"
        << (state.detected ? "true" : "false") << ",\"props\":{";
    for (size_t i = 0; i < state.props.size(); ++i)
    {
        const auto& prop = state.props[i];
        oss << "\"" << prop.name << "\":{"
            << "\"value\":\"" << prop.value << "\","
            << "\"confidence\":" << prop.confidence << ","
            << "\"valid\":" << (prop.valid ? "true" : "false") << ","
            << "\"type\":\"" << OcrPropTypeToStorageString(prop.type) << "\"}";
        if (i + 1 < state.props.size())
            oss << ",";
    }
    oss << "},\"publishedAt\":\"" << state.publishedAtIso << "\"}";
    return oss.str();
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
    g_runtime.activeElement = {};
    g_runtime.activeElementLoaded = false;
    g_runtime.referenceElementGray.release();
    g_runtime.detectThreshold = 0.70;
    g_runtime.lastPresenceScore = 0.0;
    g_runtime.onAir = false;
    g_runtime.lastSubmitTime = Clock::time_point{};
    g_runtime.stabilizers.clear();
    g_runtime.lastState = {};
    g_runtime.lastPublishedSignature.clear();
    g_runtime.pendingLogs.clear();
    g_runtime.tesseractAvailable = false;
    g_runtime.tesseractPath.clear();
    g_runtime.lastError.clear();
    g_runtime.missingBinaryLogged = false;
}
