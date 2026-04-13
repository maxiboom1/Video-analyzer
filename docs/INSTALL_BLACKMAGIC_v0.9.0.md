# Blackmagic setup for Video Analyzer v1.0.3

## Current state

Blackmagic support is already integrated into this repository.
The generated DeckLink SDK bindings are already present under:

```text
third_party\blackmagic\
```

The x64 project configurations already compile the Blackmagic path, so the
remaining machine setup is mostly runtime environment preparation, not source
integration.

## What you still need on the PC

1. Install **Blackmagic Desktop Video 16 or newer**.
2. Reboot if the installer requires it.
3. Connect the DeckLink or UltraStudio device.
4. Open **Desktop Video Setup** and verify that the hardware is detected.

## What is already in the repo

These files are already checked in:

- `third_party\blackmagic\DeckLinkAPI_h.h`
- `third_party\blackmagic\DeckLinkAPI_i.c`
- `third_party\blackmagic\DeckLinkAPIVersion.h`

The Visual Studio project already includes the generated COM C file and the
Blackmagic include directory for x64 builds.

## Build notes

Use one of these targets:

- `Debug | x64`
- `Release | x64`

Blackmagic capture should be treated as x64-only.

## Runtime behavior in 1.0.3

- Blackmagic devices appear in the same device dropdown as webcams
- capture starts with `1080i50` as the initial mode
- format detection can reconfigure capture when the incoming signal is detected
- preview is drawn in the native Win32 preview pane
- detection runs against the currently active template selected in the main UI

## Current limitations

- there is no UI yet for selecting a Blackmagic display mode manually
- no connector/profile switching is exposed
- no embedded audio handling is implemented
- no deinterlace stage is implemented for interlaced sources

## Recommended next follow-up

The next clean Blackmagic improvement would be exposing a real display-mode
selector per device, followed by optional deinterlacing before detection and
preview.
