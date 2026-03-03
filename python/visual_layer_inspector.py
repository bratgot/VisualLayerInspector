"""
Visual Layer Inspector v18.2 — Python version

v18.2: Auto-init fallback timer, direct beginRendering call (no nested QTimer).

Created by Marten Blumen
"""

import nuke
import os
import time

try:
    from PySide2 import QtWidgets, QtCore, QtGui
except ImportError:
    from PySide6 import QtWidgets, QtCore, QtGui

VLI_VERSION = "v18.2"

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
SORT_ALPHA    = 0
SORT_TYPE     = 1
SORT_CHANNELS = 2
SORT_ORIGINAL = 3


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
        self._sort_reversed = False
        self._category_visible = {
            CATEGORY_LIGHTING:    True,
            CATEGORY_UTILITY:     True,
            CATEGORY_DATA:        True,
            CATEGORY_CRYPTOMATTE: True,
            CATEGORY_CUSTOM:      True,
        }
        self._category_checks = {}

        self._layers = []
        self._channels_map = {}
        self._thumb_pixmaps = {}
        self._buttons = []
        self._temp_dir = ""
        self._nuke_nodes = []
        self._last_col_count = 0

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
        self._sort_combo.addItem("Alphabetical",    SORT_ALPHA)
        self._sort_combo.addItem("Type Group",       SORT_TYPE)
        self._sort_combo.addItem("Channel Count",    SORT_CHANNELS)
        self._sort_combo.addItem("Original Order",   SORT_ORIGINAL)
        self._sort_combo.setCurrentIndex(1)  # default: Type Group
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

        # Reverse toggle button
        self._reverse_btn = QtWidgets.QPushButton(u"\u2191")  # ↑ ascending
        self._reverse_btn.setFixedSize(28, 28)
        self._reverse_btn.setToolTip("Reverse sort order")
        self._reverse_btn.setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: bold; background-color: #444444; "
            "border: 1px solid #555555; border-radius: 3px; padding: 0px; }"
            "QPushButton:hover { background-color: #555555; }"
        )
        self._reverse_btn.clicked.connect(self._on_reverse_toggle)
        row1.addWidget(self._reverse_btn)

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

        # Category filter checkboxes
        cat_row = QtWidgets.QHBoxLayout()
        cat_label = QtWidgets.QLabel("Show:")
        cat_label.setStyleSheet("font-size: 12px; color: #999999;")
        cat_row.addWidget(cat_label)

        cat_styles = [
            (CATEGORY_LIGHTING,    "#ddaa44"),
            (CATEGORY_UTILITY,     "#44aadd"),
            (CATEGORY_DATA,        "#aa66cc"),
            (CATEGORY_CRYPTOMATTE, "#66cc66"),
            (CATEGORY_CUSTOM,      "#999999"),
        ]
        for cat_id, colour in cat_styles:
            cb = QtWidgets.QCheckBox(CATEGORY_NAMES[cat_id])
            cb.setChecked(True)
            cb.setStyleSheet(
                "QCheckBox {{ font-size: 12px; color: {}; spacing: 4px; }}"
                "QCheckBox::indicator {{ width: 14px; height: 14px; }}".format(colour)
            )
            cb.toggled.connect(self._on_category_toggle)
            cat_row.addWidget(cb)
            self._category_checks[cat_id] = cb

        cat_row.addStretch()

        self._cat_all_btn = QtWidgets.QPushButton("All")
        self._cat_all_btn.setFixedSize(40, 22)
        self._cat_all_btn.setStyleSheet(
            "QPushButton { font-size: 11px; background-color: #444444; "
            "border: 1px solid #555555; border-radius: 3px; }"
            "QPushButton:hover { background-color: #555555; }"
        )
        self._cat_all_btn.setToolTip("Check / uncheck all categories")
        self._cat_all_btn.clicked.connect(self._on_cat_all)
        cat_row.addWidget(self._cat_all_btn)

        controls.addLayout(cat_row)

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

        row2.addSpacing(10)

        self._auto_thumb_check = QtWidgets.QCheckBox("Auto Thumbnails")
        self._auto_thumb_check.setChecked(True)
        self._auto_thumb_check.setStyleSheet("QCheckBox { font-size: 12px; color: #bbbbbb; }")
        self._auto_thumb_check.setToolTip(
            "When checked, thumbnails generate automatically on launch.\n"
            "Uncheck for large EXRs \u2014 use Regenerate to render manually."
        )
        row2.addWidget(self._auto_thumb_check)

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

        # Empty state label (hidden by default)
        self._empty_label = QtWidgets.QLabel(
            u"All category checkboxes are unchecked \u2014 tick at least one to display layers."
        )
        self._empty_label.setAlignment(QtCore.Qt.AlignCenter)
        self._empty_label.setStyleSheet("font-size: 14px; color: #888888; padding: 40px;")
        self._empty_label.setWordWrap(True)
        self._empty_label.setVisible(False)
        self.main_layout.addWidget(self._empty_label, 1)

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

        # Fallback: ensure _auto_init fires even if showEvent doesn't trigger
        # reliably (common with modeless dialogs in Nuke's Qt event loop).
        # The _scanned guard inside _auto_init prevents double execution.
        QtCore.QTimer.singleShot(100, self._auto_init)

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
        self._update_category_counts()

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

        if self._auto_thumb_check.isChecked():
            self._begin_rendering()
        else:
            self._regen_btn.setEnabled(True)
            self._progress.setRange(0, 1)
            self._progress.setValue(0)
            self._progress.setFormat(u"Thumbnails disabled \u2014 click Regenerate")

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
        if mode == SORT_ALPHA:
            self._layers.sort(key=lambda x: x['name'].lower())
        elif mode == SORT_TYPE:
            self._layers.sort(key=lambda x: (x['category'], x['name'].lower()))
        elif mode == SORT_CHANNELS:
            self._layers.sort(key=lambda x: (-x['channel_count'], x['name'].lower()))
        elif mode == SORT_ORIGINAL:
            self._layers.sort(key=lambda x: x['original_index'])

        if self._sort_reversed:
            self._layers.reverse()

    # ================================================================
    #  Grid
    # ================================================================
    def _build_grid(self):
        # Destroy existing buttons
        for le in self._layers:
            btn = le.get('button')
            if btn:
                btn.setParent(None)
                btn.deleteLater()
                le['button'] = None
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
        self._last_col_count = cols

        filt = self.filter_le.text().lower() if self.filter_le else ""
        show_groups = (self._sort_mode == SORT_TYPE)
        last_cat = -1
        grid_idx = 0

        for i, le in enumerate(self._layers):
            cat_ok = self._category_visible.get(le['category'], True)
            text_ok = (not filt) or (filt in le['name'].lower())
            visible = cat_ok and text_ok

            if show_groups and visible and (i == 0 or le['category'] != last_cat):
                any_visible = False
                for j in range(i, len(self._layers)):
                    if self._layers[j]['category'] != le['category']:
                        break
                    tv = (not filt) or (filt in self._layers[j]['name'].lower())
                    if tv:
                        any_visible = True
                        break

                if any_visible and cat_ok:
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
            btn.setStyleSheet("QToolButton { background-color: #282828; border: 1px solid #3a3a3a; }")

            pm = self._thumb_pixmaps.get(name)
            if pm and not pm.isNull():
                scaled = pm.scaled(
                    self._thumb_w, self._thumb_h,
                    QtCore.Qt.KeepAspectRatio,
                    QtCore.Qt.SmoothTransformation
                )
                btn.setIcon(QtGui.QIcon(scaled))
            # No placeholder pixmap — avoids expensive rescaling during slider drag

            btn.clicked.connect(lambda checked=False, l=name: self._set_layer(l))

            btn.setVisible(visible)
            le['button'] = btn

            if visible:
                self.grid.addWidget(btn, grid_idx // cols, grid_idx % cols)
                grid_idx += 1

            self._buttons.append((i, btn))

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

        # Skip layers that already have thumbnails (e.g. after re-sort)
        while self._next_idx < total and self._layers[self._next_idx]['name'] in self._thumb_pixmaps:
            self._next_idx += 1
            self._update_progress()

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

            # Update button directly via layer reference
            btn = le.get('button')
            if btn:
                scaled = pm.scaled(
                    self._thumb_w, self._thumb_h,
                    QtCore.Qt.KeepAspectRatio,
                    QtCore.Qt.SmoothTransformation
                )
                btn.setIcon(QtGui.QIcon(scaled))

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
    def _apply_sort_and_rebuild(self):
        """Instant sort — reposition existing buttons, zero creation."""
        was_rendering = self._rendering
        if self._rendering:
            self._stop_rendering()

        self._sort_layers()
        self._reorder_grid_fast()

        # Resume rendering only unfinished thumbnails
        if was_rendering:
            self._next_idx = 0
            self._rendering = True
            self._stop_btn.setText("Stop")
            self._stop_btn.setStyleSheet("font-weight: bold; background-color: #554433;")
            self._stop_btn.setEnabled(True)
            self._regen_btn.setEnabled(False)
            self._create_render_nodes()
            self._update_progress()
            self._schedule_next()

    def _reorder_grid_fast(self):
        """Reposition existing buttons in new sorted order. No destruction."""
        # Remove everything from grid without destroying
        while self.grid.count():
            item = self.grid.takeAt(0)
            w = item.widget()
            if w:
                # Only delete group header labels, not buttons
                is_button = False
                for le in self._layers:
                    if le.get('button') is w:
                        is_button = True
                        break
                if not is_button:
                    w.hide()
                    w.deleteLater()

        vp = self.scroll.viewport()
        btn_w = self._thumb_w + 10
        cols = max(1, (vp.width() if vp else 1100) // btn_w)
        self._last_col_count = cols

        filt = self.filter_le.text().lower() if self.filter_le else ""
        show_groups = (self._sort_mode == SORT_TYPE)
        last_cat = -1
        grid_idx = 0

        # Rebuild self._buttons in new order
        self._buttons = []

        for i, le in enumerate(self._layers):
            btn = le.get('button')
            if not btn:
                continue

            cat_ok = self._category_visible.get(le['category'], True)
            text_ok = (not filt) or (filt in le['name'].lower())
            visible = cat_ok and text_ok
            btn.setVisible(visible)

            if not visible:
                self._buttons.append((i, btn))
                continue

            # Group header
            if show_groups and (grid_idx == 0 or le['category'] != last_cat):
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

            self.grid.addWidget(btn, grid_idx // cols, grid_idx % cols)
            self._buttons.append((i, btn))
            grid_idx += 1

    def _on_sort_changed(self, combo_index):
        new_mode = self._sort_combo.itemData(combo_index)
        if new_mode == self._sort_mode:
            return
        self._sort_mode = new_mode
        if not self._scanned:
            return
        self._apply_sort_and_rebuild()

    def _on_reverse_toggle(self):
        self._sort_reversed = not self._sort_reversed
        self._reverse_btn.setText(u"\u2193" if self._sort_reversed else u"\u2191")
        self._reverse_btn.setToolTip(
            "Sort: descending (click to reverse)" if self._sort_reversed
            else "Sort: ascending (click to reverse)"
        )
        if not self._scanned:
            return
        self._apply_sort_and_rebuild()

    # ================================================================
    #  Smooth slider
    # ================================================================
    def _on_size_drag(self, value):
        self._thumb_w = value
        self._thumb_h = int(value * self._ASPECT)
        self._size_label.setText("{}px".format(value))

        if not self._scanned or not self._layers:
            return

        # Check if column count changed
        btn_w = self._thumb_w + 10
        vp = self.scroll.viewport()
        new_cols = max(1, (vp.width() if vp else 1100) // btn_w)

        if new_cols != self._last_col_count:
            # Column count changed — lightweight reposition (no widget creation)
            self._reflow_grid_fast(new_cols)

        # Always resize existing buttons (fast — no layout changes)
        btn_h = self._thumb_h + 40
        icon_size = QtCore.QSize(self._thumb_w, self._thumb_h)

        # Skip all pixmap work if no thumbnails exist
        any_thumbs = bool(self._thumb_pixmaps)

        for le in self._layers:
            btn = le.get('button')
            if not btn:
                continue
            btn.setFixedSize(btn_w, btn_h)

            if not any_thumbs:
                continue  # just resize geometry, no icon update

            btn.setIconSize(icon_size)

            pm = self._thumb_pixmaps.get(le['name'])
            if pm and not pm.isNull():
                scaled = pm.scaled(
                    self._thumb_w, self._thumb_h,
                    QtCore.Qt.KeepAspectRatio,
                    QtCore.Qt.FastTransformation
                )
                btn.setIcon(QtGui.QIcon(scaled))
            # Skip placeholder — avoids expensive rescaling

    def _reflow_grid_fast(self, new_cols):
        """Lightweight reflow: reposition existing buttons without destroying them.
        Group headers are hidden during drag — they return on release."""
        self._last_col_count = new_cols

        # Build set of active buttons for fast lookup
        btn_set = set()
        for le in self._layers:
            btn = le.get('button')
            if btn:
                btn_set.add(btn)

        # Remove everything from grid without deleting buttons
        while self.grid.count():
            item = self.grid.takeAt(0)
            w = item.widget()
            if w and w not in btn_set:
                w.hide()
                w.deleteLater()

        # Re-add only visible buttons at new positions (no headers during drag)
        grid_idx = 0
        for le in self._layers:
            btn = le.get('button')
            if btn and btn.isVisible():
                self.grid.addWidget(btn, grid_idx // new_cols, grid_idx % new_cols)
                grid_idx += 1

    def _on_size_release(self):
        if not self._scanned:
            return
        if self._thumb_pixmaps:
            # Thumbnails exist — full rebuild for SmoothTransformation quality
            self._build_grid()
        else:
            # No thumbnails — just reflow, skip expensive rebuild
            self._reorder_grid_fast()

    # ================================================================
    #  UI callbacks
    # ================================================================
    def _filter_layers(self, text):
        self._apply_visibility()

    def _on_category_toggle(self, checked):
        for cat_id, cb in self._category_checks.items():
            self._category_visible[cat_id] = cb.isChecked()
        if self._scanned:
            self._apply_visibility()

    def _on_cat_all(self):
        all_checked = all(cb.isChecked() for cb in self._category_checks.values())
        for cb in self._category_checks.values():
            cb.setChecked(not all_checked)

    def _apply_visibility(self):
        filt = self.filter_le.text().lower() if self.filter_le else ""
        any_checked = any(cb.isChecked() for cb in self._category_checks.values())

        for le in self._layers:
            btn = le.get('button')
            if not btn:
                continue
            cat_ok = self._category_visible.get(le['category'], True)
            text_ok = (not filt) or (filt in le['name'].lower())
            btn.setVisible(cat_ok and text_ok)

        # Show empty state when no categories checked
        show_empty = not any_checked and self._scanned
        self._empty_label.setVisible(show_empty)
        self.scroll.setVisible(not show_empty)

    def _update_category_counts(self):
        counts = {}
        for le in self._layers:
            c = le['category']
            counts[c] = counts.get(c, 0) + 1
        for cat_id, cb in self._category_checks.items():
            n = counts.get(cat_id, 0)
            cb.setText("{} ({})".format(CATEGORY_NAMES[cat_id], n))

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
        if not self._auto_thumb_check.isChecked():
            return  # respect checkbox
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
