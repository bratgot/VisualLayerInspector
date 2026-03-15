// exr_inspector.cpp — Dual-backend EXR header parser with granular timing
// Backend 1: OpenEXR C Core API (fastest, production-grade)
// Backend 2: tinyexr (zero-dependency fallback)

#include "exr_inspector.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cmath>

// ─── tinyexr (always compiled in for fallback) ─────────────────────────────
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

// ─── OpenEXR C Core API ────────────────────────────────────────────────────
#if !defined(EXR_INSPECTOR_TINYEXR_ONLY)
#include <openexr.h>
#endif

namespace exrinspector {

// ────────────────────────────────────────────────────────────────────────────
// Utility: split "diffuse.R" -> layer="diffuse", channel="R"
// ────────────────────────────────────────────────────────────────────────────
void EXRInspector::splitChannelName(const std::string& fullName,
                                     std::string& outLayer,
                                     std::string& outChannel)
{
    auto dot = fullName.rfind('.');
    if (dot == std::string::npos) {
        outLayer.clear();
        outChannel = fullName;
    } else {
        outLayer   = fullName.substr(0, dot);
        outChannel = fullName.substr(dot + 1);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Group flat channels into layers by shared prefix
// ────────────────────────────────────────────────────────────────────────────
std::vector<LayerInfo> EXRInspector::groupIntoLayers(const std::vector<ChannelInfo>& channels)
{
    std::map<std::string, LayerInfo> layerMap;
    for (auto& ch : channels) {
        auto& layer = layerMap[ch.layerName];
        layer.name = ch.layerName;
        layer.channels.push_back(ch);
    }

    std::vector<LayerInfo> result;
    auto it = layerMap.find("");
    if (it != layerMap.end()) {
        result.push_back(std::move(it->second));
        layerMap.erase(it);
    }
    for (auto& [name, layer] : layerMap) {
        result.push_back(std::move(layer));
    }
    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// BACKEND 1: OpenEXR C Core API (fastest path)
// ════════════════════════════════════════════════════════════════════════════
#if !defined(EXR_INSPECTOR_TINYEXR_ONLY)

static const char* coreCompressionName(exr_compression_t c) {
    switch (c) {
        case EXR_COMPRESSION_NONE:   return "none";
        case EXR_COMPRESSION_RLE:    return "rle";
        case EXR_COMPRESSION_ZIPS:   return "zips";
        case EXR_COMPRESSION_ZIP:    return "zip";
        case EXR_COMPRESSION_PIZ:    return "piz";
        case EXR_COMPRESSION_PXR24:  return "pxr24";
        case EXR_COMPRESSION_B44:    return "b44";
        case EXR_COMPRESSION_B44A:   return "b44a";
        case EXR_COMPRESSION_DWAA:   return "dwaa";
        case EXR_COMPRESSION_DWAB:   return "dwab";
        default:                     return "unknown";
    }
}

static const char* coreStorageName(exr_storage_t s) {
    switch (s) {
        case EXR_STORAGE_SCANLINE:      return "scanlineimage";
        case EXR_STORAGE_TILED:         return "tiledimage";
        case EXR_STORAGE_DEEP_SCANLINE: return "deepscanline";
        case EXR_STORAGE_DEEP_TILED:    return "deeptile";
        default:                        return "unknown";
    }
}

static PixelType corePixelType(exr_pixel_type_t t) {
    switch (t) {
        case EXR_PIXEL_UINT:  return PixelType::UINT;
        case EXR_PIXEL_HALF:  return PixelType::HALF;
        case EXR_PIXEL_FLOAT: return PixelType::FLOAT;
        default:              return PixelType::UNKNOWN;
    }
}

InspectResult EXRInspector::inspectWithCore(const std::string& filePath)
{
    InspectResult result;
    result.filePath = filePath;
    result.timing.backend = "OpenEXR_C_Core";

    ScopedTimer totalTimer;

    // ── Phase 1: File open + magic number validation ───────────────────
    ScopedTimer fileOpenTimer;

    exr_context_t ctx = nullptr;
    exr_context_initializer_t init = EXR_DEFAULT_CONTEXT_INITIALIZER;
    init.flags = EXR_CONTEXT_FLAG_DISABLE_CHUNK_RECONSTRUCTION;

    exr_result_t rv = exr_start_read(&ctx, filePath.c_str(), &init);

    result.timing.fileOpenUs = fileOpenTimer.stop();

    if (rv != EXR_ERR_SUCCESS) {
        result.success = false;
        result.error   = "OpenEXR C Core: failed to open file (error " + std::to_string(rv) + ")";
        return result;
    }

    // ── Phase 2: Header / attribute table query ────────────────────────
    // Note: exr_start_read already parsed the full header internally.
    // This phase measures the cost of querying part count from the
    // already-parsed in-memory header structure.
    ScopedTimer headerTimer;

    int numParts = 0;
    exr_get_count(ctx, &numParts);
    result.numParts    = numParts;
    result.isMultiPart = (numParts > 1);

    result.timing.headerParseUs = headerTimer.stop();

    // ── Phase 3: Channel extraction ────────────────────────────────────
    ScopedTimer channelTimer;

    for (int partIdx = 0; partIdx < numParts; ++partIdx) {
        PartInfo part;
        part.index = partIdx;

        const char* partName = nullptr;
        if (exr_get_name(ctx, partIdx, &partName) == EXR_ERR_SUCCESS && partName) {
            part.name = partName;
        }

        exr_storage_t storage = EXR_STORAGE_LAST_TYPE;
        exr_get_storage(ctx, partIdx, &storage);
        part.type = coreStorageName(storage);
        if (storage == EXR_STORAGE_TILED || storage == EXR_STORAGE_DEEP_TILED) {
            result.isTiled = true;
        }

        exr_attr_box2i_t dataWindow;
        if (exr_get_data_window(ctx, partIdx, &dataWindow) == EXR_ERR_SUCCESS) {
            part.width  = dataWindow.max.x - dataWindow.min.x + 1;
            part.height = dataWindow.max.y - dataWindow.min.y + 1;
        }

        exr_compression_t comp;
        if (exr_get_compression(ctx, partIdx, &comp) == EXR_ERR_SUCCESS) {
            part.compression = coreCompressionName(comp);
        }

        const exr_attr_chlist_t* chlist = nullptr;
        if (exr_get_channels(ctx, partIdx, &chlist) == EXR_ERR_SUCCESS && chlist) {
            part.numChannels = chlist->num_channels;
            for (int c = 0; c < chlist->num_channels; ++c) {
                const exr_attr_chlist_entry_t& entry = chlist->entries[c];
                ChannelInfo ci;
                ci.fullName  = std::string(entry.name.str, entry.name.length);
                ci.type      = corePixelType(entry.pixel_type);
                ci.xSampling = entry.x_sampling;
                ci.ySampling = entry.y_sampling;
                ci.pLinear   = (entry.p_linear != 0);
                splitChannelName(ci.fullName, ci.layerName, ci.channelName);
                part.allChannels.push_back(std::move(ci));
            }
        }
        result.parts.push_back(std::move(part));
    }

    result.timing.channelExtractUs = channelTimer.stop();

    // ── Phase 4: Layer grouping ────────────────────────────────────────
    ScopedTimer layerTimer;

    for (auto& part : result.parts) {
        part.layers = groupIntoLayers(part.allChannels);
    }

    result.timing.layerGroupUs = layerTimer.stop();

    // ── Phase 5: Cleanup ───────────────────────────────────────────────
    ScopedTimer cleanupTimer;
    exr_finish(&ctx);
    result.timing.cleanupUs = cleanupTimer.stop();

    // ── Totals ─────────────────────────────────────────────────────────
    result.timing.totalUs = totalTimer.stop();
    result.parseTimeMicroseconds = result.timing.totalUs;
    result.success = true;
    return result;
}

#endif // !EXR_INSPECTOR_TINYEXR_ONLY


// ════════════════════════════════════════════════════════════════════════════
// BACKEND 2: tinyexr (single-header, zero dependencies)
// ════════════════════════════════════════════════════════════════════════════

static const char* tinyexrCompressionName(int c) {
    switch (c) {
        case TINYEXR_COMPRESSIONTYPE_NONE:  return "none";
        case TINYEXR_COMPRESSIONTYPE_RLE:   return "rle";
        case TINYEXR_COMPRESSIONTYPE_ZIPS:  return "zips";
        case TINYEXR_COMPRESSIONTYPE_ZIP:   return "zip";
        case TINYEXR_COMPRESSIONTYPE_PIZ:   return "piz";
        case TINYEXR_COMPRESSIONTYPE_PXR24: return "pxr24";
        case TINYEXR_COMPRESSIONTYPE_B44:   return "b44";
        case TINYEXR_COMPRESSIONTYPE_B44A:  return "b44a";
        case TINYEXR_COMPRESSIONTYPE_ZFP:   return "zfp";
        default:                            return "unknown";
    }
}

InspectResult EXRInspector::inspectWithTinyEXR(const std::string& filePath)
{
    InspectResult result;
    result.filePath = filePath;
    result.timing.backend = "tinyexr";

    ScopedTimer totalTimer;

    // ── Phase 1: File open + version parse ─────────────────────────────
    ScopedTimer fileOpenTimer;

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromFile(&exrVersion, filePath.c_str());

    result.timing.fileOpenUs = fileOpenTimer.stop();

    if (ret != TINYEXR_SUCCESS) {
        result.error = "tinyexr: failed to parse EXR version";
        return result;
    }

    result.isTiled     = (exrVersion.tiled != 0);
    result.isMultiPart = (exrVersion.multipart != 0);

    if (exrVersion.multipart) {
        // ── Multi-part path ────────────────────────────────────────────

        // Phase 2: Header parse
        ScopedTimer headerTimer;

        EXRHeader** headers = nullptr;
        int numHeaders = 0;
        const char* err = nullptr;
        ret = ParseEXRMultipartHeaderFromFile(&headers, &numHeaders,
                                              &exrVersion, filePath.c_str(), &err);

        result.timing.headerParseUs = headerTimer.stop();

        if (ret != TINYEXR_SUCCESS) {
            result.error = std::string("tinyexr multi-part: ") + (err ? err : "unknown error");
            if (err) FreeEXRErrorMessage(err);
            return result;
        }

        // Phase 3: Channel extraction
        ScopedTimer channelTimer;

        result.numParts = numHeaders;
        for (int p = 0; p < numHeaders; ++p) {
            EXRHeader* hdr = headers[p];
            PartInfo part;
            part.index       = p;
            part.name        = hdr->name ? hdr->name : "";
            part.numChannels = hdr->num_channels;
            part.compression = tinyexrCompressionName(hdr->compression_type);

            if (hdr->data_window.max_x > 0 || hdr->data_window.max_y > 0) {
                part.width  = hdr->data_window.max_x - hdr->data_window.min_x + 1;
                part.height = hdr->data_window.max_y - hdr->data_window.min_y + 1;
            }

            for (int c = 0; c < hdr->num_channels; ++c) {
                ChannelInfo ci;
                ci.fullName = hdr->channels[c].name;
                ci.type     = static_cast<PixelType>(hdr->channels[c].pixel_type);
                splitChannelName(ci.fullName, ci.layerName, ci.channelName);
                part.allChannels.push_back(std::move(ci));
            }
            result.parts.push_back(std::move(part));
        }

        result.timing.channelExtractUs = channelTimer.stop();

        // Phase 4: Layer grouping
        ScopedTimer layerTimer;
        for (auto& part : result.parts) {
            part.layers = groupIntoLayers(part.allChannels);
        }
        result.timing.layerGroupUs = layerTimer.stop();

        // Phase 5: Cleanup
        ScopedTimer cleanupTimer;
        for (int p = 0; p < numHeaders; ++p) {
            FreeEXRHeader(headers[p]);
            free(headers[p]);
        }
        free(headers);
        result.timing.cleanupUs = cleanupTimer.stop();

    } else {
        // ── Single-part path (most common) ─────────────────────────────

        // Phase 2: Header parse
        ScopedTimer headerTimer;

        EXRHeader header;
        InitEXRHeader(&header);
        const char* err = nullptr;
        ret = ParseEXRHeaderFromFile(&header, &exrVersion, filePath.c_str(), &err);

        result.timing.headerParseUs = headerTimer.stop();

        if (ret != TINYEXR_SUCCESS) {
            result.error = std::string("tinyexr: ") + (err ? err : "unknown error");
            if (err) FreeEXRErrorMessage(err);
            return result;
        }

        // Phase 3: Channel extraction
        ScopedTimer channelTimer;

        result.numParts = 1;
        PartInfo part;
        part.index       = 0;
        part.numChannels = header.num_channels;
        part.compression = tinyexrCompressionName(header.compression_type);

        if (header.data_window.max_x > 0 || header.data_window.max_y > 0) {
            part.width  = header.data_window.max_x - header.data_window.min_x + 1;
            part.height = header.data_window.max_y - header.data_window.min_y + 1;
        }

        for (int c = 0; c < header.num_channels; ++c) {
            ChannelInfo ci;
            ci.fullName = header.channels[c].name;
            ci.type     = static_cast<PixelType>(header.channels[c].pixel_type);
            splitChannelName(ci.fullName, ci.layerName, ci.channelName);
            part.allChannels.push_back(std::move(ci));
        }
        result.parts.push_back(std::move(part));

        result.timing.channelExtractUs = channelTimer.stop();

        // Phase 4: Layer grouping
        ScopedTimer layerTimer;
        for (auto& p : result.parts) {
            p.layers = groupIntoLayers(p.allChannels);
        }
        result.timing.layerGroupUs = layerTimer.stop();

        // Phase 5: Cleanup
        ScopedTimer cleanupTimer;
        FreeEXRHeader(&header);
        result.timing.cleanupUs = cleanupTimer.stop();
    }

    result.timing.totalUs = totalTimer.stop();
    result.parseTimeMicroseconds = result.timing.totalUs;
    result.success = true;
    return result;
}


// ════════════════════════════════════════════════════════════════════════════
// Public API: inspect()
// ════════════════════════════════════════════════════════════════════════════
InspectResult EXRInspector::inspect(const std::string& filePath)
{
#if !defined(EXR_INSPECTOR_TINYEXR_ONLY)
    auto result = inspectWithCore(filePath);
    if (result.success) return result;
    return inspectWithTinyEXR(filePath);
#else
    return inspectWithTinyEXR(filePath);
#endif
}


// ════════════════════════════════════════════════════════════════════════════
// Timing Metrics Formatter — Visual waterfall bar chart
// ════════════════════════════════════════════════════════════════════════════

std::string EXRInspector::formatTimingMetrics(const TimingMetrics& timing)
{
    std::ostringstream ss;

    const int barMaxWidth = 40;

    struct Phase {
        const char* name;
        double      us;
        double      pct;
    };

    Phase phases[] = {
        { "File Open + Validate", timing.fileOpenUs,       timing.fileOpenPct()       },
        { "Header Parse",         timing.headerParseUs,    timing.headerParsePct()    },
        { "Channel Extraction",   timing.channelExtractUs, timing.channelExtractPct() },
        { "Layer Grouping",       timing.layerGroupUs,     timing.layerGroupPct()     },
        { "Cleanup / Close",      timing.cleanupUs,        timing.cleanupPct()        },
    };

    ss << "\n";
    ss << "  TIMING METRICS  [" << timing.backend << "]\n";
    ss << "  ================================================================\n";

    // Header row
    ss << "  " << std::left << std::setw(24) << "Phase"
       << std::right << std::setw(10) << "Time (us)"
       << std::setw(8) << "(%)"
       << "  Bar\n";
    ss << "  " << std::string(24, '-')
       << std::string(10, '-')
       << std::string(8, '-')
       << "  " << std::string(barMaxWidth + 2, '-') << "\n";

    for (auto& phase : phases) {
        int barWidth = static_cast<int>(std::round((phase.pct / 100.0) * barMaxWidth));
        if (barWidth < 0) barWidth = 0;
        if (barWidth > barMaxWidth) barWidth = barMaxWidth;
        if (phase.us > 0.01 && barWidth == 0) barWidth = 1;

        std::string bar(barWidth, '#');

        ss << "  " << std::left  << std::setw(24) << phase.name
           << std::right << std::setw(9) << std::fixed << std::setprecision(1) << phase.us
           << "  " << std::setw(5) << std::fixed << std::setprecision(1) << phase.pct << "%"
           << "  |" << bar;
        int remaining = barMaxWidth - barWidth;
        if (remaining > 0) ss << std::string(remaining, '.');
        ss << "|\n";
    }

    ss << "  " << std::string(24, '-')
       << std::string(10, '-')
       << std::string(8, '-')
       << "  " << std::string(barMaxWidth + 2, '-') << "\n";

    // Total row
    ss << "  " << std::left  << std::setw(24) << "TOTAL"
       << std::right << std::setw(9) << std::fixed << std::setprecision(1) << timing.totalUs
       << "  " << std::setw(5) << "100.0" << "%"
       << "  |" << std::string(barMaxWidth, '=') << "|\n";

    // Speed verdict
    ss << "\n  ";
    if (timing.totalUs < 100.0) {
        ss << ">> BLAZING FAST: ";
    } else if (timing.totalUs < 1000.0) {
        ss << ">> Fast: ";
    } else if (timing.totalUs < 10000.0) {
        ss << ">> OK: ";
    } else {
        ss << ">> Slow: ";
    }
    ss << std::fixed << std::setprecision(1) << timing.totalUs << " us"
       << "  (" << std::setprecision(4) << timing.totalUs / 1000.0 << " ms"
       << "  /  " << std::setprecision(6) << timing.totalUs / 1000000.0 << " sec)\n";

    return ss.str();
}


// ════════════════════════════════════════════════════════════════════════════
// Text Formatter
// ════════════════════════════════════════════════════════════════════════════

std::string EXRInspector::formatAsText(const InspectResult& result, bool showTiming)
{
    std::ostringstream ss;

    if (!result.success) {
        ss << "ERROR: " << result.error << "\n";
        return ss.str();
    }

    ss << "=== EXR Channel Inspector ===\n";
    ss << "File: " << result.filePath << "\n";

    if (!result.parts.empty()) {
        auto& p0 = result.parts[0];
        ss << "Parts: " << result.numParts;
        if (p0.width > 0 && p0.height > 0)
            ss << " | Resolution: " << p0.width << "x" << p0.height;
        if (!p0.compression.empty())
            ss << " | Compression: " << p0.compression;
        ss << "\n";
    }

    if (showTiming) {
        ss << "Parsed in: " << static_cast<int>(result.parseTimeMicroseconds) << " us"
           << " [" << result.timing.backend << "]\n";
    }

    ss << "\n";

    for (auto& part : result.parts) {
        if (result.numParts > 1) {
            ss << "-- Part " << part.index;
            if (!part.name.empty()) ss << " (" << part.name << ")";
            ss << " [" << part.type << "]";
            if (part.width > 0)
                ss << " " << part.width << "x" << part.height;
            ss << " --\n";
        }

        ss << "Layers & Channels:\n";
        for (auto& layer : part.layers) {
            std::string displayName = layer.name.empty() ? "[default]" : layer.name;
            ss << "  " << displayName << "\n";

            for (size_t i = 0; i < layer.channels.size(); ++i) {
                auto& ch = layer.channels[i];
                bool isLast = (i == layer.channels.size() - 1);
                ss << "    " << (isLast ? "+-- " : "|-- ");

                std::string label = ch.fullName;
                ss << label;
                int pad = 24 - static_cast<int>(label.size());
                if (pad > 0) ss << std::string(pad, ' ');
                ss << "(" << pixelTypeName(ch.type) << ")";
                if (ch.xSampling != 1 || ch.ySampling != 1)
                    ss << " [" << ch.xSampling << "x" << ch.ySampling << "]";
                ss << "\n";
            }
        }
        ss << "\n";
    }

    if (showTiming) {
        ss << formatTimingMetrics(result.timing);
    }

    return ss.str();
}


// ════════════════════════════════════════════════════════════════════════════
// JSON Formatter
// ════════════════════════════════════════════════════════════════════════════

std::string EXRInspector::formatAsJSON(const InspectResult& result)
{
    std::ostringstream ss;
    ss << std::fixed;

    ss << "{\n";
    ss << "  \"success\": " << (result.success ? "true" : "false") << ",\n";
    ss << "  \"file\": \"" << result.filePath << "\",\n";
    ss << "  \"numParts\": " << result.numParts << ",\n";
    ss << "  \"isMultiPart\": " << (result.isMultiPart ? "true" : "false") << ",\n";
    ss << "  \"isTiled\": " << (result.isTiled ? "true" : "false") << ",\n";
    ss << "  \"totalChannels\": " << result.totalChannels() << ",\n";

    auto& t = result.timing;
    ss << "  \"timing\": {\n";
    ss << "    \"backend\": \"" << t.backend << "\",\n";
    ss << "    \"totalUs\": " << std::setprecision(2) << t.totalUs << ",\n";
    ss << "    \"totalMs\": " << std::setprecision(4) << t.totalUs / 1000.0 << ",\n";
    ss << "    \"phases\": {\n";
    ss << "      \"fileOpenUs\": " << std::setprecision(2) << t.fileOpenUs << ",\n";
    ss << "      \"headerParseUs\": " << std::setprecision(2) << t.headerParseUs << ",\n";
    ss << "      \"channelExtractUs\": " << std::setprecision(2) << t.channelExtractUs << ",\n";
    ss << "      \"layerGroupUs\": " << std::setprecision(2) << t.layerGroupUs << ",\n";
    ss << "      \"cleanupUs\": " << std::setprecision(2) << t.cleanupUs << "\n";
    ss << "    },\n";
    ss << "    \"percentages\": {\n";
    ss << "      \"fileOpen\": " << std::setprecision(1) << t.fileOpenPct() << ",\n";
    ss << "      \"headerParse\": " << std::setprecision(1) << t.headerParsePct() << ",\n";
    ss << "      \"channelExtract\": " << std::setprecision(1) << t.channelExtractPct() << ",\n";
    ss << "      \"layerGroup\": " << std::setprecision(1) << t.layerGroupPct() << ",\n";
    ss << "      \"cleanup\": " << std::setprecision(1) << t.cleanupPct() << "\n";
    ss << "    }\n";
    ss << "  },\n";

    ss << "  \"parts\": [\n";
    for (size_t p = 0; p < result.parts.size(); ++p) {
        auto& part = result.parts[p];
        ss << "    {\n";
        ss << "      \"index\": " << part.index << ",\n";
        ss << "      \"name\": \"" << part.name << "\",\n";
        ss << "      \"type\": \"" << part.type << "\",\n";
        ss << "      \"width\": " << part.width << ",\n";
        ss << "      \"height\": " << part.height << ",\n";
        ss << "      \"compression\": \"" << part.compression << "\",\n";
        ss << "      \"numChannels\": " << part.numChannels << ",\n";
        ss << "      \"layers\": [\n";

        for (size_t l = 0; l < part.layers.size(); ++l) {
            auto& layer = part.layers[l];
            ss << "        {\n";
            ss << "          \"name\": \"" << layer.name << "\",\n";
            ss << "          \"channels\": [\n";

            for (size_t c = 0; c < layer.channels.size(); ++c) {
                auto& ch = layer.channels[c];
                ss << "            {";
                ss << "\"name\": \"" << ch.fullName << "\", ";
                ss << "\"type\": \"" << pixelTypeName(ch.type) << "\", ";
                ss << "\"xSampling\": " << ch.xSampling << ", ";
                ss << "\"ySampling\": " << ch.ySampling;
                ss << "}";
                if (c + 1 < layer.channels.size()) ss << ",";
                ss << "\n";
            }

            ss << "          ]\n";
            ss << "        }";
            if (l + 1 < part.layers.size()) ss << ",";
            ss << "\n";
        }

        ss << "      ]\n";
        ss << "    }";
        if (p + 1 < result.parts.size()) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

} // namespace exrinspector
