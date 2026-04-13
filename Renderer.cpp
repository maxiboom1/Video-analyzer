#include "Renderer.h"

#include <string>

namespace
{
    cv::Mat s_previewFrame;

    RECT FitRectPreservingAspect(const RECT& bounds, int srcW, int srcH)
    {
        RECT dest = bounds;
        if (srcW <= 0 || srcH <= 0)
            return dest;

        const int dstW = bounds.right - bounds.left;
        const int dstH = bounds.bottom - bounds.top;
        if (dstW <= 0 || dstH <= 0)
            return dest;

        const double srcAspect = static_cast<double>(srcW) / static_cast<double>(srcH);
        const double dstAspect = static_cast<double>(dstW) / static_cast<double>(dstH);

        int drawW = dstW;
        int drawH = dstH;

        if (srcAspect > dstAspect)
            drawH = static_cast<int>(drawW / srcAspect);
        else
            drawW = static_cast<int>(drawH * srcAspect);

        dest.left = bounds.left + (dstW - drawW) / 2;
        dest.top = bounds.top + (dstH - drawH) / 2;
        dest.right = dest.left + drawW;
        dest.bottom = dest.top + drawH;
        return dest;
    }

    void DrawCenteredText(HDC hdc, const RECT& rect, const char* text)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(210, 210, 210));
        DrawTextA(hdc, text, -1, const_cast<RECT*>(&rect),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void Renderer_SetPreviewFrame(const cv::Mat& bgrFrame)
{
    if (bgrFrame.empty())
    {
        s_previewFrame.release();
        return;
    }

    s_previewFrame = bgrFrame.clone();
}

void Renderer_ClearPreview()
{
    s_previewFrame.release();
}

void Renderer_PaintPreview(HDC hdc, const RECT& clientRect, bool previewEnabled)
{
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0)
        return;

    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memDc, memBitmap);

    RECT localRect{ 0, 0, width, height };
    HBRUSH bg = CreateSolidBrush(RGB(13, 18, 24));
    FillRect(memDc, &localRect, bg);
    DeleteObject(bg);

    if (!previewEnabled)
    {
        DrawCenteredText(memDc, localRect, "Preview disabled.");
    }
    else if (s_previewFrame.empty())
    {
        DrawCenteredText(memDc, localRect, "Waiting for frames.");
    }
    else
    {
        RECT drawRect = FitRectPreservingAspect(localRect, s_previewFrame.cols, s_previewFrame.rows);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = s_previewFrame.cols;
        bmi.bmiHeader.biHeight = -s_previewFrame.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(memDc, HALFTONE);
        StretchDIBits(
            memDc,
            drawRect.left,
            drawRect.top,
            drawRect.right - drawRect.left,
            drawRect.bottom - drawRect.top,
            0,
            0,
            s_previewFrame.cols,
            s_previewFrame.rows,
            s_previewFrame.data,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    BitBlt(hdc, clientRect.left, clientRect.top, width, height, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDc);
}
