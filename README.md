# DuckEggs

> I was walking past a farm and a sign said, “Duck, eggs.”<br>
> I thought, “That’s an unnecessary comma…"<br>
> – and then it hit me.

<img src="resources/duckeggs-logo-256.png" alt="DuckEggs logo" width="256">

**Keeps floating windows out of your way.**

DuckEggs is a small Windows app that moves a window when it gets in the way of
your work. It was designed for browser picture-in-picture, and it can also keep
any eligible top-level window you choose manually out of your busy work area.

[Download DuckEggs 0.4.0-alpha.4 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.4/DuckEggs-0.4.0-alpha.4-windows-x64.zip)

This is an early alpha. See the
[v0.4.0-alpha.4 pre-release](https://github.com/gdiciocco/DuckEggs/releases/tag/v0.4.0-alpha.4)
for release details.

## What it does

- Moves a floating or utility window smoothly instead of making it jump around.
- Recognizes Chrome's picture-in-picture window across all 81 official Chrome
  locale titles.
- Lets you choose any eligible visible window from a browser or another
  application.
- Leaves your mouse usable while the video moves. Moving the pointer over the
  target controls stops movement.
- Temporarily keeps the target window on top while DuckEggs is active and the
  window is bound, including while DuckEggs is paused.
- Remembers your layout preferences for each app, locally on your PC.
- Includes visible Learning maps so you can see what DuckEggs has learned.
- Avoids unnecessary moves during broad page updates, such as scrolling.
- Uses a compact quote and smoother updates to reduce visual distraction.

## Requirements

DuckEggs 0.4.0-alpha.4 supports Windows 10 or Windows 11, 64-bit. Automatic
recognition remains limited to Chrome. Manual selection can target any eligible
top-level window for the current session; other applications are available by
explicit selection but are not yet live-validated as compatibility guarantees.
The DuckEggs interface is in English.

There is no installer, account, browser extension, or additional runtime to
set up.

## Quick start

1. Download the ZIP file.
2. Extract it to a folder.
3. Run `DuckEggs.exe`.
4. For Chrome, open picture-in-picture and click **Resume**. For another
   browser or application, click **Choose Window**, click the target window,
   then click **Resume**.

DuckEggs starts paused. Close any other DuckEggs versions before starting it.
Automatic mode accepts one Chrome picture-in-picture window at a time. Manual
selection always leaves DuckEggs paused and lasts only for the current session.

## Window controls

The **To tray** action in the window header hides DuckEggs without changing
whether it is paused or running. Use the notification-area icon to restore it.
Right-click the icon to open DuckEggs, pause or resume it, or exit.

**Choose Window** opens a temporary point-and-click overlay. Hover over a window
to see a teal outline and its title, then click once to pin it. Full-screen and
maximized windows appear gray and cannot be selected. Press **Esc**,
right-click, or click **Cancel** to exit without changing the current target.
**Automatic** returns to Chrome PiP discovery. Every exit leaves DuckEggs
paused, so use **Resume** as a separate action. The overlay does not capture,
preview, or store the selected window, and the selection click is not sent to
the app below.

Closing the window still exits DuckEggs.

While a target is bound, DuckEggs temporarily keeps it topmost, even while
paused. Returning to **Automatic**, choosing another window, or exiting
DuckEggs restores the target's previous topmost state. A window that was
already topmost stays topmost. If DuckEggs is force-killed or crashes, this
cleanup may not run.

## Your data

Learning data and preferences stay on your PC in
`%LOCALAPPDATA%\DuckEggs`. DuckEggs does not collect analytics, take
screenshots, or store them. Manual window names, transient hover details, and
capture pixels remain in memory for the current session only and are not
written to learning data or `status.json`.

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
