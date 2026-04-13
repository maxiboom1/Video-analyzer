# Video Analyzer v2.0.4

## Overview

Video Analyzer is a native Win32 desktop application for broadcast operators.
It monitors a selected video source, runs template-based image detection against
an active `IN` / `OUT` template pair, and sends Viz engine commands when the cue
state changes.

Version `2.0.4` moves live OCR output out of the event log and into a single
operator-facing status line under the preview window. It also polishes the
`Templates` and `Scorebug` settings tabs so preset lists, info blocks, and CRUD
actions follow the same bottom-button layout.

## Main Window

The operator-facing main window is intentionally minimal:

- top bar with the app header and a `Settings` button
- control strip with:
  - video device dropdown
  - active template dropdown
  - square `Next Cue` button
  - next-cue target image preview based on the active template ROI crop
- preview section with a `Preview` toggle and the live preview area
- live OCR status line directly under the preview area
- log section with `Auto-scroll`, `Clear`, and the log output
- footer connection line with the current Viz connection status

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
  - `Detection Presets` label above the preset list
  - template list
  - selected template details
  - `New`
  - `Edit`
  - `Delete`
  - `Set Active`
- `Scorebug`
  - `Enable OCR`
  - `OCR Presets` label above the layout list
  - detect threshold slider and current value
  - scorebug layout list
  - live OCR status details
  - `New`
  - `Edit`
  - `Delete`
  - `Set Active`
  - Tesseract status and live detection diagnostics

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

## Scorebug OCR

Scorebug layouts are stored on disk under:

```text
scorebugs\<layoutName>\
    layout.json
    reference.png
```

Each scorebug layout contains:

- layout name
- one reference image
- one normalized scorebug frame ROI
- normalized field ROI for:
  - team A label
  - team A score
  - team B label
  - team B score
  - period
  - game clock
  - shot clock
- field type metadata, OCR whitelist, and preprocessing hint

The runtime OCR pipeline:

- crops the configured scorebug frame from the live video
- crops each field ROI inside that frame
- preprocesses each crop with OpenCV
- runs `tesseract.exe` per field when available
- stabilizes values before publishing
- updates one live OCR status line under the preview area while keeping
  scoreboard on-air / off-air transitions in the event log

Current POC scope:

- one manually calibrated scorebug layout family at a time
- OCR output is shown as one live in-app status line
- no renderer override is implemented yet

## Runtime Architecture

```text
Native Win32 UI
    -> VideoSource
       -> Webcam (OpenCV / DirectShow)
       -> BlackmagicSource (DeckLink SDK 16+)
    -> Template catalog / ROI designer
    -> Scorebug catalog / ROI designer / OCR worker
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
- Tesseract OCR installed and reachable as `tesseract.exe` for scorebug OCR

The project currently builds as a Win32 desktop application in `Debug|x64` and
`Release|x64`.

## Runtime Files

- `config.ini`
  - Viz IP / port / commands
  - detection thresholds and cooldown
  - selected source type and device id
  - active template name
  - active scorebug layout name
  - scorebug OCR enabled flag
- `templates\<templateName>\template.json`
- `templates\<templateName>\in.png`
- `templates\<templateName>\out.png`
- `scorebugs\<layoutName>\layout.json`
- `scorebugs\<layoutName>\reference.png`

All runtime files are loaded from the executable directory.

## Changelog

### 2.0.4

- moved OCR field output from the event log into a single live status line under the preview window
- kept only scoreboard transition and error messages in the event log
- updated the Templates tab with a `Detection Presets` label, top-right info block, and bottom CRUD action row
- updated the Scorebug tab with an `OCR Presets` label, top-row detect-threshold control, and simplified diagnostics block
- bumped the app version to `2.0.4` through the centralized version source

### 2.0.3

- changed scorebug OCR publish behavior to emit once on `scoreboard ONAIR`, then only when OCR values change
- removed scorebug heartbeat-style publish spam
- changed the scorebug console/log output from raw JSON to a readable `OCR Data: ...` format
- kept separate `scoreboard ONAIR` and `Scoreboard turned off` transition logs

### 2.0.2

- added scorebug presence detection against the configured scoreboard ROI before OCR publishing
- added a separate scorebug detect-threshold control in `Settings -> Scorebug`
- gated scorebug JSON log output so it is emitted only while the scoreboard is considered on-air
- added `scoreboard ONAIR` and `Scoreboard turned off` transition logs
- persisted the scorebug detect threshold in `config.ini`

### 2.0.1

- bumped the app version to `2.0.1` through the centralized version source
- clarified in the scorebug editor that field ROI selection happens inside the cropped scorebug region
- relaxed the OCR pipeline for scorebug text fields by removing over-restrictive text whitelisting
- simplified scorebug preprocessing and improved period parsing so OCR reads like `2` normalize to `2nd`

### 2.0.0

- added the native `Settings -> Scorebug` layout catalog with create, edit, delete, and set-active flows
- added manual scorebug ROI calibration for the frame and individual scoreboard fields
- added an asynchronous OCR worker that uses `tesseract.exe` when present
- added compact scorebug JSON publishing to the in-app log with change detection and heartbeat
- added persistent scorebug config state in `config.ini`

### 1.0.2

- fixed ROI editor image rendering for informative or high-detail frames by switching the preview paint path to a 32-bit BGRA DIB
- kept the native template workflow, operator UI, and centralized versioning introduced in `1.0.1`

### 1.0.3

- removed the manual video-device `Refresh` button from the main operator window
- moved the Viz connection status to the bottom-left footer under the log section
- added a next-cue target preview panel that shows the ROI-cropped active template image
- changed the next-cue control from a full-width strip to a square operator button plus image preview layout
- removed the `Live Preview` and `Event Log` labels from the preview and log cards

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
