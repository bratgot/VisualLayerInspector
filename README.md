# Visual Layer Inspector

A native C++ plugin for Nuke 14.1 that displays a thumbnail grid of every layer/AOV on a connected input. Click any thumbnail to instantly switch the active Viewer to that layer.

![Nuke 14.1](https://img.shields.io/badge/Nuke-14.1-blue) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![Qt 5](https://img.shields.io/badge/Qt-5-green) ![License: MIT](https://img.shields.io/badge/License-MIT-yellow)

> **Branch note:** This is the `nuke/14.1` branch targeting Nuke 14.1 (Qt 5). For Nuke 16 (Qt 6), see the [`main`](../../tree/main) branch.

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
├── CMakeLists.txt                # Build configuration (Qt 5)
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

- **Nuke 14.1** with NDK headers (ships by default)
- **CMake ≥ 3.16**
- **C++17 compiler** — MSVC 2022/2019, GCC 9+, or Clang 11+
- **Qt 5.15 SDK** (Windows only — see below)
- **Python 3 development headers** (Windows only)

### Windows Qt Note

Nuke 14.1 bundles Qt 5 DLLs but does not ship headers or CMake config files. Install a matching Qt 5.15 SDK from the [Qt Online Installer](https://www.qt.io/download-qt-installer) and select the **MSVC 2019 64-bit** component.

Check your Nuke's Qt version in the Script Editor:
```python
from PySide2 import QtCore; print(QtCore.qVersion())
```

## Building

### Windows

```bat
cd VisualLayerInspector
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DNUKE_INSTALL_DIR="C:/Program Files/Nuke14.1v8" ^
    -DQT5_SDK_DIR="C:/Qt/5.15.2/msvc2019_64" ^
    -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"

cmake --build . --config Release
```

Output: `build/Release/VisualLayerInspector.dll`

### Linux

```bash
cd VisualLayerInspector
mkdir build && cd build

cmake .. -DNUKE_INSTALL_DIR=/usr/local/Nuke14.1v8
cmake --build . --config Release
```

### macOS

```bash
cd VisualLayerInspector
mkdir build && cd build

cmake .. -DNUKE_INSTALL_DIR=/Applications/Nuke14.1v8/Nuke14.1v8.app/Contents/MacOS
cmake --build . --config Release
```

## Installation

1. Copy the built plugin to `~/.nuke/` or any directory on your `NUKE_PATH`:

```bat
:: Windows
copy build\Release\VisualLayerInspector.dll "%USERPROFILE%\.nuke\"

# Linux/macOS
cp build/VisualLayerInspector.so ~/.nuke/
```

2. *(Optional)* Copy `menu.py` to `~/.nuke/` to add the node to the toolbar.

3. Restart Nuke.

## Usage

1. Tab-search for **VisualLayerInspector** and create the node
2. Connect a multi-layer source (EXR with AOVs, CryptoMatte, etc.) to its input
3. Open the Properties panel and click **Launch Inspector**
4. Click any thumbnail to switch the Viewer to that layer
5. Use the filter bar to search, the slider to resize, and **Update Frame** to refresh

## Branch Strategy

| Branch | Nuke Version | Qt Version |
|---|---|---|
| `main` | 16.x | Qt 6 |
| `nuke/14.1` | 14.1.x | Qt 5 |

The source code in `src/` is identical across branches. Only `CMakeLists.txt` and `README.md` differ.

## Troubleshooting

**"plugin did not define VisualLayerInspector"** — Full clean rebuild (`rd /s /q build`).

**Thumbnails are black** — View the input in the Viewer at least once before launching.

**Qt not found (Windows)** — Pass both `-DQT5_SDK_DIR` and `-DCMAKE_PREFIX_PATH`.

**Python not found** — Pass `-DPython3_ROOT_DIR="C:/Python39"`.

## License

MIT License — see [LICENSE](LICENSE).

## Credits

Created by Marten Blumen.
