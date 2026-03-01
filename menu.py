import nuke

toolbar = nuke.toolbar("Nodes")
toolbar.addCommand("Filter/VisualLayerInspector", "nuke.createNode('VisualLayerInspector')",
                   icon="Viewer.png")
