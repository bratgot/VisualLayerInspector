#ifndef INSPECTOR_DIALOG_H
#define INSPECTOR_DIALOG_H

// ============================================================================
// InspectorDialog.h — Visual Layer Inspector for Nuke 16
//
// Created by Marten Blumen
// ============================================================================

#include <QDialog>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QImage>
#include <QPixmap>
#include <QIcon>

#include <string>
#include <vector>
#include <functional>

// ============================================================================
//  LayerThumbnail
// ============================================================================
struct LayerThumbnail {
    std::string  name;
    QImage       image;
};

// ============================================================================
//  InspectorDialog
// ============================================================================
class InspectorDialog : public QDialog {
    Q_OBJECT

public:
    /// Called when user clicks a thumbnail — arg is layer name.
    using LayerCallback   = std::function<void(const std::string&)>;

    /// Called when user clicks Update Frame — should return fresh thumbnails.
    using RefreshCallback = std::function<std::vector<LayerThumbnail>()>;

    explicit InspectorDialog(const std::vector<LayerThumbnail>& thumbnails,
                             LayerCallback  onLayerSelected,
                             RefreshCallback onRefresh,
                             QWidget* parent = nullptr);
    ~InspectorDialog() override = default;

private slots:
    void filterLayers(const QString& text);
    void onThumbnailSizeChanged(int value);
    void onUpdateFrame();

private:
    void buildGrid();
    int  computeColumns() const;

    std::vector<LayerThumbnail> thumbnails_;   // stored in reverse order
    LayerCallback               onLayerSelected_;
    RefreshCallback             onRefresh_;

    int thumbWidth_  = 200;
    int thumbHeight_ = 120;

    static constexpr int kButtonPadding    = 10;
    static constexpr int kMinThumbSize     = 80;
    static constexpr int kMaxThumbSize     = 400;
    static constexpr int kDefaultThumbSize = 200;
    static constexpr float kAspectRatio    = 0.6f;

    QVBoxLayout*   mainLayout_  = nullptr;
    QScrollArea*   scrollArea_  = nullptr;
    QWidget*       container_   = nullptr;
    QGridLayout*   grid_        = nullptr;
    QLineEdit*     filterEdit_  = nullptr;
    QSlider*       sizeSlider_  = nullptr;
    QLabel*        sizeLabel_   = nullptr;

    struct ButtonEntry {
        std::string   name;
        QToolButton*  button;
    };
    std::vector<ButtonEntry> buttons_;
};

#endif // INSPECTOR_DIALOG_H
