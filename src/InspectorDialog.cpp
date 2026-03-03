// ============================================================================
// InspectorDialog.cpp — Visual Layer Inspector v12
//
// v12: Smooth grid reflow during slider drag — columns update in real time.
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
        case SortMode::Alphabetical_AZ:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    return toLower(a.name) < toLower(b.name);
                });
            break;
        case SortMode::Alphabetical_ZA:
            std::sort(layers_.begin(), layers_.end(),
                [](const LayerEntry& a, const LayerEntry& b) {
                    return toLower(a.name) > toLower(b.name);
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
}

// ============================================================================
//  Constructor — MODELESS, no setModal
// ============================================================================
InspectorDialog::InspectorDialog(PrepareCallback prepare,
                                 LayerCallback   onLayerSelected,
                                 QWidget* parent)
    : QDialog(parent)
    , prepare_(std::move(prepare))
    , onLayerSelected_(std::move(onLayerSelected))
{
    setWindowTitle(QString("Visual Layer Inspector  [%1]").arg(kVLI_Version));
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    resize(1150, 850);
    // NOT modal — Nuke stays interactive

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
        "Thumbnails generate automatically — use <b>Stop</b> to pause.");
    desc->setStyleSheet("font-size: 13px; color: #bbbbbb; margin-bottom: 5px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    // --- Controls ---
    controlsWidget_ = new QWidget;
    controlsWidget_->setVisible(false);
    auto* controlsLayout = new QVBoxLayout(controlsWidget_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);

    // Row 1: filter + sort + size
    auto* row1 = new QHBoxLayout;

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText("Filter layers (e.g., 'depth', 'spec')...");
    filterEdit_->setStyleSheet("font-size: 14px; padding: 5px;");
    connect(filterEdit_, &QLineEdit::textChanged, this, &InspectorDialog::filterLayers);
    row1->addWidget(filterEdit_, 1);

    row1->addSpacing(10);
    row1->addWidget(new QLabel("Sort:"));

    sortCombo_ = new QComboBox;
    sortCombo_->addItem("A \xe2\x86\x92 Z",       static_cast<int>(SortMode::Alphabetical_AZ));
    sortCombo_->addItem("Z \xe2\x86\x92 A",       static_cast<int>(SortMode::Alphabetical_ZA));
    sortCombo_->addItem("Type Group",               static_cast<int>(SortMode::TypeGroup));
    sortCombo_->addItem("Channel Count",            static_cast<int>(SortMode::ChannelCount));
    sortCombo_->addItem("Original Order",           static_cast<int>(SortMode::OriginalOrder));
    sortCombo_->setCurrentIndex(2);
    sortCombo_->setToolTip(
        "Type Group auto-categorises layers:\n"
        "  Lighting \xe2\x80\x94 diffuse, specular, reflection, emission, sss...\n"
        "  Utility \xe2\x80\x94 depth, normal, position, motion, uv, ao...\n"
        "  Data \xe2\x80\x94 id, mask, matte, object, material...\n"
        "  Cryptomatte \xe2\x80\x94 crypto*\n"
        "  Custom \xe2\x80\x94 everything else");
    connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorDialog::onSortChanged);
    row1->addWidget(sortCombo_);

    row1->addSpacing(10);
    row1->addWidget(new QLabel("Size:"));

    sizeSlider_ = new QSlider(Qt::Horizontal);
    sizeSlider_->setRange(kMinThumbSize, kMaxThumbSize);
    sizeSlider_->setValue(kDefaultThumbSize);
    sizeSlider_->setFixedWidth(140);
    connect(sizeSlider_, &QSlider::valueChanged, this, &InspectorDialog::onThumbnailSizeDrag);
    connect(sizeSlider_, &QSlider::sliderReleased, this, &InspectorDialog::onThumbnailSizeRelease);
    row1->addWidget(sizeSlider_);

    sizeLabel_ = new QLabel(QString::number(kDefaultThumbSize) + "px");
    sizeLabel_->setStyleSheet("font-size: 12px; color: #999999; min-width: 45px;");
    row1->addWidget(sizeLabel_);

    controlsLayout->addLayout(row1);

    // Row 2: stop + proxy + regenerate
    auto* row2 = new QHBoxLayout;

    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setFixedHeight(30);
    stopBtn_->setMinimumWidth(90);
    stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
    connect(stopBtn_, &QPushButton::clicked, this, &InspectorDialog::onStopResume);
    row2->addWidget(stopBtn_);

    row2->addSpacing(10);
    row2->addWidget(new QLabel("Proxy:"));

    proxyCombo_ = new QComboBox;
    proxyCombo_->addItem("Full Quality", 1);
    proxyCombo_->addItem("2x Proxy",     2);
    proxyCombo_->addItem("4x Proxy",     4);
    proxyCombo_->addItem("8x Proxy",     8);
    proxyCombo_->setCurrentIndex(2);
    connect(proxyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorDialog::onProxyChanged);
    row2->addWidget(proxyCombo_);

    row2->addSpacing(10);

    regenBtn_ = new QPushButton("Regenerate");
    regenBtn_->setFixedHeight(30);
    regenBtn_->setMinimumWidth(110);
    regenBtn_->setStyleSheet(
        "QPushButton { font-weight: bold; background-color: #335533; }"
        "QPushButton:disabled { background-color: #333333; color: #666666; }");
    regenBtn_->setEnabled(false);
    connect(regenBtn_, &QPushButton::clicked, this, &InspectorDialog::onRegenerate);
    row2->addWidget(regenBtn_);

    row2->addStretch();
    controlsLayout->addLayout(row2);

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

    // --- Footer ---
    auto* footerLayout = new QHBoxLayout;
    auto* credit = new QLabel(
        QString("Created by Marten Blumen  \xe2\x80\xa2  %1").arg(kVLI_Version));
    credit->setStyleSheet("font-size: 11px; color: #777777; font-style: italic;");
    footerLayout->addWidget(credit);
    footerLayout->addStretch();

    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(35);
    closeBtn->setMinimumWidth(100);
    closeBtn->setStyleSheet("font-weight: bold; background-color: #553333;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    footerLayout->addWidget(closeBtn);

    mainLayout->addLayout(footerLayout);

    controlsWidget_->setVisible(true);
}

// ============================================================================
//  showEvent → auto-init
// ============================================================================
void InspectorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!showFired_) {
        showFired_ = true;
        QTimer::singleShot(0, this, &InspectorDialog::autoInit);
    }
}

// ============================================================================
//  Auto init
// ============================================================================
void InspectorDialog::autoInit()
{
    if (scanned_ || !prepare_) return;

    statusLabel_->setText("Reading EXR headers (first open may be slow)...");
    setCursor(Qt::WaitCursor);
    repaint();

    PrepareResult result = prepare_();

    setCursor(Qt::ArrowCursor);

    if (!result.valid) {
        statusLabel_->setText(
            QString("Error: %1").arg(QString::fromStdString(result.errorMsg)));
        progressBar_->setRange(0, 1);
        progressBar_->setValue(0);
        progressBar_->setFormat("Failed");
        return;
    }

    renderOne_ = std::move(result.renderOne);

    layers_.clear();
    layers_.reserve(result.layerNames.size());
    for (int i = 0; i < static_cast<int>(result.layerNames.size()); ++i) {
        LayerEntry le;
        le.name = result.layerNames[i];
        le.prepareIndex = i;
        le.channelCount = (i < static_cast<int>(result.channelCounts.size()))
                              ? result.channelCounts[i] : 0;
        le.category = classifyLayer(le.name);
        layers_.push_back(std::move(le));
    }

    sortLayers();
    scanned_ = true;
    buildGrid();

    int nLight = 0, nUtil = 0, nData = 0, nCrypto = 0, nCustom = 0;
    for (const auto& le : layers_) {
        switch (le.category) {
            case LayerCategory::Lighting:    ++nLight;  break;
            case LayerCategory::Utility:     ++nUtil;   break;
            case LayerCategory::Data:        ++nData;   break;
            case LayerCategory::Cryptomatte: ++nCrypto; break;
            case LayerCategory::Custom:      ++nCustom; break;
        }
    }
    statusLabel_->setText(
        QString("Found %1 layers (%2 lighting, %3 utility, %4 data, %5 crypto, %6 custom)")
            .arg(layers_.size()).arg(nLight).arg(nUtil).arg(nData).arg(nCrypto).arg(nCustom));

    QTimer::singleShot(1, this, [this]() { beginRendering(); });
}

// ============================================================================
//  Rendering
// ============================================================================
void InspectorDialog::beginRendering()
{
    if (!scanned_ || layers_.empty()) return;
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
    QTimer::singleShot(1, this, &InspectorDialog::renderNextThumbnail);
}

void InspectorDialog::renderNextThumbnail()
{
    if (!rendering_) return;
    const int total = static_cast<int>(layers_.size());
    if (nextRenderIdx_ >= total) { stopRendering(); return; }

    auto& entry = layers_[nextRenderIdx_];
    if (renderOne_) {
        QImage img = renderOne_(entry.prepareIndex, proxyStep_);
        entry.thumbnail = std::move(img);
        updateButtonThumbnail(nextRenderIdx_);
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
//  Sort changed
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
    buildGrid();
    if (wasRendering || nextRenderIdx_ < static_cast<int>(layers_.size())) {
        nextRenderIdx_ = 0;
        for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
            if (layers_[i].thumbnail.isNull()) { nextRenderIdx_ = i; break; }
            nextRenderIdx_ = i + 1;
        }
        if (nextRenderIdx_ < static_cast<int>(layers_.size()))
            beginRendering();
    }
}

// ============================================================================
//  Smooth slider
// ============================================================================
void InspectorDialog::onThumbnailSizeDrag(int value)
{
    thumbWidth_  = value;
    thumbHeight_ = static_cast<int>(value * kAspectRatio);
    sizeLabel_->setText(QString::number(value) + "px");
    if (!scanned_ || buttons_.empty()) return;

    int newCols = computeColumns();
    if (newCols != lastColumnCount_) {
        // Column count changed — full grid rebuild (reflow)
        buildGrid();
    } else {
        // Same columns — just resize in place (fast)
        resizeButtonsInPlace();
    }
}

void InspectorDialog::resizeButtonsInPlace()
{
    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;
    const QSize iconSize(thumbWidth_, thumbHeight_);
    for (auto& be : buttons_) {
        auto* btn = be.button;
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setIconSize(iconSize);
        const auto& le = layers_[be.layerIdx];
        if (!le.thumbnail.isNull()) {
            QPixmap pm = QPixmap::fromImage(
                le.thumbnail.scaled(thumbWidth_, thumbHeight_, Qt::KeepAspectRatio, Qt::FastTransformation));
            btn->setIcon(QIcon(pm));
        } else {
            QPixmap placeholder(thumbWidth_, thumbHeight_);
            placeholder.fill(QColor(40, 40, 40));
            btn->setIcon(QIcon(placeholder));
        }
    }
}

void InspectorDialog::onThumbnailSizeRelease()
{
    if (scanned_) buildGrid();
}

// ============================================================================
//  Grid
// ============================================================================
int InspectorDialog::computeColumns() const
{
    int available = scrollArea_ ? scrollArea_->viewport()->width() : 1100;
    return std::max(1, available / (thumbWidth_ + kButtonPadding));
}

void InspectorDialog::buildGrid()
{
    for (auto& be : buttons_) { be.button->setParent(nullptr); delete be.button; }
    buttons_.clear();

    // Clear group headers
    while (grid_->count()) {
        QLayoutItem* item = grid_->takeAt(0);
        if (item->widget()) { item->widget()->setParent(nullptr); delete item->widget(); }
        delete item;
    }

    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;
    const int cols = computeColumns();
    lastColumnCount_ = cols;
    const QString filter = filterEdit_ ? filterEdit_->text().toLower() : QString();

    LayerCategory lastCat = LayerCategory::Custom;
    bool showGroupHeaders = (sortMode_ == SortMode::TypeGroup);
    int gridIdx = 0;

    for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
        const auto& le = layers_[i];

        if (showGroupHeaders && (i == 0 || le.category != lastCat)) {
            if (gridIdx % cols != 0) gridIdx += cols - (gridIdx % cols);
            auto* header = new QLabel(
                QString("\xe2\x80\x94 %1 \xe2\x80\x94").arg(layerCategoryName(le.category)));
            header->setStyleSheet(
                "font-size: 13px; font-weight: bold; color: #88aacc; padding: 8px 0 2px 5px;");
            grid_->addWidget(header, gridIdx / cols, 0, 1, cols);
            gridIdx += cols;
            lastCat = le.category;
        }

        auto* btn = new QToolButton;
        QString label = QString::fromStdString(le.name);
        if (le.channelCount > 0) label += QString("  [%1ch]").arg(le.channelCount);
        btn->setText(label);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setIconSize(QSize(thumbWidth_, thumbHeight_));

        if (!le.thumbnail.isNull()) {
            QPixmap pm = QPixmap::fromImage(
                le.thumbnail.scaled(thumbWidth_, thumbHeight_, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            btn->setIcon(QIcon(pm));
        } else {
            QPixmap placeholder(thumbWidth_, thumbHeight_);
            placeholder.fill(QColor(40, 40, 40));
            btn->setIcon(QIcon(placeholder));
        }

        std::string layerName = le.name;
        connect(btn, &QToolButton::clicked, this,
                [this, layerName]() { if (onLayerSelected_) onLayerSelected_(layerName); });

        std::string lower = toLower(le.name);
        btn->setVisible(filter.isEmpty() || lower.find(filter.toStdString()) != std::string::npos);

        grid_->addWidget(btn, gridIdx / cols, gridIdx % cols);
        buttons_.push_back({i, btn});
        ++gridIdx;
    }
}

void InspectorDialog::updateButtonThumbnail(int displayIndex)
{
    for (auto& be : buttons_) {
        if (be.layerIdx == displayIndex) {
            const auto& le = layers_[displayIndex];
            if (le.thumbnail.isNull()) return;
            QPixmap pm = QPixmap::fromImage(
                le.thumbnail.scaled(thumbWidth_, thumbHeight_, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            be.button->setIcon(QIcon(pm));
            return;
        }
    }
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
    const std::string filter = toLower(text.toStdString());
    for (auto& be : buttons_) {
        const auto& le = layers_[be.layerIdx];
        be.button->setVisible(toLower(le.name).find(filter) != std::string::npos);
    }
}

void InspectorDialog::onProxyChanged(int comboIndex)
{
    int newStep = proxyCombo_->itemData(comboIndex).toInt();
    if (newStep == proxyStep_) return;
    proxyStep_ = newStep;
    if (!scanned_) return;
    onRegenerate();
}
