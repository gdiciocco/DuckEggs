# Third-party notices

DuckEggs is an independent C++ port of the Schiva adaptive project. The
vendored dependencies below are pinned offline and are not modified by
DuckEggs platform code.

## SQLite 3.54.1

- Files: `vendor/sqlite/sqlite3.c`, `vendor/sqlite/sqlite3.h`,
  `vendor/sqlite/sqlite3ext.h`
- Origin:
  <https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip>
- Official release listing: <https://www.sqlite.org/download.html>
- Published archive SHA3-256:
  `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`
- Staged source SHA256:
  `0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52`
  for `sqlite3.c` and
  `d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c`
  for `sqlite3.h`

SQLite 3.54.1 is dedicated to the public domain by its authors. SQLite does
not require a license. Official copyright and licensing information:
<https://sqlite.org/copyright.html>.

The amalgamation is compiled as a static library without extension loading.

## nlohmann/json 3.12.0

- File: `vendor/nlohmann/json.hpp`
- Origin:
  <https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp>
- Release:
  <https://github.com/nlohmann/json/releases/tag/v3.12.0>
- SHA256:
  `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`
- License: MIT, copyright (c) 2013-2025 Niels Lohmann

The library is used as a header-only compile-time dependency. It performs no
network operation and no telemetry.

Full MIT license text:

```text
MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The same complete license and provenance files are installed in the portable
package's `licenses/` directory.

## Chromium PiP title data

- Files: `design/chromium-pip-titles.json`,
  `src/platform/chromium_pip_titles.hpp`
- Origin:
  <https://chromium.googlesource.com/chromium/src/+/41020d875c4518f25b5b27a76c7d418235a50f31/chrome/app/resources/generated_resources_%7Blocale%7D.xtb>
- Chromium symbol: `IDS_PICTURE_IN_PICTURE_TITLE_TEXT`
- GRIT message ID: `7750228210027921155`
- Frozen dataset SHA256:
  `320691b61bb7e156f414a8b345904bc025756c4a7fbbc553e9ecf7b4bff7532c`

The dataset records the exact English source text and titles for 81 locales,
the source commit, and a SHA256 for each locale's XTB resource. Unicode is
preserved as published by Chromium. The strings are used only to identify
Chrome's Picture-in-Picture window, with the compatibility rules documented in
[docs/DEVELOPING.md](docs/DEVELOPING.md).

Chromium is distributed under the BSD 3-Clause license. The complete license
text is installed in the portable package as
`licenses/LICENSE.Chromium.BSD`.

DuckEggs itself is licensed under the MIT license in [LICENSE](LICENSE).
