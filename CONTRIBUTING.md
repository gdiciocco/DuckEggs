# Contributing to DuckEggs

DuckEggs keeps a strict safety and ownership boundary.

- Treat `include/schiva/types.hpp`, `include/schiva/core.hpp`, `src/core/**`,
  the core tests and the parity fixtures as frozen integration interfaces.
- Keep the namespace and header paths `schiva` for source compatibility.
- Do not add network operation, telemetry, autostart, registry changes,
  Python/Qt/server runtimes, keyboard hooks, window activation, resizing,
  reordering, or content/key logging.
- Do not add a movement path that bypasses the core's committed trajectory.
- Keep all visible product text in English and use the DuckEggs name.
- Persist only bounded aggregate learning state and title-free status. Pixels
  and cursor traces must not be persisted.
- The Python reference and live database are read-only external inputs. Only
  explicit backup import may read them.

Before submitting a Windows change, use the pinned offline vendor tree and run:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

On Linux, the portable core and unsupported-native checks are available with:

```sh
cmake --preset linux-core
cmake --build --preset linux-core --parallel
ctest --preset linux-core
```

Portable tests must not capture the desktop, move a real window or require a
specific browser page. Report source inspection, automated tests, compilation,
packaging and hardware validation separately.
