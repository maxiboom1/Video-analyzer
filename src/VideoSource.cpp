#include "VideoSource.h"
#include "BlackmagicSource.h"
#include "Logger.h"
#include <sstream>

namespace
{
    std::vector<VideoDeviceInfo> EnumerateWebcams()
    {
        std::vector<VideoDeviceInfo> devices;
        for (int i = 0; i <= 10; ++i)
        {
            cv::VideoCapture probe;
            if (!probe.open(i, cv::CAP_DSHOW))
                continue;

            VideoDeviceInfo info;
            info.kind = VideoSourceKind::Webcam;
            info.id = i;
            info.uniqueId = "webcam:" + std::to_string(i);
            info.displayName = "Web Camera " + std::to_string(i);
            devices.push_back(info);
            probe.release();
        }
        return devices;
    }

    const VideoDeviceInfo* FindSelectedDevice(const AppState& state)
    {
        for (const auto& item : state.availableDevices)
        {
            if (item.kind == state.selectedSourceKind && item.id == state.cameraIndex)
                return &item;
        }
        return nullptr;
    }
}

const char* VideoSourceKindToString(VideoSourceKind kind)
{
    return (kind == VideoSourceKind::Blackmagic) ? "Blackmagic" : "Webcam";
}

void VideoSource_Init(VideoSourceContext& ctx)
{
    ctx.blackmagic = BlackmagicSource::Create();
}

void VideoSource_Shutdown(VideoSourceContext& ctx)
{
    if (ctx.blackmagic)
    {
        BlackmagicSource::Destroy(ctx.blackmagic);
        ctx.blackmagic = nullptr;
    }
    if (ctx.webcamCap.isOpened())
        ctx.webcamCap.release();
}

void VideoSource_RefreshDeviceList(AppState& state)
{
    state.availableDevices.clear();

    auto webcams = EnumerateWebcams();
    state.availableDevices.insert(state.availableDevices.end(), webcams.begin(), webcams.end());

    auto blackmagic = BlackmagicSource::EnumerateDevices();
    state.availableDevices.insert(state.availableDevices.end(), blackmagic.begin(), blackmagic.end());

    state.selectedDeviceListIndex = -1;
    for (size_t i = 0; i < state.availableDevices.size(); ++i)
    {
        const auto& item = state.availableDevices[i];
        if (item.kind == state.selectedSourceKind && item.id == state.cameraIndex)
        {
            state.selectedDeviceListIndex = (int)i;
            break;
        }
    }

    if (state.selectedDeviceListIndex < 0 && !state.availableDevices.empty())
    {
        state.selectedDeviceListIndex = 0;
        state.selectedSourceKind = state.availableDevices[0].kind;
        state.cameraIndex = state.availableDevices[0].id;
    }

    state.deviceListDirty = false;

    std::ostringstream oss;
    oss << CurrentTimestamp() << " | Video devices refreshed | webcams=" << webcams.size()
        << " | blackmagic=" << blackmagic.size();
    AddLog(oss.str());
}

void VideoSource_Release(VideoSourceContext& ctx, AppState& state)
{
    if (ctx.webcamCap.isOpened())
        ctx.webcamCap.release();
    BlackmagicSource::Close(ctx.blackmagic);
    state.currentCamera = -1;
    state.frameWidth = 0;
    state.frameHeight = 0;
}

void VideoSource_Update(VideoSourceContext& ctx, AppState& state)
{
    if (state.deviceListDirty || state.availableDevices.empty())
        VideoSource_RefreshDeviceList(state);

    if (state.cameraIndex == state.currentCamera && state.selectedSourceKind == state.currentSourceKind)
        return;

    VideoSource_Release(ctx, state);

    const auto* selected = FindSelectedDevice(state);
    if (!selected)
    {
        AddLog(CurrentTimestamp() + " | ERROR: Selected video device not found in current list");
        return;
    }

    if (selected->kind == VideoSourceKind::Webcam)
    {
        if (!ctx.webcamCap.open(selected->id, cv::CAP_DSHOW))
        {
            AddLog(CurrentTimestamp() + " | ERROR: Failed to open webcam " + std::to_string(selected->id));
            return;
        }

        state.currentCamera = selected->id;
        state.currentSourceKind = selected->kind;
        state.frameWidth = (int)ctx.webcamCap.get(cv::CAP_PROP_FRAME_WIDTH);
        state.frameHeight = (int)ctx.webcamCap.get(cv::CAP_PROP_FRAME_HEIGHT);

        std::ostringstream oss;
        oss << CurrentTimestamp() << " | Webcam opened | index=" << selected->id
            << " | " << state.frameWidth << "x" << state.frameHeight;
        AddLog(oss.str());
        return;
    }

    if (!BlackmagicSource::Open(ctx.blackmagic, *selected, state))
    {
        state.currentCamera = -1;
        state.frameWidth = 0;
        state.frameHeight = 0;
        return;
    }

    state.currentCamera = selected->id;
    state.currentSourceKind = selected->kind;
    state.frameWidth = WORK_W;
    state.frameHeight = WORK_H;
}

cv::Mat VideoSource_GrabFrame(VideoSourceContext& ctx, AppState& state)
{
    if (state.currentSourceKind == VideoSourceKind::Blackmagic)
    {
        cv::Mat frame = BlackmagicSource::GrabFrame(ctx.blackmagic);
        if (!frame.empty())
        {
            state.frameWidth  = frame.cols;
            state.frameHeight = frame.rows;

            // Keep WORK_W/H in sync with actual DeckLink frame size
            // so template matching operates at native resolution
            if (WORK_W != frame.cols || WORK_H != frame.rows)
            {
                WORK_W = frame.cols;
                WORK_H = frame.rows;
                AddLog(CurrentTimestamp() + " | BM: Working resolution updated to " +
                       std::to_string(WORK_W) + "x" + std::to_string(WORK_H));
            }
        }
        return frame;
    }

    cv::Mat frame;
    if (ctx.webcamCap.isOpened())
        ctx.webcamCap >> frame;
    if (!frame.empty())
    {
        state.frameWidth  = frame.cols;
        state.frameHeight = frame.rows;
    }
    return frame;
}
