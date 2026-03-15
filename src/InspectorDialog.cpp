// ============================================================================
// InspectorDialog.cpp — Visual Layer Inspector v2.0.0
//
// v2.0.0: setUpdatesEnabled(false/true) batching on all layout-heavy operations.
//        Buttons without thumbnails show a dark placeholder outline.
//      their data. reorderGridFast() just repositions, zero creation.
//      All button for category checkboxes. Empty state message.
//
// Created by Marten Blumen
// ============================================================================

#include "InspectorDialog.h"

#include <algorithm>
#include <cctype>

// ============================================================================
//  Layer classification
// ============================================================================
const char* layerCategoryName(LayerCategory cat)
{
    switch (cat) {
        case LayerCategory::Lighting:    return "Lighting";
        case LayerCategory::Utility:     return "Utility";
        case LayerCategory::Data:        return "Data";
        case LayerCategory::Cryptomatte: return "Cryptomatte";
        case LayerCategory::Custom:      return "Custom";
    }
    return "Custom";
}

static std::string toLower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

static bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

LayerCategory classifyLayer(const std::string& name)
{
    const std::string lower = toLower(name);

    if (lower.substr(0, 6) == "crypto") return LayerCategory::Cryptomatte;

    static const char* lightingPatterns[] = {
        "diffuse", "specular", "reflect", "refract", "emission", "emissive",
        "coat", "sheen", "transmis", "sss", "subsurface", "indirect",
        "direct", "albedo", "beauty", "light", "shadow", "gi",
        "illumin", "radiance", "irradiance", "glossy", "volume",
        "scatter", "translucen", "caustic", "firefly", "aov_light",
        nullptr
    };
    for (int i = 0; lightingPatterns[i]; ++i)
        if (contains(lower, lightingPatterns[i])) return LayerCategory::Lighting;

    static const char* utilityPatterns[] = {
        "depth", "normal", "position", "uv", "motion", "velocity",
        "fresnel", "curvature", "occlusion", "ao", "pointworld",
        "worldnormal", "worldposition", "worldpoint", "pworld",
        "nworld", "pref", "st_map", "stmap", "z_depth", "zdepth",
        "facing_ratio", "faceratio", "barycentric", "tangent",
        "opacity", "coverage", "rgba",
        nullptr
    };
    for (int i = 0; utilityPatterns[i]; ++i)
        if (contains(lower, utilityPatterns[i])) return LayerCategory::Utility;

    static const char* dataPatterns[] = {
        "id", "mask", "matte", "object", "material", "puzzle",
        "asset", "element", "holdout",
        nullptr
    };
    for (int i = 0; dataPatterns[i]; ++i)
        if (contains(lower, dataPatterns[i])) return LayerCategory::Data;

    return LayerCategory::Custom;
}

// ============================================================================
//  Sorting
// ============================================================================
void InspectorDialog::sortLayers()
{
    switch (sortMode_) {
        case SortMode::Alphabetical:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    return toLower(a.name) < toLower(b.name);
                });
            break;
        case SortMode::TypeGroup:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    if (a.category != b.category)
                        return static_cast<int>(a.category) < static_cast<int>(b.category);
                    return toLower(a.name) < toLower(b.name);
                });
            break;
        case SortMode::ChannelCount:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    if (a.channelCount != b.channelCount)
                        return a.channelCount > b.channelCount;
                    return toLower(a.name) < toLower(b.name);
                });
            break;
        case SortMode::OriginalOrder:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    return a.prepareIndex < b.prepareIndex;
                });
            break;
    }

    if (sortReversed_)
        std::reverse(layers_.begin(), layers_.end());
}

// ============================================================================
//  Constructor
// ============================================================================
InspectorDialog::InspectorDialog(ScanCallback    scanLayers,
                                 PrepareCallback prepareRender,
                                 LayerCallback   onLayerSelected,
                                 ShuffleCallback  onCreateShuffle,
                                 const InspectorSettings& settings,
                                 QWidget* parent)
    : QDialog(parent)
    , scanLayers_(std::move(scanLayers))
    , prepareRender_(std::move(prepareRender))
    , onLayerSelected_(std::move(onLayerSelected))
    , onCreateShuffle_(std::move(onCreateShuffle))
    , proxyStep_(settings.proxyStep)
    , thumbWidth_(settings.thumbSize)
    , thumbHeight_(static_cast<int>(settings.thumbSize * kAspectRatio))
    , sortMode_(settings.sortMode)
{
    setWindowTitle(QString("Visual Layer Inspector  [%1]").arg(kVLI_Version));
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    resize(1150, 850);

    auto* mainLayout = new QVBoxLayout(this);

    // --- Title + version ---
    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel("Visual Layer Inspector");
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #eeeeee;");
    titleRow->addWidget(title);
    titleRow->addStretch();
    auto* versionLabel = new QLabel(kVLI_Version);
    versionLabel->setStyleSheet(
        "font-size: 14px; font-weight: bold; color: #66bb66; "
        "background-color: #224422; padding: 3px 10px; border-radius: 3px;");
    titleRow->addWidget(versionLabel);
    mainLayout->addLayout(titleRow);

    auto* desc = new QLabel(
        "Click any layer name to view it in the Viewer. "
        "Thumbnails generate automatically \xe2\x80\x94 use <b>Stop</b> to pause.");
    desc->setStyleSheet("font-size: 13px; color: #bbbbbb; margin-bottom: 5px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    // --- Controls ---
    controlsWidget_ = new QWidget;
    controlsWidget_->setVisible(false);
    auto* controlsLayout = new QVBoxLayout(controlsWidget_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);

    // Row 1: filter + sort + reverse + size
    auto* row1 = new QHBoxLayout;

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText("Filter layers (e.g., 'depth', 'spec')...");
    filterEdit_->setStyleSheet("font-size: 14px; padding: 5px;");
    connect(filterEdit_, &QLineEdit::textChanged, this, &InspectorDialog::filterLayers);
    row1->addWidget(filterEdit_, 1);

    row1->addSpacing(10);
    row1->addWidget(new QLabel("Sort:"));

    sortCombo_ = new QComboBox;
    sortCombo_->addItem("Alphabetical",   static_cast<int>(SortMode::Alphabetical));
    sortCombo_->addItem("Type AOV",      static_cast<int>(SortMode::TypeGroup));
    sortCombo_->addItem("Channel Count",   static_cast<int>(SortMode::ChannelCount));
    sortCombo_->addItem("Original Order",  static_cast<int>(SortMode::OriginalOrder));
    sortCombo_->setCurrentIndex(static_cast<int>(sortMode_));
    sortCombo_->setToolTip(
        "Type AOV auto-categorises layers:\n"
        "  Lighting \xe2\x80\x94 diffuse, specular, reflection, emission, sss...\n"
        "  Utility \xe2\x80\x94 depth, normal, position, motion, uv, ao...\n"
        "  Data \xe2\x80\x94 id, mask, matte, object, material...\n"
        "  Cryptomatte \xe2\x80\x94 crypto*\n"
        "  Custom \xe2\x80\x94 everything else");
    connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorDialog::onSortChanged);
    row1->addWidget(sortCombo_);

    reverseBtn_ = new QPushButton("\xe2\x86\x91");
    reverseBtn_->setFixedSize(28, 28);
    reverseBtn_->setToolTip("Reverse sort order");
    reverseBtn_->setStyleSheet(
        "QPushButton { font-size: 16px; font-weight: bold; background-color: #444444; "
        "border: 1px solid #555555; border-radius: 3px; padding: 0px; }"
        "QPushButton:hover { background-color: #555555; }");
    connect(reverseBtn_, &QPushButton::clicked, this, &InspectorDialog::onReverseToggle);
    row1->addWidget(reverseBtn_);

    row1->addSpacing(10);
    row1->addWidget(new QLabel("Size:"));

    sizeSlider_ = new QSlider(Qt::Horizontal);
    sizeSlider_->setRange(kMinThumbSize, kMaxThumbSize);
    sizeSlider_->setValue(thumbWidth_);
    sizeSlider_->setFixedWidth(140);
    connect(sizeSlider_, &QSlider::valueChanged, this, &InspectorDialog::onThumbnailSizeDrag);
    connect(sizeSlider_, &QSlider::sliderReleased, this, &InspectorDialog::onThumbnailSizeRelease);
    row1->addWidget(sizeSlider_);

    sizeLabel_ = new QLabel(QString::number(thumbWidth_) + "px");
    sizeLabel_->setStyleSheet("font-size: 12px; color: #999999; min-width: 45px;");
    row1->addWidget(sizeLabel_);

    controlsLayout->addLayout(row1);

    // Row 2: category checkboxes
    auto* catLayout = new QHBoxLayout;

    auto* catLabel = new QLabel("Show:");
    catLabel->setStyleSheet("font-size: 12px; color: #999999;");
    catLayout->addWidget(catLabel);

    struct CatStyle { LayerCategory cat; const char* colour; bool checked; };
    CatStyle styles[] = {
        { LayerCategory::Lighting,    "#ddaa44", settings.showLighting },
        { LayerCategory::Utility,     "#44aadd", settings.showUtility },
        { LayerCategory::Data,        "#aa66cc", settings.showData },
        { LayerCategory::Cryptomatte, "#66cc66", settings.showCryptomatte },
        { LayerCategory::Custom,      "#999999", settings.showCustom },
    };

    for (auto& s : styles) {
        auto* cb = new QCheckBox(layerCategoryName(s.cat));
        cb->setChecked(s.checked);
        cb->setStyleSheet(
            QString("QCheckBox { font-size: 12px; color: %1; spacing: 4px; }"
                    "QCheckBox::indicator { width: 14px; height: 14px; }")
                .arg(s.colour));
        connect(cb, &QCheckBox::toggled, this, &InspectorDialog::onCategoryToggle);
        catLayout->addWidget(cb);
        categoryChecks_[s.cat] = cb;
    }

    catLayout->addSpacing(6);

    catAllBtn_ = new QPushButton("All");
    catAllBtn_->setFixedSize(40, 22);
    catAllBtn_->setStyleSheet(
        "QPushButton { font-size: 11px; background-color: #444444; "
        "border: 1px solid #555555; border-radius: 3px; color: #cccccc; }"
        "QPushButton:hover { background-color: #555555; }");
    catAllBtn_->setToolTip("Check / uncheck all categories");
    connect(catAllBtn_, &QPushButton::clicked, this, &InspectorDialog::onCatAll);
    catLayout->addWidget(catAllBtn_);

    catLayout->addStretch();
    controlsLayout->addLayout(catLayout);

    // Row 3: stop + proxy + regenerate
    auto* row3 = new QHBoxLayout;

    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setFixedHeight(30);
    stopBtn_->setMinimumWidth(90);
    stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
    connect(stopBtn_, &QPushButton::clicked, this, &InspectorDialog::onStopResume);
    row3->addWidget(stopBtn_);

    row3->addSpacing(10);
    row3->addWidget(new QLabel("Proxy:"));

    proxyCombo_ = new QComboBox;
    proxyCombo_->addItem("Full Quality", 1);
    proxyCombo_->addItem("2x Proxy",     2);
    proxyCombo_->addItem("4x Proxy",     4);
    proxyCombo_->addItem("8x Proxy",     8);
    // Map proxyStep_ to combo index
    int proxyIdx = 0;
    for (int i = 0; i < proxyCombo_->count(); ++i) {
        if (proxyCombo_->itemData(i).toInt() == proxyStep_) { proxyIdx = i; break; }
    }
    proxyCombo_->setCurrentIndex(proxyIdx);
    connect(proxyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorDialog::onProxyChanged);
    row3->addWidget(proxyCombo_);

    row3->addSpacing(10);

    regenBtn_ = new QPushButton("Regenerate");
    regenBtn_->setFixedHeight(30);
    regenBtn_->setMinimumWidth(110);
    regenBtn_->setStyleSheet(
        "QPushButton { font-weight: bold; background-color: #335533; }"
        "QPushButton:disabled { background-color: #333333; color: #666666; }");
    regenBtn_->setEnabled(false);
    connect(regenBtn_, &QPushButton::clicked, this, &InspectorDialog::onRegenerate);
    row3->addWidget(regenBtn_);

    row3->addStretch();
    controlsLayout->addLayout(row3);

    // Progress
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 0);
    progressBar_->setTextVisible(true);
    progressBar_->setFormat("Scanning layers...");
    progressBar_->setFixedHeight(18);
    progressBar_->setStyleSheet(
        "QProgressBar { border: 1px solid #444; border-radius: 3px; "
        "background: #222; text-align: center; color: #ccc; font-size: 11px; }"
        "QProgressBar::chunk { background: #446644; }");
    controlsLayout->addWidget(progressBar_);

    statusLabel_ = new QLabel("Scanning layers...");
    statusLabel_->setStyleSheet("font-size: 11px; color: #999999;");
    controlsLayout->addWidget(statusLabel_);

    mainLayout->addWidget(controlsWidget_);

    // --- Grid ---
    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    container_ = new QWidget;
    grid_ = new QGridLayout(container_);
    scrollArea_->setWidget(container_);
    mainLayout->addWidget(scrollArea_, 1);

    // Empty state label
    emptyLabel_ = new QLabel(
        "All category checkboxes are unchecked \xe2\x80\x94 tick at least one to display layers.");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet("font-size: 14px; color: #888888; padding: 40px;");
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setVisible(false);
    mainLayout->addWidget(emptyLabel_, 1);

    // --- Footer ---
    auto* footerLayout = new QHBoxLayout;
    auto* credit = new QLabel(
        QString("Created by Marten Blumen  \xe2\x80\xa2  %1").arg(kVLI_Version));
    credit->setStyleSheet("font-size: 11px; color: #777777; font-style: italic;");
    footerLayout->addWidget(credit);
    footerLayout->addStretch();

    shuffleBtn_ = new QPushButton("Shuffle Export (shift+click to select)");
    shuffleBtn_->setFixedHeight(35);
    shuffleBtn_->setMinimumWidth(200);
    shuffleBtn_->setEnabled(false);
    shuffleBtn_->setToolTip("Shift+click thumbnails to select layers,\nthen click here to create Shuffle2 nodes for each");
    shuffleBtn_->setStyleSheet(
        "QPushButton { font-weight: bold; background-color: #334455; }"
        "QPushButton:disabled { background-color: #333333; color: #666666; }");
    connect(shuffleBtn_, &QPushButton::clicked, this, [this]() {
        if (!onCreateShuffle_) return;
        // Collect all pinned layers + the currently viewed layer
        std::vector<std::string> toExport;
        bool currentIncluded = false;
        for (auto& le : layers_) {
            if (le.pinned) {
                toExport.push_back(le.name);
                if (le.name == currentLayer_) currentIncluded = true;
            }
        }
        if (!currentLayer_.empty() && !currentIncluded)
            toExport.push_back(currentLayer_);

        if (!toExport.empty()) {
            onCreateShuffle_(toExport);
            // Clear pins after export, keep currentLayer_ for viewing
            for (auto& le : layers_) {
                le.pinned = false;
                le.selected = false;
            }
            for (int j = 0; j < static_cast<int>(layers_.size()); ++j)
                updateSelectionStyle(j);
            updateShuffleButton();
        }
    });
    footerLayout->addWidget(shuffleBtn_);

    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(35);
    closeBtn->setMinimumWidth(100);
    closeBtn->setStyleSheet("font-weight: bold; background-color: #553333;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    footerLayout->addWidget(closeBtn);

    mainLayout->addLayout(footerLayout);

    controlsWidget_->setVisible(true);
}

InspectorDialog::~InspectorDialog()
{
    stopRendering();
}

// ============================================================================
//  showEvent -> auto-init (single entry point, no race)
// ============================================================================
void InspectorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!showFired_) {
        showFired_ = true;
        // 200ms delay ensures dialog is fully visible and painted first
        QTimer::singleShot(200, this, &InspectorDialog::autoInit);
    }
}

void InspectorDialog::closeEvent(QCloseEvent* event)
{
    stopRendering();
    if (onClose_) onClose_();
    QDialog::closeEvent(event);
}

// ============================================================================
//  Rescan — re-read input and regenerate all thumbnails
// ============================================================================
void InspectorDialog::rescan()
{
    stopRendering();

    // Clean up
    for (auto& le : layers_) {
        if (le.button) { le.button->setParent(nullptr); delete le.button; le.button = nullptr; }
    }
    for (auto* h : groupHeaders_) { h->setParent(nullptr); delete h; }
    groupHeaders_.clear();

    // Fresh container
    auto* newContainer = new QWidget;
    grid_ = new QGridLayout(newContainer);
    grid_->setSpacing(6);
    scrollArea_->setWidget(newContainer);
    container_ = newContainer;

    scanned_ = false;
    initializing_ = false;
    showFired_ = true;       // don't re-trigger showEvent init
    currentLayer_.clear();
    layers_.clear();
    renderOne_ = nullptr;
    nextRenderIdx_ = 0;

    statusLabel_->setText("Input changed — rescanning...");
    progressBar_->setRange(0, 0);
    progressBar_->setFormat("Scanning layers...");

    QTimer::singleShot(50, this, &InspectorDialog::autoInit);
}

// ============================================================================
//  Auto init
// ============================================================================
void InspectorDialog::autoInit()
{
    if (scanned_ || initializing_ || !scanLayers_) return;
    initializing_ = true;

    // ── Phase 1: fast scan — just layer names ──
    statusLabel_->setText("Scanning layers...");
    setCursor(Qt::WaitCursor);

    ScanResult scan = scanLayers_();
    setCursor(Qt::ArrowCursor);

    if (!scan.valid) {
        statusLabel_->setText(
            QString("Error: %1").arg(QString::fromStdString(scan.errorMsg)));
        progressBar_->setRange(0, 1);
        progressBar_->setValue(0);
        progressBar_->setFormat("Failed");
        initializing_ = false;
        return;
    }

    layers_.clear();
    layers_.reserve(scan.layerNames.size());
    for (int i = 0; i < static_cast<int>(scan.layerNames.size()); ++i) {
        LayerEntry le;
        le.name = scan.layerNames[i];
        le.prepareIndex = i;
        le.channelCount = (i < static_cast<int>(scan.channelCounts.size()))
                              ? scan.channelCounts[i] : 0;
        le.category = classifyLayer(le.name);
        le.button = nullptr;
        layers_.push_back(std::move(le));
    }

    sortLayers();
    scanned_ = true;
    updateCategoryCounts();
    buildGrid();                // grid appears immediately with placeholders
    initializing_ = false;

    // ── Phase 2: deferred render setup (after grid is visible) ──
    QTimer::singleShot(50, this, [this]() {
        if (!prepareRender_) return;
        statusLabel_->setText("Preparing render engine...");
        PrepareResult pr = prepareRender_();
        if (pr.valid) {
            renderOne_ = std::move(pr.renderOne);
            beginRendering();
        } else {
            statusLabel_->setText("Ready — click Regenerate to render thumbnails.");
            regenBtn_->setEnabled(true);
        }
    });
}

// ============================================================================
//  Category counts
// ============================================================================
void InspectorDialog::updateCategoryCounts()
{
    std::map<LayerCategory, int> counts;
    for (const auto& le : layers_)
        counts[le.category]++;

    for (auto& kv : categoryChecks_) {
        int n = counts.count(kv.first) ? counts[kv.first] : 0;
        kv.second->setText(
            QString("%1 (%2)").arg(layerCategoryName(kv.first)).arg(n));
    }

    int total = static_cast<int>(layers_.size());
    QStringList parts;
    for (auto& kv : counts)
        parts.append(QString("%1 %2").arg(kv.second).arg(
            QString(layerCategoryName(kv.first)).toLower()));
    statusLabel_->setText(
        QString("Found %1 layers (%2)").arg(total).arg(parts.join(", ")));
}

// ============================================================================
//  Rendering — one thumbnail per timer tick, UI stays responsive
// ============================================================================
void InspectorDialog::beginRendering()
{
    if (!scanned_ || layers_.empty()) return;

    // Don't render if no buttons are visible — nothing to update
    bool anyButtons = false;
    for (auto& le : layers_) {
        if (le.button) { anyButtons = true; break; }
    }
    if (!anyButtons) return;

    // Ensure render callback is ready (deferred setup)
    if (!renderOne_ && prepareRender_) {
        statusLabel_->setText("Preparing render engine...");
        setCursor(Qt::WaitCursor);
        PrepareResult pr = prepareRender_();
        setCursor(Qt::ArrowCursor);
        if (pr.valid)
            renderOne_ = std::move(pr.renderOne);
    }
    if (!renderOne_) {
        statusLabel_->setText("Error: could not set up rendering.");
        return;
    }

    rendering_ = true;
    nextRenderIdx_ = 0;
    perfTimer_.start();
    stopBtn_->setText("Stop");
    stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
    stopBtn_->setEnabled(true);
    regenBtn_->setEnabled(false);
    updateProgress();
    scheduleNextRender();
}

void InspectorDialog::scheduleNextRender()
{
    if (!rendering_) return;
    // singleShot(0) yields to the event loop so Qt paints the last icon
    // before we block the main thread rendering the next one
    QTimer::singleShot(0, this, &InspectorDialog::renderNextThumbnail);
}

void InspectorDialog::renderNextThumbnail()
{
    if (!rendering_) return;
    const int total = static_cast<int>(layers_.size());

    // Skip layers that already have thumbnails
    while (nextRenderIdx_ < total && !layers_[nextRenderIdx_].thumbnail.isNull())
        ++nextRenderIdx_;

    if (nextRenderIdx_ >= total) { stopRendering(); return; }

    auto& entry = layers_[nextRenderIdx_];
    if (renderOne_) {
        QImage img = renderOne_(entry.prepareIndex, proxyStep_);
        entry.thumbnail = std::move(img);
        // Update the button icon immediately
        if (entry.button && !entry.thumbnail.isNull()) {
            entry.button->setIconSize(QSize(thumbWidth_, thumbHeight_));
            QPixmap pm = QPixmap::fromImage(
                entry.thumbnail.scaled(thumbWidth_, thumbHeight_,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
            entry.button->setIcon(QIcon(pm));
            // Force the button to repaint NOW before we render the next one
            entry.button->repaint();
        }
    }

    ++nextRenderIdx_;
    updateProgress();

    if (nextRenderIdx_ >= total) stopRendering();
    else scheduleNextRender();
}

// ============================================================================
//  Stop / Resume / Regenerate
// ============================================================================
void InspectorDialog::stopRendering()
{
    rendering_ = false;
    regenBtn_->setEnabled(true);
    int done = nextRenderIdx_;
    int total = static_cast<int>(layers_.size());
    if (done >= total) {
        double elapsed = perfTimer_.elapsed() / 1000.0;
        statusLabel_->setText(QString("Done \xe2\x80\x94 %1 layers in %2 s").arg(total).arg(elapsed, 0, 'f', 2));
        stopBtn_->setEnabled(false);
    } else {
        statusLabel_->setText(QString("Paused \xe2\x80\x94 %1 / %2 rendered").arg(done).arg(total));
        stopBtn_->setText("Resume");
        stopBtn_->setStyleSheet("font-weight: bold; background-color: #335544;");
    }
}

void InspectorDialog::onStopResume()
{
    if (rendering_) {
        stopRendering();
    } else if (scanned_ && nextRenderIdx_ < static_cast<int>(layers_.size())) {
        rendering_ = true;
        regenBtn_->setEnabled(false);
        stopBtn_->setText("Stop");
        stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
        stopBtn_->setEnabled(true);
        scheduleNextRender();
    }
}

void InspectorDialog::onRegenerate()
{
    if (!scanned_ || layers_.empty()) return;
    stopRendering();
    for (auto& le : layers_) le.thumbnail = QImage();
    buildGrid();
    beginRendering();
}

// ============================================================================
//  Sort / Reverse — INSTANT via reorderGridFast
// ============================================================================
void InspectorDialog::onSortChanged(int comboIndex)
{
    SortMode newMode = static_cast<SortMode>(sortCombo_->itemData(comboIndex).toInt());
    if (newMode == sortMode_) return;
    sortMode_ = newMode;
    if (!scanned_) return;

    bool wasRendering = rendering_;
    if (rendering_) stopRendering();

    sortLayers();
    reorderGridFast();

    // Resume rendering unfinished thumbnails
    if (wasRendering) {
        beginRendering();
    }
}

void InspectorDialog::onReverseToggle()
{
    sortReversed_ = !sortReversed_;
    reverseBtn_->setText(sortReversed_ ? "\xe2\x86\x93" : "\xe2\x86\x91");
    reverseBtn_->setToolTip(sortReversed_ ? "Sort: descending (click to reverse)"
                                          : "Sort: ascending (click to reverse)");
    if (!scanned_) return;

    bool wasRendering = rendering_;
    if (rendering_) stopRendering();

    sortLayers();
    reorderGridFast();

    if (wasRendering) {
        beginRendering();
    }
}

void InspectorDialog::onCategoryToggle()
{
    if (!scanned_) return;

    if (rendering_) stopRendering();

    buildGrid();

    // Start rendering for any visible layers missing thumbnails
    beginRendering();
}

void InspectorDialog::onCatAll()
{
    bool allChecked = true;
    for (auto& kv : categoryChecks_)
        if (!kv.second->isChecked()) { allChecked = false; break; }
    for (auto& kv : categoryChecks_)
        kv.second->setChecked(!allChecked);
}

// ============================================================================
//  Visibility — text filter + category checkboxes + empty state
// ============================================================================
void InspectorDialog::applyVisibility()
{
    const std::string textFilter = filterEdit_
        ? toLower(filterEdit_->text().toStdString()) : std::string();

    bool anyChecked = false;
    for (auto& kv : categoryChecks_)
        if (kv.second->isChecked()) { anyChecked = true; break; }

    for (auto& le : layers_) {
        if (!le.button) continue;
        bool catVisible = true;
        auto it = categoryChecks_.find(le.category);
        if (it != categoryChecks_.end())
            catVisible = it->second->isChecked();
        bool textVisible = textFilter.empty()
            || (toLower(le.name).find(textFilter) != std::string::npos);
        le.button->setVisible(catVisible && textVisible);
    }

    bool showEmpty = !anyChecked && scanned_;
    emptyLabel_->setVisible(showEmpty);
    scrollArea_->setVisible(!showEmpty);
}

// ============================================================================
//  reorderGridFast — INSTANT sort: repositions existing buttons, no creation
// ============================================================================
void InspectorDialog::reorderGridFast()
{
    // Detach buttons from old layout (keep objects alive)
    for (auto& le : layers_) {
        if (le.button) le.button->setParent(nullptr);
    }
    for (auto* h : groupHeaders_) { h->setParent(nullptr); delete h; }
    groupHeaders_.clear();

    // Fresh container + layout
    auto* newContainer = new QWidget;
    newContainer->setUpdatesEnabled(false);
    grid_ = new QGridLayout(newContainer);
    grid_->setSpacing(6);
    scrollArea_->setWidget(newContainer);
    container_ = newContainer;

    const int cols = computeColumns();
    lastColumnCount_ = cols;
    bool showGroupHeaders = (sortMode_ == SortMode::TypeGroup);
    const std::string textFilter = filterEdit_
        ? toLower(filterEdit_->text().toStdString()) : std::string();

    int row = 0;
    int col = 0;
    LayerCategory lastCat = static_cast<LayerCategory>(-1);

    for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
        auto& le = layers_[i];
        if (!le.button) continue;

        bool catVisible = true;
        auto it = categoryChecks_.find(le.category);
        if (it != categoryChecks_.end())
            catVisible = it->second->isChecked();
        bool textVisible = textFilter.empty()
            || (toLower(le.name).find(textFilter) != std::string::npos);
        bool visible = catVisible && textVisible;
        le.button->setVisible(visible);

        if (!visible) continue;

        if (showGroupHeaders && le.category != lastCat) {
            if (col > 0) { row++; col = 0; }
            auto* header = new QLabel(
                QString("\xe2\x80\x94 %1 \xe2\x80\x94").arg(layerCategoryName(le.category)));
            header->setStyleSheet(
                "font-size: 13px; font-weight: bold; color: #88aacc; padding: 8px 0 2px 5px;");
            grid_->addWidget(header, row, 0, 1, cols);
            groupHeaders_.push_back(header);
            row++;
            col = 0;
            lastCat = le.category;
        }

        grid_->addWidget(le.button, row, col);
        col++;
        if (col >= cols) { row++; col = 0; }
    }

    newContainer->setUpdatesEnabled(true);
}

// ============================================================================
//  Smooth slider
// ============================================================================
void InspectorDialog::onThumbnailSizeDrag(int value)
{
    thumbWidth_  = value;
    thumbHeight_ = static_cast<int>(value * kAspectRatio);
    sizeLabel_->setText(QString::number(value) + "px");
    if (!scanned_) return;

    int newCols = computeColumns();
    if (newCols != lastColumnCount_) {
        reflowGridFast();
    }
    resizeButtonsInPlace();
}

void InspectorDialog::resizeButtonsInPlace()
{
    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;

    // --- v2.0.0: geometry only during drag — pixmaps rebuild on release ---
    QWidget* container = scrollArea_->widget();
    if (container) container->setUpdatesEnabled(false);

    for (auto& le : layers_) {
        if (!le.button) continue;
        le.button->setFixedSize(btnWidth, btnHeight);
    }

    if (container) container->setUpdatesEnabled(true);
}

void InspectorDialog::reflowGridFast()
{
    const int cols = computeColumns();
    lastColumnCount_ = cols;

    // Detach buttons
    for (auto& le : layers_) {
        if (le.button) le.button->setParent(nullptr);
    }
    for (auto* h : groupHeaders_) { h->setParent(nullptr); delete h; }
    groupHeaders_.clear();

    // Fresh container — no stale grid rows
    auto* newContainer = new QWidget;
    newContainer->setUpdatesEnabled(false);
    grid_ = new QGridLayout(newContainer);
    grid_->setSpacing(6);
    scrollArea_->setWidget(newContainer);
    container_ = newContainer;

    int row = 0;
    int col = 0;
    for (auto& le : layers_) {
        if (le.button && le.button->isVisible()) {
            grid_->addWidget(le.button, row, col);
            col++;
            if (col >= cols) { row++; col = 0; }
        }
    }

    newContainer->setUpdatesEnabled(true);
}

void InspectorDialog::onThumbnailSizeRelease()
{
    if (!scanned_) return;
    buildGrid();
}

// ============================================================================
//  Placeholder thumbnail — dark card so user sees something is pending
// ============================================================================
QImage InspectorDialog::makePlaceholder() const
{
    QImage img(thumbWidth_, thumbHeight_, QImage::Format_RGB32);
    img.fill(QColor(40, 40, 40));
    // Top/bottom single-pixel border lines for subtle outline
    for (int x = 0; x < thumbWidth_; ++x) {
        img.setPixel(x, 0, qRgb(60, 60, 60));
        img.setPixel(x, thumbHeight_ - 1, qRgb(60, 60, 60));
    }
    for (int y = 0; y < thumbHeight_; ++y) {
        img.setPixel(0, y, qRgb(60, 60, 60));
        img.setPixel(thumbWidth_ - 1, y, qRgb(60, 60, 60));
    }
    return img;
}

// ============================================================================
//  Grid — full rebuild (init, release, regenerate)
// ============================================================================
int InspectorDialog::computeColumns() const
{
    int available = scrollArea_ ? scrollArea_->viewport()->width() : 0;
    // If scroll area was just shown or hasn't laid out yet, use dialog width
    if (available < 100)
        available = width() - 40;
    if (available < 100)
        available = 1100;
    return std::max(1, available / (thumbWidth_ + kButtonPadding));
}

void InspectorDialog::buildGrid()
{
    // Clean up old buttons
    for (auto& le : layers_) {
        if (le.button) { le.button->setParent(nullptr); delete le.button; le.button = nullptr; }
    }
    for (auto* h : groupHeaders_) { h->setParent(nullptr); delete h; }
    groupHeaders_.clear();

    // Determine visibility state first
    bool anyChecked = false;
    for (auto& kv : categoryChecks_)
        if (kv.second->isChecked()) { anyChecked = true; break; }
    bool showEmpty = !anyChecked && scanned_;

    emptyLabel_->setVisible(showEmpty);
    scrollArea_->setVisible(!showEmpty);

    if (showEmpty) return;   // nothing to build

    // Ensure scroll area is visible and sized before computing columns
    scrollArea_->show();
    scrollArea_->updateGeometry();

    // Fresh container + layout
    auto* newContainer = new QWidget;
    newContainer->setUpdatesEnabled(false);
    grid_ = new QGridLayout(newContainer);
    grid_->setSpacing(6);
    scrollArea_->setWidget(newContainer);
    container_ = newContainer;

    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;
    const int cols = computeColumns();
    lastColumnCount_ = cols;
    const QString textFilter = filterEdit_ ? filterEdit_->text().toLower() : QString();
    bool showGroupHeaders = (sortMode_ == SortMode::TypeGroup);

    // Collect visible layers
    std::vector<int> visibleIndices;
    for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
        auto& le = layers_[i];
        bool catVisible = true;
        auto it = categoryChecks_.find(le.category);
        if (it != categoryChecks_.end())
            catVisible = it->second->isChecked();
        bool textVisible = textFilter.isEmpty()
            || toLower(le.name).find(textFilter.toStdString()) != std::string::npos;
        if (catVisible && textVisible)
            visibleIndices.push_back(i);
    }

    int row = 0;
    int col = 0;
    LayerCategory lastCat = static_cast<LayerCategory>(-1);

    for (int vi = 0; vi < static_cast<int>(visibleIndices.size()); ++vi) {
        int i = visibleIndices[vi];
        auto& le = layers_[i];

        // Group header when category changes
        if (showGroupHeaders && le.category != lastCat) {
            if (col > 0) { row++; col = 0; }
            auto* header = new QLabel(
                QString("\xe2\x80\x94 %1 \xe2\x80\x94").arg(layerCategoryName(le.category)));
            header->setStyleSheet(
                "font-size: 13px; font-weight: bold; color: #88aacc; padding: 8px 0 2px 5px;");
            grid_->addWidget(header, row, 0, 1, cols);
            groupHeaders_.push_back(header);
            row++;
            col = 0;
            lastCat = le.category;
        }

        // Create button
        auto* btn = new QToolButton;
        QString label = QString::fromStdString(le.name);
        if (le.channelCount > 0) label += QString("  [%1ch]").arg(le.channelCount);
        btn->setText(label);
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setStyleSheet(
            "QToolButton { background-color: #282828; border: 1px solid #3a3a3a; }");
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIconSize(QSize(thumbWidth_, thumbHeight_));

        if (!le.thumbnail.isNull()) {
            QPixmap pm = QPixmap::fromImage(
                le.thumbnail.scaled(thumbWidth_, thumbHeight_,
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            btn->setIcon(QIcon(pm));
        } else {
            btn->setIcon(QIcon(QPixmap::fromImage(makePlaceholder())));
        }

        std::string layerName = le.name;
        int layerIdx = i;
        connect(btn, &QToolButton::clicked, this,
                [this, layerName, layerIdx]() {
                    bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;
                    if (shift) {
                        layers_[layerIdx].pinned = !layers_[layerIdx].pinned;
                        layers_[layerIdx].selected = layers_[layerIdx].pinned;
                    } else {
                        currentLayer_ = layerName;
                        if (onLayerSelected_) onLayerSelected_(layerName);
                    }
                    for (int j = 0; j < static_cast<int>(layers_.size()); ++j)
                        updateSelectionStyle(j);
                    updateShuffleButton();
                });

        le.button = btn;
        grid_->addWidget(btn, row, col);
        col++;
        if (col >= cols) { row++; col = 0; }
    }

    // Restore selection highlights
    for (int i = 0; i < static_cast<int>(layers_.size()); ++i)
        updateSelectionStyle(i);

    newContainer->setUpdatesEnabled(true);
}

void InspectorDialog::updateProgress()
{
    int total = static_cast<int>(layers_.size());
    progressBar_->setMaximum(total);
    progressBar_->setValue(nextRenderIdx_);
    progressBar_->setFormat(QString("%1 / %2 layers").arg(nextRenderIdx_).arg(total));
    if (rendering_ && nextRenderIdx_ > 0 && nextRenderIdx_ < total) {
        double elapsed = perfTimer_.elapsed() / 1000.0;
        double perLayer = elapsed / nextRenderIdx_;
        double remaining = perLayer * (total - nextRenderIdx_);
        statusLabel_->setText(
            QString("Rendering... ~%1 s remaining  (%2 ms/layer, %3x proxy)")
                .arg(remaining, 0, 'f', 1).arg(perLayer * 1000, 0, 'f', 0).arg(proxyStep_));
    }
}

// ============================================================================
//  Filter / Proxy
// ============================================================================
void InspectorDialog::filterLayers(const QString& text)
{
    if (!scanned_) return;
    buildGrid();
}

void InspectorDialog::onProxyChanged(int comboIndex)
{
    int newStep = proxyCombo_->itemData(comboIndex).toInt();
    if (newStep == proxyStep_) return;
    proxyStep_ = newStep;
}

// ============================================================================
//  Selection helpers
// ============================================================================
void InspectorDialog::updateSelectionStyle(int layerIdx)
{
    auto& le = layers_[layerIdx];
    if (!le.button) return;
    bool isViewing = (le.name == currentLayer_);
    if (le.pinned && isViewing) {
        // Pinned + currently viewing: blue outer, pink inside
        le.button->setStyleSheet(
            "QToolButton { background-color: #cc6699; "
            "border: 3px solid #5599dd; "
            "margin: 0px; padding: 1px; }");
    } else if (le.pinned) {
        // Pinned only: blue border
        le.button->setStyleSheet(
            "QToolButton { background-color: #2a3a4a; border: 2px solid #5599dd; }");
    } else if (isViewing) {
        // Currently viewing: pink border
        le.button->setStyleSheet(
            "QToolButton { background-color: #3a2a3a; border: 2px solid #cc6699; }");
    } else {
        le.button->setStyleSheet(
            "QToolButton { background-color: #282828; border: 1px solid #3a3a3a; }");
    }
}

void InspectorDialog::updateShuffleButton()
{
    if (!shuffleBtn_) return;

    // Count exportable layers: all pinned + the currently viewed layer
    int count = 0;
    bool currentIncluded = false;
    for (auto& le : layers_) {
        if (le.pinned) {
            ++count;
            if (le.name == currentLayer_) currentIncluded = true;
        }
    }
    // Add current layer if it's not already pinned
    if (!currentLayer_.empty() && !currentIncluded) ++count;

    if (count == 0) {
        shuffleBtn_->setEnabled(false);
        shuffleBtn_->setText("Shuffle Export (shift+click to select)");
        shuffleBtn_->setStyleSheet(
            "QPushButton { font-weight: bold; background-color: #334455; }"
            "QPushButton:disabled { background-color: #333333; color: #666666; }");
    } else {
        shuffleBtn_->setEnabled(true);
        shuffleBtn_->setText(QString("Shuffle Export %1 layer%2")
            .arg(count).arg(count > 1 ? "s" : ""));

        // Shift from blue (#334455) toward pink (#aa3366) as more layers selected
        float t = std::min(1.0f, count / 15.0f);
        int r = static_cast<int>(0x33 + t * (0xaa - 0x33));
        int g = static_cast<int>(0x44 + t * (0x33 - 0x44));
        int b = static_cast<int>(0x55 + t * (0x66 - 0x55));
        shuffleBtn_->setStyleSheet(
            QString("QPushButton { font-weight: bold; background-color: #%1%2%3; }")
            .arg(r, 2, 16, QChar('0'))
            .arg(g, 2, 16, QChar('0'))
            .arg(b, 2, 16, QChar('0')));
    }
}
