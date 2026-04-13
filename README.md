# Video Analyzer v1.0.2

## Overview

Video Analyzer is a native Win32 desktop application for broadcast operators.
It monitors a selected video source, runs template-based image detection against
an active `IN` / `OUT` template pair, and sends Viz engine commands when the cue
state changes.

Version `1.0.2` includes the ROI preview rendering fix on top of the native
Win32 operator UI, template catalog, ROI-based template designer, and
centralized app versioning.

## Main Window

The operator-facing main window is intentionally minimal:

- top bar with the app header and a `Settings` button
- control strip with:
  - video device dropdown
  - `Refresh`
  - active template dropdown
  - renderer connection status
  - `Next Cue`
- preview section with a `Preview` toggle and the live preview area
- log section with `Auto-scroll`, `Clear`, and the log output

Changing the active template from the main window switches live detection
immediately and persists the selection to `config.ini`.

## Settings Window

The settings window is a native tabbed Win32 window.

- `Detection`
  - `Detection enabled`
  - `Detect Threshold`
  - `Reset Threshold`
  - `Cooldown (ms)`
- `Engine`
  - Viz IP
  - Viz port
  - `GFX ON` command
  - `GFX OFF` command
  - current connection status
  - `Save Config`
- `Templates`
  - template list
  - `New`
  - `Edit`
  - `Delete`
  - `Set Active`
  - selected template details

## Template System

Templates are stored on disk under:

```text
templates\<templateName>\
    template.json
    in.png
    out.png
```

Each template contains:

- template name
- `IN` image
- `OUT` image
- independent normalized ROI for `IN`
- independent normalized ROI for `OUT`

The runtime pipeline loads the active template, resizes the original images to
the current working resolution, applies the matching ROI, and matches only the
cropped region per frame. Full-frame behavior is used when the ROI is cleared.

## Template Editor

The template editor is a native Win32 dialog used for both create and edit.

Flow:

1. Enter the template name
2. Select the `IN` image
3. Optionally define the `IN` ROI
4. Select the `OUT` image
5. Optionally define the `OUT` ROI
6. Save

The ROI editor uses drag selection on a scaled image preview and stores
normalized coordinates, so ROI definitions remain valid across source image
sizes and runtime resize operations.

## Runtime Architecture

```text
Native Win32 UI
    -> VideoSource
       -> Webcam (OpenCV / DirectShow)
       -> BlackmagicSource (DeckLink SDK 16+)
    -> Template catalog / ROI designer
    -> Detection / cue state machine
    -> Viz TCP output
```

Notes:

- webcam enumeration probes indices `0..10`
- Blackmagic input starts in `1080i50` and can switch when format detection reports a signal
- template assets are loaded from the EXE directory under `templates\`
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
  - active template name
- `templates\<templateName>\template.json`
- `templates\<templateName>\in.png`
- `templates\<templateName>\out.png`

All runtime files are loaded from the executable directory.

## Changelog

### 1.0.2

- fixed ROI editor image rendering for informative or high-detail frames by switching the preview paint path to a 32-bit BGRA DIB
- kept the native template workflow, operator UI, and centralized versioning introduced in `1.0.1`

### 1.0.1

- added the native template catalog with persistent active-template selection
- replaced fixed `wiper_in.png` / `wiper_out.png` loading with folder-based templates
- added template create, edit, delete, and set-active flows in `Settings -> Templates`
- added native ROI designer dialogs for `IN` and `OUT` images
- updated detection to use ROI-aware cropped runtime template assets
- added the main-window active template chooser for live switching
- fixed template-editor captioning, ROI selection flicker, and template-combo selection jumping
- centralized app version strings so the window titles, header, and log banner all use one source

## Known Limitations

- webcam names are still generic (`Web Camera N`)
- webcam enumeration is still probe-based rather than using friendly device names
- Blackmagic display-mode selection is not exposed in the UI yet
- no deinterlace stage is implemented for interlaced Blackmagic input
- template import/export and package management are not implemented yet

## Verification Notes

The native Win32 build links successfully and produces
`x64\\Debug\\video-analyzer.exe` in this workspace. In this environment, MSBuild
may still report a final cleanup error on the generated `unsuccessfulbuild`
tlog file, but compilation and linking complete before that cleanup step.
