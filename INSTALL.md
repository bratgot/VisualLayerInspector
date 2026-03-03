# Visual Layer Inspector v18.3 — Installation

## What's in this package

```
VisualLayerInspector/
├── nuke14/VisualLayerInspector.dll   ← C++ plugin for Nuke 14.x
├── nuke16/VisualLayerInspector.dll   ← C++ plugin for Nuke 16.x
├── visual_layer_inspector.py         ← Python version (any Nuke)
├── init.py                           ← Nuke startup config
├── menu.py                           ← Nuke menu entry
├── install.bat                       ← Automatic installer
└── INSTALL.md                        ← This file
```

## Quick install (Windows)

Double-click **install.bat** — it copies everything to the right place automatically.

## Manual install

### C++ plugin (recommended — faster thumbnails)

1. Create a folder: `C:\Users\<YourName>\.nuke\VisualLayerInspector\`
   - Copy `nuke14\VisualLayerInspector.dll` into a `nuke14` subfolder
   - Copy `nuke16\VisualLayerInspector.dll` into a `nuke16` subfolder

2. Copy the included `init.py` to your `.nuke` folder (if you don't already have one).
   It auto-detects your Nuke version and loads the correct DLL.

3. Copy the included `menu.py` to your `.nuke` folder, or if you already have one, add this line:
   ```python
   import visual_layer_inspector
   nuke.menu("Nuke").addCommand("Filter/Visual Layer Inspector (Python)", "visual_layer_inspector.launch()", "")
   ```

4. Restart Nuke. The node appears under **Filter > VisualLayerInspector**.

### Python version (no compilation needed)

1. Copy `visual_layer_inspector.py` to your `.nuke` folder.

2. If you didn't already do so above, copy the included `menu.py` to your `.nuke` folder,
   or add the menu entry to your existing `menu.py`.

3. Restart Nuke.

## Usage

1. Select any node with multiple layers/AOVs (e.g. an EXR Read node).
2. Launch the inspector from the Filter menu or the node's properties panel.
3. Thumbnails generate automatically — click any layer to switch the Viewer.

## Features

- Thumbnail grid of every layer/AOV on a node
- Click to switch Viewer to that layer
- Sort by type group, alphabetical, channel count, or original order
- Filter by name and category (Lighting, Utility, Data, Cryptomatte, Custom)
- Proxy modes for fast browsing of heavy EXRs
- Resizable thumbnails via slider
- Modeless — Nuke stays fully interactive

## Requirements

- Nuke 14.1+ or Nuke 16.0+ (C++ plugin)
- Any Nuke with Python/PySide support (Python version)
- Windows only (macOS/Linux not currently supported)

---
Created by Marten Blumen
