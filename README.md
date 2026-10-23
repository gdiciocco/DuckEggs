# Schiva Adattivo C++ 0.4.0-cpp.1

Schiva Adattivo C++ is a native C++20 port of Schiva adaptive 0.3.0. It has
one adaptive mode and moves Chrome's native Picture-in-Picture window only when
the frozen shared-learning core proposes a safe, time-based step.

Windows is the only implemented native backend. The portable core, command
line help, status, import logic and tests can be built elsewhere, but Linux and
macOS intentionally return an explicit unsupported-native error. This
repository does not include a Linux or macOS desktop adapter.

The implementation uses native Win32 controls and GDI. It does not bundle or
start Python, Qt, a browser, a web view, a server, a scripting runtime, an
interpreter, or telemetry.

## Safety model

- Only a visible top-level `chrome.exe` window with class
  `Chrome_WidgetWin_1` and the complete localized title `Picture in picture` or
  `Immagine nell'immagine` is a candidate. Partial Chrome titles are rejected.
- Exactly one candidate is required. Multiple PiP windows fail closed instead
  of choosing arbitrarily.
- Window identity includes HWND, process ID, process creation time, class,
  executable basename and the exact title. A process-handle cache is reused
  only when the current creation time still matches.
- Movement starts only after a strict full-scan lease. The target is fully
  revalidated at least every 200 ms, cheap identity checks run on every frame,
  and current geometry, cursor and held-button state are checked immediately
  before `SetWindowPos`.
- The pointer must remain outside the PiP controls and the native motion
  corridor. A destination must fit the destination monitor work area.
- The implementation never activates, resizes, reorders or closes a user
  window. It installs no keyboard hooks and does not log key contents.
- The default startup is PAUSED. `--resume` is the only explicit active start.
  It refuses to start if Python's
  `Local\SchivaAdaptive.Instance.v1` named mutex exists. Every active tick and
  every move checks that mutex again. It never signals or stops Python.
- The C++ program uses its own
  `Local\SchivaAdaptiveCpp.Instance.v1` mutex and
  `Local\SchivaAdaptiveCpp.Stop.v1` event. `--stop` cannot reach Python.

Capture is a separate bounded worker with latest-request and latest-result
mailboxes. Sampling is capped at 2 Hz, remains a single 64 by 36 grayscale
frame in memory, and is delivered at most once. Pause, motion, held input,
PiP proximity, context changes and geometry changes invalidate stale frames.
No pixels are persisted. Capture failure leaves the application usable and is
reported in status.

## Adaptive runtime

The worker thread exclusively owns native observation, the frozen core, SQLite,
movement and heatmap insight queries. The scheduler uses absolute deadlines at
a target 120 Hz during committed motion, 60 Hz while idle and 15 Hz while
paused. Missed deadlines are skipped, and controls are checked during bounded
12 ms waits. There is no busy loop and no process-wide timer-resolution change.

The GUI receives deeply detached JSON snapshots. It never opens SQLite and
never calls the controller. A heatmap request is coalesced to the newest
selected profile. While the read-only viewer is open it requests one refresh
about every second; requests are deferred during a moving step.

The frozen core owns the shared 12-feature model, normalized 24 by 14 maps,
decay, delayed measured/censored feedback, full-footprint scoring, the 65%
vacated-area goal with maximum-safe fallback, manual/helper interaction,
hold/pause behavior and path planning. Platform code supplies observations and
applies only the single safe target returned by the core.

## Learning viewer

The Win32/GDI viewer provides the same three semantic layers as Python:

- **Uso delle aree**: observed activity. Higher means the area is more used and
  therefore less suitable for PiP.
- **Preferenze manuali**: explicit user choices/corrections. This is separate
  from LinUCB reward.
- **Posizionamento stimato**: relative full-footprint placement estimate. It is
  not a probability or certainty.

Unknown, invalid, unsafe and unlearned cells share a neutral gray. Hovering
reports a cell's value; the placement layer also draws the complete PiP
footprint. Foreground, current PiP and recommendation outlines are shown when
the core supplies them. Projection and cold-start notices are explicit. The
selector preserves an explicit profile choice while data refreshes.

## Command line

```text
Schiva-Adattivo-Cpp.exe --help
Schiva-Adattivo-Cpp.exe --version
Schiva-Adattivo-Cpp.exe --probe
Schiva-Adattivo-Cpp.exe --status [--data-dir PATH]
Schiva-Adattivo-Cpp.exe --resume
Schiva-Adattivo-Cpp.exe --start-paused --no-visual
Schiva-Adattivo-Cpp.exe --observe-only --no-gui --duration 10
Schiva-Adattivo-Cpp.exe --stop
```

`--help`, `--version`, `--status`, `--probe` and `--import-python` exit instead
of activating the runtime. `--probe` performs no capture and creates no data.
There is intentionally no `--mode` option.

`--start-paused` and `--resume` are mutually exclusive. The default is paused,
including `--no-gui` operation unless `--resume` is present.

## Python data import

Importing is an explicit one-time copy:

```text
Schiva-Adattivo-Cpp.exe --import-python "C:\path\python-backup.sqlite3" --data-dir "C:\Temp\SchivaCppImport"
```

The Python source is opened with SQLite `READONLY`, checked for schema version
1, table compatibility and integrity, then copied with SQLite's backup API.
The destination data directory must be explicitly supplied and empty. An
existing C++ `learning.sqlite3` is never overwritten, and the source and
destination cannot be the same file. Import exits before any native backend is
created. The C++ program never writes into Python's data directory.

Default isolated C++ data locations:

- Windows: `%LOCALAPPDATA%\SchivaAdaptiveCpp`
- Linux: `$XDG_STATE_HOME/schiva-cpp` or `~/.local/state/schiva-cpp`
- macOS: `~/Library/Application Support/SchivaCpp`

`status.json` contains aggregate state only. Window titles, URLs, cursor
coordinates, raw frames and key contents are not persisted.

## Build

Requirements are CMake 3.20 or newer, a C++20 compiler and the pinned offline
vendor files:

```text
vendor/sqlite/sqlite3.c
vendor/sqlite/sqlite3.h
vendor/nlohmann/json.hpp
```

The build uses no `FetchContent`, package manager, network access, external
toolchain download or runtime DLL dependency. MSVC x64 builds use the static
`/MT` runtime.

```bat
cmake --preset windows-msvc
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cd out\build\windows-msvc
cpack -C Release
```

The coordinator may point the build at an offline vendor location:

```bat
cmake -S . -B out\vendor-build -G "Visual Studio 17 2022" -A x64 ^
  -DSCHIVA_SQLITE_DIR=C:\offline\sqlite ^
  -DSCHIVA_JSON_INCLUDE_DIR=C:\offline\json
```

`CPACK_GENERATOR=ZIP` produces the portable Windows package. A separate source
ZIP can be produced with `cpack --config CPackSourceConfig.cmake`.

## Tests and verification status

`tests/test_platform.cpp` is portable and uses only synthetic data. It checks
strict identity, geometry safety, deadline skipping, atomic status output,
capture coalescing/invalidation/fresh-once behavior, heatmap semantics, and a
real SQLite backup round trip against a source whose bytes must remain
unchanged. It does not capture a real desktop or move a window.

The C++ core and parity tests are discovered when `tests/test_core.cpp` and
`tests/parity_runner.cpp` are present.

Implementation review and portable source inspection are not hardware
validation. MSVC compilation, CTest, executable dependency inspection,
package hashing, and live Windows smoke tests remain coordinator tasks. No live
browser-animation or real-PiP movement validation is claimed here.

## Known limitations

- Windows is the only native adapter.
- Caret/UI Automation acquisition is unavailable; observations carry no caret.
- GDI cannot observe occluded content, protected video or the area beneath the
  PiP. A denied or black capture is treated as unavailable evidence.
- Visual reading evidence is uncertain and is not proof that a person is
  reading.
- The native UI, capture path and movement path are implemented from the Python
  0.3.0 behavior but were not executed on a real desktop by the platform worker.

See [CONTRIBUTING.md](CONTRIBUTING.md), [THIRD_PARTY.md](THIRD_PARTY.md), and
[LICENSE](LICENSE).
