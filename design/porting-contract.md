# Schiva C++ port contract — 2026-10-24

Coordinator scopes/reviews/integrates/builds/tests/packages. ALL source code, tests,
build files, scripts and code fixes are written exclusively by DeepSeek workers.
Workers do assigned work directly, never delegate, publish, push, deploy, or touch
real desktop input/capture/window movement. No alteration of the Python reference
at /home/deffie/schiva-adaptive (HEAD ff9c1e6), its Windows export or live database.
Existing evidence: /mnt/c/Users/deffie/Documents/Codex/2026-10-23/esi/work/
smooth-motion-evidence.json plus adaptive/heatmaps-clearance/smooth-motion contracts.
This contract supersedes implementation-language details in older contracts only.

## Product and architecture

Full C++20 port with functional parity to Python 0.3.0: ONE adaptive mode, genuine
shared 12-feature LinUCB; per-app/role/layout 24x14 activity/manual maps; decay,
delayed measured/censored feedback; full-footprint scoring, >=65% vacated area when
safe feasible, maximum feasible fallback, safe committed time-based eased motion.
Inner20 controls/held buttons/manual drag/helper interaction/pause stop moves;
35ms trigger, 250ms hover exit/cooldown, 800ms manual grace. Safety outranks speed.
Preserve interpolation acknowledgement, route/segment cursor guards and stale
identity protections. Target120Hz moving/60Hz idle/15Hz paused, deadline scheduling,
no busy loop or systemwide timer changes. Capture isolated bounded one-worker
latest mailbox <=2Hz, fresh-once frames, generation invalidation, no pixel storage.
Thread owner exclusively observes/core/SQLite/moves; GUI only detached snapshots.

Portable standard C++ core, thin native adapters. Only Windows native implemented;
Linux/macOS core/testing/help/status supported with honest unsupported native error.
Native Win32 controls + GDI heatmap, no Qt/Electron/webview/server/interpreter.
CMake>=3.20, C++20, CTest, static MSVC runtime (/MT), x64 Windows portable binary.
Only bundled pinned SQLite amalgamation (public domain) and nlohmann/json3.12.0
(MIT). Coordinator sources vendor files/licenses/provenance; no FetchContent,
network builds, package manager runtime dependencies, external DLLs or telemetry.
All own code MIT, THIRD_PARTY notices, CMake presets/CI/docs. Version0.4.0-cpp.1.

## Frozen public core interface (core worker writes these headers)

include/schiva/types.hpp, namespace schiva:
using Json=nlohmann::json (include <nlohmann/json.hpp>).
struct Point { int x{},y{}; default equality; };
struct Rect { int left{},top{},right{},bottom{}; int width() const;
 int height() const; Rect moved_to(int left,int top) const; default equality; };
struct WindowInfo { std::string window_id,app_id,role; Rect rect; };
struct VisualFrame { int width{},height{}; std::vector<std::uint8_t> pixels;
 Rect bounds; std::vector<Rect> excluded; };
struct Observation { Point cursor; bool buttons_down{};
 std::optional<WindowInfo> pip,foreground; Rect work_area;
 std::vector<WindowInfo> windows; std::optional<VisualFrame> visual_frame;
 std::optional<Rect> caret; bool input_active{}; double input_age{};
 std::vector<Rect> interaction_exclusions; };
struct Decision { std::string state; std::optional<Point> move_to;
 std::optional<std::string> pip_id; std::string reason;
 std::optional<Rect> recommendation; std::optional<std::string> context_key;
 int learning_count{}; };

include/schiva/core.hpp, namespace schiva, PIMPL to isolate detail:
class LearningStore { explicit LearningStore(const std::filesystem::path&);
 ~LearningStore(); Json stats() const; void flush(); void close();
 noncopyable; implementation helpers/private access for Controller worker free. };
class Controller { explicit Controller(LearningStore&,bool observe_only=false);
 ~Controller(); bool paused() const; void set_paused(bool);
 Decision tick(double monotonic_seconds,const Observation&);
 void movement_failed(); Json status() const;
 Json insights(std::optional<std::string> profile_key=std::nullopt) const;
 void close(); noncopyable; all calls same constructing owner thread. };
No Windows headers/API inside core. status/insights JSON schema same as Python.
Storage SQLite schema1 and JSON states compatible, reject newer/corrupt safely
without resetting, bounded100profiles/1000records, batched writes, finite aggregates.
Geometry helpers used by native may be implemented privately by native worker,
not a dependency on unagreed core internal symbols. Keep duplicate safety helper
small and test it meaningfully. Optional read-only fixture runner emits JSON.

## Runtime/native requirements

Windows strict visible top-level chrome.exe + Chrome_WidgetWin_1 exact localized
PiP titles as Python, Unicode conversion/case/whitespace handled; ambiguous fail
closed. Physical coordinates, per-monitor DPI awareness. PID+creation time+class+
title checked; cache process identity only with safe expiry/handle reuse defense.
Unique full validation at motion start, <=200ms fullscan lease revalidation,
cheap target checks each frame, fresh rect/cursor/buttons just before SetWindowPos.
Never activate/resize/reorder/close user window. No keyboard hooks/content logging.
Retain real foreground context when helper/PiP focused. Helper rect masks and input
exclusions. GUI reusable heatmap selector/layers/legend/null gray/hover value+full
candidate footprint/current/PiP/recommendation outlines; projection and cold state
honest. Viewer read-only, refresh~1Hz only when open, deferred while moving.

CLI: --help --version --status --stop --probe --start-paused --resume
 --observe-only --no-visual --no-gui --duration N --data-dir PATH
 --import-python PATH. No --mode. Default GUI startup PAUSED for safe side-by-side
evaluation. --resume is explicit active startup. --probe no capture/move or data
creation. --status/help/version never create native backend/data. Import is an
explicit SQLite backup into EMPTY SEPARATE C++ data dir, source read-only, no
overwrite of destination learning DB, reject samepath/invalid/newer schema; stop
before activating. Isolate default data dir SchivaAdaptiveCpp (XDG schiva-cpp,
macOS SchivaCpp), own status.json/mutex/stop event/hotkeys. If Python named mutex
Local\\SchivaAdaptive.Instance.v1 exists, refuse resume/movement while showing
clear reason; never stop Python on user's behalf. Check before each active tick
and initial resume; don't contend with live Python. No autostart/registry/network.
Fail safely on SQL/capture/move errors, controls responsive. Stdout CLI should
work; console-subsystem EXE acceptable (window can be hidden only normal GUI start
with an attached console preserved for CLI). No installation requirement.

## Ownership / integration

CORE worker owns include/schiva/types.hpp, include/schiva/core.hpp, src/core/**,
tests/test_core.cpp, tests/parity_runner.cpp, tools/export_reference.py,
tools/check_parity.py, tests/fixtures/**, design/core-port-notes.md. May create
internal headers in src/core. Do not change root/CMake/native/runtime/docs/vendor.
Use Python reference source directly read-only and fixture generation, not import
into production executable. Add substantive parity/regression tests: LinUCB known
state+matrix validation+persistence; normalized maps/decay/integrated whole-rect;
visual temporal/burst/mask reset; controller50ms reaction/smoothness/65%clearance/
holds/stalls/cursor/context/manual/outcomes/censoring/observe-only; roundtrip a COPY
of real backup and Python-schema fixture. Null-insights means unknown, never zero.

PLATFORM worker owns CMakeLists.txt,CMakePresets.json,.github/**,.gitignore,
src/platform/**,src/runtime/**,src/main.cpp,include/schiva/platform.hpp,
include/schiva/runtime.hpp,tests/test_platform.cpp,README.md,CONTRIBUTING.md,LICENSE,
THIRD_PARTY.md, launchers and optional platform-only headers/tests. Do NOT edit
core interfaces/core sources/vendor. CMake schiva_core static with all src/core/*.cpp,
vendored static sqlite3.c, json INTERFACE target, executable Schiva-Adattivo-Cpp,
test_core executable tests/test_core.cpp, test_platform tests/test_platform.cpp,
parity_runner tests/parity_runner.cpp optional exists. Standard tests use simple
assert/check harness no frameworks/downloads. Vendor expected vendor/sqlite/sqlite3.c
and .h; vendor/nlohmann/json.hpp, includes vendor for <nlohmann/json.hpp>.
Optional CMake switch SCHIVA_BUILD_TESTS defaults ON. Windows API links systemlibs
only. No downloaded toolchain. Windows MSVC2022 installed locally, CMake installed;
WSL has no C++ compiler. Coordinator runs Windows builds/tests and copies review
fix requests back to DeepSeek. Do not run actual UI/capture/movement from workers.

Acceptance: complete build and tests under MSVCx64, meaningful Python/C++ fixture
comparison, isolated import backup counts+model/maps roundtrip, static-runtime
portable executable dependency inspection/size/hash, smoke --help/status/probe
and --no-gui --start-paused --no-visual --duration .3 with TEMP data only. No
switching off or editing running Python, no real movement/capture tests. Package
separate C++ source and portable Windows ZIP with notices/docs; no claim of live
browser animation validation. Coordinator verifies original Python file hashes.
