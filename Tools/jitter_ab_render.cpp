// jitter_ab_render.cpp
//
// Listening A/B helper for the v2.10 mirror-symmetric ER jitter fix.
// Renders two IRs (source on the left, source on the right) using the
// CURRENT engine — i.e. the post-fix hash-keyed jitter scheme — and writes
// each one to a stereo WAV so the user can flip between them and verify
// that the right-side image now sounds as cleanly localised as the left.
//
// To compare against the OLD (pre-fix) behaviour, do this with both
// branches checked out:
//   1. git checkout main (or the commit before the fix), build, run this
//      tool → wav files written to listening-ab/old-{left,right}.wav.
//   2. git checkout fix/mirror-symmetric-er-jitter, build, run this tool
//      with `--prefix new` → wav files at listening-ab/new-{left,right}.wav.
//   3. Open all four files in a DAW, level-match, and A/B blind.
//
// The IRs use the small-room geometry from IR_32 with ER-only OFF (so the
// FDN tail is included) and the Decca tree at default rotation. This is the
// same configuration the user reported the original asymmetry in.
//
// Build (from repo root):
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/jitter_ab_render.cpp -o build/jitter_ab_render
//   ./build/jitter_ab_render
//   ./build/jitter_ab_render --prefix new
//
// The output filenames embed the prefix so the same tool produces a
// distinct pair for each branch without overwriting earlier renders.

#define PING_TESTING_BUILD 1
#include "../Source/IRSynthEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    // Minimal little-endian PCM-16 stereo WAV writer. Sufficient for
    // listening tests (no metadata, no float, no >2 channels).
    bool writeStereoWav (const std::string& path,
                         const std::vector<double>& l,
                         const std::vector<double>& r,
                         int sampleRate)
    {
        const std::size_t n = std::min (l.size(), r.size());
        if (n == 0) return false;

        // Normalise to peak ≤ 0.95 so quantisation noise stays clear of
        // clipping. Both channels share the same gain so relative L/R
        // levels are preserved.
        double peak = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            peak = std::max ({ peak, std::fabs (l[i]), std::fabs (r[i]) });
        const double gain = (peak > 1e-30) ? (0.95 / peak) : 1.0;

        const uint32_t byteRate    = (uint32_t) sampleRate * 2 /*ch*/ * 2 /*bytes*/;
        const uint32_t dataBytes   = (uint32_t) (n * 2 * 2);
        const uint32_t riffBytes   = 36u + dataBytes;

        std::ofstream out (path, std::ios::binary);
        if (! out) return false;

        auto u16 = [&](uint16_t v) { out.write (reinterpret_cast<const char*>(&v), 2); };
        auto u32 = [&](uint32_t v) { out.write (reinterpret_cast<const char*>(&v), 4); };

        out.write ("RIFF", 4);  u32 (riffBytes);   out.write ("WAVE", 4);
        out.write ("fmt ", 4);  u32 (16);
        u16 (1);                                   // PCM
        u16 (2);                                   // 2 channels
        u32 ((uint32_t) sampleRate);
        u32 (byteRate);
        u16 (4);                                   // block align (2ch * 2 bytes)
        u16 (16);                                  // bits per sample
        out.write ("data", 4);  u32 (dataBytes);

        for (std::size_t i = 0; i < n; ++i)
        {
            const int16_t li = (int16_t) std::clamp ((int) std::round (l[i] * gain * 32767.0), -32768, 32767);
            const int16_t ri = (int16_t) std::clamp ((int) std::round (r[i] * gain * 32767.0), -32768, 32767);
            out.write (reinterpret_cast<const char*>(&li), 2);
            out.write (reinterpret_cast<const char*>(&ri), 2);
        }
        return out.good();
    }

    IRSynthParams smallRoomDeccaParams()
    {
        IRSynthParams p;
        p.shape  = "Rectangular";
        p.width  = 10.0;
        p.depth  =  8.0;
        p.height =  5.0;

        // Match the user's reported configuration: Decca tree, mono speaker
        // source, default toe-out and centre fill, default mic pattern.
        p.diffusion = 0.55;
        p.organ_case = 0.40;
        p.balconies  = 0.60;
        p.audience  = 0.30;

        p.spkl_angle = 1.5707963267948966;   // π/2 — forward
        p.spkr_angle = 1.5707963267948966;
        p.mono_source = true;

        p.main_decca_enabled = true;
        p.decca_cx = 0.5;
        p.decca_cy = 0.65;
        p.decca_angle = -1.5707963267948966; // forward
        // Defaults: toe_out = π/2, centre_gain = 0.5.

        p.sample_rate = 48000;
        return p;
    }
}

int main (int argc, char** argv)
{
    std::string prefix = "old";
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp (argv[i], "--prefix") == 0)
            prefix = argv[i + 1];

    namespace fs = std::filesystem;
    const fs::path outDir = "listening-ab";
    std::error_code ec;
    fs::create_directories (outDir, ec);

    auto progress = [](double, const std::string&) {};

    IRSynthParams pL = smallRoomDeccaParams();
    pL.source_lx = 0.30; pL.source_ly = 0.50;
    pL.source_rx = 0.30; pL.source_ry = 0.50;
    auto rL = IRSynthEngine::synthIR (pL, progress);
    if (! rL.success) { std::fprintf (stderr, "Left scene failed: %s\n", rL.errorMessage.c_str()); return 1; }

    IRSynthParams pR = smallRoomDeccaParams();
    pR.source_lx = 0.70; pR.source_ly = 0.50;
    pR.source_rx = 0.70; pR.source_ry = 0.50;
    auto rR = IRSynthEngine::synthIR (pR, progress);
    if (! rR.success) { std::fprintf (stderr, "Right scene failed: %s\n", rR.errorMessage.c_str()); return 1; }

    // The engine renders 4 channels (iLL/iRL/iLR/iRR). For mono input the
    // L mic output is iLL + iRL and the R mic output is iLR + iRR.
    auto mix = [] (const IRSynthResult& r, std::vector<double>& outL, std::vector<double>& outR)
    {
        const std::size_t n = (std::size_t) r.irLen;
        outL.resize (n);
        outR.resize (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            outL[i] = r.iLL[i] + r.iRL[i];
            outR[i] = r.iLR[i] + r.iRR[i];
        }
    };

    std::vector<double> lL, lR, rL2, rR2;
    mix (rL, lL, lR);
    mix (rR, rL2, rR2);

    const fs::path leftPath  = outDir / (prefix + "-left.wav");
    const fs::path rightPath = outDir / (prefix + "-right.wav");
    if (! writeStereoWav (leftPath.string(),  lL,  lR,  rL.sampleRate)) { std::fprintf (stderr, "write %s failed\n", leftPath.string().c_str()); return 2; }
    if (! writeStereoWav (rightPath.string(), rL2, rR2, rR.sampleRate)) { std::fprintf (stderr, "write %s failed\n", rightPath.string().c_str()); return 2; }

    std::printf ("Wrote %s (sr=%d, len=%d)\n", leftPath.string().c_str(),  rL.sampleRate, rL.irLen);
    std::printf ("Wrote %s (sr=%d, len=%d)\n", rightPath.string().c_str(), rR.sampleRate, rR.irLen);
    return 0;
}
