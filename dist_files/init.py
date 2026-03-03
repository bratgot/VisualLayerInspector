import nuke
import os

_vli_dir = os.path.join(os.path.expanduser("~"), ".nuke", "VisualLayerInspector")
_nuke_major = nuke.NUKE_VERSION_MAJOR

if _nuke_major >= 16:
    nuke.pluginAddPath(os.path.join(_vli_dir, "nuke16"))
elif _nuke_major >= 14:
    nuke.pluginAddPath(os.path.join(_vli_dir, "nuke14"))
