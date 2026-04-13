#pragma once
#include "AppState.h"
#include <opencv2/opencv.hpp>
#include <vector>

struct BlackmagicCaptureContext;

namespace BlackmagicSource
{
    // Must be called once at app startup (calls CoInitializeEx)
    void InitCOM();

    bool IsSdkEnabled();
    std::vector<VideoDeviceInfo> EnumerateDevices();

    BlackmagicCaptureContext* Create();
    void Destroy(BlackmagicCaptureContext* ctx);

    // Opens the device and starts streaming.
    // Uses format detection - VideoInputFormatChanged will restart capture
    // with the actual detected format automatically.
    bool Open(BlackmagicCaptureContext* ctx, const VideoDeviceInfo& device, AppState& state);

    // Returns the latest captured frame as a BGR cv::Mat.
    // Returns empty Mat if no frame has arrived yet.
    cv::Mat GrabFrame(BlackmagicCaptureContext* ctx);

    void Close(BlackmagicCaptureContext* ctx);
}
