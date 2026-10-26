cmake_minimum_required(VERSION 3.20)

project(
    DuckEggs
    VERSION 0.4.0
    DESCRIPTION "DuckEggs adaptive window placement"
    LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SCHIVA_BUILD_TESTS "Build portable and core CTest targets" ON)
set(
    SCHIVA_SQLITE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite"
    CACHE PATH "Directory containing the pinned SQLite amalgamation")
set(
    SCHIVA_JSON_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    CACHE PATH "Directory containing nlohmann/json.hpp")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(
    SCHIVA_SQLITE_SOURCE
    "${SCHIVA_SQLITE_DIR}/sqlite3.c")
set(
    SCHIVA_SQLITE_HEADER
    "${SCHIVA_SQLITE_DIR}/sqlite3.h")
set(
    SCHIVA_JSON_HEADER
    "${SCHIVA_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
if(NOT EXISTS "${SCHIVA_SQLITE_SOURCE}" OR
   NOT EXISTS "${SCHIVA_SQLITE_HEADER}")
    message(
        FATAL_ERROR
        "SQLite amalgamation not found in ${SCHIVA_SQLITE_DIR}. "
        "Set SCHIVA_SQLITE_DIR to the coordinator-supplied offline vendor path.")
endif()
if(NOT EXISTS "${SCHIVA_JSON_HEADER}")
    message(
        FATAL_ERROR
        "nlohmann/json.hpp not found under ${SCHIVA_JSON_INCLUDE_DIR}. "
        "Set SCHIVA_JSON_INCLUDE_DIR to the coordinator-supplied offline path.")
endif()

add_library(sqlite3_shiva STATIC "${SCHIVA_SQLITE_SOURCE}")
set_source_files_properties(
    "${SCHIVA_SQLITE_SOURCE}" PROPERTIES LANGUAGE C)
target_include_directories(sqlite3_shiva PUBLIC "${SCHIVA_SQLITE_DIR}")
target_compile_definitions(
    sqlite3_shiva
    PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_LOAD_EXTENSION=1
        SQLITE_DQS=0)
if(WIN32)
    target_compile_definitions(sqlite3_shiva PRIVATE SQLITE_OS_WIN=1)
endif()

add_library(nlohmann_json INTERFACE)
target_include_directories(
    nlohmann_json INTERFACE "${SCHIVA_JSON_INCLUDE_DIR}")

file(
    GLOB_RECURSE SCHIVA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
if(NOT SCHIVA_CORE_SOURCES)
    message(
        FATAL_ERROR
        "The frozen core sources are absent. Integrate the core worker's "
        "src/core/*.cpp files before configuring this target.")
endif()
add_library(schiva_core STATIC ${SCHIVA_CORE_SOURCES})
target_include_directories(
    schiva_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(schiva_core PUBLIC nlohmann_json sqlite3_shiva)
if(WIN32)
    target_compile_definitions(
        schiva_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

add_library(
    schiva_platform STATIC
    src/platform/capture.cpp
    src/platform/import.cpp
    src/platform/platform_common.cpp)
target_include_directories(
    schiva_platform PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${SCHIVA_SQLITE_DIR}")
target_link_libraries(
    schiva_platform PUBLIC schiva_core nlohmann_json sqlite3_shiva)
if(WIN32)
    target_sources(schiva_platform PRIVATE src/platform/windows.cpp)
    target_compile_definitions(
        schiva_platform PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE
            _WIN32_WINNT=0x0A00)
    target_link_libraries(schiva_platform PRIVATE shcore)
    target_link_libraries(schiva_platform PRIVATE normaliz)
elseif(APPLE)
    find_library(SCHIVA_APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(SCHIVA_CORE_GRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(SCHIVA_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(schiva_platform PRIVATE src/platform/macos.mm)
    set_source_files_properties(
        src/platform/macos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(
        schiva_platform PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF)
    target_link_libraries(
        schiva_platform PRIVATE
            "${SCHIVA_APPLICATION_SERVICES_FRAMEWORK}"
            "${SCHIVA_CORE_GRAPHICS_FRAMEWORK}"
            "${SCHIVA_FOUNDATION_FRAMEWORK}")
    # ScreenCaptureKit is only used behind a macOS 14 runtime availability
    # check. Weak linking keeps the Catalina Intel deployment floor intact.
    target_link_options(
        schiva_platform INTERFACE "LINKER:-weak_framework,ScreenCaptureKit")
else()
    target_sources(schiva_platform PRIVATE src/platform/unsupported.cpp)
endif()

add_library(
    schiva_runtime STATIC
    src/runtime/heatmap.cpp
    src/runtime/runtime.cpp)
target_include_directories(
    schiva_runtime PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")
target_link_libraries(schiva_runtime PUBLIC schiva_platform)
if(WIN32)
    target_compile_definitions(
        schiva_runtime PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# runtime.cpp calls into the UI while the UI uses the runtime. Linking two
# static archives with that cycle is not portable across linkers, so keep the
# UI object on every consumer's link line directly.
if(APPLE)
    find_library(SCHIVA_APPKIT_FRAMEWORK AppKit REQUIRED)
    add_library(schiva_ui OBJECT src/runtime/macos_ui.mm)
    set_source_files_properties(
        src/runtime/macos_ui.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(
        schiva_ui PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF)
    target_link_libraries(schiva_ui PUBLIC schiva_runtime PRIVATE
        "${SCHIVA_APPKIT_FRAMEWORK}"
        "${SCHIVA_APPLICATION_SERVICES_FRAMEWORK}"
        "${SCHIVA_CORE_GRAPHICS_FRAMEWORK}")
else()
    add_library(schiva_ui OBJECT src/runtime/win32_ui.cpp)
    target_link_libraries(schiva_ui PUBLIC schiva_runtime)
endif()
target_include_directories(
    schiva_ui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    target_compile_definitions(
        schiva_ui PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE
            _WIN32_WINNT=0x0A00)
    target_link_libraries(
        schiva_ui PRIVATE
            user32 gdi32 gdiplus comctl32 shell32 dwmapi uxtheme ole32 advapi32)
endif()

add_executable(DuckEggs src/main.cpp)
if(WIN32)
    target_sources(DuckEggs PRIVATE resources/DuckEggs.rc)
endif()
target_link_libraries(
    DuckEggs PRIVATE schiva_ui schiva_runtime schiva_platform)
set_target_properties(
    DuckEggs PROPERTIES
    OUTPUT_NAME "DuckEggs"
    WIN32_EXECUTABLE TRUE)
if(APPLE)
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "DuckEggs")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.gdiciocco.DuckEggs")
    set(MACOSX_BUNDLE_BUNDLE_NAME "DuckEggs")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "6")
    configure_file(
        resources/macos/Info.plist.in
        "${CMAKE_CURRENT_BINARY_DIR}/DuckEggs-Info.plist"
        @ONLY)

    set(DUCKEGGS_MACOS_ICONSET_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/DuckEggs.iconset")
    set(DUCKEGGS_MACOS_ICNS
        "${CMAKE_CURRENT_BINARY_DIR}/DuckEggs.icns")
    add_custom_command(
        OUTPUT "${DUCKEGGS_MACOS_ICNS}"
        COMMAND /bin/bash
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_macos_icon.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/duckeggs-logo-master.png"
            "${DUCKEGGS_MACOS_ICONSET_DIR}"
            "${DUCKEGGS_MACOS_ICNS}"
        DEPENDS
            tools/build_macos_icon.sh
            resources/duckeggs-logo-master.png
        VERBATIM)
    set_source_files_properties(
        "${DUCKEGGS_MACOS_ICNS}"
        resources/duckeggs-logo-256.png
        PROPERTIES
            GENERATED TRUE
            HEADER_FILE_ONLY TRUE
            MACOSX_PACKAGE_LOCATION Resources)
    set(DUCKEGGS_BUNDLE_NOTICES
        LICENSE
        THIRD_PARTY.md
        vendor/PROVENANCE.txt
        vendor/sqlite/LICENSE.SQLite.txt
        vendor/nlohmann/LICENSE.nlohmann-json.MIT
        vendor/LICENSE.Chromium.BSD)
    set_source_files_properties(
        ${DUCKEGGS_BUNDLE_NOTICES}
        PROPERTIES
            HEADER_FILE_ONLY TRUE
            MACOSX_PACKAGE_LOCATION Resources/licenses)
    target_sources(
        DuckEggs PRIVATE
            "${DUCKEGGS_MACOS_ICNS}"
            resources/duckeggs-logo-256.png
            ${DUCKEGGS_BUNDLE_NOTICES})
    set_target_properties(
        DuckEggs PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_BINARY_DIR}/DuckEggs-Info.plist"
            MACOSX_BUNDLE_ICON_FILE DuckEggs.icns
            MACOSX_BUNDLE_GUI_IDENTIFIER
                "${MACOSX_BUNDLE_GUI_IDENTIFIER}"
            MACOSX_BUNDLE_BUNDLE_NAME "${MACOSX_BUNDLE_BUNDLE_NAME}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING
                "${MACOSX_BUNDLE_SHORT_VERSION_STRING}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${MACOSX_BUNDLE_BUNDLE_VERSION}")
endif()
if(SCHIVA_BUILD_TESTS)
    include(CTest)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_platform.cpp")
        add_executable(test_platform tests/test_platform.cpp)
        target_link_libraries(
            test_platform PRIVATE schiva_ui schiva_runtime schiva_platform)
        add_test(NAME test_platform COMMAND test_platform)
    endif()

    if(WIN32)
        add_executable(
            test_win32_icons
            tests/test_win32_icons.cpp
            resources/DuckEggs.rc)
        target_include_directories(
            test_win32_icons PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_compile_definitions(
            test_win32_icons PRIVATE
                NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
        target_link_libraries(test_win32_icons PRIVATE user32 gdi32)
        add_test(NAME test_win32_icons COMMAND test_win32_icons)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_core.cpp")
        add_executable(test_core tests/test_core.cpp)
        target_link_libraries(test_core PRIVATE schiva_core)
        add_test(NAME test_core COMMAND test_core)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_manual_selection.cpp")
        add_executable(
            test_manual_selection tests/test_manual_selection.cpp)
        target_link_libraries(
            test_manual_selection PRIVATE
                schiva_ui schiva_runtime schiva_platform)
        add_test(NAME test_manual_selection COMMAND test_manual_selection)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_runner.cpp")
        add_executable(parity_runner tests/parity_runner.cpp)
        target_link_libraries(parity_runner PRIVATE schiva_core)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_parity.py" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/core-parity.json")
            add_test(
                NAME core_parity
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_parity.py"
                    --runner "$<TARGET_FILE:parity_runner>"
                    --fixture
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/core-parity.json")
        endif()
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_runtime.cpp")
        add_executable(
            benchmark_runtime tests/benchmark_runtime.cpp)
        target_link_libraries(
            benchmark_runtime PRIVATE
                schiva_ui schiva_runtime schiva_platform)
    endif()

    if(APPLE)
        add_executable(test_macos tests/test_macos.cpp)
        target_include_directories(
            test_macos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(
            test_macos PRIVATE
                schiva_core
                "${SCHIVA_CORE_GRAPHICS_FRAMEWORK}")
        add_test(NAME test_macos COMMAND test_macos)
        add_test(
            NAME native_probe
            COMMAND "$<TARGET_FILE:DuckEggs>" --probe)
    elseif(NOT WIN32)
        add_test(NAME unsupported_native_probe COMMAND DuckEggs --probe)
        set_tests_properties(
            unsupported_native_probe PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

if(APPLE)
    install(TARGETS DuckEggs BUNDLE DESTINATION .)
else()
    install(TARGETS DuckEggs RUNTIME DESTINATION .)
endif()
install(
    FILES
        README.md
        CONTRIBUTING.md
        LICENSE
        THIRD_PARTY.md
    DESTINATION .)
if(WIN32)
    install(FILES Start-DuckEggs.cmd Stop-DuckEggs.cmd DESTINATION .)
endif()
install(
    FILES
        vendor/PROVENANCE.txt
        vendor/sqlite/LICENSE.SQLite.txt
        vendor/nlohmann/LICENSE.nlohmann-json.MIT
        vendor/LICENSE.Chromium.BSD
    DESTINATION licenses)
install(
    FILES
        docs/DEVELOPING.md
        docs/MACOS.md
        docs/RELEASE-NOTES-v0.4.0-alpha.6.md
    DESTINATION docs)
install(
    FILES resources/duckeggs-logo-256.png
    DESTINATION resources)

set(CPACK_PACKAGE_NAME "DuckEggs")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}-alpha.6")
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME
        "DuckEggs-${PROJECT_VERSION}-alpha.6-windows-x64")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64;arm64")
        set(CPACK_PACKAGE_FILE_NAME
            "DuckEggs-${PROJECT_VERSION}-alpha.6-macos-universal")
    else()
        set(CPACK_PACKAGE_FILE_NAME
            "DuckEggs-${PROJECT_VERSION}-alpha.6-macos-intel")
    endif()
else()
    set(CPACK_PACKAGE_FILE_NAME
        "DuckEggs-${PROJECT_VERSION}-alpha.6-linux-core")
endif()
set(CPACK_GENERATOR "ZIP")
set(CPACK_SOURCE_GENERATOR "ZIP")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
    "DuckEggs-${PROJECT_VERSION}-alpha.6-source")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(
    CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/out/"
    "/build/"
    "\\\\.zip$")
include(CPack)
