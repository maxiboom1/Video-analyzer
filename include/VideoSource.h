#pragma once
#include "AppState.h"
#include <opencv2/opencv.hpp>

struct BlackmagicCaptureContext;

struct VideoSourceContext
{
    cv::VideoCapture webcamCap;
    BlackmagicCaptureContext* blackmagic = nullptr;
};

void VideoSource_Init(VideoSourceContext& ctx);
void VideoSource_Shutdown(VideoSourceContext& ctx);

void VideoSource_RefreshDeviceList(AppState& state);
void VideoSource_Update(VideoSourceContext& ctx, AppState& state);
cv::Mat VideoSource_GrabFrame(VideoSourceContext& ctx, AppState& state);
void VideoSource_Release(VideoSourceContext& ctx, AppState& state);
const char* VideoSourceKindToString(VideoSourceKind kind);
