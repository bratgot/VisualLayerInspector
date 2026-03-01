# ============================================================================
# CMakeLists.txt — Visual Layer Inspector, Nuke 14.1 NDK plugin
#
# Usage (Windows):
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64 ^
#       -DNUKE_INSTALL_DIR="C:/Program Files/Nuke14.1v8" ^
#       -DQT5_SDK_DIR="C:/Qt/5.15.2/msvc2019_64"
#   cmake --build . --config Release
#
# Usage (Linux/macOS):
#   mkdir build && cd build
#   cmake .. -DNUKE_INSTALL_DIR=/usr/local/Nuke14.1v8
#   cmake --build . --config Release
#
# IMPORTANT — Qt on Windows:
#   Nuke 14.1 ships Qt 5 DLLs but NOT the cmake config files, headers, or
#   import libraries needed to build C++ plugins that use Qt.  Install a
#   matching standalone Qt 5.15 SDK and pass its path via -DQT5_SDK_DIR.
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(VisualLayerInspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
#  Nuke install path
# --------------------------------------------------------------------------
if(NOT DEFINED NUKE_INSTALL_DIR)
    if(DEFINED ENV{NUKE_INSTALL_DIR})
        set(NUKE_INSTALL_DIR $ENV{NUKE_INSTALL_DIR})
    else()
        message(FATAL_ERROR
            "NUKE_INSTALL_DIR not set. Pass -DNUKE_INSTALL_DIR=/path/to/Nuke14.1vX")
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
#  Qt 5
# --------------------------------------------------------------------------
if(WIN32)
    # --- Windows: use standalone Qt 5 SDK ---
    if(NOT DEFINED QT5_SDK_DIR)
        if(DEFINED ENV{QT5_SDK_DIR})
            set(QT5_SDK_DIR $ENV{QT5_SDK_DIR})
        else()
            message(FATAL_ERROR
                "QT5_SDK_DIR not set.\n"
                "Nuke 14.1 ships Qt 5 DLLs but not the headers or cmake config\n"
                "files needed to build C++ plugins that use Qt.\n\n"
                "Install Qt 5.15.x from the Qt online installer and select the\n"
                "MSVC 2019 64-bit component. Then pass:\n"
                "  -DQT5_SDK_DIR=\"C:/Qt/5.15.2/msvc2019_64\"\n"
                "(adjust the version to match what Nuke 14.1 ships).")
        endif()
    endif()

    if(NOT EXISTS "${QT5_SDK_DIR}/lib/cmake/Qt5/Qt5Config.cmake")
        message(FATAL_ERROR
            "Cannot find Qt5Config.cmake in ${QT5_SDK_DIR}/lib/cmake/Qt5/\n"
            "Check that QT5_SDK_DIR points to the correct Qt prefix.")
    endif()

    message(STATUS "Qt5 SDK dir: ${QT5_SDK_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${QT5_SDK_DIR}")

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

find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
message(STATUS "Found Qt5: ${Qt5Core_VERSION}")
set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)

# Enable MOC for Q_OBJECT macros
set(CMAKE_AUTOMOC ON)

# --------------------------------------------------------------------------
#  Python 3
# --------------------------------------------------------------------------
set(NUKE_PYTHON_INCLUDE "${NUKE_INSTALL_DIR}/include")
if(EXISTS "${NUKE_PYTHON_INCLUDE}/Python.h")
    message(STATUS "Using Nuke's bundled Python headers: ${NUKE_PYTHON_INCLUDE}")
    set(PYTHON_INCLUDE_DIR "${NUKE_PYTHON_INCLUDE}")
    set(PYTHON_LINK_LIBS "")
else()
    find_package(Python3 COMPONENTS Development REQUIRED)
    message(STATUS "Found Python3: ${Python3_VERSION} at ${Python3_INCLUDE_DIRS}")
    set(PYTHON_INCLUDE_DIR "${Python3_INCLUDE_DIRS}")
    if(WIN32)
        set(PYTHON_LINK_LIBS Python3::Python)
    else()
        set(PYTHON_LINK_LIBS "")
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

target_compile_definitions(VisualLayerInspector PRIVATE
    FN_EXAMPLE_PLUGIN
    USE_GLEW
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
        -undefined dynamic_lookup
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
    target_link_options(VisualLayerInspector PRIVATE
        /OPT:NOREF
    )
endif()

# --------------------------------------------------------------------------
#  Install
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
    message(STATUS "  Qt SDK:  ${QT5_SDK_DIR}")
endif()
message(STATUS "  Output:  ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "")
message(STATUS "After building, copy the .dll/.so/.dylib to ~/.nuke/ or a NUKE_PATH directory.")
