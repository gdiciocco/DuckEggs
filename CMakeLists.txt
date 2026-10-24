cmake_minimum_required(VERSION 3.20)

project(
    DuckEggs
    VERSION 0.4.0
    DESCRIPTION "DuckEggs adaptive Chrome PiP placement"
    LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
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
add_library(schiva_ui OBJECT src/runtime/win32_ui.cpp)
target_include_directories(
    schiva_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")
target_link_libraries(schiva_ui PUBLIC schiva_runtime)
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
            user32 gdi32 gdiplus comctl32 shell32 dwmapi uxtheme ole32)
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
if(SCHIVA_BUILD_TESTS)
    include(CTest)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_platform.cpp")
        add_executable(test_platform tests/test_platform.cpp)
        target_link_libraries(
            test_platform PRIVATE schiva_ui schiva_runtime schiva_platform)
        add_test(NAME test_platform COMMAND test_platform)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_core.cpp")
        add_executable(test_core tests/test_core.cpp)
        target_link_libraries(test_core PRIVATE schiva_core)
        add_test(NAME test_core COMMAND test_core)
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

    if(NOT WIN32)
        add_test(NAME unsupported_native_probe COMMAND DuckEggs --probe)
        set_tests_properties(
            unsupported_native_probe PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

install(TARGETS DuckEggs RUNTIME DESTINATION .)
install(
    FILES
        README.md
        CONTRIBUTING.md
        LICENSE
        THIRD_PARTY.md
        Start-DuckEggs.cmd
        Stop-DuckEggs.cmd
    DESTINATION .)
install(
    FILES
        vendor/PROVENANCE.txt
        vendor/sqlite/LICENSE.SQLite.txt
        vendor/nlohmann/LICENSE.nlohmann-json.MIT
    DESTINATION licenses)
install(FILES docs/DEVELOPING.md DESTINATION docs)
install(
    FILES resources/duckeggs-logo-256.png
    DESTINATION resources)

set(CPACK_PACKAGE_NAME "DuckEggs")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}-alpha.2")
set(CPACK_PACKAGE_FILE_NAME
    "DuckEggs-${PROJECT_VERSION}-alpha.2-windows-x64")
set(CPACK_GENERATOR "ZIP")
set(CPACK_SOURCE_GENERATOR "ZIP")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
    "DuckEggs-${PROJECT_VERSION}-alpha.2-source")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(
    CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/out/"
    "/build/"
    "\\\\.zip$")
include(CPack)
