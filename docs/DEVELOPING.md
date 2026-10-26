# Developing DuckEggs 0.4.0-alpha.5

This document preserves the detailed engineering, build, and verification
information for DuckEggs. See the [root README](../README.md) for the public
overview.

DuckEggs is a native C++20 port of the Schiva adaptive placement experience.
Automatic mode learns where Chrome's native PiP window fits your work and moves
it only when the shared adaptive model finds a safe destination. An additional
session-only manual mode can target any eligible visible top-level window from
another browser or application after explicit selection.

The product interface is entirely in English. Internal `schiva` namespaces and
header paths remain frozen for compatibility with the ported core.

Windows is the implemented native backend. Linux and macOS builds include the
portable core, command-line actions and tests, but report an explicit
unsupported-native error. DuckEggs does not bundle or start Python, Qt, a
browser, a web view, a server, a scripting runtime, an interpreter or
telemetry.

## Interface

The compact DuckEggs window uses a dark neutral interface with teal accents,
Segoe UI typography, DPI-scaled metrics and native keyboard controls. It starts
paused and shows a clear Paused/Watching/Moving/No target status, target
availability, learning updates, visual-signal state, and the primary
Resume/Pause action. Learning maps and Exit are secondary actions.

The compact footer also includes **Choose Window**. It opens a paced native
desktop picking overlay. Hovering a window shows its title and either a teal
eligible outline or a gray full-screen state with a short reason. One
deliberate left click sends that screen point to the owner thread, which
freshly resolves and pins the window before the overlay closes. **Esc**,
right-click, and **Cancel** leave the existing target unchanged.
**Automatic** returns to Chrome-only discovery. Opening, selecting, resetting,
or closing the overlay leaves DuckEggs paused; Resume is a separate action.
The overlay does not take screenshots or preview window contents. The compact
fixed 600 by 360 client area keeps the footer buttons and captions visible.
The window can be minimized, moved, closed or sent to the tray; it does not
resize or maximize. Learning maps remains independently resizable.

The product quote is presented compactly. UI updates avoid unnecessary
refreshes to reduce flicker while runtime status changes.

The header includes **To tray**, which hides the window without changing its
current paused or running state. The notification-area icon restores the
window; its menu provides Open, Pause/Resume and Exit. Closing the window
exits DuckEggs.

The **Start with Windows** checkbox is unchecked when no `DuckEggs` value is
present in the current user's `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
key. Reading that value at launch is read-only. Only a user checkbox click
creates or removes that one value; the command contains the quoted absolute
path to the current executable. No administrator access is needed, and an
error restores the checkbox to its previous state. Startup launches still
begin paused. Moving or renaming the application folder leaves the old Run
command stale; turn the option off and on again from the new location. Windows
also limits Run command length, so deeply nested install paths may be rejected
with an error and the checkbox restored.

The more prominent gold **Donate** action opens the donation chooser.
Donations are voluntary; the suggested one-time amounts are €2, €5 and €10,
with a custom amount available through PayPal.

The brand mark is the approved 1254 by 1254 RGBA DuckEggs raster artwork. Its
checked-in master PNG is authoritative and read-only; the native header renders
it as a 52 logical-pixel aspect-fit mark from an embedded 256 px copy (with a
128 px fallback) without filesystem lookup. Header resources and the 512 px
preview resample the complete master canvas. ICO entries instead use a centered
square viewport around the alpha-at-least-16 artwork bounds, with 8 source
pixels of padding; this retains the approved artwork while reducing transparent
border space. The 48 px preview and contact sheet show that ICO framing. The ICO
contains 32-bit DIB entries with alpha and AND masks at 16, 20, 24, 32, 40, 48,
64, 80, 96 and 128 px, plus a separately rendered 256 px PNG entry. The
retained 512 px preview is not an ICO dimension. No SVG is used as a
reconstructed vector source.

Top-level windows select 32 logical-pixel and 16 logical-pixel icon resources
at their actual DPI and refresh those icons after `WM_DPICHANGED`. Tray icons
use the current small-icon system metrics. HICONs are loaded without
`LR_SHARED`, cached by actual pixel size, and owned by DuckEggs. The cache is
destroyed only after the tray icon is removed, windows are destroyed, and the
window classes are unregistered.

`tools/render_duckeggs_icon.py` refuses to render unless the master hash and
dimensions match the approved values. As a development-only artifact it also
writes `build/duckeggs-logo-contact-sheet.png`, showing every actual ICO size on
both dark and white backgrounds. The build directory is excluded from source
packages.

The Learning maps window is a read-only local viewer with:

- An application/layout selector that preserves the selected profile while
  snapshots refresh.
- Activity, Manual preference and Placement layers.
- A relative-value legend and gray unknown/unavailable cells.
- Full-PiP footprint hover, plus foreground, current-PiP and recommendation
  outlines.
- Explicit cold-start and projected-map notices.

The viewer requests a detached snapshot about once per second while open.
Requests are deferred while DuckEggs is moving the PiP. Refreshes do not focus
or activate the viewer.

## Safety model

- Automatic mode accepts as a candidate only a visible, top-level `chrome.exe`
  window with class `Chrome_WidgetWin_1` and the complete localized PiP title.
  Partial Chrome titles fail closed.
- Exactly one candidate is required. Multiple PiP windows are not chosen
  arbitrarily.
- Window identity includes the handle, process ID, process creation time,
  class, executable basename and exact title. Full identity validation is
  repeated throughout a movement.
- Manual mode accepts a visible, non-full-screen, non-cloaked top-level window
  from a non-DuckEggs process, including other browsers and media
  applications. It excludes shell/desktop surfaces, minimized or maximized
  windows, windows covering at least 97% of their monitor or work area, invalid
  rectangles, and windows whose process identity cannot be queried.
- A manual binding pins the handle, process ID, process creation time, class,
  executable basename, and a private per-instance `SetPropW` lifetime marker.
  The marker is checked before observation, motion validation and the native
  move. Title changes are allowed because video titles update; handle reuse or
  any other identity mismatch fails closed.
- Manual hover and selection use only screen points. Selection performs a fresh
  native z-order hit-test at the actual click point; no hover token or cached
  candidate is accepted. The shared hit-test skips DuckEggs and shell/desktop
  surfaces, but returns a full-screen window so it can be rejected rather than
  selecting the window behind it.
- Hover requests are coalesced to the latest point. Hit-testing, identity
  validation, and every native binding mutation run only on the runtime worker
  thread. The UI reads small title/rectangle/eligibility snapshots and sends
  queued commands; it never owns a native window handle or resolves processes.
- The picking overlay is one temporary layered desktop window. It spans the
  virtual desktop, including negative monitor origins, remains input-opaque so
  selection clicks cannot reach the target app, and installs no global input
  hook. It is cleaned up on close, focus loss, tray shutdown, and application
  quit.
- Manual target eligibility uses the same portable geometry policy at hover,
  click time, and bound-target revalidation. If a bound target later becomes
  full-screen or maximized, the binding is cleared and DuckEggs remains
  explicitly manual and paused, with no automatic Chrome fallback.
- While a target is bound, DuckEggs temporarily makes that top-level window
  topmost, including during the paused state. Returning to Automatic, changing
  the target, or exiting restores the state captured before DuckEggs applied
  its temporary topmost state. A window that was already topmost remains
  topmost. Forced process termination or a crash can bypass cleanup.
- A movement target is revalidated for geometry, cursor and held buttons
  immediately before it is applied. DuckEggs never activates, resizes or closes
  a user window. The temporary topmost behavior above is the only intentional
  z-order change; moving a target does not otherwise reorder it.
- The pointer must remain outside PiP controls and the native motion corridor.
  The destination must fit the monitor work area.
- DuckEggs installs no keyboard hooks and does not log key contents.
- Startup is paused. `--resume` is the only explicit active start.
- Resume fails safely while Python's original
  `Local\SchivaAdaptive.Instance.v1` mutex is active. DuckEggs never signals,
  stops or modifies the Python application or its database.
- DuckEggs uses the isolated `Local\DuckEggs.Instance.v1` mutex and
  `Local\DuckEggs.Stop.v1` event. `--stop` cannot reach Python.

Recognition uses exact title aliases from Chromium localization resources at
commit `41020d875c4518f25b5b27a76c7d418235a50f31`, for
`IDS_PICTURE_IN_PICTURE_TITLE_TEXT` (GRIT message `7750228210027921155`).
[`design/chromium-pip-titles.json`](../design/chromium-pip-titles.json)
provides the English source text, all 81 locale titles, and per-locale XTB
SHA256 provenance. The compiled table is generated offline from that frozen
dataset.

The table includes the English source and retains the legacy Italian title.
Matching uses the same compatibility normalizer for observed and official
titles, then requires exact normalized equality against this finite table.
There is no prefix, class-only, topmost-only or size-only fallback. Official
Unicode is preserved, including non-breaking spaces and trailing zero-width
spaces. Automatic recognition remains limited to Chrome; Edge, Brave, Firefox
and other applications require explicit manual selection. This manual path is
implemented but other applications are not claimed as live-validated
compatibility targets.

Capture is a bounded one-worker latest-request/latest-result pipeline. Sampling
is capped at 2 Hz and uses one in-memory 64 by 36 grayscale frame at a time.
Stale frames are invalidated after pause, input, motion, context or geometry
changes. Pixels and cursor traces are not persisted.

Visual change detection keeps the existing tile-mean path for localized
activity. It also compares pixels within comparable cells: a cell is counted
only when it is outside the PiP, DuckEggs and other capture masks in both
frames, and it changes when at least 25% of its pixels differ by 12 gray levels
or more. At least 60% of the 16 by 9 cells (and at least eight cells) must be
comparable. A frame is broadly active when at least 50% of comparable cells
change, or when at least 35% change in two fresh successive captures and
changes span at least half the grid's rows and columns.

Broad activity suppresses only visual-evidence movement, clears outstanding
visual risk, and latches for one second. Fresh broad captures extend the latch;
time without broad captures does not. Localized updates can become actionable
again after the latch expires. Cursor proximity and a genuine caret conflict
remain independent triggers, and an actual manual correction can still cancel
movement and supply its negative reward. The controller reports the latch as
`visual_broad_update` in its status. Pixels remain memory-only.

## Adaptive runtime

The worker thread exclusively owns native observation, the frozen core,
SQLite, movement and insight queries. The UI only receives detached JSON
snapshots. The scheduler targets 120 Hz during committed motion, 60 Hz while
idle and 15 Hz while paused with missed deadlines skipped. It does not change
the system timer resolution or use a busy loop.

The frozen core owns the shared 12-feature model, 24 by 14 activity/manual
maps, decay, delayed measured/censored feedback, full-footprint scoring, the
65% vacated-area goal with a maximum-safe fallback, interaction holds, pause
behavior and trajectory planning. Native code supplies observations and
applies only the safe target returned by the core.

## Command line

The Windows executable is a GUI-subsystem application, so opening DuckEggs
normally does not create an unwanted console. CLI actions attach to an existing
terminal only when needed; redirected stdout and stderr remain available.
Windows command-line arguments are read through the Unicode command line and
converted safely to UTF-8 only at the CLI parsing boundary.

```text
DuckEggs.exe --help
DuckEggs.exe --version
DuckEggs.exe --probe
DuckEggs.exe --status [--data-dir PATH]
DuckEggs.exe --resume
DuckEggs.exe --start-paused --no-visual
DuckEggs.exe --observe-only --no-gui --duration 10
DuckEggs.exe --stop
```

`--help`, `--version`, `--status`, `--probe` and `--import-python` exit without
starting the runtime. `--probe` performs no capture and creates no data. There
is intentionally no `--mode`.

`--start-paused` and `--resume` are mutually exclusive. The default is paused,
including `--no-gui` operation unless `--resume` is present.

## Python data import

Import is an explicit one-time copy into a separate, empty DuckEggs data
directory:

```text
DuckEggs.exe --import-python "C:\path\python-backup.sqlite3" --data-dir "C:\Temp\DuckEggsImport"
```

The Python source is opened read-only, checked for schema version 1, required
tables and integrity, then copied with SQLite's backup API. The destination
must be explicitly supplied and empty. Existing DuckEggs learning data is
never overwritten. Import exits before the native backend starts.

Default isolated data locations:

- Windows: `%LOCALAPPDATA%\DuckEggs`
- Linux: `$XDG_DATA_HOME/duckeggs` or `~/.local/share/duckeggs`
- macOS: `~/Library/Application Support/DuckEggs`

`status.json` contains bounded aggregate state only. Window titles, URLs,
cursor coordinates, raw frames and key contents are not persisted.

## Launching

- `Start-DuckEggs.cmd` opens the GUI paused.
- `Stop-DuckEggs.cmd` sends a stop request to DuckEggs only.

## Build

Requirements are CMake 3.20 or newer for direct configuration, CMake 3.25 or
newer for the presets, a C++20 compiler and the pinned offline vendor files:

```text
vendor/sqlite/sqlite3.c
vendor/sqlite/sqlite3.h
vendor/nlohmann/json.hpp
```

The build performs no network fetch and has no external runtime DLL
dependency. MSVC x64 builds use the static `/MT` runtime for every target.
`schiva_ui` is intentionally an object library because the runtime and UI
reference each other; direct object inclusion avoids relying on linker
archive-rescanning behavior or CMake link groups.

```bat
cmake --preset windows-msvc
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cd out\build\windows-msvc
cpack -C Release
```

The portable artifact is
`DuckEggs-0.4.0-alpha.5-windows-x64.zip`. It contains the executable,
launchers, documentation, and the complete SQLite and nlohmann/json license
and provenance files under `licenses/`.

Python is optional and is used only in development/CI to compare the parity
runner's JSON with `tests/fixtures/core-parity.json`. It is not a DuckEggs
runtime dependency.

## Tests and verification boundary

`test_platform` uses synthetic data for strict target identity, geometry,
scheduling, status output, capture mailbox behavior, heatmap helpers and a
SQLite backup round trip. On Windows, `test_win32_icons` loads the embedded ICO
through the same size-keyed cache as the UI and checks real HICON bitmap sizes
and same-size cache reuse. It does not create windows or alter the registry.
`test_platform` does not capture a real desktop or move a window.
`test_manual_selection` uses a fake owner-thread backend to cover overlay pause
barriers, queued toggle resistance, coalesced hover points, actual-click-point
selection, full-screen rejection without selecting behind it, cancellation,
rapid reopen, explicit selection/reset, manual-target loss without automatic
fallback, and portable identity/eligibility/hit-test policies. It does not
control a real desktop window.
The core tests cover model, storage, controller and visual behavior.
`core_parity` compares the complete C++ fixture output against the pinned
Python reference when Python is available at configure time.

The Windows CI job builds and runs the release tests and produces the portable
ZIP. The Linux CI job builds and runs the portable core and unsupported-native
tests. Linux and macOS do not claim a desktop adapter.

Source inspection and portable tests are not hardware validation. MSVC
compilation, dependency inspection, package hashing, and actual desktop/PiP
smoke tests remain coordinator verification steps. No live browser-animation,
other-application compatibility, or real window-movement validation is claimed
here.

## Known limitations

- Windows is the only native adapter.
- Manual selection is session-only and is not restored after restart.
- Caret/UI Automation acquisition is not implemented.
- GDI capture cannot inspect occluded content, protected video or the area
  beneath the PiP. A denied or black capture is treated as unavailable
  evidence.
- Visual reading evidence is uncertain and is not proof that a person is
  reading.

See [CONTRIBUTING.md](../CONTRIBUTING.md),
[THIRD_PARTY.md](../THIRD_PARTY.md), and [LICENSE](../LICENSE).
