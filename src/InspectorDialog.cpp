// ============================================================================
// InspectorDialog.cpp — Visual Layer Inspector v9
//
// v9: Smooth slider — resizes existing buttons in-place during drag
//     (no widget destruction), reflows grid columns on release only.
//
// Created by Marten Blumen
// ============================================================================

#include "InspectorDialog.h"

#include <algorithm>

// ============================================================================
//  Constructor
// ============================================================================
InspectorDialog::InspectorDialog(PrepareCallback prepare,
                                 LayerCallback   onLayerSelected,
                                 QWidget* parent)
    : QDialog(parent)
    , prepare_(std::move(prepare))
    , onLayerSelected_(std::move(onLayerSelected))
{
    setWindowTitle(QString("Visual Layer Inspector  [%1]").arg(kVLI_Version));
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    resize(1150, 850);
    setModal(true);

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

    // Row 1: filter + size
    auto* row1 = new QHBoxLayout;

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText("Filter layers (e.g., 'depth', 'spec')...");
    filterEdit_->setStyleSheet("font-size: 14px; padding: 5px;");
    connect(filterEdit_, &QLineEdit::textChanged,
            this,        &InspectorDialog::filterLayers);
    row1->addWidget(filterEdit_, 1);

    row1->addSpacing(15);
    row1->addWidget(new QLabel("Size:"));

    sizeSlider_ = new QSlider(Qt::Horizontal);
    sizeSlider_->setRange(kMinThumbSize, kMaxThumbSize);
    sizeSlider_->setValue(kDefaultThumbSize);
    sizeSlider_->setFixedWidth(160);
    // Live resize during drag — no rebuild, just resize existing buttons
    connect(sizeSlider_, &QSlider::valueChanged,
            this,        &InspectorDialog::onThumbnailSizeDrag);
    // Full grid reflow on release — recomputes column count
    connect(sizeSlider_, &QSlider::sliderReleased,
            this,        &InspectorDialog::onThumbnailSizeRelease);
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
    proxyCombo_->addItem("Full Quality",  1);
    proxyCombo_->addItem("2x Proxy",      2);
    proxyCombo_->addItem("4x Proxy",      4);
    proxyCombo_->addItem("8x Proxy",      8);
    proxyCombo_->setCurrentIndex(2);
    proxyCombo_->setToolTip("Higher proxy = faster but lower quality thumbnails.");
    connect(proxyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,        &InspectorDialog::onProxyChanged);
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
    container_  = new QWidget;
    grid_       = new QGridLayout(container_);
    scrollArea_->setWidget(container_);
    mainLayout->addWidget(scrollArea_, 1);

    // --- Footer ---
    auto* footerLayout = new QHBoxLayout;
    auto* credit = new QLabel(
        QString("Created by Marten Blumen  •  %1").arg(kVLI_Version));
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

    layerNames_.assign(result.layerNames.rbegin(), result.layerNames.rend());
    thumbnailImages_.resize(layerNames_.size());
    renderOne_ = std::move(result.renderOne);
    scanned_ = true;

    buildGrid();

    statusLabel_->setText(
        QString("Found %1 layers — generating thumbnails...")
            .arg(layerNames_.size()));

    QTimer::singleShot(1, this, [this]() { beginRendering(); });
}

// ============================================================================
//  Rendering
// ============================================================================
void InspectorDialog::beginRendering()
{
    if (!scanned_ || layerNames_.empty()) return;

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

    const int total = static_cast<int>(layerNames_.size());
    if (nextRenderIdx_ >= total) {
        stopRendering();
        return;
    }

    const int originalIdx = total - 1 - nextRenderIdx_;

    if (renderOne_) {
        QImage img = renderOne_(originalIdx, proxyStep_);
        thumbnailImages_[nextRenderIdx_] = std::move(img);
        updateButtonThumbnail(nextRenderIdx_);
    }

    ++nextRenderIdx_;
    updateProgress();

    if (nextRenderIdx_ >= total)
        stopRendering();
    else
        scheduleNextRender();
}

// ============================================================================
//  Stop / Resume / Regenerate
// ============================================================================
void InspectorDialog::stopRendering()
{
    rendering_ = false;
    regenBtn_->setEnabled(true);

    int done = nextRenderIdx_;
    int total = static_cast<int>(layerNames_.size());

    if (done >= total) {
        double elapsed = perfTimer_.elapsed() / 1000.0;
        statusLabel_->setText(
            QString("Done — %1 layers in %2 s").arg(total).arg(elapsed, 0, 'f', 2));
        stopBtn_->setEnabled(false);
    } else {
        statusLabel_->setText(
            QString("Paused — %1 / %2 rendered  (click any layer name to view it)")
                .arg(done).arg(total));
        stopBtn_->setText("Resume");
        stopBtn_->setStyleSheet("font-weight: bold; background-color: #335544;");
    }
}

void InspectorDialog::onStopResume()
{
    if (rendering_) {
        stopRendering();
    } else if (scanned_ && nextRenderIdx_ < static_cast<int>(layerNames_.size())) {
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
    if (!scanned_ || layerNames_.empty()) return;
    stopRendering();
    thumbnailImages_.assign(layerNames_.size(), QImage());
    buildGrid();
    beginRendering();
}

// ============================================================================
//  Smooth slider: live resize in-place (no rebuild)
// ============================================================================
void InspectorDialog::onThumbnailSizeDrag(int value)
{
    thumbWidth_  = value;
    thumbHeight_ = static_cast<int>(value * kAspectRatio);
    sizeLabel_->setText(QString::number(value) + "px");

    if (!scanned_ || buttons_.empty()) return;

    // Resize existing buttons without destroying them — fast path
    resizeButtonsInPlace();
}

void InspectorDialog::resizeButtonsInPlace()
{
    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;
    const QSize iconSize(thumbWidth_, thumbHeight_);

    for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
        auto* btn = buttons_[i].button;
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setIconSize(iconSize);

        // Re-scale existing thumbnail image if we have one
        if (i < static_cast<int>(thumbnailImages_.size()) &&
            !thumbnailImages_[i].isNull())
        {
            QPixmap pm = QPixmap::fromImage(
                thumbnailImages_[i].scaled(thumbWidth_, thumbHeight_,
                                           Qt::KeepAspectRatio,
                                           Qt::FastTransformation));
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
    // Full grid rebuild to reflow columns for new button size
    if (scanned_) buildGrid();
}

// ============================================================================
//  Grid
// ============================================================================
int InspectorDialog::computeColumns() const
{
    int available = scrollArea_ ? scrollArea_->viewport()->width() : 1100;
    int btnWidth  = thumbWidth_ + kButtonPadding;
    return std::max(1, available / btnWidth);
}

void InspectorDialog::buildGrid()
{
    for (auto& entry : buttons_) {
        entry.button->setParent(nullptr);
        delete entry.button;
    }
    buttons_.clear();

    const int btnWidth  = thumbWidth_ + kButtonPadding;
    const int btnHeight = thumbHeight_ + 40;
    const int cols = computeColumns();
    const QString filter = filterEdit_ ? filterEdit_->text().toLower() : QString();

    int gridIdx = 0;
    for (int i = 0; i < static_cast<int>(layerNames_.size()); ++i) {
        const auto& name = layerNames_[i];

        auto* btn = new QToolButton;
        btn->setText(QString::fromStdString(name));
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setIconSize(QSize(thumbWidth_, thumbHeight_));

        if (i < static_cast<int>(thumbnailImages_.size()) &&
            !thumbnailImages_[i].isNull())
        {
            QPixmap pm = QPixmap::fromImage(
                thumbnailImages_[i].scaled(thumbWidth_, thumbHeight_,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
            btn->setIcon(QIcon(pm));
        } else {
            QPixmap placeholder(thumbWidth_, thumbHeight_);
            placeholder.fill(QColor(40, 40, 40));
            btn->setIcon(QIcon(placeholder));
        }

        std::string layerName = name;
        connect(btn, &QToolButton::clicked, this,
                [this, layerName]() {
                    if (onLayerSelected_)
                        onLayerSelected_(layerName);
                });

        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool visible = filter.isEmpty() ||
                       lower.find(filter.toStdString()) != std::string::npos;
        btn->setVisible(visible);

        grid_->addWidget(btn, gridIdx / cols, gridIdx % cols);
        buttons_.push_back({name, btn});
        ++gridIdx;
    }
}

void InspectorDialog::updateButtonThumbnail(int index)
{
    if (index < 0 || index >= static_cast<int>(buttons_.size())) return;
    if (index >= static_cast<int>(thumbnailImages_.size())) return;
    const QImage& img = thumbnailImages_[index];
    if (img.isNull()) return;

    QPixmap pm = QPixmap::fromImage(
        img.scaled(thumbWidth_, thumbHeight_,
                   Qt::KeepAspectRatio, Qt::SmoothTransformation));
    buttons_[index].button->setIcon(QIcon(pm));
}

void InspectorDialog::updateProgress()
{
    int total = static_cast<int>(layerNames_.size());
    progressBar_->setMaximum(total);
    progressBar_->setValue(nextRenderIdx_);
    progressBar_->setFormat(
        QString("%1 / %2 layers").arg(nextRenderIdx_).arg(total));

    if (rendering_ && nextRenderIdx_ > 0 && nextRenderIdx_ < total) {
        double elapsed = perfTimer_.elapsed() / 1000.0;
        double perLayer = elapsed / nextRenderIdx_;
        double remaining = perLayer * (total - nextRenderIdx_);
        statusLabel_->setText(
            QString("Rendering... ~%1 s remaining  (%2 ms/layer, %3x proxy)")
                .arg(remaining, 0, 'f', 1)
                .arg(perLayer * 1000, 0, 'f', 0)
                .arg(proxyStep_));
    }
}

// ============================================================================
//  Filter / Proxy
// ============================================================================
void InspectorDialog::filterLayers(const QString& text)
{
    const std::string filter = text.toLower().toStdString();
    for (auto& entry : buttons_) {
        std::string lower = entry.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        entry.button->setVisible(lower.find(filter) != std::string::npos);
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
