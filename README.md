# DuckEggs

> I was walking past a farm and a sign said, “Duck, eggs.”<br>
> I thought, “That’s an unnecessary comma…"<br>
> – and then it hit me.

<img src="resources/duckeggs-logo-256.png" alt="DuckEggs logo" width="256">

**Keeps floating videos out of your way.**

DuckEggs is a small Windows app that moves a floating video window when it gets
in the way of your work. Automatic discovery is intentionally limited to
Chrome. For other browsers and applications, choose the floating window
explicitly; that choice lasts only for the current session.

[Download DuckEggs 0.4.0-alpha.3 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.3/DuckEggs-0.4.0-alpha.3-windows-x64.zip)

This is an early alpha. See the
[v0.4.0-alpha.3 pre-release](https://github.com/gdiciocco/DuckEggs/releases/tag/v0.4.0-alpha.3)
for release details.

## What it does

- Moves the floating video smoothly instead of making it jump around.
- Recognizes Chrome's picture-in-picture window when Chrome is using another
  language.
- Lets you choose a visible window from another browser or application when
  you want to use manual mode.
- Leaves your mouse usable while the video moves. Moving the pointer over the
  PiP controls stops movement.
- Remembers your layout preferences for each app, locally on your PC.
- Includes visible Learning maps so you can see what DuckEggs has learned.
- Avoids unnecessary moves during broad page updates, such as scrolling.
- Uses a compact quote and smoother updates to reduce visual distraction.

## Requirements

DuckEggs 0.4.0-alpha.3 supports Windows 10 or Windows 11, 64-bit. Automatic
recognition is Chrome-only; manual selection can target another browser or
application window for the current session. Other applications are available
by explicit selection but are not yet live-validated as a compatibility
guarantee. The DuckEggs interface remains in English.

There is no installer, account, browser extension, or additional runtime to
set up.

## Quick start

1. Download the ZIP file.
2. Extract it to a folder.
3. Run `DuckEggs.exe`.
4. Open a video in Chrome and turn on picture-in-picture.
5. Click **Resume** in DuckEggs.

DuckEggs starts paused. Close any other DuckEggs versions before starting it.
Automatic mode uses only one Chrome picture-in-picture window at a time.
For another browser or application, open its floating video window, click
**Choose PiP...**, then click the floating video window itself. Manual selection
always leaves DuckEggs paused; use **Resume** when you are ready. The selection
is not saved after exit.

## Window controls

The **To tray** action in the window header hides DuckEggs without changing
whether it is paused or running. Use the notification-area icon to restore it.
Right-click that icon to open DuckEggs, pause or resume it, or exit.

**Choose PiP...** opens a temporary point-and-click overlay. Hover over a window
to see a teal outline and its title, then click once to pin it. Full-screen and
maximized windows appear gray and cannot be selected. Press **Esc**, right-click,
or click **Cancel** to exit without changing the current target. **Automatic**
returns to Chrome-only discovery. Every exit leaves DuckEggs paused, so use
**Resume** as a separate action. The overlay does not capture, preview, or store
the selected window, and the selection click is not sent to the app below.

Closing the window still exits DuckEggs.

## Your data

Learning data and preferences stay on your PC in
`%LOCALAPPDATA%\DuckEggs`. DuckEggs does not store screenshots and does not
include analytics. Manual window names and transient hover details remain in
memory for the current session only and are not written to learning data or
`status.json`.

To reset DuckEggs, exit the app and delete that folder.

## Alpha feedback

This alpha may have rough edges and bugs. If something does not work as
expected, please
[open a GitHub issue](https://github.com/gdiciocco/DuckEggs/issues).

The Windows build in this alpha is unsigned.

## Donate

Donations are voluntary. They are not required to use DuckEggs, and you can
choose another amount through PayPal. Suggested one-time amounts are:

- [Donate €2](https://www.paypal.me/GiacomoDiCiocco/2EUR)
- [Donate €5](https://www.paypal.me/GiacomoDiCiocco/5EUR)
- [Donate €10](https://www.paypal.me/GiacomoDiCiocco/10EUR)
- [Choose another amount](https://www.paypal.me/GiacomoDiCiocco)

The more prominent gold **Donate** action in DuckEggs opens the donation
chooser. Selecting a donation opens PayPal in your browser. DuckEggs does not
include analytics.

## License

DuckEggs is available under the [MIT License](LICENSE).

Interested in the code or building DuckEggs? See the
[developer and build notes](docs/DEVELOPING.md) or
[contribution guidelines](CONTRIBUTING.md).
