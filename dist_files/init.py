import nuke
import os

# Auto-load the correct VisualLayerInspector build for this Nuke version
_vli_dir = os.path.join(os.path.dirname(__file__), "VisualLayerInspector")
_nuke_major = nuke.NUKE_VERSION_MAJOR

if _nuke_major >= 16:
    _vli_path = os.path.join(_vli_dir, "nuke16")
elif _nuke_major >= 14:
    _vli_path = os.path.join(_vli_dir, "nuke14")
else:
    _vli_path = None

if _vli_path and os.path.isdir(_vli_path):
    nuke.pluginAddPath(_vli_path)
    print("Visual Layer Inspector v18.4 loaded (Nuke %d)" % _nuke_major)
