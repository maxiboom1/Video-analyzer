#include "Renderer.h"

#include <string>

namespace
{
    cv::Mat s_previewFrame;
    cv::Mat s_cueFrame;

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

    void PaintMatFit(HDC hdc, const RECT& bounds, const cv::Mat& frame)
    {
        if (frame.empty())
            return;

        cv::Mat bgraFrame;
        switch (frame.channels())
        {
        case 4:
            bgraFrame = frame;
            break;
        case 3:
            cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
            break;
        case 1:
            cv::cvtColor(frame, bgraFrame, cv::COLOR_GRAY2BGRA);
            break;
        default:
            return;
        }

        RECT drawRect = FitRectPreservingAspect(bounds, bgraFrame.cols, bgraFrame.rows);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgraFrame.cols;
        bmi.bmiHeader.biHeight = -bgraFrame.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdc, HALFTONE);
        StretchDIBits(
            hdc,
            drawRect.left,
            drawRect.top,
            drawRect.right - drawRect.left,
            drawRect.bottom - drawRect.top,
            0,
            0,
            bgraFrame.cols,
            bgraFrame.rows,
            bgraFrame.data,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);
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
    s_cueFrame.release();
}

void Renderer_SetCueFrame(const cv::Mat& frame)
{
    if (frame.empty())
    {
        s_cueFrame.release();
        return;
    }

    s_cueFrame = frame.clone();
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
        PaintMatFit(memDc, localRect, s_previewFrame);
    }

    BitBlt(hdc, clientRect.left, clientRect.top, width, height, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDc);
}

void Renderer_PaintCuePreview(HDC hdc, const RECT& clientRect, bool hasTemplate)
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

    if (!hasTemplate)
    {
        DrawCenteredText(memDc, localRect, "No template.");
    }
    else if (s_cueFrame.empty())
    {
        DrawCenteredText(memDc, localRect, "No cue image.");
    }
    else
    {
        PaintMatFit(memDc, localRect, s_cueFrame);
    }

    BitBlt(hdc, clientRect.left, clientRect.top, width, height, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDc);
}
