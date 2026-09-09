cmake_minimum_required(VERSION 3.20)
project(dsptop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- OS / Arch detection (used for matrix) ----
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION} Arch: ${CMAKE_SYSTEM_PROCESSOR}")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(DSPTOP_PLATFORM "macOS")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(DSPTOP_ARCH "arm64")
    else()
        set(DSPTOP_ARCH "x64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DSPTOP_PLATFORM "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(DSPTOP_ARCH "arm64")
    else()
        set(DSPTOP_ARCH "x64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(DSPTOP_PLATFORM "Windows")
    # MSVC reports AMD64 even on ARM64 emulation; use CMAKE_VS_PLATFORM_NAME if available
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(DSPTOP_ARCH "arm64")
    else()
        set(DSPTOP_ARCH "x64")
    endif()
endif()
message(STATUS "DSPTOP Platform: ${DSPTOP_PLATFORM} Arch: ${DSPTOP_ARCH}")
add_compile_definitions(DSPTOP_PLATFORM="${DSPTOP_PLATFORM}" DSPTOP_ARCH="${DSPTOP_ARCH}")

# ---- Options ----
option(DSPTOP_BUILD_TESTS "Build tests" OFF)
option(DSPTOP_MOCK "Force mock backend" OFF)

# ---- Source ----
set(DSPTOP_SOURCES
    src/main.cpp
    src/hal/hal.cpp
    src/tui/tui.cpp
    src/ci/ci_mode.cpp
    src/daemon/daemon.cpp
)

# Platform-specific HAL
if(DSPTOP_PLATFORM STREQUAL "macOS")
    list(APPEND DSPTOP_SOURCES src/hal/darwin_ane.cpp)
elseif(DSPTOP_PLATFORM STREQUAL "Linux")
    list(APPEND DSPTOP_SOURCES src/hal/linux_dsp.cpp)
elseif(DSPTOP_PLATFORM STREQUAL "Windows")
    list(APPEND DSPTOP_SOURCES src/hal/windows_npu.cpp)
endif()

# Fallback mock is already in hal.cpp

add_executable(dsptop ${DSPTOP_SOURCES})
target_include_directories(dsptop PRIVATE include)

# ---- Compiler flags ----
if(MSVC)
    target_compile_options(dsptop PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(dsptop PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # Windows Console VT: no extra flags, but ensure Unicode
    add_compile_definitions(UNICODE _UNICODE)
else()
    target_compile_options(dsptop PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(dsptop PRIVATE -fcolor-diagnostics)
    endif()
endif()

# ---- Platform libs ----
if(DSPTOP_PLATFORM STREQUAL "macOS")
    # IOKit for ANE telemetry + Foundation
    find_library(IOKIT IOKit)
    find_library(FOUNDATION Foundation)
    if(IOKIT)
        target_link_libraries(dsptop PRIVATE ${IOKIT} ${FOUNDATION})
        target_compile_definitions(dsptop PRIVATE HAS_IOKIT=1)
        message(STATUS "Found IOKit: ${IOKIT}")
    else()
        message(WARNING "IOKit not found — falling back to powermetrics parsing only")
    endif()
elseif(DSPTOP_PLATFORM STREQUAL "Linux")
    # Link pthread, filesystem, sysfs needs no extra lib
    find_package(Threads REQUIRED)
    target_link_libraries(dsptop PRIVATE Threads::Threads)
    # stdc++fs for GCC <9
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
        target_link_libraries(dsptop PRIVATE stdc++fs)
    endif()
elseif(DSPTOP_PLATFORM STREQUAL "Windows")
    target_link_libraries(dsptop PRIVATE pdh wbemuuid setupapi ws2_32)
    # Static runtime control for MSVC matrix
    if(MSVC)
        set_property(TARGET dsptop PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ---- Install ----
install(TARGETS dsptop RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION share/doc/dsptop OPTIONAL)

# ---- Daemon binary (standalone, reuses main --daemon) ----
# Instead of a separate daemon binary, we alias dsptop as dsptopd for now
# to avoid duplicate main. If a dedicated daemon main is desired, create src/daemon_main.cpp.
# For matrix CI we only need the main dsptop binary.
# add_executable(dsptopd src/daemon/daemon.cpp src/hal/hal.cpp src/ci/ci_mode.cpp) # disabled: no main

# ---- Summary ----
message(STATUS "=== DSPTOP Build Summary ===")
message(STATUS "  Platform: ${DSPTOP_PLATFORM} (${DSPTOP_ARCH})")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Sources: ${DSPTOP_SOURCES}")
