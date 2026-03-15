# Visual Layer Inspector v2.0.0 — Installation

## What's in this package

```
VisualLayerInspector/
├── nuke14/VisualLayerInspector.dll   ← C++ plugin for Nuke 14.x
├── nuke16/VisualLayerInspector.dll   ← C++ plugin for Nuke 16.x
├── init.py                           ← Nuke startup config (auto-detects version)
├── menu.py                           ← Adds node to Filter menu
├── install.bat                       ← Automatic installer
└── INSTALL.md                        ← This file
```

## Quick install (Windows)

Double-click **install.bat** — it copies everything to the right place automatically.

## Manual install

1. Create a folder: `C:\Users\<YourName>\.nuke\VisualLayerInspector\`
   - Copy `nuke14\VisualLayerInspector.dll` into a `nuke14` subfolder
   - Copy `nuke16\VisualLayerInspector.dll` into a `nuke16` subfolder

2. Copy the included `init.py` to your `.nuke` folder (if you don't already have one).
   It auto-detects your Nuke version and loads the correct DLL.

3. Copy the included `menu.py` to your `.nuke` folder (if you don't already have one).
   If you already have a `menu.py`, add this line to it:
   ```python
   toolbar = nuke.toolbar("Nodes")
   toolbar.addCommand("Filter/VisualLayerInspector", "nuke.createNode('VisualLayerInspector')", "F4", icon="Viewer.png")
   ```

4. Restart Nuke. The node appears under **Filter > VisualLayerInspector**.

## Usage

1. Select any node with multiple layers/AOVs (e.g. an EXR Read node).
2. Create a VisualLayerInspector node and connect it.
3. Set your preferred Proxy, Sort, and Category options in the properties panel.
4. Click **Launch Inspector** to open the thumbnail grid.
5. Click any thumbnail to switch the active Viewer to that layer.

## Features

- Thumbnail grid of every layer/AOV on a node
- Click to switch Viewer to that layer
- Sort by type group, alphabetical, channel count, or original order
- Filter by name and category (Lighting, Utility, Data, Cryptomatte, Custom)
- Proxy modes for fast browsing of heavy EXRs
- Resizable thumbnails via slider
- Settings configurable from the Nuke properties panel
- Modeless — Nuke stays fully interactive

## Requirements

- Nuke 14.1+ or Nuke 16.0+
- Windows only (macOS/Linux not currently supported)

---
Created by Marten Blumen
