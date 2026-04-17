# Video Analyzer v2.1.0

## Overview

Video Analyzer is a native Win32 desktop application for broadcast operators.
It monitors a selected video source, runs template-based image detection against
an active `IN` / `OUT` template pair, and sends Viz engine commands when the cue
state changes.

Version `2.1.0` replaces the fixed scorebug-field OCR model with a universal
OCR element builder. Operators now define one `GFX element` ROI and up to 12
named OCR props inside it, then apply the selected element to live runtime OCR
only when `Save Config` is pressed.

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
- `OCR`
  - `Enable OCR`
  - detect-threshold slider and current value in the top row
  - separator line under the control band
  - `GFX Element List`
  - `GFX Properties`
  - left-side `New` / `Delete` buttons for OCR elements
  - right-side `New` / `Delete` buttons for OCR props
  - single-click selects an element for editing
  - double-click opens `GFX Element Editor`
  - double-click on a property opens `GFX Property Editor`
- `Save Config`
  - persists OCR enabled state
  - detect threshold slider and current value
  - persists the currently selected OCR element as the active runtime OCR source
  - does not re-save element/prop manifests

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

## OCR Element Builder

OCR elements are stored on disk under:

```text
ocr_elements\<elementName>\
    element.json
    reference.png
```

Each OCR element contains:

- element name
- one reference image
- one normalized element ROI
- up to 12 named OCR props
- one normalized ROI per prop, relative to the element ROI canvas
- a prop type for each prop:
  - `Number`
  - `Text`
  - `Auto`

The runtime OCR pipeline:

- compares the live frame ROI against the saved reference crop to detect element presence
- crops each configured prop ROI from inside the detected element ROI
- preprocesses each crop with OpenCV
- runs `tesseract.exe` per prop when available
- keeps partial results when some props are unreadable
- stabilizes prop values before publishing
- updates one live OCR status line under the preview area while keeping
  OCR on-air / off-air transitions in the event log

Main OCR status line behavior:

- OCR disabled:
  - `[OCR Disabled]`
- OCR enabled but no detected element:
  - `[OCR] Element not detected`
- OCR enabled and detected:
  - `[OCR] <ElementName> || <PropName>: <Value> ...`

Editor behavior:

- `GFX Element Editor`
  - sets the element name
  - browses for the reference image
  - selects the full element ROI on the full reference image
  - saves `element.json` immediately
- `GFX Property Editor`
  - sets the property name
  - sets the property type
  - selects the property ROI on the cropped element ROI canvas
  - saves the property change immediately into `element.json`

Apply behavior:

- selecting an element in the OCR tab updates only the editor-side selection
- runtime OCR continues using the previously applied active element
- clicking `Save Config` applies the selected OCR element to live runtime OCR

## Runtime Architecture

```text
Native Win32 UI
    -> VideoSource
       -> Webcam (OpenCV / DirectShow)
       -> BlackmagicSource (DeckLink SDK 16+)
    -> Template catalog / ROI designer
    -> OCR element catalog / ROI designer / OCR worker
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
- Tesseract OCR installed and reachable as `tesseract.exe` for OCR elements

The project currently builds as a Win32 desktop application in `Debug|x64` and
`Release|x64`.

## Runtime Files

- `config.ini`
  - Viz IP / port / commands
  - detection thresholds and cooldown
  - selected source type and device id
  - active template name
  - active OCR element name
  - OCR enabled flag
  - OCR detect threshold
- `templates\<templateName>\template.json`
- `templates\<templateName>\in.png`
- `templates\<templateName>\out.png`
- `ocr_elements\<elementName>\element.json`
- `ocr_elements\<elementName>\reference.png`

All runtime files are loaded from the executable directory.

## Changelog

### 2.1.0

- replaced the fixed scorebug OCR layout with a universal OCR element builder
- added `ocr_elements\<elementName>\element.json` as the new OCR storage contract
- added up to 12 named OCR props per element with `Number`, `Text`, and `Auto` types
- added `GFX Element Editor` and `GFX Property Editor` native Win32 dialogs
- added explicit headings and visible labels inside the OCR editor and ROI selector windows
- changed property ROI editing to use the cropped element ROI canvas instead of the full frame
- changed the settings tab label from `Scorebug` to `OCR`
- rebuilt the OCR settings tab into side-by-side `GFX Element List` and `GFX Properties` panes
- reduced OCR tab selection jitter by avoiding unnecessary per-frame list repopulation
- replaced the OCR tab separator with a thinner custom-painted divider
- changed OCR runtime apply behavior so element and prop edits save immediately, but live OCR switches only on `Save Config`
- changed live OCR output and JSON generation to use generic prop names instead of hard-coded team/clock fields
- improved OCR preprocessing by trying multiple thresholded image variants and type-specific Tesseract settings before choosing the best candidate
- bumped the app version to `2.1.0` through the centralized version source

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
