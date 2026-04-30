// source_radiation_probe.cpp
//
// Investigates whether a frequency-dependent source radiation model is a
// better match for real instruments than the engine's current pure cardioid.
//
// Three pieces of output:
//
//   1. Per-band, per-angle gain table for each model — pattern shape only,
//      no engine involved. Lets us reason about the polar pattern before
//      committing to a render.
//
//   2. Engine-level early-energy probe. Renders the same scene under
//      models 0..3 and prints, per band, the early-window (0..50 ms) RMS
//      captured at the L/R Decca outer mics. This shows how much each
//      model actually changes what the mics hear, in dB, at each band,
//      without listening yet.
//
//   3. Listening A/B WAV renderer. Writes one stereo WAV per model so the
//      user can drag them into a DAW, level-match and blind A/B.
//
// Build (from repo root):
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/source_radiation_probe.cpp \
//       -o build/source_radiation_probe
//   ./build/source_radiation_probe

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

    // ── Pattern-only evaluators (mirror what's in IRSynthEngine.cpp) ────
    //
    // These have to track the engine's
    // fillSpkBandGainsExperimental()/spkG() definitions exactly. Kept as a
    // local reference here so the probe is self-contained and the table
    // it prints is the same numbers the engine actually uses.

    constexpr std::array<double, 8> kSpkBandExpProbe = {
        0.0, 0.1, 0.3, 0.7, 1.2, 2.0, 3.0, 4.0
    };
    constexpr double kSpkFloorProbe = 0.10;

    double patternGain (int model, int band, double thetaDeg)
    {
        const double cosTh = std::cos (thetaDeg * kDeg);
        switch (model)
        {
            default:
            case 0:                                       // Pure cardioid (legacy)
                return std::max (0.0, 0.5 + 0.5 * cosTh);
            case 1:
            {                                             // Per-band cardioid + floor
                const double card = std::max (0.0, 0.5 + 0.5 * cosTh);
                return kSpkFloorProbe
                     + (1.0 - kSpkFloorProbe) * std::pow (card, kSpkBandExpProbe[(size_t) band]);
            }
            case 2:                                       // Omni
                return 1.0;
            case 3:                                       // Supercardioid + floor
                return std::max (kSpkFloorProbe, 0.378 + 0.622 * cosTh);
        }
    }

    const char* modelName (int m)
    {
        switch (m)
        {
            case 0: return "cardioid (legacy)";
            case 1: return "per-band cardioid + floor";
            case 2: return "omni";
            case 3: return "supercardioid + floor";
            default: return "?";
        }
    }

    // ── Octave-band IIR bandpass for early-energy probe ─────────────────
    //
    // We need to attribute early-window RMS to each octave band. A single
    // 2nd-order Butterworth bandpass per band is plenty for a probe (we're
    // looking at 6+ dB differences, not splitting hairs). Filter centres
    // match the engine's N_BANDS = 8 [125, 250, 500, 1k, 2k, 4k, 8k, 16k].
    constexpr std::array<double, 8> kBandFc = {
        125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };

    struct BiquadBP
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        // RBJ Audio EQ Cookbook BPF (constant 0 dB peak gain).
        static BiquadBP make (double fc, double sr, double q = 0.707)
        {
            BiquadBP f;
            const double w0    = 2.0 * M_PI * fc / sr;
            const double cosw0 = std::cos (w0);
            const double sinw0 = std::sin (w0);
            const double alpha = sinw0 / (2.0 * q);
            const double a0    = 1.0 + alpha;
            f.b0 =  alpha / a0;
            f.b1 =  0.0;
            f.b2 = -alpha / a0;
            f.a1 = -2.0 * cosw0 / a0;
            f.a2 = (1.0 - alpha) / a0;
            return f;
        }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    double rmsBandDb (const std::vector<double>& v, int sr, double fc, int windowMs)
    {
        BiquadBP f = BiquadBP::make (fc, (double) sr);
        const int n = std::min ((int) v.size(), sr * windowMs / 1000);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double y = f.process (v[(size_t) i]);
            sumSq += y * y;
        }
        const double rms = std::sqrt (sumSq / std::max (1, n));
        return (rms > 1e-30) ? 20.0 * std::log10 (rms) : -200.0;
    }

    // ── Octagonal scene matching the user's screenshots ─────────────────
    // aimMode == 0: speaker aimed at Decca tree (forward-firing — typical
    //               solo-instrument use case).
    // aimMode == 1: speaker rotated 90° relative to Decca tree (lateral —
    //               instrument aimed sideways, e.g. brass section turned
    //               toward the wall).
    // aimMode == 2: speaker aimed AWAY from Decca tree (rear — drives most
    //               energy through reflections rather than direct).
    IRSynthParams sceneOctagonal (int aimMode)
    {
        IRSynthParams p;
        p.shape  = "Octagonal";
        p.width  = 26.0; p.depth  = 20.5; p.height = 14.0;
        p.floor_material   = "Hardwood floor";
        p.ceiling_material = "Painted plaster";
        p.wall_material    = "Painted plaster";
        p.window_fraction  = 0.28;
        p.audience  = 0.45; p.diffusion = 0.68;
        p.vault_type = "Groin / cross vault  (Lyndhurst Hall)";
        p.organ_case = 0.06; p.balconies = 0.13;

        p.lambert_scatter_enabled = true;
        p.spk_directivity_full    = true;        // Match the user's setting
        p.mono_source             = true;

        p.main_decca_enabled = true;
        p.mic_pattern        = "cardioid (LDC)";
        p.decca_cx           = 0.5; p.decca_cy = 0.65;
        p.decca_angle        = -M_PI_2;
        p.decca_toe_out      = M_PI_2;
        p.decca_tilt         = -30.0 * kDeg;
        p.decca_centre_gain  = 0.5012;

        p.outrig_enabled = true;
        p.outrig_lx = 0.15; p.outrig_ly = 0.80;
        p.outrig_rx = 0.85; p.outrig_ry = 0.80;
        p.outrig_langle = -2.35619449019;
        p.outrig_rangle = -0.785398163397;

        p.ambient_enabled = true;
        p.ambient_lx = 0.20; p.ambient_ly = 0.95;
        p.ambient_rx = 0.80; p.ambient_ry = 0.95;
        p.ambient_langle = -2.35619449019;
        p.ambient_rangle = -0.785398163397;

        p.direct_enabled   = true;
        p.direct_max_order = 1;

        const double sx = 0.30, sy = 0.30;
        p.source_lx = sx; p.source_ly = sy;
        p.source_rx = sx; p.source_ry = sy;
        const double aimAtTree = std::atan2 (0.65 - sy, 0.5 - sx);
        double engAng = aimAtTree;
        if (aimMode == 1) engAng = aimAtTree + M_PI_2;     // rotated 90°
        if (aimMode == 2) engAng = aimAtTree + M_PI;       // rotated 180°
        p.spkl_angle = engAng;
        p.spkr_angle = engAng;

        p.sample_rate = 48000;
        return p;
    }

    // ── Mix MAIN + OUTRIG + AMBIENT + DIRECT into stereo L/R ────────────
    constexpr double kMainDb    =  0.0;
    constexpr double kOutrigDb  = -6.0;
    constexpr double kAmbientDb = -9.0;
    constexpr double kDirectDb  = -6.0;
    double dbToLin (double db) { return std::pow (10.0, db * 0.05); }

    void addPath (std::vector<double>& outL, std::vector<double>& outR,
                  const std::vector<double>& LL, const std::vector<double>& RL,
                  const std::vector<double>& LR, const std::vector<double>& RR,
                  double gain)
    {
        const std::size_t n = std::min ({ outL.size(), LL.size(), RL.size(), LR.size(), RR.size() });
        for (std::size_t i = 0; i < n; ++i)
        {
            outL[i] += gain * (LL[i] + RL[i]);
            outR[i] += gain * (LR[i] + RR[i]);
        }
    }

    void mixFullStereo (const IRSynthResult& r,
                        std::vector<double>& outL, std::vector<double>& outR)
    {
        outL.assign ((size_t) r.irLen, 0.0);
        outR.assign ((size_t) r.irLen, 0.0);
        addPath (outL, outR, r.iLL, r.iRL, r.iLR, r.iRR, dbToLin (kMainDb));
        if (r.outrig.synthesised)
            addPath (outL, outR, r.outrig.LL, r.outrig.RL, r.outrig.LR, r.outrig.RR, dbToLin (kOutrigDb));
        if (r.ambient.synthesised)
            addPath (outL, outR, r.ambient.LL, r.ambient.RL, r.ambient.LR, r.ambient.RR, dbToLin (kAmbientDb));
        if (r.direct.synthesised)
            addPath (outL, outR, r.direct.LL, r.direct.RL, r.direct.LR, r.direct.RR, dbToLin (kDirectDb));
    }

    // ── Minimal stereo PCM-16 WAV writer ────────────────────────────────
    // Each WAV is independently peak-normalised to 0.95 so all four files
    // sit at similar listening level — relative inter-model loudness is
    // still printed in the early-energy table.
    bool writeStereoWav (const std::string& path,
                         const std::vector<double>& l,
                         const std::vector<double>& r,
                         int sampleRate)
    {
        const std::size_t n = std::min (l.size(), r.size());
        if (n == 0) return false;
        double peak = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            peak = std::max ({ peak, std::fabs (l[i]), std::fabs (r[i]) });
        const double gain = (peak > 1e-30) ? (0.95 / peak) : 1.0;

        const uint32_t byteRate  = (uint32_t) sampleRate * 2 * 2;
        const uint32_t dataBytes = (uint32_t) (n * 2 * 2);
        const uint32_t riffBytes = 36u + dataBytes;

        std::ofstream out (path, std::ios::binary);
        if (! out) return false;
        auto u16 = [&](uint16_t v) { out.write (reinterpret_cast<const char*>(&v), 2); };
        auto u32 = [&](uint32_t v) { out.write (reinterpret_cast<const char*>(&v), 4); };
        out.write ("RIFF", 4); u32 (riffBytes); out.write ("WAVE", 4);
        out.write ("fmt ", 4); u32 (16);
        u16 (1); u16 (2); u32 ((uint32_t) sampleRate); u32 (byteRate); u16 (4); u16 (16);
        out.write ("data", 4); u32 (dataBytes);
        for (std::size_t i = 0; i < n; ++i)
        {
            const int16_t li = (int16_t) std::clamp ((int) std::round (l[i] * gain * 32767.0), -32768, 32767);
            const int16_t ri = (int16_t) std::clamp ((int) std::round (r[i] * gain * 32767.0), -32768, 32767);
            out.write (reinterpret_cast<const char*>(&li), 2);
            out.write (reinterpret_cast<const char*>(&ri), 2);
        }
        return out.good();
    }
}

int main()
{
    // ── 1. Per-band per-angle pattern table (pattern only) ─────────────
    std::printf ("\n=== Source radiation pattern gain (dB), pattern only ===\n");
    std::printf ("Rows = octave band, columns = angle off-axis (0° = on-axis, 180° = directly behind).\n\n");

    constexpr std::array<double, 8> bandFc = { 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    const std::array<double, 7> angles = { 0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0 };

    for (int model = 0; model < 4; ++model)
    {
        std::printf ("Model %d: %s\n", model, modelName (model));
        std::printf ("                       ");
        for (double a : angles) std::printf ("%6.0f° ", a);
        std::printf ("\n");
        for (int b = 0; b < 8; ++b)
        {
            std::printf ("  band %d (%5.0f Hz):  ", b, bandFc[(size_t) b]);
            for (double a : angles)
            {
                const double g  = patternGain (model, b, a);
                const double db = (g > 1e-9) ? 20.0 * std::log10 (g) : -200.0;
                std::printf ("%+6.2f ", db);
            }
            std::printf ("\n");
        }
        std::printf ("\n");
    }

    // ── 2. Engine-level early-energy probe across three aim modes ─────
    auto noProgress = [](double, const std::string&) {};

    auto runScene = [&](int aimMode, const char* label, const char* prefix)
    {
        std::printf ("=== Engine early-window (0..50 ms) RMS per band — %s ===\n", label);
        std::printf ("Mono puck at (0.30, 0.30); MAIN + OUTRIG(-6dB) + AMBIENT(-9dB) + DIRECT(-6dB).\n\n");

        // Map old M0..M3 indices to the new SourceRadiation registry.
        const std::array<SourceRadiation, 4> testModels = {
            SourceRadiation::legacyCardioid(),         // M0
            SourceRadiation::genericInstrument(),      // M1 — per-band cardioid + floor
            SourceRadiation::omni(),                   // M2
            SourceRadiation::supercardioidWithFloor()  // M3
        };
        std::vector<IRSynthResult> results (4);
        for (int model = 0; model < 4; ++model)
        {
            IRSynthParams p = sceneOctagonal (aimMode);
            p.source_radiation = testModels[(size_t) model];
            results[(size_t) model] = IRSynthEngine::synthIR (p, noProgress);
            if (! results[(size_t) model].success)
            {
                std::fprintf (stderr, "model %d synth failed: %s\n",
                              model, results[(size_t) model].errorMessage.c_str());
                std::exit (1);
            }
        }

        std::printf ("Band  fc(Hz)     ");
        for (int m = 0; m < 4; ++m)
            std::printf ("│  M%d L     M%d R    M%d L−R ", m, m, m);
        std::printf ("\n");
        for (int b = 0; b < 8; ++b)
        {
            std::printf ("  %d   %5.0f       ", b, kBandFc[(size_t) b]);
            for (int m = 0; m < 4; ++m)
            {
                std::vector<double> oL, oR;
                mixFullStereo (results[(size_t) m], oL, oR);
                const double dbL = rmsBandDb (oL, results[(size_t) m].sampleRate, kBandFc[(size_t) b], 50);
                const double dbR = rmsBandDb (oR, results[(size_t) m].sampleRate, kBandFc[(size_t) b], 50);
                std::printf ("│ %+6.2f  %+6.2f  %+6.2f  ", dbL, dbR, dbL - dbR);
            }
            std::printf ("\n");
        }

        std::printf ("                      ");
        for (int m = 0; m < 4; ++m)
            std::printf ("│  M%d L     M%d R    M%d L−R ", m, m, m);
        std::printf ("\n  (wide-band, full)  ");
        for (int m = 0; m < 4; ++m)
        {
            std::vector<double> oL, oR;
            mixFullStereo (results[(size_t) m], oL, oR);
            const int n = std::min ((int) oL.size(), results[(size_t) m].sampleRate * 50 / 1000);
            double sL = 0.0, sR = 0.0;
            for (int i = 0; i < n; ++i) { sL += oL[(size_t) i]*oL[(size_t) i]; sR += oR[(size_t) i]*oR[(size_t) i]; }
            const double rL = std::sqrt (sL / std::max (1, n));
            const double rR = std::sqrt (sR / std::max (1, n));
            const double dbL = (rL > 1e-30) ? 20.0 * std::log10 (rL) : -200.0;
            const double dbR = (rR > 1e-30) ? 20.0 * std::log10 (rR) : -200.0;
            std::printf ("│ %+6.2f  %+6.2f  %+6.2f  ", dbL, dbR, dbL - dbR);
        }
        std::printf ("\n\n");

        // WAVs for this aim mode
        namespace fs = std::filesystem;
        const fs::path outDir = "listening-ab";
        std::error_code ec;
        fs::create_directories (outDir, ec);
        for (int m = 0; m < 4; ++m)
        {
            std::vector<double> oL, oR;
            mixFullStereo (results[(size_t) m], oL, oR);
            char fname[96];
            std::snprintf (fname, sizeof fname, "srcmodel-%s-M%d.wav", prefix, m);
            const fs::path p = outDir / fname;
            if (! writeStereoWav (p.string(), oL, oR, results[(size_t) m].sampleRate))
                std::fprintf (stderr, "Write failed: %s\n", p.string().c_str());
        }
    };

    runScene (0, "speaker AIMED at Decca tree (forward, normal use)", "fwd");
    runScene (1, "speaker rotated 90° from Decca tree (lateral)",      "side");
    runScene (2, "speaker rotated 180° from Decca tree (rear-firing)", "rear");

    std::printf ("=== Listening A/B WAV files ===\n");
    std::printf ("  listening-ab/srcmodel-{fwd,side,rear}-M{0..3}.wav  (12 files total)\n");
    std::printf ("  M0 = cardioid (legacy)        M1 = per-band cardioid + floor\n");
    std::printf ("  M2 = omni                     M3 = supercardioid + floor\n");
    std::printf ("\nEach WAV is independently peak-normalised to 0.95.\n");
    std::printf ("Compare M0..M3 within each aim mode. The forward case should sound\n");
    std::printf ("nearly identical across models; the lateral and rear cases should\n");
    std::printf ("expose much bigger model-to-model differences.\n");
    return 0;
}
