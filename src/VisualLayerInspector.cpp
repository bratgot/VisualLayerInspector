// ============================================================================
// VisualLayerInspector.cpp — Nuke 16 NDK Plugin
//
// A pass-through NoIop that provides a "Launch Inspector" button. When
// clicked, it reads all layers from the input via the Tile API, renders
// down-sampled thumbnails into QImages, and opens a Qt dialog grid.
//
// Viewer channel switching uses the Python C API directly (PyGILState +
// PyRun_SimpleString) so it works reliably from Qt callbacks, unlike
// Op::script_command which requires the Nuke script engine context.
//
// Created by Marten Blumen
// ============================================================================

// Python.h must come before any standard headers on Windows/MSVC
#include <Python.h>

#include "DDImage/NoIop.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Tile.h"
#include "DDImage/Channel.h"
#include "DDImage/Row.h"

// FN_EXPORT fallback
#ifndef FN_EXPORT
  #ifdef _WIN32
    #define FN_EXPORT __declspec(dllexport)
  #else
    #define FN_EXPORT __attribute__((visibility("default")))
  #endif
#endif

#include "InspectorDialog.h"

#include <QApplication>

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DD::Image;

// ============================================================================
//  Constants
// ============================================================================
static const char* const kClass = "VisualLayerInspector";
static const char* const kHelp  =
    "Visual Layer Inspector\n\n"
    "Connect any node with multiple layers/AOVs and press 'Launch Inspector' "
    "to open a thumbnail grid of every layer. Click a thumbnail to switch the "
    "active Viewer to that layer.\n\n"
    "Thumbnails are rendered via the NDK Tile API from cached image data.";

static constexpr int kThumbMaxWidth  = 240;
static constexpr int kThumbMaxHeight = 160;

// ============================================================================
//  Python helper — run a Python command using the embedded interpreter
// ============================================================================
static void runPython(const std::string& cmd)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyRun_SimpleString(cmd.c_str());
    PyGILState_Release(gstate);
}

// ============================================================================
//  Helpers
// ============================================================================

/// Collect unique layer names from a ChannelSet.
static std::vector<std::string> collectLayers(const ChannelSet& channels)
{
    std::set<std::string> layerSet;
    foreach (z, channels) {
        const std::string name = getName(z);
        const size_t dot = name.find('.');
        if (dot != std::string::npos)
            layerSet.insert(name.substr(0, dot));
    }
    return {layerSet.begin(), layerSet.end()};
}

/// For a given layer name, find which channels map to R, G, B, A.
struct LayerChannels {
    Channel r = Chan_Black;
    Channel g = Chan_Black;
    Channel b = Chan_Black;
    Channel a = Chan_Black;
    int count = 0;
};

static LayerChannels resolveLayerChannels(const std::string& layer,
                                          const ChannelSet& allChannels)
{
    LayerChannels lc;
    std::vector<Channel> found;

    foreach (z, allChannels) {
        const std::string name = getName(z);
        const size_t dot = name.find('.');
        if (dot == std::string::npos) continue;
        if (name.substr(0, dot) != layer) continue;

        found.push_back(z);
        const std::string suffix = name.substr(dot + 1);

        if (suffix == "red"   || suffix == "r" || suffix == "x") lc.r = z;
        else if (suffix == "green" || suffix == "g" || suffix == "y") lc.g = z;
        else if (suffix == "blue"  || suffix == "b" || suffix == "z") lc.b = z;
        else if (suffix == "alpha" || suffix == "a")                  lc.a = z;
    }

    lc.count = static_cast<int>(found.size());

    if (lc.r == Chan_Black && found.size() > 0) lc.r = found[0];
    if (lc.g == Chan_Black && found.size() > 1) lc.g = found[1];
    if (lc.b == Chan_Black && found.size() > 2) lc.b = found[2];

    if (found.size() == 1) {
        lc.g = lc.r;
        lc.b = lc.r;
    }

    return lc;
}

/// Clamp a float to [0, 1] and convert to uint8.
static inline uint8_t floatToByte(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    v = std::pow(v, 1.0f / 2.2f);
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

/// Render a thumbnail QImage for a layer.
static QImage renderThumbnail(Iop& input, const LayerChannels& lc,
                              const Box& bbox)
{
    const int srcW = bbox.w();
    const int srcH = bbox.h();
    if (srcW <= 0 || srcH <= 0)
        return {};

    const float aspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    int thumbW, thumbH;
    if (aspect >= 1.0f) {
        thumbW = kThumbMaxWidth;
        thumbH = std::max(1, static_cast<int>(kThumbMaxWidth / aspect));
    } else {
        thumbH = kThumbMaxHeight;
        thumbW = std::max(1, static_cast<int>(kThumbMaxHeight * aspect));
    }

    ChannelSet requestChans;
    if (lc.r != Chan_Black) requestChans += lc.r;
    if (lc.g != Chan_Black) requestChans += lc.g;
    if (lc.b != Chan_Black) requestChans += lc.b;
    if (requestChans.empty())
        return {};

    Tile tile(input, bbox.x(), bbox.y(), bbox.r(), bbox.t(), requestChans);
    if (!tile.valid())
        return {};

    QImage img(thumbW, thumbH, QImage::Format_RGB32);
    img.fill(Qt::black);

    const float scaleX = static_cast<float>(srcW) / thumbW;
    const float scaleY = static_cast<float>(srcH) / thumbH;

    for (int ty = 0; ty < thumbH; ++ty) {
        const int srcY0 = bbox.y() + static_cast<int>(ty * scaleY);
        const int srcY1 = std::min(bbox.y() + static_cast<int>((ty + 1) * scaleY),
                                   bbox.t() - 1);

        for (int tx = 0; tx < thumbW; ++tx) {
            const int srcX0 = bbox.x() + static_cast<int>(tx * scaleX);
            const int srcX1 = std::min(bbox.x() + static_cast<int>((tx + 1) * scaleX),
                                       bbox.r() - 1);

            float sumR = 0, sumG = 0, sumB = 0;
            int   count = 0;

            for (int sy = srcY0; sy <= srcY1; ++sy) {
                for (int sx = srcX0; sx <= srcX1; ++sx) {
                    if (lc.r != Chan_Black) sumR += tile[lc.r][sy][sx];
                    if (lc.g != Chan_Black) sumG += tile[lc.g][sy][sx];
                    if (lc.b != Chan_Black) sumB += tile[lc.b][sy][sx];
                    ++count;
                }
            }

            if (count > 0) {
                const float inv = 1.0f / count;
                const int flippedY = thumbH - 1 - ty;
                img.setPixel(tx, flippedY,
                             qRgb(floatToByte(sumR * inv),
                                  floatToByte(sumG * inv),
                                  floatToByte(sumB * inv)));
            }
        }
    }

    return img;
}

// ============================================================================
//  Op definition
// ============================================================================
class VisualLayerInspectorOp : public NoIop {
public:
    VisualLayerInspectorOp(Node* node) : NoIop(node) {}

    const char* Class()     const override { return kClass; }
    const char* node_help() const override { return kHelp; }

    void knobs(Knob_Callback f) override
    {
        Text_knob(f, "Select a node with multiple layers/AOVs, then click below.");
        Divider(f, "");
        Button(f, "launch_inspector", "Launch Inspector");
        Divider(f, "");
        Text_knob(f, "<i>Created by Marten Blumen</i>");
    }

    int knob_changed(Knob* k) override
    {
        if (k->is("launch_inspector")) {
            launchInspector();
            return 1;
        }
        return NoIop::knob_changed(k);
    }

private:
    // ------------------------------------------------------------------
    //  Render all layer thumbnails from the input Iop
    // ------------------------------------------------------------------
    std::vector<LayerThumbnail> renderAllThumbnails()
    {
        std::vector<LayerThumbnail> thumbnails;

        Op* rawInp = input(0);
        if (!rawInp) return thumbnails;

        Iop* inp = dynamic_cast<Iop*>(rawInp);
        if (!inp) return thumbnails;

        inp->validate(true);
        const Info& info = inp->info();
        const ChannelSet allChannels = info.channels();
        if (allChannels.empty()) return thumbnails;

        std::vector<std::string> layers = collectLayers(allChannels);
        if (layers.empty()) return thumbnails;

        const Box& bbox = info.box();
        inp->request(bbox.x(), bbox.y(), bbox.r(), bbox.t(), allChannels, 1);

        thumbnails.reserve(layers.size());
        for (const auto& layerName : layers) {
            LayerChannels lc = resolveLayerChannels(layerName, allChannels);
            QImage thumb = renderThumbnail(*inp, lc, bbox);
            thumbnails.push_back({layerName, std::move(thumb)});
        }

        return thumbnails;
    }

    // ------------------------------------------------------------------
    //  Launch
    // ------------------------------------------------------------------
    void launchInspector()
    {
        Op* rawInp = input(0);
        if (!rawInp) {
            runPython("import nuke; nuke.message('Please connect an input node with layers to inspect.')");
            return;
        }

        Iop* inp = dynamic_cast<Iop*>(rawInp);
        if (!inp) {
            runPython("import nuke; nuke.message('Connected input is not an image operator (Iop).')");
            return;
        }

        auto thumbnails = renderAllThumbnails();
        if (thumbnails.empty()) {
            runPython("import nuke; nuke.message('No layers found on the connected input.')");
            return;
        }

        // Layer selection callback — uses Python C API directly
        auto onLayerSelected = [](const std::string& layerName) {
            std::string cmd =
                "import nuke\n"
                "v = nuke.activeViewer()\n"
                "if v:\n"
                "    v.node()['channels'].setValue('" + layerName + "')\n";
            runPython(cmd);
        };

        // Refresh callback — re-renders all thumbnails at current frame
        auto* self = this;
        auto onRefresh = [self]() -> std::vector<LayerThumbnail> {
            return self->renderAllThumbnails();
        };

        auto* dlg = new InspectorDialog(thumbnails, onLayerSelected, onRefresh, nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }

public:
    static const Description description;
};

static Op* build(Node* node) { return new VisualLayerInspectorOp(node); }
const Op::Description VisualLayerInspectorOp::description(kClass, "Filter/VisualLayerInspector",
                                                           build);

extern "C" FN_EXPORT const Op::Description* VisualLayerInspector_reference() {
    return &VisualLayerInspectorOp::description;
}
