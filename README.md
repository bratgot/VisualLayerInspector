# Visual Layer Inspector

A native C++ plugin for Nuke 16 that displays a thumbnail grid of every layer/AOV on a connected input. Click any thumbnail to instantly switch the active Viewer to that layer.

![Nuke 16](https://img.shields.io/badge/Nuke-16-blue) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![Qt 6](https://img.shields.io/badge/Qt-6-green) ![License: MIT](https://img.shields.io/badge/License-MIT-yellow)

## Features

- **Thumbnail grid** — Every layer rendered as a visual preview via the NDK Tile API (no temp nodes, no disk I/O)
- **Click to view** — Click any thumbnail to set the active Viewer channel
- **Filter bar** — Type to search layers by name
- **Size slider** — Resize thumbnails from 80px to 400px
- **Update Frame** — Re-capture thumbnails at the current frame to reflect animation or upstream changes
- **Zero render cost** — NoIop pass-through; the node itself adds nothing to the render graph

## Project Structure

```
VisualLayerInspector/
├── CMakeLists.txt                # Build configuration
├── menu.py                       # Optional Nuke toolbar integration
├── .gitignore
├── LICENSE
├── README.md
└── src/
    ├── VisualLayerInspector.cpp   # NDK Op — NoIop + Tile rendering + Python C API bridge
    ├── InspectorDialog.h          # Qt dialog header
    └── InspectorDialog.cpp        # Dialog — grid, filter, slider, update frame
```

## Prerequisites

- **Nuke 16** with NDK headers (ships by default)
- **CMake ≥ 3.16**
- **C++17 compiler** — MSVC 2022, GCC 9+, or Clang 11+
- **Qt 6 SDK** (Windows only — see below)
- **Python 3 development headers** (Windows only — typically bundled with your Python install)

### Windows Qt Note

Nuke 16 bundles Qt 6.5.3 DLLs but does not ship the headers or CMake config files needed to compile against them. Install a matching Qt 6.5.3 SDK from the [Qt Online Installer](https://www.qt.io/download-qt-installer) and select the **MSVC 2022 64-bit** component.

Check your Nuke's Qt version in the Script Editor:
```python
from PySide6 import QtCore; print(QtCore.qVersion())
```

## Building

### Windows

```bat
cd VisualLayerInspector
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DNUKE_INSTALL_DIR="C:/Program Files/Nuke16.0v8" ^
    -DQT6_SDK_DIR="C:/Qt/6.5.3/msvc2019_64" ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2019_64"

cmake --build . --config Release
```

Output: `build/Release/VisualLayerInspector.dll`

### Linux

```bash
cd VisualLayerInspector
mkdir build && cd build

cmake .. -DNUKE_INSTALL_DIR=/usr/local/Nuke16.0v8
cmake --build . --config Release
```

Output: `build/VisualLayerInspector.so`

### macOS

```bash
cd VisualLayerInspector
mkdir build && cd build

cmake .. -DNUKE_INSTALL_DIR=/Applications/Nuke16.0v8/Nuke16.0v8.app/Contents/MacOS
cmake --build . --config Release
```

Output: `build/VisualLayerInspector.dylib`

## Installation

1. Copy the built plugin to `~/.nuke/` or any directory on your `NUKE_PATH`:

```bat
:: Windows
copy build\Release\VisualLayerInspector.dll "%USERPROFILE%\.nuke\"

# Linux/macOS
cp build/VisualLayerInspector.so ~/.nuke/
```

2. *(Optional)* Copy `menu.py` to `~/.nuke/` to add the node to the toolbar under **Filter → VisualLayerInspector**.

3. Restart Nuke.

## Usage

1. Tab-search for **VisualLayerInspector** and create the node
2. Connect a multi-layer source (EXR with AOVs, CryptoMatte, etc.) to its input
3. Open the Properties panel and click **Launch Inspector**
4. Click any thumbnail to switch the Viewer to that layer
5. Use the filter bar to search, the slider to resize, and **Update Frame** to refresh

## How It Works

| Component | Detail |
|---|---|
| **Node type** | `NoIop` — passes input through unchanged, zero render cost |
| **Thumbnails** | `DD::Image::Tile` API reads cached pixel data, box-downsampled in-memory to `QImage` |
| **Viewer control** | Python C API (`PyGILState_Ensure` + `PyRun_SimpleString`) calls into Nuke's embedded Python |
| **UI framework** | Qt 6 `QDialog` launched from a `knob_changed` callback |

## Troubleshooting

**"plugin did not define VisualLayerInspector"** — Do a full clean rebuild (`rd /s /q build`). The `FN_EXPORT` symbol must be present for Nuke to find the plugin.

**Thumbnails are black** — The input must be validated first. View the input in the Viewer at least once before launching the inspector.

**Qt not found (Windows)** — Pass both `-DQT6_SDK_DIR` and `-DCMAKE_PREFIX_PATH` pointing to your Qt install.

**Python not found** — Pass `-DPython3_ROOT_DIR="C:/Python311"` (adjust to your install path).

**Large images are slow** — The plugin reads full-resolution tiles. For 8K+ plates there may be a brief pause.

## License

MIT License — see [LICENSE](LICENSE).

## Credits

Created by Marten Blumen.
