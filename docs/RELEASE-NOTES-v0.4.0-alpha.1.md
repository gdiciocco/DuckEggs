# DuckEggs v0.4.0-alpha.1

This is the first public alpha of DuckEggs, a Windows app that keeps Chrome's
floating video out of your way while you work.

[Download DuckEggs 0.4.0-alpha.1 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.1/DuckEggs-0.4.0-alpha.1-windows-x64.zip)

## What's in this alpha

- Smooth movement for Chrome's picture-in-picture video.
- A usable mouse while DuckEggs moves the video. Moving the pointer over the
  PiP controls stops movement.
- Layout preferences remembered locally for each app.
- Visible Learning maps so you can see what DuckEggs has learned.
- Recognizes broad page updates to reduce unnecessary movement.

## Quick start

1. Download and extract the ZIP file.
2. Run `DuckEggs.exe`.
3. Open a video in Chrome and turn on picture-in-picture.
4. Click **Resume** in DuckEggs.

DuckEggs starts paused. Close any other DuckEggs versions first, and use only
one Chrome picture-in-picture window at a time.

No installer, account, browser extension, or additional runtime is required.

## Alpha notes

- Current support is Windows 10 or Windows 11, 64-bit, with Google Chrome.
- This build is unsigned.
- This alpha may have rough edges and bugs.
- DuckEggs has not yet been validated on a live Windows desktop with actual
  Chrome picture-in-picture movement. Please treat this as an early test build.
- Learning data stays on your PC in `%LOCALAPPDATA%\DuckEggs`. DuckEggs does
  not store screenshots and does not include analytics.

If something does not work as expected,
[open a GitHub issue](https://github.com/gdiciocco/DuckEggs/issues).

DuckEggs is available under the [MIT License](https://github.com/gdiciocco/DuckEggs/blob/main/LICENSE).
