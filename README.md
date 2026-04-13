# Video Analyzer v0.9.21

## Overview

Video Analyzer is a native Win32 desktop application for broadcast operators.
It monitors a selected video source, runs full-frame template matching against
`wiper_in.png` and `wiper_out.png`, and sends Viz engine commands when the cue
state changes.

Version `0.9.21` replaces the previous ImGui/DX11 shell with a native Win32 UI
while keeping the existing video, detection, config, logging, and Viz logic.

## Main Window

The operator-facing main window is intentionally minimal:

- Top bar with the app header and a `Settings` button
- Control strip with the video device dropdown, `Refresh`, renderer status, and `Next Cue`
- Preview section with a `Preview` toggle and the live preview area
- Log section with `Auto-scroll`, `Clear`, and the log output

## Settings Window

The settings window is a native tabbed Win32 window.

- `Detection`
  - `Detection enabled`
  - `Detect Threshold`
  - `Reset Threshold`
  - `Cooldown (ms)`
- `Engine Connection`
  - Viz IP
  - Viz port
  - `GFX ON` command
  - `GFX OFF` command
  - current connection status
  - `Save Config`
- `Templates`
  - reserved placeholder tab for future template management

## Runtime Architecture

```text
Native Win32 UI
    -> VideoSource
       -> Webcam (OpenCV / DirectShow)
       -> BlackmagicSource (DeckLink SDK 16+)
    -> Detection / cue state machine
    -> Viz TCP output
```

Notes:

- webcam enumeration probes indices `0..10`
- Blackmagic input starts in `1080i50` and can switch when format detection reports a signal
- templates are loaded from the EXE directory and resized to the active working resolution
- preview rendering is native GDI painting inside the Win32 preview pane

## Build Requirements

- Windows 10/11 x64
- Visual Studio 2022 toolset (`v143`)
- OpenCV 4.12.x installed at the paths referenced by the `.vcxproj`
- Blackmagic Desktop Video 16+ for DeckLink capture

The project currently builds as a Win32 desktop application in `Debug|x64` and
`Release|x64`.

## Runtime Files

- `config.ini`
  - Viz IP / port / commands
  - detection thresholds and cooldown
  - selected source type and device id
- `wiper_in.png`
- `wiper_out.png`

All three are loaded from the executable directory at runtime.

## Known Limitations

- webcam names are still generic (`Web Camera N`)
- webcam enumeration is still probe-based rather than using friendly device names
- Blackmagic display-mode selection is not exposed in the UI yet
- no deinterlace stage is implemented for interlaced Blackmagic input
- the Templates tab is still a placeholder in `0.9.21`

## Verification Notes

The updated native Win32 build links successfully and produces
`x64\\Debug\\video-analyzer.exe` in this workspace. In this environment, MSBuild
still reports a final cleanup error on the generated `unsuccessfulbuild` tlog
file, but compilation and linking complete before that cleanup step fails.
