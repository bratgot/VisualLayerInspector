"""
Visual Layer Inspector v11 — Python version

v11: Modeless window — Nuke stays fully interactive, Viewer updates live.
     show() instead of exec_(), global reference prevents garbage collection.

Created by Marten Blumen
"""

import nuke
import os
import time

try:
    from PySide2 import QtWidgets, QtCore, QtGui
except ImportError:
    from PySide6 import QtWidgets, QtCore, QtGui

VLI_VERSION = "v11"

# Global reference to keep dialog alive (prevents GC)
_inspector_dialog = None

# ============================================================================
#  Layer classification
# ============================================================================
CATEGORY_LIGHTING    = 0
CATEGORY_UTILITY     = 1
CATEGORY_DATA        = 2
CATEGORY_CRYPTOMATTE = 3
CATEGORY_CUSTOM      = 4

CATEGORY_NAMES = {
    CATEGORY_LIGHTING:    "Lighting",
    CATEGORY_UTILITY:     "Utility",
    CATEGORY_DATA:        "Data",
    CATEGORY_CRYPTOMATTE: "Cryptomatte",
    CATEGORY_CUSTOM:      "Custom",
}

_LIGHTING_PATTERNS = [
    "diffuse", "specular", "reflect", "refract", "emission", "emissive",
    "coat", "sheen", "transmis", "sss", "subsurface", "indirect",
    "direct", "albedo", "beauty", "light", "shadow", "gi",
    "illumin", "radiance", "irradiance", "glossy", "volume",
    "scatter", "translucen", "caustic", "firefly", "aov_light",
]

_UTILITY_PATTERNS = [
    "depth", "normal", "position", "uv", "motion", "velocity",
    "fresnel", "curvature", "occlusion", "ao", "pointworld",
    "worldnormal", "worldposition", "worldpoint", "pworld",
    "nworld", "pref", "st_map", "stmap", "z_depth", "zdepth",
    "facing_ratio", "faceratio", "barycentric", "tangent",
    "opacity", "coverage", "rgba",
]

_DATA_PATTERNS = [
    "id", "mask", "matte", "object", "material", "puzzle",
    "asset", "element", "holdout",
]


def classify_layer(name):
    lower = name.lower()
    if lower.startswith("crypto"):
        return CATEGORY_CRYPTOMATTE
    for p in _LIGHTING_PATTERNS:
        if p in lower:
            return CATEGORY_LIGHTING
    for p in _UTILITY_PATTERNS:
        if p in lower:
            return CATEGORY_UTILITY
    for p in _DATA_PATTERNS:
        if p in lower:
            return CATEGORY_DATA
    return CATEGORY_CUSTOM


# ============================================================================
#  Sort modes
# ============================================================================
SORT_AZ       = 0
SORT_ZA       = 1
SORT_TYPE     = 2
SORT_CHANNELS = 3
SORT_ORIGINAL = 4


class VisualLayerPicker(QtWidgets.QDialog):

    _THUMB_DEFAULT = 200
    _THUMB_MIN     = 80
    _THUMB_MAX     = 400
    _ASPECT        = 0.6

    def __init__(self, node_name):
        super(VisualLayerPicker, self).__init__()

        self._node_name = node_name
        self.node = None

        self.setWindowTitle("Visual Layer Inspector  [{}]".format(VLI_VERSION))
        # Modeless — Qt::Window gives us a proper taskbar entry,
        # WindowStaysOnTopHint keeps us visible over Nuke
        self.setWindowFlags(QtCore.Qt.Window | QtCore.Qt.WindowStaysOnTopHint |
                            QtCore.Qt.WindowCloseButtonHint)
        self.resize(1150, 850)
        # NOT modal — Nuke stays interactive
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

        self._thumb_w = self._THUMB_DEFAULT
        self._thumb_h = int(self._THUMB_DEFAULT * self._ASPECT)
        self._proxy_factor = 4
        self._rendering = False
        self._scanned = False
        self._show_fired = False
        self._next_idx = 0
        self._render_start = 0.0
        self._sort_mode = SORT_TYPE

        self._layers = []
        self._channels_map = {}
        self._thumb_pixmaps = {}
        self._buttons = []
        self._temp_dir = ""
        self._nuke_nodes = []

        # ─── UI ───
        self.main_layout = QtWidgets.QVBoxLayout(self)

        # Title + version
        title_row = QtWidgets.QHBoxLayout()
        title = QtWidgets.QLabel("Visual Layer Inspector")
        title.setStyleSheet("font-size: 20px; font-weight: bold; color: #eeeeee;")
        title_row.addWidget(title)
        title_row.addStretch()
        version_badge = QtWidgets.QLabel(VLI_VERSION)
        version_badge.setStyleSheet(
            "font-size: 14px; font-weight: bold; color: #66bb66; "
            "background-color: #224422; padding: 3px 10px; border-radius: 3px;"
        )
        title_row.addWidget(version_badge)
        self.main_layout.addLayout(title_row)

        desc = QtWidgets.QLabel(
            "Click any layer name to view it in the Viewer. "
            "Thumbnails generate automatically — use <b>Stop</b> to pause."
        )
        desc.setStyleSheet("font-size: 13px; color: #bbbbbb; margin-bottom: 5px;")
        desc.setWordWrap(True)
        self.main_layout.addWidget(desc)

        # Controls
        controls = QtWidgets.QVBoxLayout()

        # Row 1: filter + sort + size
        row1 = QtWidgets.QHBoxLayout()

        self.filter_le = QtWidgets.QLineEdit()
        self.filter_le.setPlaceholderText("Filter layers (e.g., 'depth', 'spec')...")
        self.filter_le.setStyleSheet("font-size: 14px; padding: 5px;")
        self.filter_le.textChanged.connect(self._filter_layers)
        row1.addWidget(self.filter_le, 1)

        row1.addSpacing(10)
        row1.addWidget(QtWidgets.QLabel("Sort:"))

        self._sort_combo = QtWidgets.QComboBox()
        self._sort_combo.addItem(u"A \u2192 Z",     SORT_AZ)
        self._sort_combo.addItem(u"Z \u2192 A",     SORT_ZA)
        self._sort_combo.addItem("Type Group",       SORT_TYPE)
        self._sort_combo.addItem("Channel Count",    SORT_CHANNELS)
        self._sort_combo.addItem("Original Order",   SORT_ORIGINAL)
        self._sort_combo.setCurrentIndex(2)
        self._sort_combo.setToolTip(
            "Type Group auto-categorises layers:\n"
            "  Lighting - diffuse, specular, reflection, emission, sss...\n"
            "  Utility - depth, normal, position, motion, uv, ao...\n"
            "  Data - id, mask, matte, object, material...\n"
            "  Cryptomatte - crypto*\n"
            "  Custom - everything else"
        )
        self._sort_combo.currentIndexChanged.connect(self._on_sort_changed)
        row1.addWidget(self._sort_combo)

        row1.addSpacing(10)
        row1.addWidget(QtWidgets.QLabel("Size:"))

        self._size_slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        self._size_slider.setRange(self._THUMB_MIN, self._THUMB_MAX)
        self._size_slider.setValue(self._THUMB_DEFAULT)
        self._size_slider.setFixedWidth(140)
        self._size_slider.valueChanged.connect(self._on_size_drag)
        self._size_slider.sliderReleased.connect(self._on_size_release)
        row1.addWidget(self._size_slider)

        self._size_label = QtWidgets.QLabel("{}px".format(self._THUMB_DEFAULT))
        self._size_label.setStyleSheet("font-size: 12px; color: #999999; min-width: 45px;")
        row1.addWidget(self._size_label)

        controls.addLayout(row1)

        # Row 2: stop + proxy + regenerate
        row2 = QtWidgets.QHBoxLayout()

        self._stop_btn = QtWidgets.QPushButton("Stop")
        self._stop_btn.setFixedHeight(30)
        self._stop_btn.setMinimumWidth(90)
        self._stop_btn.setStyleSheet("font-weight: bold; background-color: #554433;")
        self._stop_btn.clicked.connect(self._on_stop_resume)
        row2.addWidget(self._stop_btn)

        row2.addSpacing(10)
        row2.addWidget(QtWidgets.QLabel("Proxy:"))

        self._proxy_combo = QtWidgets.QComboBox()
        self._proxy_combo.addItem("Full Quality", 1)
        self._proxy_combo.addItem("2x Proxy",     2)
        self._proxy_combo.addItem("4x Proxy",     4)
        self._proxy_combo.addItem("8x Proxy",     8)
        self._proxy_combo.setCurrentIndex(2)
        self._proxy_combo.currentIndexChanged.connect(self._on_proxy_changed)
        row2.addWidget(self._proxy_combo)

        row2.addSpacing(10)

        self._regen_btn = QtWidgets.QPushButton("Regenerate")
        self._regen_btn.setFixedHeight(30)
        self._regen_btn.setMinimumWidth(110)
        self._regen_btn.setStyleSheet(
            "QPushButton { font-weight: bold; background-color: #335533; }"
            "QPushButton:disabled { background-color: #333333; color: #666666; }"
        )
        self._regen_btn.setEnabled(False)
        self._regen_btn.clicked.connect(self._on_regenerate)
        row2.addWidget(self._regen_btn)

        row2.addStretch()
        controls.addLayout(row2)

        # Progress
        self._progress = QtWidgets.QProgressBar()
        self._progress.setRange(0, 0)
        self._progress.setFixedHeight(18)
        self._progress.setTextVisible(True)
        self._progress.setFormat("Scanning layers...")
        self._progress.setStyleSheet(
            "QProgressBar { border: 1px solid #444; border-radius: 3px; "
            "background: #222; text-align: center; color: #ccc; font-size: 11px; }"
            "QProgressBar::chunk { background: #446644; }"
        )
        controls.addWidget(self._progress)

        self._status_label = QtWidgets.QLabel("Scanning layers...")
        self._status_label.setStyleSheet("font-size: 11px; color: #999999;")
        controls.addWidget(self._status_label)

        self.main_layout.addLayout(controls)

        # Grid
        self.scroll = QtWidgets.QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.container = QtWidgets.QWidget()
        self.grid = QtWidgets.QGridLayout(self.container)
        self.scroll.setWidget(self.container)
        self.main_layout.addWidget(self.scroll, 1)

        # Footer
        footer = QtWidgets.QHBoxLayout()
        credit = QtWidgets.QLabel(
            u"Created by Marten Blumen  \u2022  {}".format(VLI_VERSION)
        )
        credit.setStyleSheet("font-size: 11px; color: #777777; font-style: italic;")
        footer.addWidget(credit)
        footer.addStretch()

        close_btn = QtWidgets.QPushButton("Close")
        close_btn.setFixedHeight(35)
        close_btn.setMinimumWidth(100)
        close_btn.setStyleSheet("font-weight: bold; background-color: #553333;")
        close_btn.clicked.connect(self.close)
        footer.addWidget(close_btn)

        self.main_layout.addLayout(footer)

    # ================================================================
    #  showEvent → auto-init
    # ================================================================
    def showEvent(self, event):
        super(VisualLayerPicker, self).showEvent(event)
        if not self._show_fired:
            self._show_fired = True
            QtCore.QTimer.singleShot(0, self._auto_init)

    def _auto_init(self):
        if self._scanned:
            return

        self._status_label.setText("Reading EXR headers (first open may be slow)...")
        self.setCursor(QtCore.Qt.WaitCursor)
        self.repaint()

        self.node = nuke.toNode(self._node_name)
        if not self.node:
            self.setCursor(QtCore.Qt.ArrowCursor)
            self._status_label.setText("Error: node not found")
            self._progress.setRange(0, 1)
            self._progress.setValue(0)
            self._progress.setFormat("Failed")
            return

        t0 = time.time()
        self._prepare_layers()
        scan_time = time.time() - t0

        self._scanned = True
        self.setCursor(QtCore.Qt.ArrowCursor)

        self._sort_layers()
        self._build_grid()

        cats = {}
        for le in self._layers:
            c = le['category']
            cats[c] = cats.get(c, 0) + 1
        parts = []
        for c in sorted(cats.keys()):
            parts.append("{} {}".format(cats[c], CATEGORY_NAMES[c].lower()))

        self._status_label.setText(
            "Found {} layers ({}) in {:.1f}s — generating thumbnails...".format(
                len(self._layers), ", ".join(parts), scan_time
            )
        )

        QtCore.QTimer.singleShot(1, self._begin_rendering)

    # ================================================================
    #  Layer preparation
    # ================================================================
    def _prepare_layers(self):
        channels = self.node.channels()
        layer_names = sorted(list(set(c.split('.')[0] for c in channels)))

        self._channels_map = {}
        self._layers = []

        for idx, layer in enumerate(layer_names):
            layer_chans = [c for c in channels if c.startswith(layer + '.')]
            r, g, b, a = '0', '0', '0', '1'
            for c in layer_chans:
                suffix = c.split('.')[-1].lower()
                if suffix in ('r', 'red', 'x'):     r = c
                elif suffix in ('g', 'green', 'y'): g = c
                elif suffix in ('b', 'blue', 'z'):   b = c
                elif suffix in ('a', 'alpha'):        a = c

            if r == '0' and len(layer_chans) > 0: r = layer_chans[0]
            if g == '0' and len(layer_chans) > 1: g = layer_chans[1]
            if b == '0' and len(layer_chans) > 2: b = layer_chans[2]
            if len(layer_chans) == 1:
                g = r
                b = r

            self._channels_map[layer] = {'r': r, 'g': g, 'b': b, 'a': a}

            self._layers.append({
                'name': layer,
                'original_index': idx,
                'channel_count': len(layer_chans),
                'category': classify_layer(layer),
            })

        nuke_cache = nuke.toNode('preferences').knob('DiskCachePath').evaluate()
        if not nuke_cache or not os.path.exists(nuke_cache):
            import tempfile
            nuke_cache = tempfile.gettempdir()

        self._temp_dir = os.path.join(nuke_cache, "LayerInspector_Temp").replace("\\", "/")
        if not os.path.exists(self._temp_dir):
            try:
                os.makedirs(self._temp_dir)
            except Exception as e:
                nuke.message("Could not create temp directory: {}".format(e))

    # ================================================================
    #  Sorting
    # ================================================================
    def _sort_layers(self):
        mode = self._sort_mode
        if mode == SORT_AZ:
            self._layers.sort(key=lambda x: x['name'].lower())
        elif mode == SORT_ZA:
            self._layers.sort(key=lambda x: x['name'].lower(), reverse=True)
        elif mode == SORT_TYPE:
            self._layers.sort(key=lambda x: (x['category'], x['name'].lower()))
        elif mode == SORT_CHANNELS:
            self._layers.sort(key=lambda x: (-x['channel_count'], x['name'].lower()))
        elif mode == SORT_ORIGINAL:
            self._layers.sort(key=lambda x: x['original_index'])

    # ================================================================
    #  Grid
    # ================================================================
    def _build_grid(self):
        for _, btn in self._buttons:
            btn.setParent(None)
            btn.deleteLater()
        self._buttons = []

        while self.grid.count():
            item = self.grid.takeAt(0)
            w = item.widget()
            if w:
                w.setParent(None)
                w.deleteLater()

        btn_w = self._thumb_w + 10
        btn_h = self._thumb_h + 40
        vp = self.scroll.viewport()
        cols = max(1, (vp.width() if vp else 1100) // btn_w)

        filt = self.filter_le.text().lower() if self.filter_le else ""
        show_groups = (self._sort_mode == SORT_TYPE)
        last_cat = -1
        grid_idx = 0

        for i, le in enumerate(self._layers):
            if show_groups and (i == 0 or le['category'] != last_cat):
                if grid_idx % cols != 0:
                    grid_idx += cols - (grid_idx % cols)
                header = QtWidgets.QLabel(
                    u"\u2014 {} \u2014".format(CATEGORY_NAMES[le['category']])
                )
                header.setStyleSheet(
                    "font-size: 13px; font-weight: bold; color: #88aacc; "
                    "padding: 8px 0 2px 5px;"
                )
                self.grid.addWidget(header, grid_idx // cols, 0, 1, cols)
                grid_idx += cols
                last_cat = le['category']

            name = le['name']
            btn = QtWidgets.QToolButton()
            label = name
            if le['channel_count'] > 0:
                label += "  [{}ch]".format(le['channel_count'])
            btn.setText(label)
            btn.setToolButtonStyle(QtCore.Qt.ToolButtonTextUnderIcon)
            btn.setFixedSize(btn_w, btn_h)
            btn.setIconSize(QtCore.QSize(self._thumb_w, self._thumb_h))

            pm = self._thumb_pixmaps.get(name)
            if pm and not pm.isNull():
                scaled = pm.scaled(
                    self._thumb_w, self._thumb_h,
                    QtCore.Qt.KeepAspectRatio,
                    QtCore.Qt.SmoothTransformation
                )
                btn.setIcon(QtGui.QIcon(scaled))
            else:
                placeholder = QtGui.QPixmap(self._thumb_w, self._thumb_h)
                placeholder.fill(QtGui.QColor(40, 40, 40))
                btn.setIcon(QtGui.QIcon(placeholder))

            btn.clicked.connect(lambda checked=False, l=name: self._set_layer(l))

            visible = (not filt) or (filt in name.lower())
            btn.setVisible(visible)

            self.grid.addWidget(btn, grid_idx // cols, grid_idx % cols)
            self._buttons.append((i, btn))
            grid_idx += 1

        self._progress.setRange(0, len(self._layers))
        self._progress.setValue(self._next_idx)

    # ================================================================
    #  Rendering
    # ================================================================
    def _begin_rendering(self):
        if not self._scanned or not self._layers:
            return

        self._next_idx = 0
        self._rendering = True
        self._render_start = time.time()
        self._stop_btn.setText("Stop")
        self._stop_btn.setStyleSheet("font-weight: bold; background-color: #554433;")
        self._stop_btn.setEnabled(True)
        self._regen_btn.setEnabled(False)

        self._create_render_nodes()
        self._update_progress()
        self._schedule_next()

    def _schedule_next(self):
        if self._rendering:
            QtCore.QTimer.singleShot(1, self._render_next)

    def _render_next(self):
        if not self._rendering:
            return

        total = len(self._layers)
        if self._next_idx >= total:
            self._stop_rendering()
            return

        if not self._nuke_nodes:
            self._stop_rendering()
            return

        expr, rf, wr = self._nuke_nodes
        le = self._layers[self._next_idx]
        layer = le['name']
        chans = self._channels_map[layer]

        thumb_path = os.path.join(
            self._temp_dir, "nk_thumb_{}.jpg".format(layer)
        ).replace("\\", "/")

        expr['expr0'].setValue(chans['r'])
        expr['expr1'].setValue(chans['g'])
        expr['expr2'].setValue(chans['b'])
        expr['expr3'].setValue(chans['a'])
        wr['file'].setValue(thumb_path)

        try:
            nuke.execute(wr, nuke.frame(), nuke.frame())
        except Exception as e:
            print("Skipped rendering {}: {}".format(layer, e))

        if os.path.exists(thumb_path):
            pm = QtGui.QPixmap(thumb_path)
            self._thumb_pixmaps[layer] = pm

            for layer_idx, btn in self._buttons:
                if layer_idx == self._next_idx:
                    scaled = pm.scaled(
                        self._thumb_w, self._thumb_h,
                        QtCore.Qt.KeepAspectRatio,
                        QtCore.Qt.SmoothTransformation
                    )
                    btn.setIcon(QtGui.QIcon(scaled))
                    break

        self._next_idx += 1
        self._update_progress()

        if self._next_idx >= total:
            self._stop_rendering()
        else:
            self._schedule_next()

    # ================================================================
    #  Nuke render nodes
    # ================================================================
    def _create_render_nodes(self):
        self._cleanup_render_nodes()

        proxy = self._proxy_factor
        render_w = max(60, 240 // proxy)

        expr = nuke.nodes.Expression(inputs=[self.node])
        rf = nuke.nodes.Reformat(
            inputs=[expr], type="to box",
            box_width=render_w, box_fixed=True, resize="fit"
        )
        wr = nuke.nodes.Write(
            inputs=[rf], file_type="jpeg", _jpeg_quality=0.7
        )
        self._nuke_nodes = [expr, rf, wr]
        return expr, rf, wr

    def _cleanup_render_nodes(self):
        for n in reversed(self._nuke_nodes):
            try:
                nuke.delete(n)
            except Exception:
                pass
        self._nuke_nodes = []

    # ================================================================
    #  Stop / Resume / Regenerate
    # ================================================================
    def _stop_rendering(self):
        self._rendering = False
        self._cleanup_render_nodes()
        self._regen_btn.setEnabled(True)

        total = len(self._layers)
        done = self._next_idx

        if done >= total:
            elapsed = time.time() - self._render_start
            self._status_label.setText(
                "Done — {} layers in {:.2f}s".format(total, elapsed)
            )
            self._stop_btn.setEnabled(False)
        else:
            self._status_label.setText(
                "Paused — {} / {} rendered  (click any layer name to view it)".format(
                    done, total
                )
            )
            self._stop_btn.setText("Resume")
            self._stop_btn.setStyleSheet("font-weight: bold; background-color: #335544;")

    def _on_stop_resume(self):
        if self._rendering:
            self._stop_rendering()
        else:
            if self._scanned and self._next_idx < len(self._layers):
                self._rendering = True
                self._regen_btn.setEnabled(False)
                self._stop_btn.setText("Stop")
                self._stop_btn.setStyleSheet("font-weight: bold; background-color: #554433;")
                self._stop_btn.setEnabled(True)
                self._create_render_nodes()
                self._schedule_next()

    def _on_regenerate(self):
        if not self._scanned or not self._layers:
            return
        self._stop_rendering()
        self._thumb_pixmaps.clear()
        self._build_grid()
        self._begin_rendering()

    def _update_progress(self):
        total = len(self._layers)
        done = self._next_idx
        self._progress.setValue(done)
        self._progress.setFormat("{} / {} layers".format(done, total))

        if self._rendering and done > 0 and done < total:
            elapsed = time.time() - self._render_start
            per_layer = elapsed / done
            remaining = per_layer * (total - done)
            self._status_label.setText(
                "Rendering... ~{:.1f}s remaining  ({:.0f}ms/layer, {}x proxy)".format(
                    remaining, per_layer * 1000, self._proxy_factor
                )
            )

    # ================================================================
    #  Sort changed
    # ================================================================
    def _on_sort_changed(self, combo_index):
        new_mode = self._sort_combo.itemData(combo_index)
        if new_mode == self._sort_mode:
            return
        self._sort_mode = new_mode
        if not self._scanned:
            return

        was_rendering = self._rendering
        if self._rendering:
            self._stop_rendering()

        self._sort_layers()
        self._build_grid()

        if was_rendering or self._next_idx < len(self._layers):
            self._next_idx = 0
            for i, le in enumerate(self._layers):
                if le['name'] not in self._thumb_pixmaps:
                    self._next_idx = i
                    break
                self._next_idx = i + 1
            if self._next_idx < len(self._layers):
                self._begin_rendering()

    # ================================================================
    #  Smooth slider
    # ================================================================
    def _on_size_drag(self, value):
        self._thumb_w = value
        self._thumb_h = int(value * self._ASPECT)
        self._size_label.setText("{}px".format(value))

        if not self._scanned or not self._buttons:
            return

        btn_w = self._thumb_w + 10
        btn_h = self._thumb_h + 40
        icon_size = QtCore.QSize(self._thumb_w, self._thumb_h)

        for layer_idx, btn in self._buttons:
            btn.setFixedSize(btn_w, btn_h)
            btn.setIconSize(icon_size)

            name = self._layers[layer_idx]['name']
            pm = self._thumb_pixmaps.get(name)
            if pm and not pm.isNull():
                scaled = pm.scaled(
                    self._thumb_w, self._thumb_h,
                    QtCore.Qt.KeepAspectRatio,
                    QtCore.Qt.FastTransformation
                )
                btn.setIcon(QtGui.QIcon(scaled))
            else:
                placeholder = QtGui.QPixmap(self._thumb_w, self._thumb_h)
                placeholder.fill(QtGui.QColor(40, 40, 40))
                btn.setIcon(QtGui.QIcon(placeholder))

    def _on_size_release(self):
        if self._scanned:
            self._build_grid()

    # ================================================================
    #  UI callbacks
    # ================================================================
    def _filter_layers(self, text):
        filt = text.lower()
        for layer_idx, btn in self._buttons:
            name = self._layers[layer_idx]['name']
            btn.setVisible(filt in name.lower())

    def _set_layer(self, layer_name):
        v = nuke.activeViewer()
        if v:
            v.node()['channels'].setValue(layer_name)

    def _on_proxy_changed(self, combo_index):
        new_factor = self._proxy_combo.itemData(combo_index)
        if new_factor == self._proxy_factor:
            return
        self._proxy_factor = new_factor
        if not self._scanned:
            return
        self._on_regenerate()

    def closeEvent(self, event):
        global _inspector_dialog
        self._stop_rendering()
        _inspector_dialog = None
        super(VisualLayerPicker, self).closeEvent(event)


def launch():
    global _inspector_dialog

    # If already open, just bring to front
    if _inspector_dialog is not None:
        _inspector_dialog.raise_()
        _inspector_dialog.activateWindow()
        return

    if not nuke.selectedNodes():
        nuke.message("Please select a node with layers to inspect.")
        return

    sel = nuke.selectedNode()

    # Modeless — show() returns immediately, Nuke stays interactive.
    # Global reference prevents garbage collection.
    dlg = VisualLayerPicker(sel.fullName())
    _inspector_dialog = dlg
    dlg.show()
    dlg.raise_()
    dlg.activateWindow()
