# Third-party notices

The coordinator supplies the following pinned offline files. They are not
modified by platform code.

## SQLite

- Files: `vendor/sqlite/sqlite3.c`, `vendor/sqlite/sqlite3.h`
- Project: SQLite
- Version: 3.54.1
- License: public domain

SQLite is in the public domain. See the upstream SQLite source distribution and
the coordinator's provenance record for the exact pin and hash. This project
compiles the amalgamation as a static library and does not load extensions.

## nlohmann/json

- File: `vendor/nlohmann/json.hpp`
- Project: JSON for Modern C++
- Copyright: (c) 2013-2025 Niels Lohmann
- License: MIT

The coordinator supplies nlohmann/json 3.12.0 and its upstream license text.
The library is used as a header-only compile-time dependency. It is not exposed
over a network service and performs no telemetry.

Schiva Adattivo C++ itself is licensed under the MIT license in
[LICENSE](LICENSE).
