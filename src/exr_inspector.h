#pragma once
// exr_inspector.h — High-performance EXR channel/layer inspector
// Header-only parse: NEVER reads pixel data.

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>
#include <functional>

namespace exrinspector {

// ═══════════════════════════════════════════════════════════════════════════
// High-resolution timing utilities
// ═══════════════════════════════════════════════════════════════════════════

using Clock     = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;

// Scoped timer: measures elapsed time between construction and stop()/destruction
class ScopedTimer {
public:
    ScopedTimer() : _start(Clock::now()), _stopped(false), _elapsed(0.0) {}

    // Stop the timer and return elapsed microseconds
    double stop() {
        if (!_stopped) {
            auto end = Clock::now();
            _elapsed = std::chrono::duration<double, std::micro>(end - _start).count();
            _stopped = true;
        }
        return _elapsed;
    }

    // Get elapsed microseconds (stops if still running)
    double elapsedUs() {
        if (!_stopped) return stop();
        return _elapsed;
    }

    // Get current lap time WITHOUT stopping (for intermediate measurements)
    double lapUs() const {
        auto now = Clock::now();
        return std::chrono::duration<double, std::micro>(now - _start).count();
    }

    // Reset and restart the timer
    void restart() {
        _start   = Clock::now();
        _stopped = false;
        _elapsed = 0.0;
    }

private:
    TimePoint _start;
    bool      _stopped;
    double    _elapsed;
};

// ─── Per-phase timing breakdown ────────────────────────────────────────────
struct TimingMetrics {
    double totalUs         = 0.0;  // Wall-clock total
    double fileOpenUs      = 0.0;  // File open + magic number validation
    double headerParseUs   = 0.0;  // Header/attribute table parsing
    double channelExtractUs= 0.0;  // Channel list extraction from parsed header
    double layerGroupUs    = 0.0;  // Grouping channels into layers
    double cleanupUs       = 0.0;  // File handle close / resource free

    std::string backend;           // "OpenEXR_C_Core" or "tinyexr"

    // Percentage of total for each phase
    double fileOpenPct()       const { return totalUs > 0 ? (fileOpenUs / totalUs) * 100.0 : 0; }
    double headerParsePct()    const { return totalUs > 0 ? (headerParseUs / totalUs) * 100.0 : 0; }
    double channelExtractPct() const { return totalUs > 0 ? (channelExtractUs / totalUs) * 100.0 : 0; }
    double layerGroupPct()     const { return totalUs > 0 ? (layerGroupUs / totalUs) * 100.0 : 0; }
    double cleanupPct()        const { return totalUs > 0 ? (cleanupUs / totalUs) * 100.0 : 0; }
};


// ─── Pixel type enumeration ────────────────────────────────────────────────
enum class PixelType : int {
    UINT   = 0,  // 32-bit unsigned int
    HALF   = 1,  // 16-bit float
    FLOAT  = 2,  // 32-bit float
    UNKNOWN = -1
};

inline const char* pixelTypeName(PixelType t) {
    switch (t) {
        case PixelType::UINT:    return "uint";
        case PixelType::HALF:    return "half";
        case PixelType::FLOAT:   return "float";
        default:                 return "unknown";
    }
}

// ─── Channel info ──────────────────────────────────────────────────────────
struct ChannelInfo {
    std::string fullName;    // e.g. "diffuse.R"
    std::string layerName;   // e.g. "diffuse"  (empty for default layer)
    std::string channelName; // e.g. "R"
    PixelType   type = PixelType::UNKNOWN;
    int         xSampling = 1;
    int         ySampling = 1;
    bool        pLinear   = false;
};

// ─── Layer = group of channels sharing a prefix ────────────────────────────
struct LayerInfo {
    std::string              name;     // "" for the default/root layer
    std::vector<ChannelInfo> channels;
};

// ─── Part info (multi-part EXR support) ────────────────────────────────────
struct PartInfo {
    int         index = 0;
    std::string name;
    std::string type;          // "scanlineimage", "tiledimage", "deepscanline", "deeptile"
    int         width  = 0;
    int         height = 0;
    std::string compression;
    int         numChannels = 0;

    // Channels grouped by layer
    std::vector<LayerInfo>   layers;
    // Flat list of all channels in this part
    std::vector<ChannelInfo> allChannels;
};

// ─── File-level result ─────────────────────────────────────────────────────
struct InspectResult {
    bool        success = false;
    std::string error;
    std::string filePath;
    int         numParts = 0;
    bool        isMultiPart  = false;
    bool        isTiled      = false;
    double      parseTimeMicroseconds = 0.0;

    // Granular timing breakdown
    TimingMetrics timing;

    std::vector<PartInfo> parts;

    // Convenience: total unique channel count across all parts
    int totalChannels() const {
        int n = 0;
        for (auto& p : parts) n += p.numChannels;
        return n;
    }
};

// ─── Inspector class ───────────────────────────────────────────────────────
class EXRInspector {
public:
    // Inspect an EXR file. Returns immediately after parsing the header.
    // No pixel data is ever read.
    static InspectResult inspect(const std::string& filePath);

    // Format result as a human-readable string (for CLI / Nuke text knob)
    static std::string formatAsText(const InspectResult& result, bool showTiming = true);

    // Format timing metrics as a visual waterfall bar chart
    static std::string formatTimingMetrics(const TimingMetrics& timing);

    // Format result as JSON (for pipeline integration)
    static std::string formatAsJSON(const InspectResult& result);

private:
    // Internal: split "layer.channel" into layer + channel parts
    static void splitChannelName(const std::string& fullName,
                                 std::string& outLayer,
                                 std::string& outChannel);

    // Internal: group flat channel list into layers
    static std::vector<LayerInfo> groupIntoLayers(const std::vector<ChannelInfo>& channels);

#if !defined(EXR_INSPECTOR_TINYEXR_ONLY)
    // OpenEXR C Core API path (fastest)
    static InspectResult inspectWithCore(const std::string& filePath);
#endif

    // tinyexr fallback path
    static InspectResult inspectWithTinyEXR(const std::string& filePath);
};

} // namespace exrinspector
