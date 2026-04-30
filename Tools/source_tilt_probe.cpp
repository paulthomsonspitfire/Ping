// source_tilt_probe.cpp — v2.12 source elevation tilt diagnostics
//
// Renders a simple Decca-tree scene under several SourceRadiation preset +
// tilt combinations and:
//   1. Prints per-band early-window RMS at the L Decca outer mic so the
//      effect of tilt on each frequency band is visible numerically.
//   2. Writes one stereo WAV per (preset, tilt) so the user can blind-A/B
//      in a DAW. Files land in `listening-ab/` next to the cwd, named
//      tilt-<preset>-<tiltDeg>.wav.
//
// Build (from repo root, must run AFTER PingTests has been built so the
// libPingData object isn't needed — this probe doesn't load JSON):
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/source_tilt_probe.cpp \
//       -o build/source_tilt_probe
//   ./build/source_tilt_probe

#define PING_TESTING_BUILD 1
#include "../Source/IRSynthEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr double kDeg = M_PI / 180.0;

    // 16-bit PCM WAV writer (interleaved stereo). Used for the listening-A/B
    // outputs only — no claim to high-fidelity, just enough to drag into a DAW.
    void writeStereoWav (const std::filesystem::path& out,
                         const std::vector<float>& interleaved,
                         int sampleRate)
    {
        std::filesystem::create_directories (out.parent_path());
        std::ofstream f (out, std::ios::binary);
        if (! f) { std::fprintf (stderr, "cannot open %s for writing\n", out.string().c_str()); return; }
        const uint16_t numCh   = 2;
        const uint16_t bitsPS  = 16;
        const uint32_t byteRate  = (uint32_t) (sampleRate * numCh * bitsPS / 8);
        const uint16_t blockAlign = (uint16_t) (numCh * bitsPS / 8);
        const uint32_t dataLen  = (uint32_t) (interleaved.size() * sizeof (int16_t));
        const uint32_t fmtLen   = 16;
        const uint32_t riffLen  = 36 + dataLen;
        auto w32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
        auto w16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
        f.write ("RIFF", 4);  w32 (riffLen);  f.write ("WAVE", 4);
        f.write ("fmt ", 4);  w32 (fmtLen);
        w16 (1);              w16 (numCh);
        w32 ((uint32_t) sampleRate); w32 (byteRate);
        w16 (blockAlign);     w16 (bitsPS);
        f.write ("data", 4);  w32 (dataLen);
        for (float s : interleaved)
        {
            const float clamped = std::max (-1.0f, std::min (1.0f, s));
            const int16_t i16 = (int16_t) std::lround (clamped * 32767.0f);
            f.write (reinterpret_cast<const char*> (&i16), 2);
        }
    }

    // Decca-tree scene matching the IR_36 / IR_37 setup. Mono source so a
    // single tilt knob actually matters; small room so renders are fast.
    IRSynthParams sceneDecca()
    {
        IRSynthParams p;
        p.width  = 10.0; p.depth = 8.0; p.height = 5.0;
        p.diffusion = 0.55;
        p.organ_case = 0.40;
        p.balconies  = 0.60;
        p.spkl_angle = 1.5707963267948966;   // forward (= +90° in screen az)
        p.spkr_angle = 1.5707963267948966;
        p.mono_source = true;
        p.main_decca_enabled = true;
        p.decca_cx = 0.5;  p.decca_cy = 0.65;
        p.decca_angle = -1.5707963267948966; // mics aimed back at source
        p.source_lx = 0.30; p.source_ly = 0.50;
        p.source_rx = 0.30; p.source_ry = 0.50;
        return p;
    }

    // Crude single-pole bandpass for per-band RMS estimation (not engineering-
    // grade, just consistent across runs).
    constexpr std::array<double, 8> kBandFc {
        125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };

    double bandRmsDb (const std::vector<double>& v, int sr, double fc, int windowMs)
    {
        const double w0    = 2.0 * M_PI * fc / sr;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double q     = 0.707;
        const double alpha = sinw0 / (2.0 * q);
        const double a0    = 1.0 + alpha;
        const double b0    =  alpha / a0;
        const double b2    = -alpha / a0;
        const double a1    = -2.0 * cosw0 / a0;
        const double a2    = (1.0 - alpha) / a0;
        double z1 = 0.0, z2 = 0.0, sumSq = 0.0;
        const int n = std::min ((int) v.size(), sr * windowMs / 1000);
        for (int i = 0; i < n; ++i)
        {
            const double x = v[(size_t) i];
            const double y = b0 * x + z1;
            z1 = -a1 * y + z2;
            z2 = b2 * x - a2 * y;
            sumSq += y * y;
        }
        const double rms = std::sqrt (sumSq / std::max (1, n));
        return (rms > 1e-30) ? 20.0 * std::log10 (rms) : -200.0;
    }
}

int main()
{
    const auto base = sceneDecca();

    struct Variant { std::string label; SourceRadiation sr; double tiltDeg; };

    const std::vector<Variant> variants {
        { "legacy-flat",        SourceRadiation::legacyCardioid(),    0.0  },
        { "legacy-tilt45",      SourceRadiation::legacyCardioid(),   45.0  },  // ignored
        { "generic-flat",       SourceRadiation::genericInstrument(), 0.0  },
        { "generic-tilt30",     SourceRadiation::genericInstrument(),30.0  },
        { "generic-tilt60",     SourceRadiation::genericInstrument(),60.0  },
        { "generic-tilt90",     SourceRadiation::genericInstrument(),90.0  },
        { "strings-default",    SourceRadiation::stringsBroad(),     30.0  },  // preset hint
        { "strings-flat",       SourceRadiation::stringsBroad(),      0.0  },
        { "voice-default",      SourceRadiation::voice(),            10.0  },
        { "voice-tilt45",       SourceRadiation::voice(),            45.0  },
        { "brass-flat",         SourceRadiation::brassForward(),      0.0  },
        { "brass-tilt30",       SourceRadiation::brassForward(),     30.0  },
    };

    std::printf ("\n=== source_tilt_probe (v2.12) ===\n");
    std::printf ("Scene: small Decca, source forward, mono, decca aimed back.\n\n");
    std::printf ("%-22s | RMS dB per band (L outer, 0..50ms)\n", "variant");
    std::printf ("%-22s | %6s %6s %6s %6s %6s %6s %6s %6s\n",
                 "", "125", "250", "500", "1k", "2k", "4k", "8k", "16k");
    std::printf ("------------------------------------------------------------"
                 "----------------------------\n");

    auto noop = [] (double, const std::string&) {};

    for (const auto& v : variants)
    {
        IRSynthParams p = base;
        p.source_radiation = v.sr;
        p.spkl_tilt = v.tiltDeg * kDeg;
        p.spkr_tilt = v.tiltDeg * kDeg;       // mono mode mirrors anyway

        const auto r = IRSynthEngine::synthIR (p, noop);
        if (! r.success) { std::fprintf (stderr, "synthIR failed for %s\n", v.label.c_str()); continue; }

        std::printf ("%-22s |", v.label.c_str());
        for (int b = 0; b < 8; ++b)
        {
            const double db = bandRmsDb (r.iLL, r.sampleRate, kBandFc[(size_t) b], 50);
            std::printf (" %+6.1f", db);
        }
        std::printf ("\n");

        // Listening A/B WAV: stereo (iLL → L, iLR → R), normalised peak 0.7
        // so the level differences across variants are audible.
        std::vector<float> il (r.iLL.size()), ir (r.iLR.size());
        double peak = 0.0;
        for (size_t i = 0; i < r.iLL.size(); ++i) peak = std::max (peak, std::fabs (r.iLL[i]));
        for (size_t i = 0; i < r.iLR.size(); ++i) peak = std::max (peak, std::fabs (r.iLR[i]));
        const double g = (peak > 1e-9) ? (0.7 / peak) : 1.0;
        std::vector<float> stereo (2 * std::max (r.iLL.size(), r.iLR.size()), 0.0f);
        for (size_t i = 0; i < r.iLL.size(); ++i) stereo[2 * i + 0] = (float) (g * r.iLL[i]);
        for (size_t i = 0; i < r.iLR.size(); ++i) stereo[2 * i + 1] = (float) (g * r.iLR[i]);
        const std::filesystem::path out =
            std::filesystem::path ("listening-ab") / ("tilt-" + v.label + ".wav");
        writeStereoWav (out, stereo, r.sampleRate);
    }

    std::printf ("\nWAVs written to ./listening-ab/tilt-*.wav\n");
    std::printf ("Compare strings-default vs strings-flat to hear the +30° tilt; the\n");
    std::printf ("Decca tree is mounted high so the tilted-up source sends more\n");
    std::printf ("on-axis energy at the mic array and HF balance lifts.\n");
    return 0;
}
