# Contributing

Schiva Adattivo C++ keeps a strict ownership boundary:

- `include/schiva/types.hpp`, `include/schiva/core.hpp`, `src/core/**`,
  `tests/test_core.cpp`, `tests/parity_runner.cpp` and core fixtures belong to
  the core worker.
- This platform change owns `include/schiva/platform.hpp`,
  `include/schiva/runtime.hpp`, `include/schiva/heatmap.hpp`,
  `src/platform/**`, `src/runtime/**`, `src/main.cpp`, platform tests, build
  files, launchers, CI and user-facing notices.
- Do not duplicate or redefine the frozen core types or controller interface.

Keep all safety checks fail-closed. Never add network operation, telemetry,
autostart, registry changes, Python/Qt/server runtimes, keyboard hooks, window
activation, resizing, reordering, or content/key logging. Do not add a fallback
mover that bypasses the core's committed trajectory.

Persist only bounded aggregate learning state and title-free status. Pixels and
cursor traces must not be persisted. The Python reference and live database are
read-only external inputs used only for behavior review and an explicit backup
import.

Before submitting, use an offline, pinned vendor tree and run:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Portable tests must not capture the desktop, move a real window or require a
specific browser page. Label MSVC, dependency inspection, packaging and live
Windows checks separately from source inspection and mocked tests.
