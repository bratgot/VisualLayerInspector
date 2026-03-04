// ============================================================================
// VisualLayerInspector.cpp — Nuke 16 NDK Plugin
// Version 18.3
//
// v18.3: No placeholder pixmaps, auto-thumbnails checkbox, instant sort.
//
// Created by Marten Blumen
// ============================================================================

#include "DDImage/NoIop.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Channel.h"
#include "DDImage/Row.h"

#ifndef FN_EXPORT
  #ifdef _WIN32
    #define FN_EXPORT __declspec(dllexport)
  #else
    #define FN_EXPORT __attribute__((visibility("default")))
  #endif
#endif

#include "InspectorDialog.h"

#include <QApplication>
#include <QPointer>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

using namespace DD::Image;

static const char* const kClass = "VisualLayerInspector";
static const char* const kHelp  =
    "Visual Layer Inspector v18.3\n\n"
    "Connect any node with multiple layers/AOVs and press 'Launch Inspector' "
    "to open a thumbnail grid of every layer. Click a thumbnail to switch the "
    "active Viewer to that layer.\n\n"
    "The inspector window is modeless — Nuke stays fully interactive. "
    "The Viewer updates live when you click a layer.\n\n"
    "Thumbnails render progressively using the Row API with strided fetching "
    "for minimal memory usage. Use Proxy mode for faster browsing.\n\n"
    "Sort by Type Group to auto-categorise layers into Lighting, Utility, "
    "Data, Cryptomatte, and Custom groups.";

static constexpr int kThumbMaxWidth  = 240;
static constexpr int kThumbMaxHeight = 160;

// ============================================================================
//  Python helper
// ============================================================================
using Fn_PyGILState_Ensure  = int (*)();
using Fn_PyGILState_Release = void (*)(int);
using Fn_PyRun_SimpleString = int (*)(const char*);

static Fn_PyGILState_Ensure  s_gilEnsure  = nullptr;
static Fn_PyGILState_Release s_gilRelease = nullptr;
static Fn_PyRun_SimpleString s_pyRun      = nullptr;

static bool resolvePython()
{
    if (s_gilEnsure && s_gilRelease && s_pyRun) return true;
#ifdef _WIN32
    HMODULE hPython = nullptr;
    const char* names[] = {
        "python313.dll", "python312.dll", "python311.dll",
        "python310.dll", "python39.dll", "python3.dll", nullptr
    };
    for (int i = 0; names[i] && !hPython; ++i)
        hPython = GetModuleHandleA(names[i]);
    if (!hPython) return false;
    s_gilEnsure  = (Fn_PyGILState_Ensure)  GetProcAddress(hPython, "PyGILState_Ensure");
    s_gilRelease = (Fn_PyGILState_Release) GetProcAddress(hPython, "PyGILState_Release");
    s_pyRun      = (Fn_PyRun_SimpleString) GetProcAddress(hPython, "PyRun_SimpleString");
#else
    s_gilEnsure  = (Fn_PyGILState_Ensure)  dlsym(RTLD_DEFAULT, "PyGILState_Ensure");
    s_gilRelease = (Fn_PyGILState_Release) dlsym(RTLD_DEFAULT, "PyGILState_Release");
    s_pyRun      = (Fn_PyRun_SimpleString) dlsym(RTLD_DEFAULT, "PyRun_SimpleString");
#endif
    return s_gilEnsure && s_gilRelease && s_pyRun;
}

static void runPython(const std::string& cmd)
{
    if (!resolvePython()) return;
    int gstate = s_gilEnsure();
    s_pyRun(cmd.c_str());
    s_gilRelease(gstate);
}

// ============================================================================
//  Helpers
// ============================================================================
struct LayerInfo {
    std::string name;
    int channelCount;
};

static std::vector<LayerInfo> collectLayers(const ChannelSet& channels)
{
    std::map<std::string, int> layerMap;
    foreach (z, channels) {
        const std::string name = getName(z);
        const size_t dot = name.find('.');
        if (dot != std::string::npos)
            layerMap[name.substr(0, dot)]++;
    }
    std::vector<LayerInfo> result;
    result.reserve(layerMap.size());
    for (const auto& kv : layerMap)
        result.push_back({kv.first, kv.second});
    return result;
}

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
        if      (suffix == "red"   || suffix == "r" || suffix == "x") lc.r = z;
        else if (suffix == "green" || suffix == "g" || suffix == "y") lc.g = z;
        else if (suffix == "blue"  || suffix == "b" || suffix == "z") lc.b = z;
        else if (suffix == "alpha" || suffix == "a")                  lc.a = z;
    }

    lc.count = static_cast<int>(found.size());
    if (lc.r == Chan_Black && found.size() > 0) lc.r = found[0];
    if (lc.g == Chan_Black && found.size() > 1) lc.g = found[1];
    if (lc.b == Chan_Black && found.size() > 2) lc.b = found[2];
    if (found.size() == 1) { lc.g = lc.r; lc.b = lc.r; }
    return lc;
}

static inline uint8_t floatToByte(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    v = std::pow(v, 1.0f / 2.2f);
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

// ============================================================================
//  Row-based strided thumbnail renderer
// ============================================================================
static QImage renderThumbnailStrided(Iop& input, const LayerChannels& lc,
                                     const Box& bbox, int proxyStep)
{
    const int srcW = bbox.w();
    const int srcH = bbox.h();
    if (srcW <= 0 || srcH <= 0) return {};

    const float aspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    int thumbW, thumbH;
    if (aspect >= 1.0f) {
        thumbW = kThumbMaxWidth;
        thumbH = std::max(1, static_cast<int>(kThumbMaxWidth / aspect));
    } else {
        thumbH = kThumbMaxHeight;
        thumbW = std::max(1, static_cast<int>(kThumbMaxHeight * aspect));
    }

    if (proxyStep > 1) {
        thumbW = std::max(16, thumbW / proxyStep);
        thumbH = std::max(16, thumbH / proxyStep);
    }

    const int strideX = std::max(1, srcW / thumbW);
    const int strideY = std::max(1, srcH / thumbH);

    ChannelSet requestChans;
    if (lc.r != Chan_Black) requestChans += lc.r;
    if (lc.g != Chan_Black) requestChans += lc.g;
    if (lc.b != Chan_Black) requestChans += lc.b;
    if (requestChans.empty()) return {};

    QImage img(thumbW, thumbH, QImage::Format_RGB32);
    img.fill(Qt::black);

    Row row(bbox.x(), bbox.r());

    for (int ty = 0; ty < thumbH; ++ty) {
        const int srcY = bbox.y() + (ty * strideY);
        if (srcY >= bbox.t()) break;
        input.get(srcY, bbox.x(), bbox.r(), requestChans, row);

        const int flippedY = thumbH - 1 - ty;
        for (int tx = 0; tx < thumbW; ++tx) {
            const int srcX = bbox.x() + (tx * strideX);
            if (srcX >= bbox.r()) break;

            float r = (lc.r != Chan_Black) ? row[lc.r][srcX] : 0.0f;
            float g = (lc.g != Chan_Black) ? row[lc.g][srcX] : 0.0f;
            float b = (lc.b != Chan_Black) ? row[lc.b][srcX] : 0.0f;

            img.setPixel(tx, flippedY,
                         qRgb(floatToByte(r), floatToByte(g), floatToByte(b)));
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
        // ── Title ──
        Text_knob(f,
            "<span style='font-size:18px; font-weight:bold; color:#eee;'>"
            "Visual Layer Inspector</span><br>"
            "<span style='color:#999;'>"
            "Browse and preview every layer/AOV on a multi-layer input. "
            "Click a thumbnail to switch the active Viewer.</span>");
        Divider(f, "");

        // ── Settings ──
        static const char* const kProxyNames[] = {
            "Full Quality", "2x Proxy", "4x Proxy", "8x Proxy", nullptr
        };
        Enumeration_knob(f, &proxyIdx_, kProxyNames, "proxy", "proxy");
        Tooltip(f, "Thumbnail rendering resolution.\n"
                   "Higher proxy = faster but lower quality.\n"
                   "Full Quality fetches every pixel.\n"
                   "Change takes effect on next Regenerate.");

        static const char* const kSortNames[] = {
            "Alphabetical", "Type Group", "Channel Count", "Original Order", nullptr
        };
        Enumeration_knob(f, &sortIdx_, kSortNames, "sort_mode", "sort");
        Tooltip(f, "How layers are ordered in the grid.\n\n"
                   "Type Group auto-categorises layers:\n"
                   "  Lighting \xe2\x80\x94 diffuse, specular, reflection, emission, sss...\n"
                   "  Utility \xe2\x80\x94 depth, normal, position, motion, uv, ao...\n"
                   "  Data \xe2\x80\x94 id, mask, matte, object, material...\n"
                   "  Cryptomatte \xe2\x80\x94 crypto*\n"
                   "  Custom \xe2\x80\x94 everything else");

        Int_knob(f, &thumbSize_, "thumb_size", "thumbnail size");
        SetRange(f, 80, 400);
        Tooltip(f, "Width in pixels for each thumbnail in the grid.\n"
                   "Smaller = more layers visible, larger = more detail.");

        Divider(f, "categories");

        Bool_knob(f, &showLighting_, "show_lighting", "Lighting");
        SetFlags(f, Knob::STARTLINE);
        Bool_knob(f, &showUtility_, "show_utility", "Utility");
        ClearFlags(f, Knob::STARTLINE);
        Bool_knob(f, &showData_, "show_data", "Data");
        ClearFlags(f, Knob::STARTLINE);
        Bool_knob(f, &showCryptomatte_, "show_cryptomatte", "Cryptomatte");
        ClearFlags(f, Knob::STARTLINE);
        Bool_knob(f, &showCustom_, "show_custom", "Custom");
        ClearFlags(f, Knob::STARTLINE);

        Divider(f, "");

        Button(f, "launch_inspector", "Launch Inspector");
        Tooltip(f, "Open the thumbnail grid window.\n"
                   "Thumbnails generate automatically.\n"
                   "The window is modeless \xe2\x80\x94 Nuke stays fully interactive.");

        Divider(f, "");

        // ── Usage Notes ──
        BeginClosedGroup(f, "usage_notes", "usage notes");
        Text_knob(f,
            "<b>For Artists:</b><br>"
            "\xe2\x80\xa2 Select any node with multi-layer data (e.g. an EXR Read node)<br>"
            "\xe2\x80\xa2 Click <b>Launch Inspector</b> to open the thumbnail grid<br>"
            "\xe2\x80\xa2 Click any thumbnail to switch the Viewer to that layer<br>"
            "\xe2\x80\xa2 Use the <b>Filter</b> box to search by name (e.g. 'spec', 'depth')<br>"
            "\xe2\x80\xa2 Use <b>Stop</b> to pause thumbnail generation on heavy EXRs<br>"
            "\xe2\x80\xa2 Change <b>Proxy</b> before launching for faster browsing<br>"
            "\xe2\x80\xa2 <b>Shift+click</b> thumbnails to select layers for Shuffle export<br>"
            "\xe2\x80\xa2 Click <b>Shuffle</b> to create Shuffle2 nodes for all selected layers<br>"
            "\xe2\x80\xa2 The window stays on top \xe2\x80\x94 Nuke is fully interactive underneath<br>"
            "<br>"
            "<b>Tips:</b><br>"
            "\xe2\x80\xa2 <b>Type Group</b> sort auto-categorises AOVs into Lighting, Utility, "
            "Data, Cryptomatte, and Custom groups<br>"
            "\xe2\x80\xa2 Uncheck categories to hide layers you don't need<br>"
            "\xe2\x80\xa2 Drag the <b>Size</b> slider to resize thumbnails \xe2\x80\x94 "
            "smaller shows more layers at once<br>"
            "\xe2\x80\xa2 Click <b>Regenerate</b> after changing Proxy to re-render thumbnails"
        );
        EndGroup(f);

        // ── Technical Notes ──
        BeginClosedGroup(f, "tech_notes", "technical notes");
        Text_knob(f,
            "<b>Technical Details:</b><br>"
            "\xe2\x80\xa2 Thumbnails render via the Row API with strided pixel fetching<br>"
            "\xe2\x80\xa2 Each layer is rendered one at a time to keep Nuke responsive<br>"
            "\xe2\x80\xa2 Proxy 2x/4x/8x reduces thumbnail resolution proportionally<br>"
            "\xe2\x80\xa2 The dialog is modeless (non-blocking) \xe2\x80\x94 it does not lock Nuke<br>"
            "\xe2\x80\xa2 Layer classification uses pattern matching on channel name prefixes<br>"
            "\xe2\x80\xa2 Thumbnails are stored in memory, not written to disk (C++ version)<br>"
            "\xe2\x80\xa2 The Python version writes temporary JPEGs via Nuke\xe2\x80\x99s Write node<br>"
            "<br>"
            "<b>Performance:</b><br>"
            "\xe2\x80\xa2 Full Quality: ~100\xe2\x80\x93" "300 ms/layer depending on resolution<br>"
            "\xe2\x80\xa2 4x Proxy: ~10\xe2\x80\x93" "50 ms/layer \xe2\x80\x94 recommended for browsing<br>"
            "\xe2\x80\xa2 8x Proxy: fastest, suitable for very large EXR sequences<br>"
            "\xe2\x80\xa2 Slider resizing is geometry-only during drag, icons refresh on release"
        );
        EndGroup(f);

        Divider(f, "");
        Text_knob(f,
            "<i style='color:#777;'>Created by Marten Blumen  \xe2\x80\xa2  v18.3</i>");
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
    QPointer<InspectorDialog> inspectorDialog_;

    // Knob storage
    int  proxyIdx_        = 0;   // 0=Full, 1=2x, 2=4x, 3=8x
    int  sortIdx_         = 1;   // 1=Type Group
    int  thumbSize_       = 200;
    bool showLighting_    = true;
    bool showUtility_     = true;
    bool showData_        = true;
    bool showCryptomatte_ = true;
    bool showCustom_      = true;

    struct PreparedInput {
        Iop*                       iop = nullptr;
        std::vector<LayerInfo>     layers;
        std::vector<LayerChannels> layerChannels;
        ChannelSet                 allChannels;
        Box                        bbox;
        bool                       valid = false;
    };

    PreparedInput prepareInput()
    {
        PreparedInput pi;
        Op* rawInp = input(0);
        if (!rawInp) return pi;
        pi.iop = dynamic_cast<Iop*>(rawInp);
        if (!pi.iop) return pi;
        pi.iop->validate(true);
        const Info& info = pi.iop->info();
        pi.allChannels = info.channels();
        if (pi.allChannels.empty()) return pi;
        pi.layers = collectLayers(pi.allChannels);
        if (pi.layers.empty()) return pi;
        pi.bbox = info.box();
        pi.iop->request(pi.bbox.x(), pi.bbox.y(),
                        pi.bbox.r(), pi.bbox.t(),
                        pi.allChannels, 1);
        pi.layerChannels.reserve(pi.layers.size());
        for (const auto& li : pi.layers)
            pi.layerChannels.push_back(resolveLayerChannels(li.name, pi.allChannels));
        pi.valid = true;
        return pi;
    }

    RenderOneCallback makeRenderCallback(const PreparedInput& pi)
    {
        Iop*                       iop  = pi.iop;
        std::vector<LayerChannels> lcs  = pi.layerChannels;
        Box                        bbox = pi.bbox;
        return [iop, lcs, bbox](int layerIndex, int proxyStep) -> QImage {
            if (layerIndex < 0 || layerIndex >= static_cast<int>(lcs.size()))
                return {};
            return renderThumbnailStrided(*iop, lcs[layerIndex], bbox, proxyStep);
        };
    }

    void launchInspector()
    {
        // If dialog already open, just bring it to front
        if (inspectorDialog_) {
            inspectorDialog_->raise();
            inspectorDialog_->activateWindow();
            return;
        }

        auto* self = this;

        auto prepare = [self]() -> PrepareResult {
            PrepareResult pr;
            PreparedInput pi = self->prepareInput();
            if (!pi.valid) {
                pr.valid = false;
                pr.errorMsg = pi.iop
                    ? "No layers found on the connected input."
                    : "No input connected, or input is not an Iop.";
                return pr;
            }
            pr.valid = true;
            pr.layerNames.reserve(pi.layers.size());
            pr.channelCounts.reserve(pi.layers.size());
            for (const auto& li : pi.layers) {
                pr.layerNames.push_back(li.name);
                pr.channelCounts.push_back(li.channelCount);
            }
            pr.renderOne = self->makeRenderCallback(pi);
            return pr;
        };

        auto onLayerSelected = [](const std::string& layerName) {
            std::string cmd =
                "import nuke\n"
                "v = nuke.activeViewer()\n"
                "if v:\n"
                "    v.node()['channels'].setValue('" + layerName + "')\n";
            runPython(cmd);
        };

        auto onCreateShuffle = [](const std::vector<std::string>& layerNames) {
            // Build a Python list of layer names
            std::string pyList = "[";
            for (size_t i = 0; i < layerNames.size(); ++i) {
                if (i > 0) pyList += ", ";
                pyList += "'" + layerNames[i] + "'";
            }
            pyList += "]";

            std::string cmd =
                "import nuke\n"
                "vli = None\n"
                "for n in nuke.allNodes('VisualLayerInspector'):\n"
                "    if n.input(0):\n"
                "        vli = n\n"
                "        break\n"
                "if vli and vli.input(0):\n"
                "    inp = vli.input(0)\n"
                "    for n in nuke.selectedNodes(): n.setSelected(False)\n"
                "    layers = " + pyList + "\n"
                "    for i, layer in enumerate(layers):\n"
                "        inp.setSelected(True)\n"
                "        s = nuke.createNode('Shuffle2')\n"
                "        s['in1'].setValue(layer)\n"
                "        s['label'].setValue('[value in1]')\n"
                "        s.setXYpos(inp.xpos() + (i * 110), inp.ypos() + 80)\n"
                "        s.setSelected(False)\n"
                "        inp.setSelected(False)\n";
            runPython(cmd);
        };

        // Build settings from knob values
        static const int proxySteps[] = {1, 2, 4, 8};
        InspectorSettings settings;
        int pi = std::max(0, std::min(proxyIdx_, 3));
        int si = std::max(0, std::min(sortIdx_, 3));
        settings.proxyStep     = proxySteps[pi];
        settings.sortMode      = static_cast<SortMode>(si);
        settings.thumbSize     = std::max(80, std::min(thumbSize_, 400));
        settings.showLighting    = showLighting_;
        settings.showUtility     = showUtility_;
        settings.showData        = showData_;
        settings.showCryptomatte = showCryptomatte_;
        settings.showCustom      = showCustom_;

        // Heap-allocate — dialog lives beyond knob_changed return
        auto* dlg = new InspectorDialog(prepare, onLayerSelected, onCreateShuffle, settings, nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        inspectorDialog_ = dlg;

        // show() returns immediately — knob_changed exits, Nuke resumes
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
