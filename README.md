# Video Analyzer v1.0.6

## Overview

Video Analyzer is a native Win32 desktop application for broadcast operators.
It monitors a selected video source, runs template-based image detection against
an active `IN` / `OUT` template pair, and sends Viz engine commands when the cue
state changes.

Version `1.0.6` is the current supported baseline on `main`. All future development
continues from this version. It includes startup authentication, Blackmagic capture,
template detection, manual event commands, bounded background renderer sending,
thread-safe logs, capture retry delays, and Detection setting tooltips.

## Startup Password

The password form is the first application screen. It accepts either of the two
hardcoded passwords defined in `src/StartupAuth.cpp`, with exact, case-sensitive
matching. The input is masked. An incorrect password clears the input and shows
an error so the operator can retry; Cancel, Escape, or closing the form exits.

The main window, configuration, template catalog, video devices, and frame timer
are initialized only after successful authentication. Detection and Viz command
sending are unavailable before login.

## Main Window

The operator-facing main window is intentionally minimal:

- top bar with the app header and a `Settings` button
- control strip with:
  - video device dropdown
  - active template dropdown
  - square `Next Cue` button
  - same-size `SEND EVENT COMMAND` button immediately to its right
  - narrower next-cue target image preview based on the active template ROI crop
- preview section with a `Preview` toggle and the live preview area
- log section with `Auto-scroll`, `Clear`, and the log output
- footer renderer line with the last Viz send result

Changing the active template from the main window switches live detection
immediately and persists the selection to `config.ini`.

`Next Cue` changes only the expected cue. `SEND EVENT COMMAND` sends the command
for the cue currently displayed, then advances the cue and preview after a
successful TCP send:

| Current cue | Command | Next cue after sending |
| --- | --- | --- |
| WIPER IN | Configured GFX ON command | WIPER OUT |
| WIPER OUT | Configured GFX OFF command | WIPER IN |

A failed manual send leaves the cue unchanged and reports the error in the log and
renderer status. A successful manual send starts the configured detection
cooldown, as an automatic detection event does. Manual sending also works when
detection is disabled or no video device is connected.

Sending runs on one background worker with a two-second total connection/send
deadline. While a send is pending, both cue buttons are disabled and new automatic
events are paused; capture, preview, and the UI continue running. Commands are not
queued or automatically replayed. Automatic events still advance their cue on
detection, even if the send fails. The worker starts only after successful login
and is cancelled and joined on exit.

The renderer status is `Not tested`, `Sending...`, `Last send succeeded`, or
`Last send failed` with details. Changing the IP or port resets its status.
Success means the command bytes were sent over TCP, not that Viz confirmed execution.

## Settings Window

The settings window is a native tabbed Win32 window.

- `Detection`
  - `Detection enabled`
  - `Detect Threshold`
  - `Cooldown (ms)`
- `Engine`
  - Viz IP
  - Viz port
  - `GFX ON` command
  - `GFX OFF` command
  - last send status
  - `Save Config`
- `Templates`
  - `Detection Presets` label above the preset list
  - template list
  - selected template details
  - `New`
  - `Edit`
  - `Delete`
  - `Set Active`

Hover over the Detection checkbox, slider labels, sliders, or values for operator
guidance. Changes take effect after `Save Config`. `Reset Threshold` is hidden
because it has no effect in the current detection flow; its existing configuration
value is retained for compatibility. Detection still uses cue switching and cooldown.

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
- failed capture opens and empty-device enumeration retry at most every two seconds;
  selecting another source resets the delay
- logs retain the latest 500 entries with synchronized append, snapshot, and clear

## Build Requirements

For local setup and build commands, see [Local Windows build](docs/LOCAL_BUILD.md).

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

### 1.0.6

- moved renderer sends off the UI thread with a shared two-second connection/send deadline
- prevent concurrent commands, cancel pending network work on exit, and keep cue behavior intact
- replaced misleading connection status with the actual last-send result
- protected logging against concurrent DeckLink callbacks and UI access
- limited failed capture opening and empty enumeration retries to once every two seconds
- matched the SEND EVENT COMMAND caption and styling to Next Cue
- added Detection tooltips and hid the inactive Reset Threshold control
- added offline/stalled renderer, cancellation, partial-write, logging, retry, and tooltip regression checks

### 1.0.5

- added a masked startup password form accepting either configured hardcoded password
- kept all application initialization behind successful authentication
- added the same-size Send event command button beside Next Cue and narrowed the preview
- send the current cue's configured command before advancing; preserve the cue on failure
- apply detection cooldown after a successful manual event and refresh the cue UI immediately
- handle partial TCP writes before reporting a command as sent
- added native password/startup tests, button layout checks, and local TCP command tests

### 1.0.4

- established the template-detection application with a full video preview area
- retained the Templates tab's Detection Presets heading, top-right details, and bottom action row
- retained change-aware connection/cue labels and cached cue preview rendering
- set all application titles, headers, and log banners to version `1.0.4`

### 1.0.3

- removed the manual video-device `Refresh` button from the main operator window
- moved the Viz connection status to the bottom-left footer under the log section
- added a next-cue target preview panel that shows the ROI-cropped active template image
- changed the next-cue control from a full-width strip to a square operator button plus image preview layout
- removed the `Live Preview` and `Event Log` labels from the preview and log cards

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

See [Local Windows build](docs/LOCAL_BUILD.md) for build instructions and the
latest local verification results.
