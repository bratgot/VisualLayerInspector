// ============================================================================
// VisualLayerInspector.cpp — Nuke 16 NDK Plugin
// Version 8
//
// exec() opens modal dialog with nested event loop.
// showEvent auto-triggers scan + progressive rendering.
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

#include <string>
#include <vector>
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
    "Visual Layer Inspector v8\n\n"
    "Connect any node with multiple layers/AOVs and press 'Launch Inspector' "
    "to open a thumbnail grid of every layer. Click a thumbnail to switch the "
    "active Viewer to that layer.\n\n"
    "Thumbnails render progressively using the Row API with strided fetching "
    "for minimal memory usage. Use Proxy mode for faster browsing.";

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
        Text_knob(f, "Select a node with multiple layers/AOVs, then click below.");
        Divider(f, "");
        Button(f, "launch_inspector", "Launch Inspector");
        Divider(f, "");
        Text_knob(f, "<i>Created by Marten Blumen  •  v8</i>");
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
    struct PreparedInput {
        Iop*                       iop = nullptr;
        std::vector<std::string>   layers;
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
        for (const auto& name : pi.layers)
            pi.layerChannels.push_back(resolveLayerChannels(name, pi.allChannels));
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
            pr.layerNames = std::move(pi.layers);
            pr.renderOne  = self->makeRenderCallback(pi);
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

        InspectorDialog dlg(prepare, onLayerSelected, nullptr);
        dlg.exec();
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
