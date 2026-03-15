# ============================================================================
# CMakeLists.txt — Visual Layer Inspector, Nuke 16 NDK plugin
#
# Usage (Windows):
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64 ^
#       -DNUKE_INSTALL_DIR="C:/Program Files/Nuke16.0v8" ^
#       -DQT6_SDK_DIR="C:/Qt/6.7.2/msvc2022_64"
#   cmake --build . --config Release
#
# Usage (Linux/macOS):
#   mkdir build && cd build
#   cmake .. -DNUKE_INSTALL_DIR=/usr/local/Nuke16.0v8
#   cmake --build . --config Release
#
# The resulting .dll / .so / .dylib goes into your ~/.nuke or NUKE_PATH.
#
# IMPORTANT — Qt on Windows:
#   Nuke 16 ships Qt 6 DLLs but NOT the cmake config files, headers, or
#   import libraries needed to compile against them.  You must install a
#   matching standalone Qt 6 SDK (from the Qt online installer) and pass
#   its path via -DQT6_SDK_DIR.  At runtime the plugin will use Nuke's
#   own Qt 6 DLLs since Nuke loads them first.
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(VisualLayerInspector LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
#  Nuke install path — set via -DNUKE_INSTALL_DIR=... or environment
# --------------------------------------------------------------------------
if(NOT DEFINED NUKE_INSTALL_DIR)
    if(DEFINED ENV{NUKE_INSTALL_DIR})
        set(NUKE_INSTALL_DIR $ENV{NUKE_INSTALL_DIR})
    else()
        message(FATAL_ERROR
            "NUKE_INSTALL_DIR not set. Pass -DNUKE_INSTALL_DIR=/path/to/Nuke16.0vX")
    endif()
endif()

message(STATUS "Nuke install dir: ${NUKE_INSTALL_DIR}")

# --------------------------------------------------------------------------
#  Nuke DDImage headers
# --------------------------------------------------------------------------
set(NUKE_INCLUDE_DIR "${NUKE_INSTALL_DIR}/include")

if(NOT EXISTS "${NUKE_INCLUDE_DIR}/DDImage/Op.h")
    message(FATAL_ERROR
        "Cannot find DDImage headers at ${NUKE_INCLUDE_DIR}/DDImage/Op.h. "
        "Check NUKE_INSTALL_DIR.")
endif()

# --------------------------------------------------------------------------
#  DDImage library
# --------------------------------------------------------------------------
find_library(DDIMAGE_LIBRARY
    NAMES DDImage
    PATHS "${NUKE_INSTALL_DIR}" "${NUKE_INSTALL_DIR}/lib"
    NO_DEFAULT_PATH
)

if(NOT DDIMAGE_LIBRARY)
    message(FATAL_ERROR "Cannot find DDImage library in ${NUKE_INSTALL_DIR}")
endif()
message(STATUS "DDImage library: ${DDIMAGE_LIBRARY}")

# --------------------------------------------------------------------------
#  Qt — auto-detect Qt5 or Qt6 based on which SDK is provided
# --------------------------------------------------------------------------
#
# Pass -DQT5_SDK_DIR=... for Nuke 14 (Qt5) builds
# Pass -DQT6_SDK_DIR=... for Nuke 16 (Qt6) builds
#
# build_all.bat passes the correct one for each branch.
# --------------------------------------------------------------------------

# Pick up from environment if not passed on command line
if(NOT DEFINED QT5_SDK_DIR AND DEFINED ENV{QT5_SDK_DIR})
    set(QT5_SDK_DIR $ENV{QT5_SDK_DIR})
endif()
if(NOT DEFINED QT6_SDK_DIR AND DEFINED ENV{QT6_SDK_DIR})
    set(QT6_SDK_DIR $ENV{QT6_SDK_DIR})
endif()

if(DEFINED QT5_SDK_DIR)
    # ── Qt 5 path (Nuke 14) ──
    message(STATUS "Qt5 SDK dir: ${QT5_SDK_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${QT5_SDK_DIR}")
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    message(STATUS "Found Qt5: ${Qt5Core_VERSION}")
    set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)

elseif(DEFINED QT6_SDK_DIR)
    # ── Qt 6 path (Nuke 16) ──
    message(STATUS "Qt6 SDK dir: ${QT6_SDK_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${QT6_SDK_DIR}")
    find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
    message(STATUS "Found Qt6: ${Qt6Core_VERSION}")
    set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)

elseif(NOT WIN32)
    # ── Linux / macOS: try Nuke's bundled Qt ──
    if(APPLE)
        file(GLOB NUKE_APP "${NUKE_INSTALL_DIR}/*.app")
        if(NUKE_APP)
            list(APPEND CMAKE_PREFIX_PATH "${NUKE_APP}/Contents/Frameworks")
            list(APPEND CMAKE_PREFIX_PATH "${NUKE_APP}/Contents/lib/cmake")
        endif()
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${NUKE_INSTALL_DIR}/lib/cmake")
    list(APPEND CMAKE_PREFIX_PATH "${NUKE_INSTALL_DIR}")
    # Try Qt6 first, fall back to Qt5
    find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
    if(Qt6_FOUND)
        message(STATUS "Found Qt6: ${Qt6Core_VERSION}")
        set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)
    else()
        find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
        message(STATUS "Found Qt5: ${Qt5Core_VERSION}")
        set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)
    endif()

else()
    message(FATAL_ERROR
        "Neither QT5_SDK_DIR nor QT6_SDK_DIR is set.\n"
        "Pass -DQT5_SDK_DIR=... for Nuke 14 or -DQT6_SDK_DIR=... for Nuke 16.")
endif()

# Enable MOC for Q_OBJECT macros
set(CMAKE_AUTOMOC ON)

# --------------------------------------------------------------------------
#  tinyexr — header-only EXR parser for fast layer scanning
# --------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        release
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(tinyexr)
if(NOT tinyexr_POPULATED)
    FetchContent_Populate(tinyexr)
endif()

# --------------------------------------------------------------------------
#  Plugin target
# --------------------------------------------------------------------------
add_library(VisualLayerInspector SHARED
    src/VisualLayerInspector.cpp
    src/InspectorDialog.cpp
    src/InspectorDialog.h
    src/exr_inspector.cpp
    src/exr_inspector.h
    ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c
)

target_include_directories(VisualLayerInspector PRIVATE
    "${NUKE_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${tinyexr_SOURCE_DIR}
    ${tinyexr_SOURCE_DIR}/deps/miniz
)

target_link_libraries(VisualLayerInspector PRIVATE
    ${DDIMAGE_LIBRARY}
    ${QT_LIBS}
)

# Nuke plugins need specific defines
target_compile_definitions(VisualLayerInspector PRIVATE
    FN_EXAMPLE_PLUGIN          # suppresses some DDImage warnings
    USE_GLEW                   # Nuke's GL requirement
    _USE_MATH_DEFINES          # required by Nuke 16 DDImage headers on MSVC
    EXR_INSPECTOR_TINYEXR_ONLY=1  # use tinyexr only — no OpenEXR C Core dependency
)

# --------------------------------------------------------------------------
#  Platform-specific settings
# --------------------------------------------------------------------------
if(APPLE)
    set_target_properties(VisualLayerInspector PROPERTIES
        SUFFIX ".dylib"
        INSTALL_RPATH "@loader_path"
    )
    target_link_options(VisualLayerInspector PRIVATE
        -undefined dynamic_lookup   # DDImage symbols resolved at load time
    )
elseif(UNIX)
    set_target_properties(VisualLayerInspector PROPERTIES
        SUFFIX ".so"
        INSTALL_RPATH "$ORIGIN"
    )
    target_compile_options(VisualLayerInspector PRIVATE -fPIC)
elseif(WIN32)
    set_target_properties(VisualLayerInspector PROPERTIES
        SUFFIX ".dll"
    )
    target_compile_definitions(VisualLayerInspector PRIVATE
        _WIN32
        NOMINMAX
    )
    # Prevent MSVC linker from stripping the static Op::Description object
    target_link_options(VisualLayerInspector PRIVATE
        /OPT:NOREF
    )
endif()

# --------------------------------------------------------------------------
#  Install — drop the plugin into a standard Nuke plugin path
# --------------------------------------------------------------------------
install(TARGETS VisualLayerInspector
    LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/nuke_plugins"
    RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/nuke_plugins"
)

message(STATUS "")
message(STATUS "=== Build configured ===")
message(STATUS "  Plugin:  VisualLayerInspector")
message(STATUS "  Nuke:    ${NUKE_INSTALL_DIR}")
if(DEFINED QT5_SDK_DIR)
    message(STATUS "  Qt SDK:  ${QT5_SDK_DIR}")
elseif(DEFINED QT6_SDK_DIR)
    message(STATUS "  Qt SDK:  ${QT6_SDK_DIR}")
endif()
message(STATUS "  Output:  ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "")
message(STATUS "After building, copy the .dll/.so/.dylib to ~/.nuke/ or a NUKE_PATH directory.")
