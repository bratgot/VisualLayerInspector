#ifndef INSPECTOR_DIALOG_H
#define INSPECTOR_DIALOG_H

// ============================================================================
// InspectorDialog.h — Visual Layer Inspector v11
//
// v11: Modeless window — Nuke stays interactive, Viewer updates live.
//      show() from knob_changed, all Nuke work deferred via showEvent.
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
#include <QComboBox>
#include <QProgressBar>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <QElapsedTimer>
#include <QApplication>
#include <QShowEvent>

#include <string>
#include <vector>
#include <functional>

static constexpr const char* kVLI_Version = "v11";

// ============================================================================
//  Callback types
// ============================================================================
using RenderOneCallback = std::function<QImage(int layerIndex, int proxyStep)>;
using LayerCallback     = std::function<void(const std::string&)>;

struct PrepareResult {
    std::vector<std::string> layerNames;
    std::vector<int>         channelCounts;
    RenderOneCallback        renderOne;
    bool                     valid = false;
    std::string              errorMsg;
};
using PrepareCallback = std::function<PrepareResult()>;

// ============================================================================
//  Layer categories
// ============================================================================
enum class LayerCategory : int {
    Lighting = 0, Utility, Data, Cryptomatte, Custom
};

const char* layerCategoryName(LayerCategory cat);
LayerCategory classifyLayer(const std::string& name);

// ============================================================================
//  Per-layer data
// ============================================================================
struct LayerEntry {
    std::string   name;
    int           prepareIndex;
    int           channelCount;
    LayerCategory category;
    QImage        thumbnail;
};

// ============================================================================
//  Sort modes
// ============================================================================
enum class SortMode : int {
    Alphabetical_AZ = 0, Alphabetical_ZA, TypeGroup, ChannelCount, OriginalOrder
};

// ============================================================================
//  InspectorDialog
// ============================================================================
class InspectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit InspectorDialog(PrepareCallback prepare,
                             LayerCallback   onLayerSelected,
                             QWidget* parent = nullptr);
    ~InspectorDialog() override = default;

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void autoInit();
    void onStopResume();
    void onRegenerate();
    void renderNextThumbnail();
    void filterLayers(const QString& text);
    void onThumbnailSizeDrag(int value);
    void onThumbnailSizeRelease();
    void onProxyChanged(int comboIndex);
    void onSortChanged(int comboIndex);

private:
    void buildGrid();
    void resizeButtonsInPlace();
    void sortLayers();
    int  computeColumns() const;
    void beginRendering();
    void scheduleNextRender();
    void stopRendering();
    void updateProgress();
    void updateButtonThumbnail(int displayIndex);

    PrepareCallback           prepare_;
    LayerCallback             onLayerSelected_;
    RenderOneCallback         renderOne_;

    std::vector<LayerEntry>   layers_;

    int           nextRenderIdx_ = 0;
    bool          rendering_     = false;
    bool          scanned_       = false;
    bool          showFired_     = false;
    QElapsedTimer perfTimer_;
    int           proxyStep_     = 4;
    int           thumbWidth_    = 200;
    int           thumbHeight_   = 120;
    SortMode      sortMode_      = SortMode::TypeGroup;

    static constexpr int   kButtonPadding    = 10;
    static constexpr int   kMinThumbSize     = 80;
    static constexpr int   kMaxThumbSize     = 400;
    static constexpr int   kDefaultThumbSize = 200;
    static constexpr float kAspectRatio      = 0.6f;

    QPushButton*   stopBtn_        = nullptr;
    QPushButton*   regenBtn_       = nullptr;
    QScrollArea*   scrollArea_     = nullptr;
    QWidget*       container_      = nullptr;
    QGridLayout*   grid_           = nullptr;
    QLineEdit*     filterEdit_     = nullptr;
    QSlider*       sizeSlider_     = nullptr;
    QLabel*        sizeLabel_      = nullptr;
    QComboBox*     proxyCombo_     = nullptr;
    QComboBox*     sortCombo_      = nullptr;
    QProgressBar*  progressBar_    = nullptr;
    QLabel*        statusLabel_    = nullptr;
    QWidget*       controlsWidget_ = nullptr;

    struct ButtonEntry {
        int            layerIdx;
        QToolButton*   button;
    };
    std::vector<ButtonEntry> buttons_;
};

#endif // INSPECTOR_DIALOG_H
