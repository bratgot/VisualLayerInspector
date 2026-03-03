# Visual Layer Inspector for Nuke

A fast, interactive thumbnail grid for browsing EXR layers and AOVs in Foundry Nuke. Click any layer to instantly switch the Viewer — no more scrolling through channel dropdowns.

![Version](https://img.shields.io/badge/version-v11-green)
![Nuke](https://img.shields.io/badge/Nuke-14%20%7C%2016-blue)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)

## Features

- **Thumbnail grid** of every layer/AOV in your EXR, rendered progressively
- **Click to view** — switches the active Viewer to that layer instantly
- **Modeless window** — Nuke stays fully interactive while the inspector is open
- **Smart sorting** — sort by Type Group, Alphabetical, Channel Count, or Original Order
- **Type Group auto-categorisation** — layers are classified into Lighting, Utility, Data, Cryptomatte, and Custom based on naming conventions
- **Filter** — type to search across layer names in real time
- **Adjustable thumbnail size** — smooth slider resizes live during drag
- **Proxy modes** — Full, 2x, 4x, 8x for fast browsing of heavy EXRs
- **Stop / Resume** — pause thumbnail generation at any time
- **Channel count badges** — each layer shows its channel count (e.g. `[3ch]`, `[4ch]`)

## Layer Categories (Type Group Sort)

| Category | Matches |
|---|---|
| **Lighting** | diffuse, specular, reflection, emission, sss, albedo, shadow, gi, glossy... |
| **Utility** | depth, normal, position, motion, uv, ao, fresnel, curvature, opacity... |
| **Data** | id, mask, matte, object, material, puzzle, holdout... |
| **Cryptomatte** | crypto* |
| **Custom** | everything else |

## Installation

### C++ Plugin (recommended)

The C++ version uses the NDK Row API for direct pixel access — no temp files, no render nodes.

1. Clone this repo or download the latest release
2. Run `build_all.bat` from a Visual Studio Developer Command Prompt
3. Restart Nuke

The build script compiles for both Nuke 14 and 16 and installs to `~/.nuke/VisualLayerInspector/`.

### Python Version

The Python version works without compiling but uses nuke.execute() to render thumbnails via temporary JPEG files.

1. Copy `src/visual_layer_inspector.py` to `~/.nuke/`
2. Add to your `menu.py`:

```python
import visual_layer_inspector
nuke.menu('Nuke').addCommand(
    'Tools/Visual Layer Inspector',
    'visual_layer_inspector.launch()',
    'ctrl+shift+l'
)
```

3. Restart Nuke

## Usage

### C++ Version
1. Create a **VisualLayerInspector** node (found under Filter)
2. Connect it to any node with multiple layers (e.g. a Read node with a multi-layer EXR)
3. Open the node properties and click **Launch Inspector**

### Python Version
1. Select a node with layers
2. Run **Tools > Visual Layer Inspector** from the menu (or `Ctrl+Shift+L`)

## Building from Source

### Requirements

- Visual Studio 2022 (Community or higher)
- Nuke 14 and/or Nuke 16 NDK

### Build

Open a **Developer Command Prompt for VS 2022** and run:

```
cd C:\dev\VisualLayerInspector
build_all.bat
```

The script:
- Checks out the `nuke/14.1` branch, compiles the Nuke 14 DLL
- Checks out the `main` branch, compiles the Nuke 16 DLL
- Copies both DLLs and the Python file to `~/.nuke/`

### Branch Structure

| Branch | Purpose |
|---|---|
| `main` | Nuke 16 source (Qt6, Python 3.11+) |
| `nuke/14.1` | Nuke 14 source (Qt5, Python 3.9) |

Both branches share the same source files. The build system handles API differences automatically.

## File Structure

```
src/
├── InspectorDialog.h       # Qt dialog — UI, grid, sorting, slider
├── InspectorDialog.cpp     # Dialog implementation
├── VisualLayerInspector.cpp # Nuke NDK plugin — Op, Row API renderer
└── visual_layer_inspector.py # Python version (standalone)
```

## Changelog

### v11
- Modeless window — Nuke stays fully interactive, Viewer updates live when clicking layers

### v10
- Sort dropdown with 5 modes (A→Z, Z→A, Type Group, Channel Count, Original Order)
- Auto-categorisation of layers into Lighting, Utility, Data, Cryptomatte, Custom
- Group headers in Type Group sort mode
- Channel count badges on each layer button

### v9
- Smooth thumbnail size slider — resizes in-place during drag, reflows grid on release

### v8
- Auto-scan and auto-generate — dialog opens, scans layers, starts rendering automatically
- No manual button clicks needed

### v7
- User-driven workflow with exec() modal dialog
- Fixed UI freeze caused by knob_changed blocking Qt event loop
- Version number visible in title bar, badge, and footer

## Technical Notes

### Why modeless with show()?

Nuke's `knob_changed` callback blocks the Qt event loop. Earlier versions used `exec()` to create a nested event loop (which solved the freeze), but this made the Viewer unresponsive to layer changes. v11 uses `show()` with heap allocation and `WA_DeleteOnClose` — the dialog constructor does zero Nuke work, and `showEvent` defers all initialization via `singleShot(0)` so `knob_changed` returns immediately.

### Row API with strided fetching (C++)

The C++ renderer reads pixels directly from the input Iop using the Row API, sampling every Nth row and column to produce thumbnails. This avoids creating temporary Nuke nodes or writing files to disk, and keeps memory usage minimal (~240 rows per thumbnail regardless of source resolution).

### Progressive rendering

Thumbnails render one at a time via `QTimer::singleShot(1)` chaining. Each render completes, updates its button icon, then yields back to the event loop before starting the next one. This keeps the UI responsive throughout — you can filter, resize, stop, or click layers while rendering continues in the background.

## Credits

Created by **Marten Blumen**

## License

MIT.
