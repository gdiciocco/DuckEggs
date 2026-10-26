# DuckEggs 0.4.0-alpha.6

This alpha adds a native macOS app alongside the portable Windows release.
macOS supports Intel 10.15+ and Apple Silicon 11+; the universal ZIP contains
`DuckEggs.app` and does not require a separate runtime. The app is ad-hoc
signed for integrity, without Developer ID signing or notarization.

## Changes

- Added a native Cocoa interface with a menu-bar item, Learning maps, and a
  click-to-choose overlay for eligible visible windows.
- Kept automatic discovery restricted to Chrome PiP. Manual selections remain
  session-only and leave DuckEggs paused until Resume is pressed.
- Added explicit Accessibility and optional Screen Recording permission
  controls. Startup, capability probes, and CI do not request permissions.
- Added an unchecked-by-default per-user macOS LaunchAgent option.
- Added a universal app bundle with the approved logo, English metadata, and
  license/provenance files inside the bundle.
- Preserved the existing adaptive core, model, schema, and saved data format.

## Download

- [Windows x64 portable ZIP](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.6/DuckEggs-0.4.0-alpha.6-windows-x64.zip)
- [macOS universal ZIP](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.6/DuckEggs-0.4.0-alpha.6-macos-universal.zip)

## Limits

macOS public APIs cannot force another app's window to stay above every other
application. Accessibility is required for movement; Screen Recording is
optional for visual learning. The app is not notarized, and first launch may
require the user's explicit Open Anyway action in System Settings. CI and
synthetic tests do not establish live visual quality, permission grants, or
real window-movement compatibility. The Intel 10.15 deployment target has not
been tested on Catalina hardware.
