# DuckEggs product update — direct user instruction 2026-10-24

This supersedes product branding and UI language in porting-contract.md, not
the frozen internal core C++ API or safety/learning/privacy requirements.
Python source/export/running app remain entirely unchanged.

- New C++ application name: **DuckEggs**. All visible interface language English.
- Executable DuckEggs.exe, CMake target/project DuckEggs, portable ZIP DuckEggs
  Windows x64. New launchers Start-DuckEggs.cmd / Stop-DuckEggs.cmd. Docs and
  --help/--version/errors/status text use the new brand. Internal namespace schiva
  and core header paths may stay to avoid breaking the agreed integration API.
- Isolated default data: %LOCALAPPDATA%/DuckEggs, XDG duckeggs, macOS DuckEggs;
  dedicated DuckEggs instance/stop names. Preserve detection of original
  Local\\SchivaAdaptive.Instance.v1 and refuse simultaneous movement. Start paused.
- MIT own code, retain upstream attribution. SQLite/json vendor unchanged. Only
  built-in Windows graphics libraries are allowed for UI polish (e.g. GDI+/DWM),
  no external GUI frameworks, runtimes, web servers, network, telemetry.

## Modern native UI direction

Dark neutral background (#11151C), card surfaces (#1B222D), restrained teal accent
(#73DCC5), light primary text (#E8EEF5), secondary (#A3AEBE), clear spacing.
Segoe UI at readable DPI-scaled sizes, generous margins, rounded cards/buttons,
subtle borders, flat hierarchy; Windows dark title bar/rounded corners if supported
with safe fallback. Use system vector drawing/anti-aliased graphics as useful.
Resizable, sensible minimum sizes and per-monitor DPI handling. Avoid an old
utility form with tightly packed stock gray buttons or implementation-heavy text.
Keep native interactive controls/accessibility/keyboard behavior where practical.

Main window: DuckEggs title, short Adaptive PiP placement subtitle, clear status
badge (Paused/Watching/Moving/No PiP), a concise status card for Chrome PiP,
learning updates and visual signals. Primary Resume/Pause action, secondary
Learning maps action, clean Exit control. Helpful human reason when Python
prevents resume. Refresh must not steal focus. No extra product modes.

Learning maps window: application/layout selector, layers Activity / Manual
preference / Placement, clear relative-value legend and unknown-gray semantics,
prominent projection/cold-state messages, heatmap with full PiP footprint hover,
current foreground/PiP/recommendation outlines. English all notes/errors emitted
by core insights as well as renderer labels. Keep selection stable, redraw only
when appropriate, ~1Hz snapshots while open deferred during movement.

Acceptance: Windows Release build, existing safety/parity tests retained, English
user-facing strings, executable/default data namespace branded. Coordinator will
inspect paused DuckEggs windows only (no real PiP actuation/capture); all code,
test, rendering helpers, build changes and fixes exclusively DeepSeek. Workers
must not use actual native UI/capture/mouse. No Python stop/overwrite/migration.

## Logo requirement — user clarification 2026-10-24
The mark depicts a stylized man being hit by an egg. Use the friendly simplified
head-and-shoulders drawing with the egg and small impact strokes near the head,
keeping the dark slate, teal and yellow palette. The same composition appears
in both native window headers and the embedded Windows executable/window icon.
Original editable vector artwork: resources/duckeggs-logo.svg. Static Windows
icon: resources/DuckEggs.ico (16,24,32,48,64,128,256px). Native scalable rendering:
src/runtime/brand_mark.hpp. No extra runtime dependency is introduced.

## Compact UI and dynamic logo — user update 2026-10-24
This supersedes the earlier generous-spacing direction. Reduce empty surface area,
not legibility: main window targets600x360 logical pixels with a560x340 minimum;
Learning maps targets920x660 with an860x620 minimum. Reorganize controls and text
for these sizes, including projected maps and status/error messages. Keep English,
DPI scaling, meaningful legends and the three-layer/profile selection behavior.

The logo should feel more dynamic and distinctive: a clear backward reaction to
the incoming egg, an expressive face and a bold impact mark. Prefer a strong flat
silhouette and useful negative space over an ordinary boxed avatar. Preserve the
man-hit-by-egg subject and coherent teal/white/yellow on slate colors. Native
headers, SVG and all icon resolutions must share the same composition. No new
runtime dependencies, backend/model changes or Python modifications are allowed.
