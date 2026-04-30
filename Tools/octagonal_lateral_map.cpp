// octagonal_lateral_map.cpp
//
// Sweeps the mono-puck position over a grid in the same Octagonal room as
// the user's screenshots, with the speaker auto-aimed at the Decca centre,
// and prints the L−R rms (dB) the engine produces over the early 50 ms
// (DIRECT-only). This shows how sensitive perceived lateralisation is to
// puck placement, which is what we suspect is behind the observed L↔R
// asymmetry between the two screenshots.
//
// Build:
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/octagonal_lateral_map.cpp \
//       -o build/octagonal_lateral_map
//   ./build/octagonal_lateral_map

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

        p.direct_enabled   = true;  p.direct_max_order = 1;
        p.lambert_scatter_enabled = true;
        p.spk_directivity_full    = true;
        p.mono_source             = true;

        p.main_decca_enabled = true;
        p.mic_pattern        = "cardioid (LDC)";
        p.decca_cx           = 0.5; p.decca_cy = 0.65;
        p.decca_angle        = -M_PI_2;
        p.decca_toe_out      = M_PI_2;
        p.decca_tilt         = -30.0 * kDeg;
        p.decca_centre_gain  = 0.5012;

        p.outrig_enabled  = false;
        p.ambient_enabled = false;
        p.sample_rate = 48000;
        return p;
    }

    double earlyRmsDb (const std::vector<double>& v, int sr)
    {
        const int n = std::min ((int) v.size(), sr * 50 / 1000);
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

    // Grid: x ∈ {0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45,
    //              0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85}
    //       y ∈ {0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50}
    // For each point, aim the speaker at the Decca centre (0.5, 0.65) so
    // the cardioid main lobe always pumps energy into the tree.

    std::vector<double> xs = { 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45,
                               0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85 };
    std::vector<double> ys = { 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50 };

    std::printf ("\nL−R rms (dB) for mono puck at (x_norm, y_norm), speaker auto-aimed at Decca tree:\n");
    std::printf ("       x: ");
    for (double x : xs) std::printf ("%6.2f ", x);
    std::printf ("\n");

    for (double y : ys)
    {
        std::printf ("  y=%4.2f   ", y);
        for (double x : xs)
        {
            // Speaker world-direction from puck to Decca centre, in engine
            // angle convention (0 = +x, positive = +y/down).
            // Use a synthesis-realistic offset: convert normalised (x,y)
            // to engine world coords using the polygon's bounding box
            // assumption (matches what calcRefsPolygon does).
            const double sx = x;       // normalised X already matches engine's source_lx
            const double sy = y;
            const double dx = 0.5 - sx;
            const double dy = 0.65 - sy;
            const double engAng = std::atan2 (dy, dx);

            IRSynthParams p = base;
            p.source_lx = sx; p.source_ly = sy;
            p.source_rx = sx; p.source_ry = sy;     // unused in mono
            p.spkl_angle = engAng;
            p.spkr_angle = engAng;

            auto r = IRSynthEngine::synthIR (p, noProgress);
            if (! r.success) { std::printf ("   ?   "); continue; }

            std::vector<double> oL ((size_t) r.irLen), oR ((size_t) r.irLen);
            for (int i = 0; i < r.irLen; ++i)
            {
                oL[(size_t) i] = r.iLL[(size_t) i] + r.iRL[(size_t) i];
                oR[(size_t) i] = r.iLR[(size_t) i] + r.iRR[(size_t) i];
            }
            const double lr = earlyRmsDb (oL, r.sampleRate)
                            - earlyRmsDb (oR, r.sampleRate);
            std::printf ("%+5.2f  ", lr);
        }
        std::printf ("\n");
    }
    std::printf ("\nNotes:\n");
    std::printf ("  Positive value → L louder (puck on left of room).\n");
    std::printf ("  The map should be antisymmetric around x=0.5 row-by-row → engine is mirror-symmetric.\n");
    std::printf ("  Compare row-by-row: e.g. (0.30, 0.30) vs (0.70, 0.30) should have equal magnitude opposite sign.\n");
    return 0;
}
