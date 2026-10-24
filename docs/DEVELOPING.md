# Developing DuckEggs 0.4.0-alpha.2

This document preserves the detailed engineering, build, and verification
information for DuckEggs. See the [root README](../README.md) for the public
overview.

DuckEggs is a native C++20 port of the Schiva adaptive Picture-in-Picture
experience. It learns where Chrome's native PiP window fits your work and moves
it only when the shared adaptive model finds a safe destination.

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
paused and shows a clear Paused/Watching/Moving/No PiP status, Chrome PiP
availability, learning updates, visual-signal state, and the primary
Resume/Pause action. Learning maps and Exit are secondary actions.

The product quote is presented compactly. UI updates avoid unnecessary
refreshes to reduce flicker while runtime status changes.

The header includes **To tray**, which hides the window without changing its
current paused or running state. The notification-area icon restores the
window; its menu provides Open, Pause/Resume and Exit. Closing the window
exits DuckEggs.

The more prominent gold **Donate** action opens the donation chooser.
Donations are voluntary; the suggested one-time amounts are €2, €5 and €10,
with a custom amount available through PayPal.

The brand mark is the approved DuckEggs raster artwork. Its checked-in master
PNG is authoritative; the native header renders an embedded 256 px copy (with
a 128 px fallback) without filesystem lookup. The ICO and preview images are
deterministic Cairo resamples of the complete master canvas, preserving its
transparent margins and brush, expression, shell and yolk details. No SVG is
used as a reconstructed vector source.

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

- Only a visible, top-level `chrome.exe` window with class
  `Chrome_WidgetWin_1` and the complete localized PiP title is a candidate.
  Partial Chrome titles fail closed.
- Exactly one candidate is required. Multiple PiP windows are not chosen
  arbitrarily.
- Window identity includes the handle, process ID, process creation time,
  class, executable basename and exact title. Full identity validation is
  repeated throughout a movement.
- A movement target is revalidated for geometry, cursor and held buttons
  immediately before it is applied. DuckEggs never activates, resizes,
  reorders or closes a user window.
- The pointer must remain outside PiP controls and the native motion corridor.
  The destination must fit the monitor work area.
- DuckEggs installs no keyboard hooks and does not log key contents.
- Startup is paused. `--resume` is the only explicit active start.
- Resume fails safely while Python's original
  `Local\SchivaAdaptive.Instance.v1` mutex is active. DuckEggs never signals,
  stops or modifies the Python application or its database.
- DuckEggs uses the isolated `Local\DuckEggs.Instance.v1` mutex and
  `Local\DuckEggs.Stop.v1` event. `--stop` cannot reach Python.

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
`DuckEggs-0.4.0-alpha.2-windows-x64.zip`. It contains the executable,
launchers, documentation, and the complete SQLite and nlohmann/json license
and provenance files under `licenses/`.

Python is optional and is used only in development/CI to compare the parity
runner's JSON with `tests/fixtures/core-parity.json`. It is not a DuckEggs
runtime dependency.

## Tests and verification boundary

`test_platform` uses synthetic data for strict target identity, geometry,
scheduling, status output, capture mailbox behavior, heatmap helpers and a
SQLite backup round trip. It does not capture a real desktop or move a window.
The core tests cover model, storage, controller and visual behavior.
`core_parity` compares the complete C++ fixture output against the pinned
Python reference when Python is available at configure time.

The Windows CI job builds and runs the release tests and produces the portable
ZIP. The Linux CI job builds and runs the portable core and unsupported-native
tests. Linux and macOS do not claim a desktop adapter.

Source inspection and portable tests are not hardware validation. MSVC
compilation, dependency inspection, package hashing, and actual desktop/PiP
smoke tests remain coordinator verification steps. No live browser-animation
or real window-movement validation is claimed here.

## Known limitations

- Windows is the only native adapter.
- Caret/UI Automation acquisition is not implemented.
- GDI capture cannot inspect occluded content, protected video or the area
  beneath the PiP. A denied or black capture is treated as unavailable
  evidence.
- Visual reading evidence is uncertain and is not proof that a person is
  reading.

See [CONTRIBUTING.md](../CONTRIBUTING.md),
[THIRD_PARTY.md](../THIRD_PARTY.md), and [LICENSE](../LICENSE).
