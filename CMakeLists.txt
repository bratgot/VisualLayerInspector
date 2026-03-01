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
project(VisualLayerInspector LANGUAGES CXX)

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
#  Qt 6 — platform-specific discovery
# --------------------------------------------------------------------------
#
# Windows:
#   Nuke bundles Qt 6 DLLs but no cmake configs / headers / import libs.
#   We require a standalone Qt 6 SDK for compiling. Set QT6_SDK_DIR to the
#   compiler-specific prefix, e.g. C:/Qt/6.7.2/msvc2022_64
#
# Linux / macOS:
#   Nuke typically ships Qt with cmake configs in its lib tree, or we fall
#   back to a system / standalone Qt.
# --------------------------------------------------------------------------
if(WIN32)
    # --- Windows: use standalone Qt 6 SDK ---
    if(NOT DEFINED QT6_SDK_DIR)
        if(DEFINED ENV{QT6_SDK_DIR})
            set(QT6_SDK_DIR $ENV{QT6_SDK_DIR})
        else()
            message(FATAL_ERROR
                "QT6_SDK_DIR not set.\n"
                "Nuke 16 on Windows ships Qt 6 DLLs but not the headers or cmake\n"
                "config files needed to build C++ plugins that use Qt.\n\n"
                "Install the Qt 6 online installer (https://www.qt.io/download-qt-installer)\n"
                "and select the MSVC 2022 64-bit component. Then pass:\n"
                "  -DQT6_SDK_DIR=\"C:/Qt/6.7.2/msvc2022_64\"\n"
                "(adjust the version to match what Nuke 16.0v8 ships).")
        endif()
    endif()

    if(NOT EXISTS "${QT6_SDK_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
        message(FATAL_ERROR
            "Cannot find Qt6Config.cmake in ${QT6_SDK_DIR}/lib/cmake/Qt6/\n"
            "Check that QT6_SDK_DIR points to the correct Qt prefix, e.g.\n"
            "  C:/Qt/6.7.2/msvc2022_64")
    endif()

    message(STATUS "Qt6 SDK dir: ${QT6_SDK_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${QT6_SDK_DIR}")

else()
    # --- Linux / macOS: try Nuke's bundled Qt first, then system ---
    if(APPLE)
        file(GLOB NUKE_APP "${NUKE_INSTALL_DIR}/*.app")
        if(NUKE_APP)
            list(APPEND CMAKE_PREFIX_PATH "${NUKE_APP}/Contents/Frameworks")
            list(APPEND CMAKE_PREFIX_PATH "${NUKE_APP}/Contents/lib/cmake")
        endif()
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${NUKE_INSTALL_DIR}/lib/cmake")
    list(APPEND CMAKE_PREFIX_PATH "${NUKE_INSTALL_DIR}")
endif()

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
message(STATUS "Found Qt6: ${Qt6Core_VERSION}")
set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)

# Enable MOC for Q_OBJECT macros
set(CMAKE_AUTOMOC ON)

# --------------------------------------------------------------------------
#  Python 3 — Nuke embeds Python; we need the headers to call PyGILState /
#  PyRun_SimpleString. At runtime the functions resolve from Nuke's Python.
# --------------------------------------------------------------------------
# Try Nuke's bundled Python first, then fall back to system Python
set(NUKE_PYTHON_INCLUDE "${NUKE_INSTALL_DIR}/include")
if(EXISTS "${NUKE_PYTHON_INCLUDE}/Python.h")
    message(STATUS "Using Nuke's bundled Python headers: ${NUKE_PYTHON_INCLUDE}")
    set(PYTHON_INCLUDE_DIR "${NUKE_PYTHON_INCLUDE}")
    set(PYTHON_LINK_LIBS "")   # symbols resolve from Nuke at runtime
else()
    find_package(Python3 COMPONENTS Development REQUIRED)
    message(STATUS "Found Python3: ${Python3_VERSION} at ${Python3_INCLUDE_DIRS}")
    set(PYTHON_INCLUDE_DIR "${Python3_INCLUDE_DIRS}")
    if(WIN32)
        # On Windows we need the import lib for PyGILState etc.
        set(PYTHON_LINK_LIBS Python3::Python)
    else()
        set(PYTHON_LINK_LIBS "")   # Linux/macOS resolve from Nuke
    endif()
endif()

# --------------------------------------------------------------------------
#  Plugin target
# --------------------------------------------------------------------------
add_library(VisualLayerInspector SHARED
    src/VisualLayerInspector.cpp
    src/InspectorDialog.cpp
    src/InspectorDialog.h
)

target_include_directories(VisualLayerInspector PRIVATE
    "${NUKE_INCLUDE_DIR}"
    "${PYTHON_INCLUDE_DIR}"
)

target_link_libraries(VisualLayerInspector PRIVATE
    ${DDIMAGE_LIBRARY}
    ${QT_LIBS}
    ${PYTHON_LINK_LIBS}
)

# Nuke plugins need specific defines
target_compile_definitions(VisualLayerInspector PRIVATE
    FN_EXAMPLE_PLUGIN          # suppresses some DDImage warnings
    USE_GLEW                   # Nuke's GL requirement
    _USE_MATH_DEFINES          # required by Nuke 16 DDImage headers on MSVC
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
if(WIN32)
    message(STATUS "  Qt SDK:  ${QT6_SDK_DIR}")
endif()
message(STATUS "  Output:  ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "")
message(STATUS "After building, copy the .dll/.so/.dylib to ~/.nuke/ or a NUKE_PATH directory.")
