/**
 * rebake_factory_irs.cpp
 *
 * Sidecar-driven factory IR rebaker.
 *
 * Walks a directory tree looking for `.ping` sidecar XML files, parses the
 * synthesis parameters from each, runs `IRSynthEngine::synthIR` with the
 * current engine code, and writes the four resulting `.wav` files:
 *   <base>.wav          (MAIN L/R stereo pair)
 *   <base>_direct.wav   (DIRECT path,  if enabled in the sidecar)
 *   <base>_outrig.wav   (OUTRIG path,  if enabled)
 *   <base>_ambient.wav  (AMBIENT path, if enabled)
 *
 * IMPORTANT: the `.ping` sidecar is NEVER touched. If a sidecar doesn't have
 * a sibling `.wav` yet, the tool will create one; if the `.wav` already
 * exists, it is overwritten with the current engine's output but the
 * sidecar stays exactly as it was. This is the point of the tool — to apply
 * engine-code updates (e.g. the v2.9.0 polygon sign-bug fix) to every
 * factory IR that already has a hand-authored sidecar, without losing the
 * author's parameter tweaks.
 *
 * Build:
 *   cd "/Users/paulthomson/Cursor wip/Ping"
 *   g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -DPING_POLYGON_MODAL_BANK=1 \
 *       -ISource Tools/rebake_factory_irs.cpp Source/IRSynthEngine.cpp \
 *       -o build/rebake_factory_irs -lm
 *
 * Usage:
 *   ./build/rebake_factory_irs <ir_root_dir> [options]
 *
 *   e.g. ./build/rebake_factory_irs Installer/factory_irs
 *
 * Options:
 *   --dry-run              Scan and list what would be rebaked; don't write.
 *   --only <substring>     Only process sidecars whose path contains the
 *                          substring (case-sensitive).  Can be passed
 *                          multiple times; any match wins.
 *   -q / --quiet           Suppress per-venue progress output.
 */

#include "IRSynthEngine.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <map>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ── Tiny XML attribute parser ────────────────────────────────────────────────
// The .ping sidecar format is always a single self-closing element:
//   <?xml version="1.0" encoding="UTF-8"?>
//   <PingIRSynth>
//     <irSynthParams attr1="val1" attr2="val2" .../>
//   </PingIRSynth>
//
// We just extract everything between `<irSynthParams` and `/>` and parse
// key="value" pairs out of that substring.  No nested elements, no
// character references, no surprises — we wrote both ends of this file.

static std::string readFileAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

static std::map<std::string, std::string> parsePingAttrs(const std::string& xml, bool& ok)
{
    ok = false;
    std::map<std::string, std::string> out;
    const std::string openTag  = "<irSynthParams";
    const auto tagStart = xml.find(openTag);
    if (tagStart == std::string::npos) return out;
    const auto tagEnd = xml.find("/>", tagStart);
    if (tagEnd == std::string::npos) return out;

    // Attribute region is between end-of-opentag and the '/>' close.
    const size_t attrStart = tagStart + openTag.size();
    std::string block = xml.substr(attrStart, tagEnd - attrStart);

    // Walk the block extracting key="value" pairs.
    size_t i = 0;
    const size_t n = block.size();
    while (i < n)
    {
        // Skip whitespace.
        while (i < n && (block[i] == ' ' || block[i] == '\t' || block[i] == '\r' || block[i] == '\n'))
            ++i;
        if (i >= n) break;

        // Read key until '='.
        size_t keyStart = i;
        while (i < n && block[i] != '=' && block[i] != ' ' && block[i] != '\t') ++i;
        if (i >= n || block[i] != '=') break;
        std::string key(block, keyStart, i - keyStart);
        ++i;  // skip '='

        // Expect opening quote.
        if (i >= n || (block[i] != '"' && block[i] != '\'')) break;
        char quote = block[i++];

        // Read value until matching quote.
        size_t valStart = i;
        while (i < n && block[i] != quote) ++i;
        if (i >= n) break;
        std::string val(block, valStart, i - valStart);
        ++i;  // skip closing quote

        out.emplace(std::move(key), std::move(val));
    }

    ok = !out.empty();
    return out;
}

// ── Field lookup helpers ─────────────────────────────────────────────────────
static const std::string& getStr(const std::map<std::string, std::string>& m,
                                 const std::string& key,
                                 const std::string& fallback)
{
    auto it = m.find(key);
    return (it == m.end()) ? fallback : it->second;
}

static double getDouble(const std::map<std::string, std::string>& m,
                        const std::string& key, double fallback)
{
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return fallback;
    try { return std::stod(it->second); } catch (...) { return fallback; }
}

static int getInt(const std::map<std::string, std::string>& m,
                  const std::string& key, int fallback)
{
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

static bool getBool(const std::map<std::string, std::string>& m,
                    const std::string& key, bool fallback)
{
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return fallback;
    const std::string& v = it->second;
    if (v == "1" || v == "true"  || v == "True"  || v == "TRUE")  return true;
    if (v == "0" || v == "false" || v == "False" || v == "FALSE") return false;
    return fallback;
}

// ── Map attribute block → IRSynthParams ──────────────────────────────────────
// Mirrors PluginProcessor.cpp::irSynthParamsFromXml (inc. legacy shape
// migration) so a rebake is indistinguishable from clicking "Calculate" in
// the plugin after loading the sidecar.
static IRSynthParams paramsFromPingAttrs(const std::map<std::string, std::string>& a)
{
    IRSynthParams p;
    IRSynthParams defaults;  // struct defaults for fields missing from the sidecar

    // Shape + geometry (with v2.8.0 legacy migration for L-shaped / Cylindrical).
    p.shape = getStr(a, "shape", "Rectangular");
    if (p.shape == "L-shaped")    p.shape = "Rectangular";
    if (p.shape == "Cylindrical") p.shape = "Circular Hall";

    p.width          = getDouble(a, "width",  28.0);
    p.depth          = getDouble(a, "depth",  16.0);
    p.height         = getDouble(a, "height", 12.0);

    p.shapeNavePct   = getDouble(a, "shapeNavePct",   defaults.shapeNavePct);
    p.shapeTrptPct   = getDouble(a, "shapeTrptPct",   defaults.shapeTrptPct);
    p.shapeTaper     = getDouble(a, "shapeTaper",     defaults.shapeTaper);
    p.shapeCornerCut = getDouble(a, "shapeCornerCut", defaults.shapeCornerCut);

    // Materials / absorption.
    p.floor_material   = getStr(a, "floor",   "Hardwood floor");
    p.ceiling_material = getStr(a, "ceiling", "Painted plaster");
    p.wall_material    = getStr(a, "wall",    "Concrete / bare brick");
    p.window_fraction  = getDouble(a, "windows",   0.27);
    p.audience         = getDouble(a, "audience",  0.45);
    p.diffusion        = getDouble(a, "diffusion", 0.40);
    p.vault_type       = getStr(a, "vault", "Groin / cross vault  (Lyndhurst Hall)");
    p.organ_case       = getDouble(a, "organ",      0.59);
    p.balconies        = getDouble(a, "balconies",  0.54);
    p.temperature      = getDouble(a, "temp",       20.0);
    p.humidity         = getDouble(a, "humidity",   50.0);

    // Source / receiver positions.
    p.source_lx   = getDouble(a, "slx", 0.25);
    p.source_ly   = getDouble(a, "sly", 0.5);
    p.source_rx   = getDouble(a, "srx", 0.75);
    p.source_ry   = getDouble(a, "sry", 0.5);
    p.receiver_lx = getDouble(a, "rlx", 0.35);
    p.receiver_ly = getDouble(a, "rly", 0.8);
    p.receiver_rx = getDouble(a, "rrx", 0.65);
    p.receiver_ry = getDouble(a, "rry", 0.8);

    p.spkl_angle  = getDouble(a, "spkl",  1.57079632679);
    p.spkr_angle  = getDouble(a, "spkr",  1.57079632679);
    p.micl_angle  = getDouble(a, "micl", -2.35619449019);
    p.micr_angle  = getDouble(a, "micr", -0.785398163397);
    p.mic_pattern = getStr(a, "micPat", "cardioid");

    // Source radiation preset (v2.11+). Mirrors PluginProcessor.cpp ::
    // irSynthParamsFromXml — missing attribute resolves to legacy cardioid
    // for back-compat. SourceRadiation::byPreset() also falls back to legacy
    // if the named preset isn't in the registry.
    {
        const std::string srcRadName = getStr(a, "srcRad", "Cardioid (legacy)");
        p.source_radiation = SourceRadiation::byPreset(srcRadName);
    }

    // Source elevation tilt (v2.12+). Defaults to 0 when missing — keeps
    // engine output bit-identical to v2.11 even for non-Legacy radiation
    // kinds (cosThSpk == cos(2D-az-diff) when tilt = 0).
    p.spkl_tilt = getDouble(a, "srcTiltL", 0.0);
    p.spkr_tilt = getDouble(a, "srcTiltR", 0.0);

    p.er_only                 = getBool  (a, "erOnly",        false);
    p.sample_rate             = getInt   (a, "sr",            48000);
    p.bake_er_tail_balance    = getBool  (a, "bakeERTail",    false);
    p.baked_er_gain           = getDouble(a, "bakedERGain",   1.0);
    p.baked_tail_gain         = getDouble(a, "bakedTailGain", 1.0);

    // Multi-mic path enables + experimental toggles.
    p.direct_enabled          = getBool(a, "directOn",   defaults.direct_enabled);
    p.outrig_enabled          = getBool(a, "outrigOn",   defaults.outrig_enabled);
    p.ambient_enabled         = getBool(a, "ambientOn",  defaults.ambient_enabled);

    p.direct_max_order        = getInt (a, "directMaxOrder", defaults.direct_max_order);
    p.lambert_scatter_enabled = getBool(a, "lambertScatter", defaults.lambert_scatter_enabled);
    p.spk_directivity_full    = getBool(a, "spkDirFull",     defaults.spk_directivity_full);
    // Mono speaker source (v2.9.5+). Missing attribute → false (preserves
    // historical dual-speaker rendering for old sidecars).
    p.mono_source             = getBool(a, "monoSrc",        defaults.mono_source);

    // Outrigger pair. Pre-tilt sidecars (no `outrigLtilt` attr) fall back to
    // 0.0 NOT the struct default (-π/6) — same convention as the plugin.
    p.outrig_lx      = getDouble(a, "outrigLx",     defaults.outrig_lx);
    p.outrig_ly      = getDouble(a, "outrigLy",     defaults.outrig_ly);
    p.outrig_rx      = getDouble(a, "outrigRx",     defaults.outrig_rx);
    p.outrig_ry      = getDouble(a, "outrigRy",     defaults.outrig_ry);
    p.outrig_langle  = getDouble(a, "outrigLang",   defaults.outrig_langle);
    p.outrig_rangle  = getDouble(a, "outrigRang",   defaults.outrig_rangle);
    p.outrig_height  = getDouble(a, "outrigHeight", defaults.outrig_height);
    p.outrig_pattern = getStr   (a, "outrigPat",    defaults.outrig_pattern);
    p.outrig_ltilt   = getDouble(a, "outrigLtilt",  0.0);
    p.outrig_rtilt   = getDouble(a, "outrigRtilt",  0.0);

    // Ambient pair.
    p.ambient_lx      = getDouble(a, "ambientLx",     defaults.ambient_lx);
    p.ambient_ly      = getDouble(a, "ambientLy",     defaults.ambient_ly);
    p.ambient_rx      = getDouble(a, "ambientRx",     defaults.ambient_rx);
    p.ambient_ry      = getDouble(a, "ambientRy",     defaults.ambient_ry);
    p.ambient_langle  = getDouble(a, "ambientLang",   defaults.ambient_langle);
    p.ambient_rangle  = getDouble(a, "ambientRang",   defaults.ambient_rangle);
    p.ambient_height  = getDouble(a, "ambientHeight", defaults.ambient_height);
    p.ambient_pattern = getStr   (a, "ambientPat",    defaults.ambient_pattern);
    p.ambient_ltilt   = getDouble(a, "ambientLtilt",  0.0);
    p.ambient_rtilt   = getDouble(a, "ambientRtilt",  0.0);

    // MAIN mic 3D tilt.
    p.micl_tilt = getDouble(a, "miclTilt", 0.0);
    p.micr_tilt = getDouble(a, "micrTilt", 0.0);

    // Decca tree.
    p.main_decca_enabled = getBool  (a, "deccaOn",      defaults.main_decca_enabled);
    p.decca_cx           = getDouble(a, "deccaCx",      defaults.decca_cx);
    p.decca_cy           = getDouble(a, "deccaCy",      defaults.decca_cy);
    p.decca_angle        = getDouble(a, "deccaAng",     defaults.decca_angle);
    p.decca_centre_gain  = getDouble(a, "deccaCtrGain", defaults.decca_centre_gain);
    p.decca_toe_out      = getDouble(a, "deccaToeOut",  defaults.decca_toe_out);
    p.decca_tilt         = getDouble(a, "deccaTilt",    0.0);

    p.mirror_axis        = getInt   (a, "mirrorAxis",   defaults.mirror_axis);

    return p;
}

// ── Binary write helper ──────────────────────────────────────────────────────
static bool writeBytes(const fs::path& p, const void* data, size_t size)
{
    std::ofstream out(p, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    umask(022);

    if (argc < 2)
    {
        std::cerr <<
            "Usage: rebake_factory_irs <ir_root_dir> [options]\n"
            "\n"
            "  Walks <ir_root_dir> for .ping sidecar files, reads each sidecar's\n"
            "  IR-synth parameters, and regenerates the sibling .wav files\n"
            "  (main + _direct + _outrig + _ambient) using the CURRENT engine.\n"
            "  Sidecars are NEVER overwritten.\n"
            "\n"
            "Options:\n"
            "  --dry-run              List what would be rebaked; don't write.\n"
            "  --only <substring>     Only process sidecars whose path contains\n"
            "                         the substring (case-sensitive).  Repeatable.\n"
            "  -q | --quiet           Suppress per-venue progress output.\n"
            "\n"
            "Example:\n"
            "  rebake_factory_irs Installer/factory_irs --only 'Large Beauty'\n";
        return 1;
    }

    fs::path root = argv[1];
    bool dryRun = false;
    bool quiet  = false;
    std::vector<std::string> onlyFilters;

    for (int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        if      (arg == "--dry-run")              dryRun = true;
        else if (arg == "-q" || arg == "--quiet") quiet = true;
        else if (arg == "--only" && i + 1 < argc) onlyFilters.emplace_back(argv[++i]);
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    if (!fs::is_directory(root))
    {
        std::cerr << "Not a directory: " << root << "\n";
        return 1;
    }

    // Collect .ping sidecars.
    std::vector<fs::path> sidecars;
    for (auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ping") continue;

        // Filter against --only substrings (if any).
        if (!onlyFilters.empty())
        {
            const std::string pathStr = entry.path().string();
            bool match = false;
            for (const auto& sub : onlyFilters)
                if (pathStr.find(sub) != std::string::npos) { match = true; break; }
            if (!match) continue;
        }
        sidecars.push_back(entry.path());
    }
    std::sort(sidecars.begin(), sidecars.end());

    if (sidecars.empty())
    {
        std::cout << "No .ping sidecars found under " << root << "\n";
        return 0;
    }

    std::cout << "=== P!NG Factory IR Rebaker (sidecar-driven) ===\n"
              << "Root:    " << root << "\n"
              << "Sidecars: " << sidecars.size() << "\n"
              << (dryRun ? "Mode:    DRY RUN (no files will be written)\n" : "")
              << "\n";

    int done = 0, failed = 0, skipped = 0;

    for (const auto& pingPath : sidecars)
    {
        const fs::path base   = pingPath.parent_path() / pingPath.stem();
        const fs::path wavMain    = base.string() + ".wav";
        const fs::path wavDirect  = base.string() + "_direct.wav";
        const fs::path wavOutrig  = base.string() + "_outrig.wav";
        const fs::path wavAmbient = base.string() + "_ambient.wav";

        const std::string relative = fs::relative(pingPath, root).string();
        std::cout << "[" << (done + failed + skipped + 1) << "/" << sidecars.size()
                  << "] " << relative << "\n";

        // Parse sidecar.
        std::string xml = readFileAll(pingPath);
        if (xml.empty())
        {
            std::cerr << "    ERROR: cannot read " << pingPath << "\n";
            ++failed;
            continue;
        }
        bool parseOk = false;
        auto attrs = parsePingAttrs(xml, parseOk);
        if (!parseOk)
        {
            std::cerr << "    ERROR: no <irSynthParams> element found\n";
            ++failed;
            continue;
        }

        IRSynthParams p = paramsFromPingAttrs(attrs);

        if (!quiet)
        {
            std::cout << "    shape=" << p.shape
                      << "  W×D×H=" << p.width << "×" << p.depth << "×" << p.height
                      << "  sr=" << p.sample_rate
                      << "  direct=" << (p.direct_enabled  ? 1 : 0)
                      << "  outrig=" << (p.outrig_enabled  ? 1 : 0)
                      << "  ambient=" << (p.ambient_enabled ? 1 : 0) << "\n";
        }

        if (dryRun)
        {
            ++skipped;
            continue;
        }

        // Synthesise using the current engine.
        auto t0 = std::chrono::steady_clock::now();
        IRSynthProgressFn cb;
        if (!quiet)
        {
            cb = [](double frac, const std::string& msg) {
                std::cout << "    " << (int)(frac * 100.0) << "% " << msg << "            \r";
                std::cout.flush();
            };
        }
        IRSynthResult result = IRSynthEngine::synthIR(p, cb);
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        if (!result.success)
        {
            std::cerr << "    ERROR: synthIR failed: " << result.errorMessage << "\n";
            ++failed;
            continue;
        }

        // Per-channel peak amplitude readout. Useful for spotting any IR
        // that's heading toward 0 dBFS (which would clip when written to
        // 24-bit PCM in makeWav). Suppressed under -q / --quiet.
        if (!quiet)
        {
            auto peakOf = [](const std::vector<double>& v) {
                double pk = 0.0;
                for (double s : v) { double a = s < 0 ? -s : s; if (a > pk) pk = a; }
                return pk;
            };
            const double pLL = peakOf(result.iLL);
            const double pRL = peakOf(result.iRL);
            const double pLR = peakOf(result.iLR);
            const double pRR = peakOf(result.iRR);
            auto db = [](double x) {
                if (x <= 0.0) return std::string("-inf");
                char buf[32]; std::snprintf(buf, sizeof buf, "%6.2f", 20.0 * std::log10(x));
                return std::string(buf);
            };
            std::cout << "    peak (dBFS):  LL=" << db(pLL) << "  RL=" << db(pRL)
                      << "  LR=" << db(pLR) << "  RR=" << db(pRR) << "\n";
        }

        if (!quiet)
            std::cout << "    Done in " << elapsed << " s ("
                      << (result.irLen / result.sampleRate) << " s IR)\n";

        // Write MAIN .wav.
        auto mainBytes = IRSynthEngine::makeWav(
            result.iLL, result.iRL, result.iLR, result.iRR, result.sampleRate);
        if (!writeBytes(wavMain, mainBytes.data(), mainBytes.size()))
        {
            std::cerr << "    ERROR writing " << wavMain << "\n";
            ++failed;
            continue;
        }
        if (!quiet) std::cout << "    MAIN:    " << wavMain.filename() << " (" << (mainBytes.size() / 1024) << " KB)\n";

        // Write aux .wavs (only those the sidecar requested AND that the
        // engine actually synthesised).  The sibling-WAV autoload in the
        // plugin keys on presence of each file, so we remove stale aux
        // files when the sidecar disables the corresponding path.
        auto writeAux = [&](const MicIRChannels& ch, const fs::path& outP, const char* label) {
            if (!ch.synthesised || ch.LL.empty())
            {
                std::error_code ec;
                if (fs::remove(outP, ec) && !quiet)
                    std::cout << "    " << label << ":  (removed stale " << outP.filename() << ")\n";
                return;
            }
            auto bytes = IRSynthEngine::makeWav(ch.LL, ch.RL, ch.LR, ch.RR, result.sampleRate);
            if (!writeBytes(outP, bytes.data(), bytes.size()))
            {
                std::cerr << "    ERROR writing " << outP << "\n";
                return;
            }
            if (!quiet) std::cout << "    " << label << ":  " << outP.filename()
                                  << " (" << (bytes.size() / 1024) << " KB)\n";
        };
        writeAux(result.direct,  wavDirect,  "DIRECT ");
        writeAux(result.outrig,  wavOutrig,  "OUTRIG ");
        writeAux(result.ambient, wavAmbient, "AMBIENT");

        ++done;
    }

    std::cout << "\n=== Complete: " << done << " rebaked"
              << (skipped ? ", " + std::to_string(skipped) + " skipped (dry run)" : "")
              << (failed  ? ", " + std::to_string(failed)  + " failed"             : "")
              << " ===\n";
    return (failed > 0) ? 1 : 0;
}
