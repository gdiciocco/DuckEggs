# DuckEggs v0.4.0-alpha.4

This alpha adds the approved DuckEggs artwork, clearer window-placement
language, and the temporary topmost behavior that keeps a bound target visible
while DuckEggs is active.

[Download DuckEggs 0.4.0-alpha.4 for Windows](https://github.com/gdiciocco/DuckEggs/releases/download/v0.4.0-alpha.4/DuckEggs-0.4.0-alpha.4-windows-x64.zip)

## What's new

- The approved DuckEggs artwork now generates the application icon and preview
  resources from one checked-in master. The ICO includes DPI-relevant sizes
  from 16 through 256 pixels, and window and tray icons refresh at the
  appropriate display scale.
- **Choose Window** replaces the PiP-specific manual-selection label. The
  product text now describes general window placement while automatic
  discovery remains limited to Chrome picture-in-picture.
- While a target is bound, DuckEggs temporarily makes it topmost, including
  while paused. Returning to **Automatic**, choosing another window, or exiting
  restores its previous topmost state; a window that was already topmost stays
  topmost.
- The root README and developer notes now describe the general-purpose manual
  flow and the temporary topmost cleanup behavior.

## Window selection

DuckEggs starts paused. For Chrome, open picture-in-picture and click
**Resume**. To target another eligible window, click **Choose Window**, click
the window, then click **Resume**. Hover over a target to see its title and a
teal outline. Full-screen and maximized windows appear gray and cannot be
selected. Press **Esc**, right-click, or click **Cancel** to leave the current
target unchanged.

Manual selection lasts only for the current session. Automatic mode recognizes
Chrome's official picture-in-picture title in all 81 Chrome locale titles.

## Early alpha

DuckEggs requires Windows 10 or Windows 11, 64-bit. The interface is in English.
Other browsers and applications are available through explicit manual
selection but are not yet live-validated as compatibility guarantees. This
build is unsigned and may have rough edges and bugs.

Learning data stays on your PC in `%LOCALAPPDATA%\DuckEggs`. DuckEggs does not
include analytics or screenshots. Manual selections and transient hover
details remain in memory for the current session.

A forced process termination or crash can prevent topmost-state cleanup. A
normal return to **Automatic**, target change, or clean exit performs that
cleanup.

If something does not work as expected,
[open a GitHub issue](https://github.com/gdiciocco/DuckEggs/issues).

DuckEggs is available under the [MIT License](https://github.com/gdiciocco/DuckEggs/blob/main/LICENSE).
