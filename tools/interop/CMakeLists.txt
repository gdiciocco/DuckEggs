cmake_minimum_required(VERSION 3.20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
project(DuckEggsInteropTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DUCKEGGS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    CACHE PATH "DuckEggs C++ source root")
set(DUCKEGGS_CORE_LIBRARY "" CACHE FILEPATH "Existing schiva_core static library")
set(DUCKEGGS_SQLITE_LIBRARY "" CACHE FILEPATH "Existing sqlite3_shiva static library")

foreach(required IN ITEMS
        "${DUCKEGGS_SOURCE_ROOT}/include/schiva/core.hpp"
        "${DUCKEGGS_SOURCE_ROOT}/src/core/internal.hpp"
        "${DUCKEGGS_SOURCE_ROOT}/vendor/nlohmann/json.hpp"
        "${DUCKEGGS_SOURCE_ROOT}/vendor/sqlite/sqlite3.h")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Required DuckEggs source is missing: ${required}")
    endif()
endforeach()
foreach(required IN ITEMS "${DUCKEGGS_CORE_LIBRARY}" "${DUCKEGGS_SQLITE_LIBRARY}")
    if(NOT IS_ABSOLUTE "${required}" OR NOT EXISTS "${required}")
        message(FATAL_ERROR "Set an absolute existing static library path: ${required}")
    endif()
endforeach()

add_executable(inspect_backup inspect_backup.cpp)
target_include_directories(inspect_backup PRIVATE
    "${DUCKEGGS_SOURCE_ROOT}/include"
    "${DUCKEGGS_SOURCE_ROOT}/src/core"
    "${DUCKEGGS_SOURCE_ROOT}/vendor"
    "${DUCKEGGS_SOURCE_ROOT}/vendor/sqlite")
target_link_libraries(inspect_backup PRIVATE
    "${DUCKEGGS_CORE_LIBRARY}"
    "${DUCKEGGS_SQLITE_LIBRARY}")
if(MSVC)
    target_compile_options(inspect_backup PRIVATE /W4 /permissive- /utf-8)
endif()
