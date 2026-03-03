// ============================================================================
// InspectorDialog.cpp — Visual Layer Inspector for Nuke 16
// Version 7
//
// Created by Marten Blumen
// ============================================================================

#include "InspectorDialog.h"

#include <algorithm>

// ============================================================================
//  Constructor — instant, zero Nuke work
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
    versionLabel_ = new QLabel(kVLI_Version);
    versionLabel_->setStyleSheet(
        "font-size: 14px; font-weight: bold; color: #66bb66; "
        "background-color: #224422; padding: 3px 10px; border-radius: 3px;");
    titleRow->addWidget(versionLabel_);
    mainLayout->addLayout(titleRow);

    auto* desc = new QLabel(
        "<b>Step 1:</b> Click <b>Scan Layers</b> to read channels from the input. "
        "<b>Step 2:</b> Click any layer name, or click <b>Generate Thumbnails</b> "
        "to preview them all.");
    desc->setStyleSheet("font-size: 13px; color: #bbbbbb; margin-bottom: 5px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    // --- Big Scan button ---
    scanBtn_ = new QPushButton("Scan Layers");
    scanBtn_->setFixedHeight(45);
    scanBtn_->setStyleSheet(
        "QPushButton { font-size: 16px; font-weight: bold; "
        "background-color: #336633; color: #ffffff; border-radius: 5px; }"
        "QPushButton:hover { background-color: #448844; }");
    connect(scanBtn_, &QPushButton::clicked, this, &InspectorDialog::onScanLayers);
    mainLayout->addWidget(scanBtn_);

    // --- Controls (hidden until scanned) ---
    controlsWidget_ = new QWidget;
    controlsWidget_->setVisible(false);
    auto* controlsLayout = new QVBoxLayout(controlsWidget_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);

    // Row: filter + size
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
    connect(sizeSlider_, &QSlider::valueChanged,
            this,        &InspectorDialog::onThumbnailSizeChanged);
    row1->addWidget(sizeSlider_);

    sizeLabel_ = new QLabel(QString::number(kDefaultThumbSize) + "px");
    sizeLabel_->setStyleSheet("font-size: 12px; color: #999999; min-width: 45px;");
    row1->addWidget(sizeLabel_);

    controlsLayout->addLayout(row1);

    // Row: generate + proxy + stop
    auto* row2 = new QHBoxLayout;

    generateBtn_ = new QPushButton("Generate Thumbnails");
    generateBtn_->setFixedHeight(35);
    generateBtn_->setMinimumWidth(180);
    generateBtn_->setStyleSheet(
        "QPushButton { font-size: 14px; font-weight: bold; "
        "background-color: #335577; color: #ffffff; border-radius: 4px; }"
        "QPushButton:hover { background-color: #446688; }"
        "QPushButton:disabled { background-color: #333333; color: #666666; }");
    connect(generateBtn_, &QPushButton::clicked,
            this,         &InspectorDialog::onGenerateThumbnails);
    row2->addWidget(generateBtn_);

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

    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setFixedHeight(30);
    stopBtn_->setMinimumWidth(80);
    stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
    stopBtn_->setEnabled(false);
    connect(stopBtn_, &QPushButton::clicked, this, &InspectorDialog::onStopResume);
    row2->addWidget(stopBtn_);

    row2->addStretch();

    controlsLayout->addLayout(row2);

    // Progress
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->setFixedHeight(18);
    progressBar_->setFormat("Ready");
    progressBar_->setStyleSheet(
        "QProgressBar { border: 1px solid #444; border-radius: 3px; "
        "background: #222; text-align: center; color: #ccc; font-size: 11px; }"
        "QProgressBar::chunk { background: #446644; }");
    controlsLayout->addWidget(progressBar_);

    statusLabel_ = new QLabel("Click a layer name to view it, or Generate Thumbnails.");
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
}

// ============================================================================
//  Scan Layers
// ============================================================================
void InspectorDialog::onScanLayers()
{
    if (!prepare_) return;

    scanBtn_->setEnabled(false);
    scanBtn_->setText("Scanning...");
    setCursor(Qt::WaitCursor);
    repaint();

    PrepareResult result = prepare_();

    setCursor(Qt::ArrowCursor);

    if (!result.valid) {
        scanBtn_->setEnabled(true);
        scanBtn_->setText("Scan Layers (retry)");
        controlsWidget_->setVisible(true);
        statusLabel_->setText(
            QString("Error: %1").arg(QString::fromStdString(result.errorMsg)));
        return;
    }

    layerNames_.assign(result.layerNames.rbegin(), result.layerNames.rend());
    thumbnailImages_.resize(layerNames_.size());
    renderOne_ = std::move(result.renderOne);
    scanned_ = true;

    scanBtn_->setVisible(false);
    controlsWidget_->setVisible(true);

    buildGrid();

    statusLabel_->setText(
        QString("Found %1 layers — click a name to view, or Generate Thumbnails.")
            .arg(layerNames_.size()));
}

// ============================================================================
//  Generate Thumbnails
// ============================================================================
void InspectorDialog::onGenerateThumbnails()
{
    if (!scanned_ || layerNames_.empty()) return;

    thumbnailImages_.assign(layerNames_.size(), QImage());
    buildGrid();

    rendering_ = true;
    nextRenderIdx_ = 0;
    perfTimer_.start();

    generateBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    stopBtn_->setText("Stop");
    stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");

    updateProgress();
    scheduleNextRender();
}

// ============================================================================
//  singleShot chaining
// ============================================================================
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
//  Stop / Resume
// ============================================================================
void InspectorDialog::stopRendering()
{
    rendering_ = false;
    generateBtn_->setEnabled(true);

    int done = nextRenderIdx_;
    int total = static_cast<int>(layerNames_.size());

    if (done >= total) {
        double elapsed = perfTimer_.elapsed() / 1000.0;
        statusLabel_->setText(
            QString("Done — %1 layers in %2 s").arg(total).arg(elapsed, 0, 'f', 2));
        stopBtn_->setEnabled(false);
        generateBtn_->setText("Regenerate Thumbnails");
    } else {
        statusLabel_->setText(
            QString("Paused — %1 / %2 rendered").arg(done).arg(total));
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
        generateBtn_->setEnabled(false);
        stopBtn_->setText("Stop");
        stopBtn_->setStyleSheet("font-weight: bold; background-color: #554433;");
        stopBtn_->setEnabled(true);
        scheduleNextRender();
    }
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
//  Filter / Size / Proxy
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

void InspectorDialog::onThumbnailSizeChanged(int value)
{
    thumbWidth_  = value;
    thumbHeight_ = static_cast<int>(value * kAspectRatio);
    sizeLabel_->setText(QString::number(value) + "px");
    if (scanned_) buildGrid();
}

void InspectorDialog::onProxyChanged(int comboIndex)
{
    proxyStep_ = proxyCombo_->itemData(comboIndex).toInt();
}
