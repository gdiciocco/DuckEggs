# DuckEggs v0.4.0-alpha.3

This alpha expands Chrome picture-in-picture recognition to all 81 official
locale titles and adds an opt-in manual window chooser for other browsers and
applications.

[Download DuckEggs 0.4.0-alpha.3 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.3/DuckEggs-0.4.0-alpha.3-windows-x64.zip)

## What's new

- Previously, Chrome PiP recognition covered English and the legacy Italian
  title. DuckEggs now also matches official localized Chrome PiP titles.
- **Choose PiP...** lists eligible visible windows with their title,
  executable, and dimensions. The explicit session-only selection can target
  another browser or application. It is not guessed by size or window class.
- Opening the chooser, selecting a window, resetting to **Automatic**, or
  closing the chooser leaves DuckEggs paused. Resume remains a separate action.

## Early alpha

DuckEggs requires Windows 10 or Windows 11, 64-bit. Automatic discovery is
limited to Google Chrome and one Chrome picture-in-picture window at a time.
Other browsers and applications can be targeted only through explicit manual
selection; they are not yet live-validated as compatibility guarantees. The
interface remains in English. This build is unsigned and may have rough edges
and bugs.

Learning data stays on your PC in `%LOCALAPPDATA%\DuckEggs`. DuckEggs does not
store screenshots or include analytics. Manual window names and the candidate
list are session-only and are not persisted.

If something does not work as expected,
[open a GitHub issue](https://github.com/gdiciocco/DuckEggs/issues).

DuckEggs is available under the [MIT License](https://github.com/gdiciocco/DuckEggs/blob/main/LICENSE).
