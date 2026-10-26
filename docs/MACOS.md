# DuckEggs on macOS

DuckEggs is a native C++ app for Intel macOS 10.15 or newer and Apple Silicon
macOS 11 or newer. The universal ZIP contains `DuckEggs.app`; it has no
third-party runtime dependency. Automatic PiP discovery supports Chrome only.
**Choose Window** lets you select another eligible visible application window
for the current session.

## Install and open

Download the universal ZIP from the
[alpha.6 release](https://github.com/gdiciocco/DuckEggs/releases/tag/v0.4.0-alpha.6),
extract it, and move `DuckEggs.app` to Applications. The app is ad-hoc signed
for integrity checks, but it is not signed with an Apple Developer ID and is
not notarized. On first open, macOS may block it. On supported macOS versions,
open **System Settings → Privacy & Security**, review the DuckEggs notice, and
choose **Open Anyway**. On older versions, use Finder's **Open** command from
the app's context menu and confirm that you want to open it. DuckEggs does not
change Gatekeeper settings or remove quarantine attributes.

The app starts paused. To move a selected window, grant DuckEggs Accessibility
permission in **System Settings → Privacy & Security → Accessibility**, then
press **Resume**. Screen Recording permission is optional and enables visual
learning. The Permissions window explains both and offers direct settings
links. Merely launching DuckEggs, running `--probe`, or reading status never
requests either permission. Only the clearly labeled buttons in that window
start a permission request.

## Choose a window

Automatic mode recognizes Chrome PiP only. Use **Choose Window** to select a
visible, movable window from another application. Teal outlines are eligible;
gray full-screen or maximized windows cannot be selected. Click a window to
pin it for this session. Press **Esc**, right-click, or **Cancel** to leave the
picker. **Automatic** returns to Chrome PiP discovery. The app stays paused
after the picker closes; press **Resume** separately.

DuckEggs does not activate, resize, or close the selected application window.
When a window is selected, the app uses the public macOS accessibility position
API to move it. macOS does not expose a public API for forcing another
application's window above every other app. DuckEggs therefore cannot promise
that a selected window stays topmost on macOS. Chrome PiP may retain its own
floating level.

## Login and local data

**Start with macOS** is unchecked by default. Checking it creates the
per-user `~/Library/LaunchAgents/io.github.gdiciocco.DuckEggs.plist` login
item. Unchecking removes that DuckEggs-owned item. Both actions require a user
click; startup, probes, and CI do not write it. Login starts DuckEggs paused.
If you move the app, disable and re-enable the checkbox from its new location.

Learning data is stored in `~/Library/Application Support/DuckEggs`. DuckEggs
does not save screenshots, window titles, URLs, cursor coordinates, or key
contents. Visual frames are reduced in memory and discarded. No analytics are
collected.

## Command-line checks

The executable lives at
`DuckEggs.app/Contents/MacOS/DuckEggs`. `--help`, `--version`, `--probe`,
`--status`, and `--stop` do not show the Cocoa UI. `--probe` reads native
capabilities without capturing the screen or prompting for permission. GUI
runtime options such as `--start-paused`, `--no-visual`, `--duration`, and
`--data-dir` are available for controlled testing.

## Verification boundary

CI builds and tests the universal and Intel targets, checks a synthetic image
through the production grayscale converter, checks multi-display coordinate
conversion, inspects architectures and deployment metadata, and runs a
read-only native probe. A timed GUI launch checks that the AppKit event loop
can start and shut down without moving other windows. These checks do not
validate visual appearance, real Accessibility or Screen Recording grants,
PiP compatibility across every Chrome release, or live window movement. The
10.15 Intel deployment target has not been live-tested on a Catalina system.
