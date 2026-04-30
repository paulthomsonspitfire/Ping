// octagonal_direct_probe.cpp
//
// Reproduces the user's screenshot setup (Octagonal 26×20.5×14, mono puck,
// DIRECT enabled at order 1, Decca tree cardioid LDC, vault Lyndhurst) and
// renders three scenes to isolate engine asymmetry from setup asymmetry.
//
// Angle convention: the UI displays angle_ui = angle_engine * 180/π + 90°
// (so UI 0° = "up", positive = clockwise). The engine stores angle_engine
// directly with 0 = +x (right), positive = +y (down).
//
//   A   Puck LEFT  at (0.30, 0.30), UI = +148°  → engine =  58°  (scene 1)
//   B   Puck RIGHT at (0.70, 0.30), UI = −148°  → engine = 122°  (perfect
//       x-mirror of A — engine 180° − 58° = 122°)
//   C   Puck RIGHT at (0.70, 0.30), UI = −150°  → engine = 120°  (scene 2)
//
// Comparison:
//   A vs B  — tests whether the polygon DIRECT path is bit-exactly
//             mirror-symmetric. With the v2.10 fix this should be ~1e-13.
//   A vs C  — tests the user's actual sounded scene; any difference here
//             beyond A↔B is from the 2° off-mirror in the speaker angle.
//
// Per-mic peak / RMS / LUFS-style numbers are printed for each scene over
// the early 50 ms window so they line up with the meter readings the user
// observed (Peak / RMS panels in the LUFS tool).
//
// Build (from repo root):
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/octagonal_direct_probe.cpp \
//       -o build/octagonal_direct_probe
//   ./build/octagonal_direct_probe

#define PING_TESTING_BUILD 1
#include "../Source/IRSynthEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr double kDeg = M_PI / 180.0;

    // Match the screenshot UI verbatim.
    IRSynthParams sceneFromScreenshot()
    {
        IRSynthParams p;
        p.shape  = "Octagonal";
        p.width  = 26.0;
        p.depth  = 20.5;
        p.height = 14.0;

        p.floor_material   = "Hardwood floor";
        p.ceiling_material = "Painted plaster";
        p.wall_material    = "Painted plaster";
        p.window_fraction  = 0.28;

        p.audience  = 0.45;
        p.diffusion = 0.68;
        p.vault_type = "Groin / cross vault  (Lyndhurst Hall)";
        p.organ_case = 0.06;
        p.balconies  = 0.13;

        // DIRECT only, order 1 (matches "DIRECT reach 1 (+ first-order)").
        // We want to measure the DIRECT path in isolation, so we disable
        // MAIN/OUTRIG/AMBIENT — but the engine ALWAYS renders MAIN, so we'll
        // sum across the four returned channels and rely on direct_enabled
        // pumping content into the result.
        p.direct_enabled   = true;
        p.direct_max_order = 1;

        p.lambert_scatter_enabled = true;
        p.spk_directivity_full    = true;
        p.mono_source             = true;

        // Decca tree (cardioid LDC, splay ±90°, tilt −30°, C mic −6 dB).
        p.main_decca_enabled = true;
        p.mic_pattern        = "cardioid (LDC)";
        p.decca_cx           = 0.5;
        p.decca_cy           = 0.65;       // default
        p.decca_angle        = -M_PI_2;    // 0° in UI = forward in screen
        p.decca_toe_out      = M_PI_2;     // ±90° splay
        p.decca_tilt         = -30.0 * kDeg;
        p.decca_centre_gain  = 0.5012;     // −6 dB ≈ 0.5012

        // OUTRIG / AMBIENT enabled in the screenshot but we focus on DIRECT.
        p.outrig_enabled  = false;
        p.ambient_enabled = false;

        p.sample_rate = 48000;
        return p;
    }

    struct ChStats
    {
        double peakDb = -200.0;
        double rmsDb  = -200.0;
    };

    // Match the meter window: peak/RMS over the first 50 ms (covers direct
    // ray + first-order reflections in this 26 m room).
    ChStats statsEarly (const std::vector<double>& v, int sr)
    {
        const int n = std::min ((int) v.size(), sr * 50 / 1000);
        double peak = 0.0, sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = std::fabs (v[(size_t) i]);
            peak = std::max (peak, a);
            sumSq += v[(size_t) i] * v[(size_t) i];
        }
        const double rms = std::sqrt (sumSq / std::max (1, n));
        ChStats s;
        s.peakDb = (peak > 1e-30) ? 20.0 * std::log10 (peak) : -200.0;
        s.rmsDb  = (rms  > 1e-30) ? 20.0 * std::log10 (rms)  : -200.0;
        return s;
    }

    // L_out = iLL + iRL,  R_out = iLR + iRR  (mono input).
    void mixOut (const IRSynthResult& r, std::vector<double>& outL, std::vector<double>& outR)
    {
        const std::size_t n = (std::size_t) r.irLen;
        outL.resize (n);
        outR.resize (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            outL[i] = r.iLL[i] + r.iRL[i];
            outR[i] = r.iLR[i] + r.iRR[i];
        }
    }

    void printScene (const char* tag, const IRSynthResult& r)
    {
        std::vector<double> oL, oR;
        mixOut (r, oL, oR);
        const ChStats sL = statsEarly (oL, r.sampleRate);
        const ChStats sR = statsEarly (oR, r.sampleRate);
        std::printf ("  %-22s irLen=%-7d  L peak=%6.2f dB  L rms=%6.2f dB   "
                     "R peak=%6.2f dB  R rms=%6.2f dB   "
                     "L−R peak=%+5.2f dB  L−R rms=%+5.2f dB\n",
                     tag, r.irLen,
                     sL.peakDb, sL.rmsDb, sR.peakDb, sR.rmsDb,
                     sL.peakDb - sR.peakDb, sL.rmsDb - sR.rmsDb);
    }

    void compareIRs (const char* tagA, const IRSynthResult& rA,
                     const char* tagB, const IRSynthResult& rB)
    {
        if (rA.irLen != rB.irLen) { std::printf ("  irLen mismatch\n"); return; }
        std::vector<double> aL, aR, bL, bR;
        mixOut (rA, aL, aR);
        mixOut (rB, bL, bR);
        // Mirror equivalence: L_out(A) ↔ R_out(B), R_out(A) ↔ L_out(B).
        double mLR = 0.0, mRL = 0.0, peak = 0.0;
        const int n = std::min ((int) aL.size(), rA.sampleRate * 50 / 1000);
        for (int i = 0; i < n; ++i)
        {
            mLR  = std::max (mLR,  std::fabs (aL[(size_t) i] - bR[(size_t) i]));
            mRL  = std::max (mRL,  std::fabs (aR[(size_t) i] - bL[(size_t) i]));
            peak = std::max (peak, std::fabs (aL[(size_t) i]));
        }
        std::printf ("  %s ↔ %s   peak|L_A|=%.4e   maxAbs(L_A − R_B)=%.4e   maxAbs(R_A − L_B)=%.4e\n",
                     tagA, tagB, peak, mLR, mRL);
    }
}

int main()
{
    auto noProgress = [](double, const std::string&) {};

    // UI → engine: engine = (UI - 90)° in radians.
    auto uiToEngine = [](double uiDeg) { return (uiDeg - 90.0) * kDeg; };

    // ── Scene A: user's screenshot 1 (puck LEFT, UI 148°) ────────────
    IRSynthParams pA = sceneFromScreenshot();
    pA.source_lx = 0.30; pA.source_ly = 0.30;
    pA.source_rx = 0.30; pA.source_ry = 0.30;          // unused in mono
    pA.spkl_angle = uiToEngine (148.0);                // = +58°
    pA.spkr_angle = pA.spkl_angle;

    // ── Scene B: perfect x-mirror of A (puck RIGHT, UI −148°) ────────
    IRSynthParams pB = sceneFromScreenshot();
    pB.source_lx = 0.70; pB.source_ly = 0.30;
    pB.source_rx = 0.70; pB.source_ry = 0.30;
    pB.spkl_angle = uiToEngine (-148.0);               // = engine -238° ≡ +122°
    pB.spkr_angle = pB.spkl_angle;

    // ── Scene C: user's screenshot 2 (puck RIGHT, UI −150°) ────────
    IRSynthParams pC = sceneFromScreenshot();
    pC.source_lx = 0.70; pC.source_ly = 0.30;
    pC.source_rx = 0.70; pC.source_ry = 0.30;
    pC.spkl_angle = uiToEngine (-150.0);               // = engine -240° ≡ +120°  (2° off mirror)
    pC.spkr_angle = pC.spkl_angle;

    auto rA = IRSynthEngine::synthIR (pA, noProgress);
    auto rB = IRSynthEngine::synthIR (pB, noProgress);
    auto rC = IRSynthEngine::synthIR (pC, noProgress);
    if (! (rA.success && rB.success && rC.success))
    {
        std::fprintf (stderr, "synthIR failed (A=%d B=%d C=%d)\n",
                      (int) rA.success, (int) rB.success, (int) rC.success);
        return 1;
    }

    // ── Per-scene level breakdown (over first 50 ms) ─────────────────
    std::printf ("\nPer-scene early-window (0..50 ms) levels — meter analogue:\n");
    printScene ("A: LEFT,  UI=+148°", rA);
    printScene ("B: RIGHT, UI=−148° (perfect mirror)", rB);
    printScene ("C: RIGHT, UI=−150° (your scene 2)",   rC);

    // ── Mirror-symmetry comparisons ─────────────────────────────────
    std::printf ("\nMirror equivalence (smaller = better — engine bit-symmetric → ~1e-13):\n");
    compareIRs ("A", rA, "B (perfect x-mirror)", rB);
    compareIRs ("A", rA, "C (user's actual −150°)", rC);

    // ── Direct ray onset and amplitude per channel ──────────────────
    auto firstOnset = [](const std::vector<double>& v, double thr = 1e-9) {
        for (size_t i = 0; i < v.size(); ++i)
            if (std::fabs (v[i]) > thr) return (int) i;
        return -1;
    };

    std::printf ("\nFirst non-silent sample per channel (direct-ray onset):\n");
    auto onsets = [&](const char* tag, const IRSynthResult& r) {
        std::vector<double> oL, oR; mixOut (r, oL, oR);
        std::printf ("  %-22s L_out onset=%-6d   R_out onset=%-6d\n",
                     tag, firstOnset (oL), firstOnset (oR));
    };
    onsets ("A: LEFT,  UI=+148°", rA);
    onsets ("B: RIGHT, UI=−148°", rB);
    onsets ("C: RIGHT, UI=−150°", rC);

    return 0;
}
