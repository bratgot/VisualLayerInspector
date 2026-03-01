# menu.py — Add Visual Layer Inspector to Nuke's node menu
# Drop this into your ~/.nuke/ directory (or merge with your existing menu.py)

import nuke

toolbar = nuke.toolbar("Nodes")
toolbar.addCommand("Filter/VisualLayerInspector", "nuke.createNode('VisualLayerInspector')",
                   icon="Viewer.png")
