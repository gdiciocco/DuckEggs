# DuckEggs 0.4.0-alpha.7

DuckEggs 0.4.0-alpha.7 fixes pointer handling below and near floating windows.

## Changes

- The old outside-20-px hover target and 20–28 px safety dead zone are gone.
  Hover over the actual window and its buttons remains protected.
- Adds opt-in strict continuous outward recovery. Fast approaches are handled
  immediately inside the motion corridor.
- Aligns the shared core policy with the Windows and macOS native guards. Stored
  12-feature LinUCB state, SQLite data, and preferences are unchanged.

## Download

- [Windows x64 portable ZIP](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.7/DuckEggs-0.4.0-alpha.7-windows-x64.zip)
- [macOS universal ZIP](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.7/DuckEggs-0.4.0-alpha.7-macos-universal.zip)

## Verification and limits

Automated coverage includes 44 complete edge/corner escape traces and a
synthetic fast-approach case. The release workflow builds and tests Windows x64,
the Linux portable core, macOS universal2 and Intel macOS. It also checks Mac
bundle metadata, architectures, deployment targets, packaged signatures and
paused startup.

These checks are automated and synthetic. Real Chrome PiP movement, link cursor
behavior, and macOS Accessibility permission behavior were not live-verified.

macOS supports Intel macOS 10.15+ and Apple Silicon macOS 11+ in one universal
app. It is ad-hoc signed without Developer ID signing or notarization; first
launch may require **Open Anyway**. As in alpha.6, public APIs cannot keep
another app's window above every application, and Catalina hardware was not
tested.

## Updating

Close the old DuckEggs instance and extract the new release. Existing Windows
and macOS data paths remain unchanged, and no reset is required.
