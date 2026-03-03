#ifndef INSPECTOR_DIALOG_H
#define INSPECTOR_DIALOG_H

// ============================================================================
// InspectorDialog.h — Visual Layer Inspector v18.3
//
// v18.3: Batch all layout changes via setUpdatesEnabled(false/true) —
//        slider drag, reflow, sort, and buildGrid all batch into ONE repaint.
//        Buttons with no thumbnail show a dark placeholder outline.
//      without destroying/recreating. All button for category checkboxes.
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
#include <QCheckBox>
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
#include <map>
#include <functional>

static constexpr const char* kVLI_Version = "v18.3";

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
    QToolButton*  button = nullptr;   // persistent button — survives sort
};

// ============================================================================
//  Sort modes
// ============================================================================
enum class SortMode : int {
    Alphabetical = 0, TypeGroup, ChannelCount, OriginalOrder
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
    void onReverseToggle();
    void onCategoryToggle();
    void onCatAll();

private:
    void buildGrid();
    void reorderGridFast();
    void resizeButtonsInPlace();
    void reflowGridFast();
    void sortLayers();
    void applyVisibility();
    int  computeColumns() const;
    QImage makePlaceholder() const;
    void beginRendering();
    void scheduleNextRender();
    void stopRendering();
    void updateProgress();
    void updateCategoryCounts();

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
    bool          sortReversed_  = false;
    int           lastColumnCount_ = 0;

    static constexpr int   kButtonPadding    = 10;
    static constexpr int   kMinThumbSize     = 80;
    static constexpr int   kMaxThumbSize     = 400;
    static constexpr int   kDefaultThumbSize = 200;
    static constexpr float kAspectRatio      = 0.6f;

    QPushButton*   stopBtn_        = nullptr;
    QPushButton*   regenBtn_       = nullptr;
    QPushButton*   reverseBtn_     = nullptr;
    QPushButton*   catAllBtn_      = nullptr;
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
    QLabel*        emptyLabel_     = nullptr;
    std::map<LayerCategory, QCheckBox*> categoryChecks_;
    std::vector<QLabel*>     groupHeaders_;
};

#endif // INSPECTOR_DIALOG_H
