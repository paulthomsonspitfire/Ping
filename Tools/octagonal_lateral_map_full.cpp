// octagonal_lateral_map_full.cpp
//
// Same scene as octagonal_lateral_map.cpp but with MAIN + OUTRIG + AMBIENT +
// DIRECT all enabled, summed at sensible per-path gains, to see how much
// the position sensitivity smooths out vs the DIRECT-only sweep.
//
// The per-path gains roughly approximate the user's mixer balance in a
// typical full mix (MAIN dominant, OUTRIG ~−6 dB, AMBIENT ~−9 dB, DIRECT
// ~−6 dB). They aren't critical: what matters is whether the L−R rms map
// becomes smoother and whether the antisymmetry around x=0.5 still holds.
//
// Build:
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/octagonal_lateral_map_full.cpp \
//       -o build/octagonal_lateral_map_full
//   ./build/octagonal_lateral_map_full

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

    IRSynthParams baseScene()
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
        p.spk_directivity_full    = true;
        p.mono_source             = true;

        // MAIN: Decca tree with the same settings as the user's screenshots.
        p.main_decca_enabled = true;
        p.mic_pattern        = "cardioid (LDC)";
        p.decca_cx           = 0.5; p.decca_cy = 0.65;
        p.decca_angle        = -M_PI_2;
        p.decca_toe_out      = M_PI_2;
        p.decca_tilt         = -30.0 * kDeg;
        p.decca_centre_gain  = 0.5012;

        // OUTRIG: enabled, mirror-symmetric defaults.
        p.outrig_enabled = true;
        p.outrig_lx = 0.15; p.outrig_ly = 0.80;
        p.outrig_rx = 0.85; p.outrig_ry = 0.80;
        p.outrig_langle = -2.35619449019;   // -3π/4 up-left
        p.outrig_rangle = -0.785398163397;  // -π/4  up-right

        // AMBIENT: enabled, mirror-symmetric defaults.
        p.ambient_enabled = true;
        p.ambient_lx = 0.20; p.ambient_ly = 0.95;
        p.ambient_rx = 0.80; p.ambient_ry = 0.95;
        p.ambient_langle = -2.35619449019;
        p.ambient_rangle = -0.785398163397;

        // DIRECT: enabled at first order (matches the screenshot).
        p.direct_enabled   = true;
        p.direct_max_order = 1;

        p.sample_rate = 48000;
        return p;
    }

    // Per-path mixer levels (dB). Approximates a realistic full-mix balance.
    constexpr double kMainDb    = 0.0;
    constexpr double kOutrigDb  = -6.0;
    constexpr double kAmbientDb = -9.0;
    constexpr double kDirectDb  = -6.0;

    double dbToLin (double db) { return std::pow (10.0, db * 0.05); }

    // Mix path 4-channel (LL/RL/LR/RR) into 2-channel (L_out/R_out) and add
    // (with gain) into the running sum. Mono input → L_out = LL+RL,
    // R_out = LR+RR.
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

    double earlyRmsDb (const std::vector<double>& v, int sr, int windowMs = 50)
    {
        const int n = std::min ((int) v.size(), sr * windowMs / 1000);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i) sumSq += v[(size_t) i] * v[(size_t) i];
        const double rms = std::sqrt (sumSq / std::max (1, n));
        return (rms > 1e-30) ? 20.0 * std::log10 (rms) : -200.0;
    }
}

int main()
{
    auto noProgress = [](double, const std::string&) {};
    IRSynthParams base = baseScene();

    std::vector<double> xs = { 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45,
                               0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85 };
    std::vector<double> ys = { 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50 };

    const double gMain    = dbToLin (kMainDb);
    const double gOutrig  = dbToLin (kOutrigDb);
    const double gAmbient = dbToLin (kAmbientDb);
    const double gDirect  = dbToLin (kDirectDb);

    std::printf ("\nFull-mix L−R rms (dB) — MAIN(%+.0fdB) + OUTRIG(%+.0fdB) + AMBIENT(%+.0fdB) + DIRECT(%+.0fdB)\n",
                 kMainDb, kOutrigDb, kAmbientDb, kDirectDb);
    std::printf ("Window: 0..200 ms (covers ER buildup), speaker auto-aimed at Decca tree.\n\n");
    std::printf ("       x: ");
    for (double x : xs) std::printf ("%6.2f ", x);
    std::printf ("\n");

    for (double y : ys)
    {
        std::printf ("  y=%4.2f   ", y);
        for (double x : xs)
        {
            const double sx = x, sy = y;
            const double dx = 0.5 - sx;
            const double dy = 0.65 - sy;
            const double engAng = std::atan2 (dy, dx);

            IRSynthParams p = base;
            p.source_lx = sx; p.source_ly = sy;
            p.source_rx = sx; p.source_ry = sy;
            p.spkl_angle = engAng;
            p.spkr_angle = engAng;

            auto r = IRSynthEngine::synthIR (p, noProgress);
            if (! r.success) { std::printf ("   ?   "); continue; }

            std::vector<double> oL ((size_t) r.irLen, 0.0);
            std::vector<double> oR ((size_t) r.irLen, 0.0);

            addPath (oL, oR, r.iLL, r.iRL, r.iLR, r.iRR, gMain);
            if (r.outrig.synthesised)
                addPath (oL, oR, r.outrig.LL, r.outrig.RL, r.outrig.LR, r.outrig.RR, gOutrig);
            if (r.ambient.synthesised)
                addPath (oL, oR, r.ambient.LL, r.ambient.RL, r.ambient.LR, r.ambient.RR, gAmbient);
            if (r.direct.synthesised)
                addPath (oL, oR, r.direct.LL, r.direct.RL, r.direct.LR, r.direct.RR, gDirect);

            const double lr = earlyRmsDb (oL, r.sampleRate, 200)
                            - earlyRmsDb (oR, r.sampleRate, 200);
            std::printf ("%+5.2f  ", lr);
        }
        std::printf ("\n");
    }

    std::printf ("\nNotes:\n");
    std::printf ("  Positive value → L louder.\n");
    std::printf ("  If the engine is mirror-symmetric end-to-end, every row should be\n");
    std::printf ("  antisymmetric around x=0.5 (e.g. (0.15,y) ↔ (0.85,y) equal magnitude opposite sign).\n");
    std::printf ("  Compare the variability with the DIRECT-only map to see how much MAIN+OUTRIG+AMBIENT smooths it.\n");
    return 0;
}
