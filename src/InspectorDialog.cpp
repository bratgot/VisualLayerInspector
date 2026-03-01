// ============================================================================
// InspectorDialog.cpp — Visual Layer Inspector for Nuke 16
//
// Created by Marten Blumen
// ============================================================================

#include "InspectorDialog.h"

#include <algorithm>

// ============================================================================
//  Construction
// ============================================================================
InspectorDialog::InspectorDialog(const std::vector<LayerThumbnail>& thumbnails,
                                 LayerCallback  onLayerSelected,
                                 RefreshCallback onRefresh,
                                 QWidget* parent)
    : QDialog(parent)
    , onLayerSelected_(std::move(onLayerSelected))
    , onRefresh_(std::move(onRefresh))
{
    // Store thumbnails in reverse order
    thumbnails_.assign(thumbnails.rbegin(), thumbnails.rend());

    setWindowTitle("Visual Layer Inspector");
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    resize(1150, 850);

    mainLayout_ = new QVBoxLayout(this);

    // --- Title ---
    auto* title = new QLabel("Visual Layer Inspector");
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #eeeeee;");
    mainLayout_->addWidget(title);

    auto* desc = new QLabel(
        "<b>Instructions:</b> Click any thumbnail to view that layer. "
        "Use <b>Update Frame</b> to re-capture thumbnails at the current frame.");
    desc->setStyleSheet("font-size: 13px; color: #bbbbbb; margin-bottom: 5px;");
    desc->setWordWrap(true);
    mainLayout_->addWidget(desc);

    // --- Controls row: filter + size slider + update button ---
    auto* controlsLayout = new QHBoxLayout;

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText("Filter layers (e.g., 'depth', 'spec')...");
    filterEdit_->setStyleSheet("font-size: 14px; padding: 5px;");
    connect(filterEdit_, &QLineEdit::textChanged,
            this,        &InspectorDialog::filterLayers);
    controlsLayout->addWidget(filterEdit_, 1);

    controlsLayout->addSpacing(15);

    auto* sizeIcon = new QLabel("Size:");
    sizeIcon->setStyleSheet("font-size: 13px; color: #bbbbbb;");
    controlsLayout->addWidget(sizeIcon);

    sizeSlider_ = new QSlider(Qt::Horizontal);
    sizeSlider_->setRange(kMinThumbSize, kMaxThumbSize);
    sizeSlider_->setValue(kDefaultThumbSize);
    sizeSlider_->setFixedWidth(160);
    sizeSlider_->setToolTip("Adjust thumbnail size");
    connect(sizeSlider_, &QSlider::valueChanged,
            this,        &InspectorDialog::onThumbnailSizeChanged);
    controlsLayout->addWidget(sizeSlider_);

    sizeLabel_ = new QLabel(QString::number(kDefaultThumbSize) + "px");
    sizeLabel_->setStyleSheet("font-size: 12px; color: #999999; min-width: 45px;");
    controlsLayout->addWidget(sizeLabel_);

    controlsLayout->addSpacing(15);

    auto* updateBtn = new QPushButton("Update Frame");
    updateBtn->setFixedHeight(30);
    updateBtn->setMinimumWidth(110);
    updateBtn->setStyleSheet("font-weight: bold; background-color: #335533;");
    updateBtn->setToolTip("Re-render thumbnails at the current frame");
    connect(updateBtn, &QPushButton::clicked,
            this,      &InspectorDialog::onUpdateFrame);
    controlsLayout->addWidget(updateBtn);

    mainLayout_->addLayout(controlsLayout);

    // --- Scrollable grid ---
    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    container_  = new QWidget;
    grid_       = new QGridLayout(container_);
    mainLayout_->addWidget(scrollArea_);

    buildGrid();
    scrollArea_->setWidget(container_);

    // --- Footer ---
    auto* footerLayout = new QHBoxLayout;

    auto* credit = new QLabel("Created by Marten Blumen");
    credit->setStyleSheet("font-size: 11px; color: #777777; font-style: italic;");
    footerLayout->addWidget(credit);

    footerLayout->addStretch();

    auto* closeBtn = new QPushButton("Close Inspector");
    closeBtn->setFixedHeight(35);
    closeBtn->setMinimumWidth(120);
    closeBtn->setStyleSheet("font-weight: bold; background-color: #553333;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    footerLayout->addWidget(closeBtn);

    mainLayout_->addLayout(footerLayout);
}

// ============================================================================
//  Compute column count from current thumb width
// ============================================================================
int InspectorDialog::computeColumns() const
{
    int available = scrollArea_ ? scrollArea_->viewport()->width() : 1100;
    int btnWidth  = thumbWidth_ + kButtonPadding;
    return std::max(1, available / btnWidth);
}

// ============================================================================
//  Build the thumbnail grid
// ============================================================================
void InspectorDialog::buildGrid()
{
    // Clear existing buttons
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
    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        const auto& thumb = thumbnails_[i];

        auto* btn = new QToolButton;
        btn->setText(QString::fromStdString(thumb.name));
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(btnWidth, btnHeight);
        btn->setIconSize(QSize(thumbWidth_, thumbHeight_));

        if (!thumb.image.isNull()) {
            QPixmap pm = QPixmap::fromImage(
                thumb.image.scaled(thumbWidth_, thumbHeight_,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
            btn->setIcon(QIcon(pm));
        }

        // Direct callback — bypasses Qt metatype registration issues
        std::string layerName = thumb.name;
        connect(btn, &QToolButton::clicked, this,
                [this, layerName]() {
                    if (onLayerSelected_)
                        onLayerSelected_(layerName);
                });

        // Apply current filter
        std::string lower = thumb.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool visible = filter.isEmpty() ||
                       lower.find(filter.toStdString()) != std::string::npos;
        btn->setVisible(visible);

        grid_->addWidget(btn, gridIdx / cols, gridIdx % cols);
        buttons_.push_back({thumb.name, btn});
        ++gridIdx;
    }
}

// ============================================================================
//  Filter
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

// ============================================================================
//  Slider changed
// ============================================================================
void InspectorDialog::onThumbnailSizeChanged(int value)
{
    thumbWidth_  = value;
    thumbHeight_ = static_cast<int>(value * kAspectRatio);
    sizeLabel_->setText(QString::number(value) + "px");
    buildGrid();
}

// ============================================================================
//  Update Frame — re-render thumbnails at current frame
// ============================================================================
void InspectorDialog::onUpdateFrame()
{
    if (!onRefresh_)
        return;

    setCursor(Qt::WaitCursor);

    std::vector<LayerThumbnail> fresh = onRefresh_();

    // Store in reverse order
    thumbnails_.assign(fresh.rbegin(), fresh.rend());

    buildGrid();

    setCursor(Qt::ArrowCursor);
}
