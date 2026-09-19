# Local Windows build

## Current version

Version `1.0.6` on `main` is the supported baseline for all future development.
Use `main` when cloning or updating this project.

The application includes a startup password gate, Blackmagic and webcam capture,
template detection, a manual event-command button, bounded background renderer
sends, synchronized logging, capture retry backoff, and Detection setting tooltips.
Reset Threshold is hidden because it has no effect in the current detection flow.

## Prerequisites

- Windows x64.
- Visual Studio 2022 Build Tools with MSVC v143 and Windows SDK 10.0.26100.
  The required component IDs are recorded in the root `.vsconfig` file.
- OpenCV **4.12.0**, using the official prebuilt Windows package extracted to
  `C:\opencv`. This matches the paths and library names in the original project.
- The generated DeckLink SDK bindings are already in `third_party\blackmagic`.
  Blackmagic Desktop Video 16+ and suitable hardware are needed for DeckLink
  capture, but are not needed to compile or open the application.

The local Build Tools installation is at `C:\BuildTools\2022`. The build script
discovers Visual Studio using `vswhere`, so it also supports other installation
locations. A full Visual Studio IDE is optional.

### Recreate the prerequisites on another machine

Download Microsoft's [Visual Studio 2022 Build Tools installer](https://aka.ms/vs/17/release/vs_buildtools.exe).
From an elevated PowerShell, run the following from the repository root after
saving the installer as `vs_buildtools.exe` in the current directory:

```powershell
$installer = Start-Process -FilePath .\vs_buildtools.exe -ArgumentList @(
    '--quiet', '--wait', '--norestart',
    '--installPath', 'C:\BuildTools\2022',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100',
    '--addProductLang', 'en-US'
) -WindowStyle Hidden -Wait -PassThru
$installer.ExitCode # 0 = success; 3010 = success, restart required
```

See Microsoft's [installer command-line reference](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022).

Download `opencv-4.12.0-windows.exe` from the official
[OpenCV 4.12.0 release](https://github.com/opencv/opencv/releases/tag/4.12.0).
The release asset's SHA-256 is:

```text
b753b14d880b9bc8d89d6acd3b665c040baec0211078435432fcae117db707af
```

Verify with `Get-FileHash .\opencv-4.12.0-windows.exe -Algorithm SHA256`, then
extract it to `C:\` (the archive creates `C:\opencv`). Inspect any existing
`C:\opencv` installation before extracting over it. Both the Release and Debug
libraries must be present under `C:\opencv\build\x64\vc16\lib`.

## Build and run

From the repository root, in PowerShell:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Debug
```

Add `-Rebuild` for a clean rebuild. A Developer Command Prompt is not required.
If local PowerShell policy blocks scripts, invoke the helper with a process-only
policy override: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration Release`.

Outputs:

```text
x64\Release\video-analyzer.exe
x64\Debug\video-analyzer.exe
```

The helper copies the matching OpenCV world DLL and FFmpeg video I/O DLL beside
each executable. No global PATH modification is necessary. Debug builds depend
on the developer runtime installed by Build Tools. Other PCs running a Release
build also need the Microsoft Visual C++ x64 runtime.

Start the application:

```powershell
Start-Process .\x64\Release\video-analyzer.exe
```

The application first asks for a password. Only after a valid login does it
initialize its main window, configuration, templates, capture, and detection.
The two case-sensitive passwords are defined in `src/StartupAuth.cpp`.

After login, the application creates `config.ini` beside the executable. Templates belong in
`templates\<name>\` beside that executable; no operator templates are included
in the repository. Use `Settings -> Templates` to create them. Debug and Release
have separate runtime configuration and template directories.

Only x64 is supported by this setup. The legacy x86 solution configurations do
not include the required OpenCV/DeckLink settings.

## Tests

Run `./scripts/test.ps1` in PowerShell. The tests compile the production modules
and dialog resource into a separate executable under `x64/Tests`. They exercise
the actual password dialog and startup cancellation, construct the native cue
controls without starting capture, and use a temporary local TCP listener for
command verification. They never send commands to a configured Viz engine.

## Local verification (2026-09-19, version 1.0.6)

- Visual Studio Build Tools 2022 17.14.41 installed successfully, without a
  required restart; MSVC 14.44.35207 and Windows SDK 10.0.26100.0 are present.
- `Release|x64` and `Debug|x64` builds both succeeded using the existing helper.
- The native feature tests verify both accepted passwords, exact case/whitespace
  matching, masked input, error/retry behavior, and that Cancel/Close/rejection
  leave the app's main window, configuration, template catalog, and services uninitialized.
- UI geometry tests verify both cue buttons are 96 x 96 pixels, adjacent, with
  the preview to their right at normal and wider window sizes.
- The local TCP receiver verifies the configured ON/OFF command and trailing NUL,
  one command per click, successful cue/label changes, cooldown, failure preserving
  the cue, and Next Cue remaining a state-only action.
- The feature test suite passed. Fault-injected socket tests cover a stalled connection with UI timer messages
  still running, one combined connection/send deadline, partial writes,
  disconnection/refusal, request snapshots, destination status reset, cancellation,
  duplicate rejection, and automatic/manual cue behavior.
- Native UI tests check tooltips on every visible Detection setting, text wrapping,
  tab visibility, and removal of Reset Threshold. Configuration retains its old value.
- Concurrent logging tests exercise append/snapshot/clear and the 500-entry limit.
  Capture retry tests use a controlled clock and missing-device states.
- Live video capture, DeckLink hardware, and a real Viz engine have not been
  tested here. Blackmagic capture source and SDK bindings remain unchanged.
