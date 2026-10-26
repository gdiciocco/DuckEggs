DuckEggs 0.4.0-alpha.5 adapts its heatmaps when the visible content changes within the same app, while retaining saved learning and layout preferences. It also corrects cursor-triggered escapes that could begin by moving toward the pointer and then stop after a short displacement.

[Download the portable Windows x64 ZIP](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.5/DuckEggs-0.4.0-alpha.5-windows-x64.zip)

## Changes

- A confirmed content transition starts a temporary in-memory activity map. Existing manual preferences, the 12-feature LinUCB model and SQLite data remain compatible. Continuous scrolling and animated video do not repeatedly confirm new contexts in the automated scenarios.
- Cursor-triggered escape routes preserve pointer clearance. Negative return feedback starts after arrival, and a content transition preserves a committed movement while discarding feedback for the previous content.
- Windows visual capture remains eligible closer to the target: its controls exclusion margin is now 20 pixels instead of 160. Capture remains suppressed while the target moves, mouse buttons are held, or DuckEggs is paused.
- The main window uses a fixed 600 by 360 logical-pixel client area. Learning maps remains resizable. Application icon framing reduces transparent border space around the approved artwork.

## Validation

Release builds and all five CTest suites passed on Windows x64 and Linux. Automated coverage includes static-content cursor approaches, limited workspace, moving the pointer away during an escape, content changes during movement, A -> B -> A, synthetic scrolling/video, and preservation of saved preferences and learning. Controller replay references follow the new automatic trajectories while preserving deliberate manual moves and the other mathematical reference sections.

These are automated and synthetic checks; the revised motion and capture behavior still needs live desktop feedback. This early alpha is unsigned. Automatic discovery remains Chrome-only; other eligible windows require Choose Window.

## Updating

Close the old instance, extract the entire ZIP, then run DuckEggs.exe or Start-DuckEggs.cmd. Existing data remains in %LOCALAPPDATA%\DuckEggs; do not delete that folder to update. DuckEggs starts paused. If you enabled Start with Windows and move the executable, turn that option off and on again from its new location.

Visual captures are processed in memory and are not saved as screenshots. There is no analytics collection. The temporary content map does not persist across restarts and does not identify or permanently remember individual documents.
