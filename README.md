# DuckEggs

> I was walking past a farm and a sign said, “Duck, eggs.”<br>
> I thought, “That’s an unnecessary comma…”<br>
> – and then it hit me.

<img src="resources/duckeggs-logo-256.png" alt="DuckEggs logo" width="256">

**Keeps floating windows out of your way.**

DuckEggs learns where a floating window fits your work and moves it when it
gets in the way. Automatic discovery is limited to Chrome Picture-in-Picture;
Choose Window lets you explicitly select another eligible visible window for
the current session.

[Download DuckEggs 0.4.0-alpha.7 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.7/DuckEggs-0.4.0-alpha.7-windows-x64.zip) ·
[Download DuckEggs 0.4.0-alpha.7 for macOS](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.7/DuckEggs-0.4.0-alpha.7-macos-universal.zip)

This is an early alpha. See the
[v0.4.0-alpha.7 release notes](docs/RELEASE-NOTES-v0.4.0-alpha.7.md) for
platform details and verification limits.

## What it does

- Moves a floating window smoothly and keeps the pointer clear of its controls.
- Recognizes Chrome PiP across the official localized title set.
- Lets you choose another eligible visible window from any application.
- Learns per-application layout preferences locally and shows them in Learning
  maps.
- Freezes movement while you interact with the selected window.
- Avoids unnecessary moves during broad visual updates such as scrolling.
- Keeps the selected window on top while DuckEggs is active and bound to it,
  where the platform permits that behavior.

## Requirements

The Windows portable build supports 64-bit Windows 10 and Windows 11. The
macOS app targets Intel macOS 10.15 or newer and Apple Silicon macOS 11 or
newer. Automatic discovery remains Chrome-only on both platforms. The
interface is in English.

On macOS, Accessibility permission is required to move a selected window.
Screen Recording permission is optional and enables visual learning.
DuckEggs explains these permissions in the app and never requests them at
startup. On Windows, a selected window is temporarily kept on top while bound
and its previous topmost state is restored on reset, replacement, or graceful
exit. macOS public APIs cannot force another application's window above every
other app; Chrome PiP may retain its own floating behavior.

There is no installer, account, browser extension, or additional runtime to
set up. The macOS app is ad-hoc signed for integrity, but it is not signed
with an Apple Developer ID and is not notarized. macOS may show an
unidentified-developer warning; review the source, then use **Open Anyway**
in System Settings → Privacy & Security or Finder's **Open** command from the
app's context menu if you choose to run it. DuckEggs does not disable
Gatekeeper or remove quarantine attributes.

## Quick start

On Windows, extract the ZIP and run `DuckEggs.exe`. On macOS, extract the ZIP,
move `DuckEggs.app` to Applications, and open it. DuckEggs starts paused.
Open Chrome PiP or select **Choose Window**, then press **Resume** when ready.

The **Choose Window** overlay highlights eligible windows in teal. Full-screen
or maximized windows appear gray and cannot be selected. Click a window to
choose it, or press **Esc** or **Cancel** to leave the picker. A manual
selection lasts for the current session and DuckEggs stays paused after
selection. Return to **Automatic** to restore Chrome PiP discovery.

On Windows, **Start with Windows** is off by default and writes one per-user
Run entry only after you check it. On macOS, **Start with macOS** is off by
default and creates one per-user LaunchAgent only after you check it. Both
options start DuckEggs paused. If you move the app or executable, disable and
enable the option again to update its path.

## Your data and privacy

Learning data stays on your computer. DuckEggs does not save screenshots,
window titles, URLs, cursor coordinates, or key contents. Visual frames are
small, processed in memory, and discarded. The app collects no analytics.

Windows data is stored in `%LOCALAPPDATA%\DuckEggs`. macOS data is stored in
`~/Library/Application Support/DuckEggs`. To reset learning, exit DuckEggs and
remove that platform's data folder.

## Donate

Donations are voluntary and are not required to use DuckEggs. The in-app gold
**Donate** button opens a chooser for €2, €5, €10, or PayPal's custom amount
page. You can also [donate via PayPal](https://www.paypal.me/GiacomoDiCiocco).
DuckEggs does not process payments.

## License

DuckEggs is available under the [MIT License](LICENSE). See
[developer and build notes](docs/DEVELOPING.md),
[macOS notes](docs/MACOS.md), [third-party notices](THIRD_PARTY.md), and
[contribution guidelines](CONTRIBUTING.md).
