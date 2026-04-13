#include "BlackmagicSource.h"
#include "Logger.h"
#include "Config.h"
#include <DeckLinkAPI_h.h>
#include <DeckLinkAPI_i.c>

#include <windows.h>
#include <comdef.h>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

struct BlackmagicCaptureContext;

namespace
{
    bool HrOk(HRESULT hr) { return SUCCEEDED(hr); }

    std::string HrHex(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
        return oss.str();
    }

    std::string BstrToUtf8(BSTR text)
    {
        if (!text)
            return {};

        char buffer[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, sizeof(buffer), nullptr, nullptr);
        return buffer;
    }

    bool GetDeckLinkInput(IDeckLink* deckLink, IDeckLinkInput** outInput)
    {
        if (!deckLink || !outInput)
            return false;
        *outInput = nullptr;
        return deckLink->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(outInput)) == S_OK && *outInput;
    }

    bool SupportsFormatDetection(IDeckLink* deckLink)
    {
        if (!deckLink)
            return false;

        IDeckLinkProfileAttributes* attrs = nullptr;
        bool supported = false;
        if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(&attrs)) == S_OK && attrs)
        {
            BOOL value = FALSE;
            if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &value) == S_OK)
                supported = (value != FALSE);
            attrs->Release();
        }
        return supported;
    }

    bool GetSignalLocked(IDeckLink* deckLink)
    {
        if (!deckLink)
            return false;

        IDeckLinkStatus* status = nullptr;
        bool locked = false;
        if (deckLink->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(&status)) == S_OK && status)
        {
            BOOL value = FALSE;
            if (status->GetFlag(bmdDeckLinkStatusVideoInputSignalLocked, &value) == S_OK)
                locked = (value != FALSE);
            status->Release();
        }
        return locked;
    }

    std::string DisplayModeName(IDeckLinkDisplayMode* mode)
    {
        if (!mode)
            return "unknown";
        BSTR name = nullptr;
        if (mode->GetName(&name) != S_OK || !name)
            return "unknown";
        std::string text = BstrToUtf8(name);
        SysFreeString(name);
        return text;
    }

    bool ExtractFrameToBgr(IDeckLinkVideoInputFrame* videoFrame, cv::Mat& outBgr, std::string& reason)
    {
        if (!videoFrame)
        {
            reason = "null frame";
            return false;
        }

        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
        {
            reason = "no input source";
            return false;
        }

        const int width = static_cast<int>(videoFrame->GetWidth());
        const int height = static_cast<int>(videoFrame->GetHeight());
        const int rowBytes = static_cast<int>(videoFrame->GetRowBytes());
        const BMDPixelFormat pixelFormat = videoFrame->GetPixelFormat();

        if (width <= 0 || height <= 0 || rowBytes <= 0)
        {
            reason = "invalid frame geometry";
            return false;
        }

        IDeckLinkVideoBuffer* videoBuffer = nullptr;
        if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void**>(&videoBuffer)) != S_OK || !videoBuffer)
        {
            reason = "IDeckLinkVideoBuffer unavailable";
            return false;
        }

        HRESULT hr = videoBuffer->StartAccess(bmdBufferAccessRead);
        if (!HrOk(hr))
        {
            videoBuffer->Release();
            reason = "StartAccess failed " + HrHex(hr);
            return false;
        }

        void* bytes = nullptr;
        hr = videoBuffer->GetBytes(&bytes);
        if (!HrOk(hr) || !bytes)
        {
            videoBuffer->EndAccess(bmdBufferAccessRead);
            videoBuffer->Release();
            reason = "GetBytes failed " + HrHex(hr);
            return false;
        }

        bool ok = false;
        switch (pixelFormat)
        {
        case bmdFormat8BitBGRA:
        {
            cv::Mat bgra(height, width, CV_8UC4, bytes, rowBytes);
            cv::cvtColor(bgra, outBgr, cv::COLOR_BGRA2BGR);
            ok = true;
            break;
        }
        case bmdFormat8BitARGB:
        {
            cv::Mat argb(height, width, CV_8UC4, bytes, rowBytes);
            cv::cvtColor(argb, outBgr, cv::COLOR_RGBA2BGR);
            ok = true;
            break;
        }
        case bmdFormat8BitYUV:
        {
            cv::Mat uyvy(height, width, CV_8UC2, bytes, rowBytes);
            cv::cvtColor(uyvy, outBgr, cv::COLOR_YUV2BGR_UYVY);
            ok = true;
            break;
        }
        default:
            reason = "unsupported pixel format " + std::to_string(static_cast<int>(pixelFormat));
            break;
        }

        videoBuffer->EndAccess(bmdBufferAccessRead);
        videoBuffer->Release();
        return ok;
    }
}

struct BlackmagicCaptureContext
{
    IDeckLink* deckLink = nullptr;
    IDeckLinkInput* input = nullptr;
    IDeckLinkInputCallback* callback = nullptr;

    std::mutex mtx;
    cv::Mat lastFrame;
    uint64_t frameCount = 0;
    uint64_t droppedFrameLogs = 0;

    BMDDisplayMode activeDisplayMode = bmdModeHD1080i50;
    BMDPixelFormat activePixelFormat = bmdFormat8BitYUV;
    bool formatDetectionEnabled = false;
    bool open = false;
    bool formatLogged = false;
};

class DeckLinkCallback final : public IDeckLinkInputCallback
{
public:
    explicit DeckLinkCallback(BlackmagicCaptureContext* ctx)
        : m_ctx(ctx), m_refCount(1)
    {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDeckLinkInputCallback)
        {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = --m_refCount;
        if (n == 0)
            delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket*) override
    {
        if (!m_ctx || !videoFrame)
            return S_OK;

        std::string reason;
        cv::Mat bgr;
        if (!ExtractFrameToBgr(videoFrame, bgr, reason))
        {
            if (!reason.empty() && m_ctx->droppedFrameLogs < 5)
            {
                AddLog(CurrentTimestamp() + " | BM: frame skipped - " + reason);
                ++m_ctx->droppedFrameLogs;
            }
            return S_OK;
        }

        {
            std::lock_guard<std::mutex> lock(m_ctx->mtx);
            m_ctx->lastFrame = std::move(bgr);
            ++m_ctx->frameCount;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents events,
        IDeckLinkDisplayMode* newMode,
        BMDDetectedVideoInputFormatFlags detectedSignalFlags) override
    {
        if (!m_ctx || !m_ctx->input || !newMode)
            return S_OK;

        const bool modeChanged = (events & bmdVideoInputDisplayModeChanged) != 0;
        const bool formatChanged = (events & bmdVideoInputColorspaceChanged) != 0;
        if (!modeChanged && !formatChanged)
            return S_OK;

        BMDPixelFormat pixelFormat = bmdFormat8BitYUV;
        if ((detectedSignalFlags & bmdDetectedVideoInputRGB444) != 0)
            pixelFormat = bmdFormat8BitBGRA;

        const BMDDisplayMode displayMode = newMode->GetDisplayMode();
        const BMDVideoInputFlags flags = m_ctx->formatDetectionEnabled ? bmdVideoInputEnableFormatDetection : bmdVideoInputFlagDefault;

        m_ctx->input->PauseStreams();
        HRESULT hr = m_ctx->input->EnableVideoInput(displayMode, pixelFormat, flags);
        if (!HrOk(hr))
        {
            AddLog(CurrentTimestamp() + " | BM ERROR: EnableVideoInput(format change) failed hr=" + HrHex(hr));
            return S_OK;
        }

        m_ctx->input->FlushStreams();
        hr = m_ctx->input->StartStreams();
        if (!HrOk(hr))
        {
            AddLog(CurrentTimestamp() + " | BM ERROR: StartStreams(format change) failed hr=" + HrHex(hr));
            return S_OK;
        }

        m_ctx->activeDisplayMode = displayMode;
        m_ctx->activePixelFormat = pixelFormat;
        if (!m_ctx->formatLogged)
        {
            AddLog(CurrentTimestamp() + " | BM: format detected -> " + DisplayModeName(newMode) +
                   " | pixel=" + std::string(pixelFormat == bmdFormat8BitBGRA ? "BGRA" : "8BitYUV"));
            m_ctx->formatLogged = true;
        }
        return S_OK;
    }

private:
    BlackmagicCaptureContext* m_ctx;
    std::atomic<ULONG> m_refCount;
};

static IDeckLinkIterator* CreateIterator()
{
    //FIX: ensure COM initialized for THIS thread
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // It's OK if already initialized
    if (hrCo != S_OK && hrCo != S_FALSE)
    {
        AddLog(CurrentTimestamp() + " | BM ERROR: CoInitializeEx failed hr=" + HrHex(hrCo));
        return nullptr;
    }

    IDeckLinkIterator* iterator = nullptr;

    const HRESULT hr = CoCreateInstance(
        CLSID_CDeckLinkIterator,
        nullptr,
        CLSCTX_ALL,
        IID_IDeckLinkIterator,
        reinterpret_cast<void**>(&iterator));

    if (!HrOk(hr) || !iterator)
    {
        AddLog(CurrentTimestamp() + " | BM ERROR: CoCreateInstance(DeckLinkIterator) failed hr=" + HrHex(hr));
        return nullptr;
    }

    AddLog(CurrentTimestamp() + " | BM: DeckLink iterator created");

    return iterator;
}

namespace BlackmagicSource
{
    void InitCOM()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == S_OK)
            AddLog(CurrentTimestamp() + " | COM initialized (MULTITHREADED)");
        else if (hr == S_FALSE)
            AddLog(CurrentTimestamp() + " | COM already initialized on this thread (OK)");
        else if (hr == RPC_E_CHANGED_MODE)
            AddLog(CurrentTimestamp() + " | COM WARNING: thread already initialized with different apartment model");
        else
            AddLog(CurrentTimestamp() + " | COM ERROR: CoInitializeEx failed hr=" + HrHex(hr));
    }

    bool IsSdkEnabled()
    {
        return true;
    }

    std::vector<VideoDeviceInfo> EnumerateDevices()
    {
        std::vector<VideoDeviceInfo> devices;
        IDeckLinkIterator* iterator = CreateIterator();
        if (!iterator)
            return devices;

        IDeckLink* deckLink = nullptr;
        int index = 0;

        while (iterator->Next(&deckLink) == S_OK)
        {
            IDeckLinkInput* input = nullptr;
            const bool hasInput = GetDeckLinkInput(deckLink, &input);
            if (input)
                input->Release();

            if (hasInput)
            {
                BSTR nameBstr = nullptr;
                if (deckLink->GetDisplayName(&nameBstr) == S_OK && nameBstr)
                {
                    std::string name = BstrToUtf8(nameBstr);
                    SysFreeString(nameBstr);

                    VideoDeviceInfo info;
                    info.kind = VideoSourceKind::Blackmagic;
                    info.id = index;
                    info.uniqueId = name + "#" + std::to_string(index);
                    info.displayName = name;
                    devices.push_back(info);

                    AddLog(CurrentTimestamp() + " | BM ENUM: idx=" + std::to_string(index) +
                           " | name=" + name +
                           " | uid=" + info.uniqueId);
                }
            }

            deckLink->Release();
            index++;
        }

        iterator->Release();
        return devices;
    }

    BlackmagicCaptureContext* Create()
    {
        return new BlackmagicCaptureContext();
    }

    void Destroy(BlackmagicCaptureContext* ctx)
    {
        delete ctx;
    }

    bool Open(BlackmagicCaptureContext* ctx, const VideoDeviceInfo& device, AppState& state)
    {
        if (!ctx)
            return false;

        Close(ctx);

        AddLog(CurrentTimestamp() + " | BM OPEN: requested uid=" + device.uniqueId +
               " | name=" + device.displayName +
               " | id=" + std::to_string(device.id));

        IDeckLinkIterator* iterator = CreateIterator();
        if (!iterator)
            return false;

        IDeckLink* deckLink = nullptr;
        int index = 0;
        IDeckLink* found = nullptr;
        IDeckLinkInput* foundInput = nullptr;

        while (iterator->Next(&deckLink) == S_OK)
        {
            BSTR nameBstr = nullptr;
            std::string name;
            if (deckLink->GetDisplayName(&nameBstr) == S_OK && nameBstr)
            {
                name = BstrToUtf8(nameBstr);
                SysFreeString(nameBstr);
            }

            const std::string uid = name + "#" + std::to_string(index);

            AddLog(CurrentTimestamp() + " | BM OPEN: probe idx=" + std::to_string(index) +
                   " | uid=" + uid);

            if (uid == device.uniqueId)
            {
                const HRESULT hrQI = deckLink->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(&foundInput));
                if (hrQI != S_OK || !foundInput)
                {
                    AddLog(CurrentTimestamp() + " | BM ERROR: QueryInterface(IDeckLinkInput) failed hr=" + HrHex(hrQI));
                    deckLink->Release();
                    deckLink = nullptr;
                    break;
                }

                found = deckLink;
                deckLink = nullptr;
                break;
            }

            deckLink->Release();
            deckLink = nullptr;
            index++;
        }

        iterator->Release();

        if (!found || !foundInput)
        {
            if (found)
                found->Release();
            if (foundInput)
                foundInput->Release();
            AddLog(CurrentTimestamp() + " | BM ERROR: selected device could not be opened as input | requested=" + device.uniqueId);
            return false;
        }

        ctx->deckLink = found;
        ctx->input = foundInput;

        ctx->formatDetectionEnabled = SupportsFormatDetection(ctx->deckLink);
        const BMDVideoInputFlags flags = ctx->formatDetectionEnabled ? bmdVideoInputEnableFormatDetection : bmdVideoInputFlagDefault;
        const BMDDisplayMode initialMode = bmdModeHD1080i50;
        const BMDPixelFormat initialPixel = bmdFormat8BitYUV;

        ctx->formatLogged = false;
        ctx->callback = new DeckLinkCallback(ctx);
        HRESULT hr = ctx->input->SetCallback(ctx->callback);
        if (!HrOk(hr))
        {
            AddLog(CurrentTimestamp() + " | BM ERROR: SetCallback failed hr=" + HrHex(hr));
            Close(ctx);
            return false;
        }

        hr = ctx->input->EnableVideoInput(initialMode, initialPixel, flags);
        if (!HrOk(hr))
        {
            AddLog(CurrentTimestamp() + " | BM ERROR: EnableVideoInput failed hr=" + HrHex(hr));
            Close(ctx);
            return false;
        }

        hr = ctx->input->StartStreams();
        if (!HrOk(hr))
        {
            AddLog(CurrentTimestamp() + " | BM ERROR: StartStreams failed hr=" + HrHex(hr));
            Close(ctx);
            return false;
        }

        ctx->activeDisplayMode = initialMode;
        ctx->activePixelFormat = initialPixel;
        ctx->open = true;
        state.frameWidth = WORK_W;
        state.frameHeight = WORK_H;

        AddLog(CurrentTimestamp() + " | BM: capture opened | device=" + device.displayName +
               " | formatDetection=" + std::string(ctx->formatDetectionEnabled ? "on" : "off") +
               " | signalLocked=" + std::string(GetSignalLocked(ctx->deckLink) ? "yes" : "no"));
        return true;
    }

    cv::Mat GrabFrame(BlackmagicCaptureContext* ctx)
    {
        if (!ctx)
            return {};
        std::lock_guard<std::mutex> lock(ctx->mtx);
        return ctx->lastFrame.empty() ? cv::Mat() : ctx->lastFrame.clone();
    }

    void Close(BlackmagicCaptureContext* ctx)
    {
        if (!ctx)
            return;

        if (ctx->input)
        {
            ctx->input->StopStreams();
            ctx->input->FlushStreams();
            ctx->input->SetCallback(nullptr);
            ctx->input->DisableVideoInput();
            ctx->input->Release();
            ctx->input = nullptr;
        }

        if (ctx->callback)
        {
            ctx->callback->Release();
            ctx->callback = nullptr;
        }

        if (ctx->deckLink)
        {
            ctx->deckLink->Release();
            ctx->deckLink = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(ctx->mtx);
            ctx->lastFrame.release();
            ctx->frameCount = 0;
            ctx->droppedFrameLogs = 0;
        }

        ctx->open = false;
    }
}
