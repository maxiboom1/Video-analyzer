#pragma once
#include "AppState.h"
#include <opencv2/opencv.hpp>

struct BlackmagicCaptureContext;

struct VideoSourceContext
{
    cv::VideoCapture webcamCap;
    BlackmagicCaptureContext* blackmagic = nullptr;
    std::chrono::steady_clock::time_point nextAttempt{};
    VideoSourceKind retrySourceKind = VideoSourceKind::Webcam;
    int retryDeviceId = -1;
};

void VideoSource_Init(VideoSourceContext& ctx);
void VideoSource_Shutdown(VideoSourceContext& ctx);

void VideoSource_RefreshDeviceList(AppState& state);
void VideoSource_Update(VideoSourceContext& ctx, AppState& state,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
cv::Mat VideoSource_GrabFrame(VideoSourceContext& ctx, AppState& state);
void VideoSource_Release(VideoSourceContext& ctx, AppState& state);
const char* VideoSourceKindToString(VideoSourceKind kind);
