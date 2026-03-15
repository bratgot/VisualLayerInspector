# Visual Layer Inspector

**A thumbnail browser for multi-layer EXR files in Nuke.**

Browse every AOV on a node as a visual grid. Click a thumbnail to switch the Viewer. Built as a native C++ NDK plugin for Nuke 14 and Nuke 16.

> Created by Marten Blumen

---

## Features

- **Thumbnail grid** — every layer/AOV rendered as a browsable thumbnail
- **Click to view** — click any thumbnail to switch the active Viewer to that layer
- **Type AOV sorting** — auto-categorises layers into Lighting, Utility, Data, Cryptomatte, and Custom groups
- **Category filters** — show/hide entire layer types with checkboxes
- **Text filter** — search by name (e.g. "spec", "depth", "crypto")
- **Proxy modes** — Full Quality, 2x, 4x, 8x for fast browsing of heavy EXRs
- **Resizable thumbnails** — drag the size slider to fit more layers or see more detail
- **Shuffle Export** — shift+click to pin layers, then batch-create Shuffle2 nodes for all selected
- **Selection highlights** — pink border shows the layer you're viewing, blue border shows pinned layers for export
- **Auto-rescan** — reconnect to a different input and the grid rebuilds automatically
- **Viewer restore** — the Viewer returns to its original channel when the inspector closes
- **Modeless** — Nuke stays fully interactive while the inspector is open
- **Configurable from the properties panel** — proxy, sort mode, thumbnail size, and category filters all available as node knobs

## Sort Modes

| Mode | Description |
|------|-------------|
| **Type AOV** | Groups layers by category (Lighting → Utility → Data → Cryptomatte → Custom) |
| **Alphabetical** | A–Z by layer name |
| **Channel Count** | Most channels first |
| **Original Order** | As they appear in the EXR |

## Layer Categories

| Category | Matches |
|----------|---------|
| **Lighting** | diffuse, specular, reflection, emission, sss, indirect, direct, albedo, coat... |
| **Utility** | depth, normal, position, motion, uv, ao, fresnel, curvature... |
| **Data** | id, mask, matte, object, material, puzzle... |
| **Cryptomatte** | crypto* |
| **Custom** | everything else |

## Installation

### Quick install (Windows)

Download the latest release zip and double-click **install.bat**.

### Manual install

1. Create `.nuke/VisualLayerInspector/` with `nuke14/` and `nuke16/` subfolders
2. Copy the correct DLL into each subfolder
3. Copy `init.py` to your `.nuke/` folder (auto-detects Nuke version)
4. Copy `menu.py` to your `.nuke/` folder (adds Filter menu entry + F4 shortcut)
5. Restart Nuke — the node appears under **Filter > VisualLayerInspector**

See [INSTALL.md](INSTALL.md) for full details.

## Usage

1. Connect any multi-layer node (e.g. an EXR Read) to the VisualLayerInspector input
2. Set Proxy, Sort, and Category preferences in the properties panel
3. Click **Launch Inspector**
4. Click thumbnails to switch the Viewer
5. Shift+click to pin layers for batch Shuffle Export
6. Close the inspector — the Viewer returns to the original channel

## Building from Source

### Prerequisites

- Visual Studio 2022 (MSVC)
- CMake
- Nuke 14.1+ and/or Nuke 16.0+ with DDImage SDK
- Qt 5.15 SDK (for Nuke 14) and/or Qt 6.5 SDK (for Nuke 16)

### Build

Run from a **x64 Native Tools Command Prompt for VS 2022**:

```
.\build_all.bat
```

This builds both Nuke 14 and Nuke 16 versions, installs locally to `~/.nuke/VisualLayerInspector/`, and creates a distributable zip in `dist/`.

Configure paths at the top of `build_all.bat`:

```batch
set NUKE14_DIR=C:/Program Files/Nuke14.1v8
set NUKE16_DIR=C:/Program Files/Nuke16.0v8
set QT5_SDK=C:/Qt/5.15.2/msvc2019_64
set QT6_SDK=C:/Qt/6.5.3/msvc2019_64
```

### Repository Structure

```
├── src/
│   ├── InspectorDialog.h          ← Dialog UI + thumbnail grid
│   ├── InspectorDialog.cpp
│   └── VisualLayerInspector.cpp   ← Nuke Op + NDK integration
├── python/
│   └── visual_layer_inspector.py  ← Legacy Python version (reference only)
├── dist_files/
│   ├── init.py                    ← Startup config for distribution
│   └── menu.py                    ← Menu entry for distribution
├── CMakeLists.txt
├── build_all.bat                  ← Multi-version build + package script
├── install.bat                    ← End-user installer
├── INSTALL.md                     ← Installation guide
└── README.md
```

### Git Branches

| Branch | Target |
|--------|--------|
| `main` | Nuke 16 (Qt 6) |
| `nuke/14.1` | Nuke 14 (Qt 5) |

Both branches share the same source — the build script auto-switches between them.

## Changelog

### v1.9.0
- Full properties panel with proxy, sort, thumbnail size, and category knobs
- Shuffle Export — shift+click to pin layers, batch-create Shuffle2 nodes
- Pink/blue selection highlights (viewing vs pinned)
- Two-phase init: fast layer scan, deferred render setup
- Progressive thumbnail rendering with per-frame repaint
- Viewer channel auto-restores on close
- Auto-rescan when input changes
- Thumbnail resolution matches slider max (400px)
- Fresh grid container on every rebuild — no overlap bugs

### v18.x (pre-release)
- Auto-thumbnail generation on launch
- setUpdatesEnabled batching for smooth UI
- Geometry-only slider drag
- Placeholder thumbnails during generation
- Category filter checkboxes with All button
- Persistent buttons surviving sort operations
- Instant sort via button repositioning
- Modeless dialog with live Viewer updates

### Earlier versions
- v17: Instant sort, All button, empty state
- v16: All button, empty state message
- v15: Category filter checkboxes
- v12: Smooth grid reflow during slider drag
- v11: Modeless window, live Viewer, Python version
- v9: Smooth thumbnail slider
- v8: Auto-scan + auto-generate, stop/resume
- v7: User-driven scan + generate workflow

## Requirements

- Nuke 14.1+ or Nuke 16.0+
- Windows (macOS/Linux not currently supported)

## License

All rights reserved. Contact the author for licensing enquiries.
