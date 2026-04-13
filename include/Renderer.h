#pragma once
#include <windows.h>
#include <opencv2/opencv.hpp>

// Native Win32 preview renderer.
// Stores the latest BGR frame and paints it into a child window using GDI.
void Renderer_SetPreviewFrame(const cv::Mat& bgrFrame);
void Renderer_SetCueFrame(const cv::Mat& frame);
void Renderer_ClearPreview();
void Renderer_PaintPreview(HDC hdc, const RECT& clientRect, bool previewEnabled);
void Renderer_PaintCuePreview(HDC hdc, const RECT& clientRect, bool hasTemplate);
