#pragma once
#include "AppState.h"
#include <windows.h>
#include <opencv2/opencv.hpp>

bool UI_Create(HWND hwnd, HINSTANCE instance, AppState& state);
void UI_Destroy();
void UI_OnSize(int width, int height);
bool UI_HandleMainCommand(WPARAM wParam, LPARAM lParam, AppState& state);
HBRUSH UI_HandleCtlColor(HDC hdc, HWND control);
bool UI_HandleDrawItem(const DRAWITEMSTRUCT& drawItem, const AppState& state);
void UI_PaintMain(HDC hdc);
void UI_SyncState(const AppState& state);
void UI_UpdatePreview(const cv::Mat& bgrFrame);
void UI_ClearPreview();
