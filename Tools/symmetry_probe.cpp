// symmetry_probe.cpp
//
// Diagnostic tool for the perceived L/R asymmetry on the Decca tree path.
// Synthesises two IRs in mirrored configurations:
//   A) source on the LEFT  of the Decca tree (source_lx = 0.30, source_ly = 0.50)
//   B) source on the RIGHT of the Decca tree (source_lx = 0.70, source_ly = 0.50)
// Speaker is pinned to a forward-facing 90° (default) so x-axis symmetry is preserved.
// Mic layout: Decca tree at the room centre (decca_cx = 0.5, decca_cy = 0.65),
// rectangular Vienna-like room 49 × 19 × 18 m, default decca_toe_out (90°),
// default decca_centre_gain (0.5), Hardwood floor / Painted plaster ceiling /
// Plywood walls / 8% windows, audience 0.30, diffusion 0.55, vault None.
//
// If the engine is x-symmetric, then by mirror symmetry across x = 0.5:
//   IR_A.iLL[t]  ==  IR_B.iRR[t]   (L speaker → L mic in scene A equals
//                                    R speaker → R mic in scene B)
//   IR_A.iLR[t]  ==  IR_B.iRL[t]   (L → R in A equals R → L in B)
// In mono mode iRL := iLL and iRR := iLR, so iLL_A should equal iLR_B
// and iLR_A should equal iLL_B.
//
// We print four numbers per channel:
//   max |a - b|              — peak sample difference
//   sum (a - b)^2 / sum a^2  — relative squared error
//   first onset index (a)    — to verify identical timing
//   first onset index (b)
//
// Build (from repo root):
//   CATCH2 unused; this tool needs only IRSynthEngine + the testing build flag.
//   c++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -I Source \
//       Source/IRSynthEngine.cpp Tools/symmetry_probe.cpp -o build/symmetry_probe
//   ./build/symmetry_probe

#define PING_TESTING_BUILD 1
#include "../Source/IRSynthEngine.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <string>

static IRSynthParams viennaLikeParams()
{
    IRSynthParams p;
    p.shape  = "Rectangular";
    p.width  = 49.0;
    p.depth  = 19.0;
    p.height = 18.0;

    p.floor_material   = "Hardwood floor";
    p.ceiling_material = "Painted plaster";
    p.wall_material    = "Plywood panel";
    p.window_fraction  = 0.08;

    p.audience  = 0.30;
    p.diffusion = 0.55;
    p.vault_type = "None (flat)";
    p.organ_case = 0.40;
    p.balconies  = 0.60;

    // Speaker FORWARD (default 90° = down on screen). No rotation.
    p.spkl_angle = 1.5707963267948966;  // π/2
    p.spkr_angle = 1.5707963267948966;

    // Default outers / mics.
    p.receiver_lx = 0.35; p.receiver_ly = 0.80;
    p.receiver_rx = 0.65; p.receiver_ry = 0.80;
    p.micl_angle = -2.35619449019;   // -3π/4
    p.micr_angle = -0.785398163397;  // -π/4
    p.micl_tilt = -0.5235987755982988;
    p.micr_tilt = -0.5235987755982988;

    // Decca tree on, defaults.
    p.main_decca_enabled = true;
    p.decca_cx = 0.5; p.decca_cy = 0.65;
    p.decca_angle = -1.5707963267948966;
    p.decca_tilt  = -0.5235987755982988;
    p.decca_toe_out = 1.5707963267948966;  // 90°
    p.decca_centre_gain = 0.5;
    p.mic_pattern = "wide cardioid (MK21)";

    p.mono_source = true;
    p.sample_rate = 48000;
    return p;
}

static int firstOnset(const std::vector<double>& v, double thresh = 1e-9)
{
    for (size_t i = 0; i < v.size(); ++i)
        if (std::fabs(v[i]) > thresh) return (int) i;
    return -1;
}

struct CmpStats
{
    double maxAbs   = 0.0;
    double rmsRel   = 0.0;
    int    onsetA   = -1;
    int    onsetB   = -1;
    double peakA    = 0.0;
    double peakB    = 0.0;
};

static CmpStats compare(const std::vector<double>& a,
                        const std::vector<double>& b)
{
    CmpStats s;
    s.onsetA = firstOnset(a);
    s.onsetB = firstOnset(b);
    const size_t n = std::min(a.size(), b.size());
    double err2 = 0.0, ref2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double d = a[i] - b[i];
        if (std::fabs(d) > s.maxAbs) s.maxAbs = std::fabs(d);
        if (std::fabs(a[i]) > s.peakA) s.peakA = std::fabs(a[i]);
        if (std::fabs(b[i]) > s.peakB) s.peakB = std::fabs(b[i]);
        err2 += d * d;
        ref2 += a[i] * a[i];
    }
    s.rmsRel = (ref2 > 0.0) ? std::sqrt(err2 / ref2) : 0.0;
    return s;
}

static void print(const char* label, const CmpStats& s)
{
    std::printf("  %-30s  maxAbs=%-12.6e  RMSrel=%-10.4f  onsetA=%-6d  onsetB=%-6d  peakA=%.4e  peakB=%.4e\n",
                label, s.maxAbs, s.rmsRel, s.onsetA, s.onsetB, s.peakA, s.peakB);
}

// Energy in a window summed across a channel.
static double rmsWindow(const std::vector<double>& v, int a, int b)
{
    a = std::max(0, a);
    b = std::min((int) v.size(), b);
    if (b <= a) return 0.0;
    double s = 0.0;
    for (int i = a; i < b; ++i) s += v[i] * v[i];
    return std::sqrt(s / (b - a));
}

int main()
{
    auto progress = [](double, const std::string&) {};

    // ── Scene A: source on the LEFT ──────────────────────────────────
    IRSynthParams pA = viennaLikeParams();
    pA.source_lx = 0.30; pA.source_ly = 0.50;
    pA.source_rx = 0.30; pA.source_ry = 0.50;  // unused in mono
    std::printf("Scene A: source on the LEFT  (sx=0.30)\n");
    auto rA = IRSynthEngine::synthIR(pA, progress);
    if (! rA.success) { std::fprintf(stderr, "Scene A failed: %s\n", rA.errorMessage.c_str()); return 1; }
    std::printf("  irLen=%d  sr=%d\n", rA.irLen, rA.sampleRate);

    // ── Scene B: source on the RIGHT (mirrored x) ───────────────────
    IRSynthParams pB = viennaLikeParams();
    pB.source_lx = 0.70; pB.source_ly = 0.50;
    pB.source_rx = 0.70; pB.source_ry = 0.50;
    std::printf("Scene B: source on the RIGHT (sx=0.70)\n");
    auto rB = IRSynthEngine::synthIR(pB, progress);
    if (! rB.success) { std::fprintf(stderr, "Scene B failed: %s\n", rB.errorMessage.c_str()); return 1; }
    std::printf("  irLen=%d  sr=%d\n", rB.irLen, rB.sampleRate);

    if (rA.irLen != rB.irLen)
    {
        std::fprintf(stderr, "irLen mismatch — cannot compare\n");
        return 2;
    }

    // ── Mirror-symmetry expectations ─────────────────────────────────
    // In mono mode: iRL == iLL, iRR == iLR.
    // Mirror across x=0.5 swaps L mic and R mic, so:
    //   IR_A.iLL[t]  should equal  IR_B.iLR[t]   (L mic in A = R mic in B; same mono speaker → L_out)
    //   IR_A.iLR[t]  should equal  IR_B.iLL[t]
    std::printf("\nMirror-symmetry comparison (perfect mirror → maxAbs ≈ 0, RMSrel ≈ 0):\n");
    print("iLL(A) vs iLR(B)", compare(rA.iLL, rB.iLR));
    print("iLR(A) vs iLL(B)", compare(rA.iLR, rB.iLL));
    print("iRL(A) vs iRR(B)", compare(rA.iRL, rB.iRR));
    print("iRR(A) vs iRL(B)", compare(rA.iRR, rB.iRL));

    // Self-consistency: in mono mode iLL==iRL and iLR==iRR.
    std::printf("\nMono-mode internal consistency (should be exactly equal):\n");
    print("iLL(A) vs iRL(A)", compare(rA.iLL, rA.iRL));
    print("iLR(A) vs iRR(A)", compare(rA.iLR, rA.iRR));
    print("iLL(B) vs iRL(B)", compare(rB.iLL, rB.iRL));
    print("iLR(B) vs iRR(B)", compare(rB.iLR, rB.iRR));

    // Energy / level breakdown — per-channel RMS over the full IR and over
    // the early window (first 80 ms) so we can tell whether any asymmetry
    // is in the direct/ER region or in the diffuse tail.
    auto er  = (int) std::floor(0.080 * rA.sampleRate);
    std::printf("\nPer-channel RMS  (all=full IR, ER=first 80 ms):\n");
    std::printf("  Scene A  iLL all=%.4e  ER=%.4e  |  iLR all=%.4e  ER=%.4e\n",
                rmsWindow(rA.iLL, 0, rA.irLen), rmsWindow(rA.iLL, 0, er),
                rmsWindow(rA.iLR, 0, rA.irLen), rmsWindow(rA.iLR, 0, er));
    std::printf("  Scene B  iLL all=%.4e  ER=%.4e  |  iLR all=%.4e  ER=%.4e\n",
                rmsWindow(rB.iLL, 0, rB.irLen), rmsWindow(rB.iLL, 0, er),
                rmsWindow(rB.iLR, 0, rB.irLen), rmsWindow(rB.iLR, 0, er));

    // Level asymmetry indicators.
    auto db = [](double x) { return 20.0 * std::log10(std::max(1e-30, x)); };
    double erA_L = rmsWindow(rA.iLL, 0, er);
    double erA_R = rmsWindow(rA.iLR, 0, er);
    double erB_L = rmsWindow(rB.iLL, 0, er);
    double erB_R = rmsWindow(rB.iLR, 0, er);
    std::printf("\nEarly-window L−R level (dB):\n");
    std::printf("  Scene A: L_mic−R_mic = %+.2f dB  (positive → L louder, expected for left source)\n",
                db(erA_L) - db(erA_R));
    std::printf("  Scene B: L_mic−R_mic = %+.2f dB  (negative → R louder, expected for right source)\n",
                db(erB_L) - db(erB_R));
    std::printf("  |A| − |B| = %+.2f dB  (≈0 if engine is symmetric)\n",
                std::fabs(db(erA_L) - db(erA_R)) - std::fabs(db(erB_L) - db(erB_R)));

    // ── Diagnostic: also run the same scene with the speaker rotated to
    //    118° (the screenshot rotation) for comparison. This isolates the
    //    speaker-rotation contribution from any engine-side residual.
    std::printf("\n── With speaker rotated to 118° (screenshot config) ──\n");
    IRSynthParams pA2 = pA;  pA2.spkl_angle = 118.0 * M_PI / 180.0;
    IRSynthParams pB2 = pB;  pB2.spkl_angle = 118.0 * M_PI / 180.0;
    auto rA2 = IRSynthEngine::synthIR(pA2, progress);
    auto rB2 = IRSynthEngine::synthIR(pB2, progress);
    double erA2_L = rmsWindow(rA2.iLL, 0, er);
    double erA2_R = rmsWindow(rA2.iLR, 0, er);
    double erB2_L = rmsWindow(rB2.iLL, 0, er);
    double erB2_R = rmsWindow(rB2.iLR, 0, er);
    std::printf("  Scene A (left,  spk=118°): L_mic−R_mic = %+.2f dB\n",  db(erA2_L) - db(erA2_R));
    std::printf("  Scene B (right, spk=118°): L_mic−R_mic = %+.2f dB\n",  db(erB2_L) - db(erB2_R));
    std::printf("  |A2| − |B2| = %+.2f dB  (asymmetry contributed by speaker rotation)\n",
                std::fabs(db(erA2_L) - db(erA2_R)) - std::fabs(db(erB2_L) - db(erB2_R)));

    // ── Diagnostic: minimise ts (so the per-reflection time jitter is OFF)
    //    by zeroing diffusion / organ / balconies and using a vault profile
    //    with vs == 0. ts = vs + organ·0.35 + balconies·0.25 + diffusion·0.30.
    //    With all four → 0 we get ts = 0.0, which fails the ts > 0.05 gate
    //    in calcRefs. This isolates the seed-dependent jitter contribution
    //    from any deeper geometric asymmetry.
    std::printf("\n── With ts→0 (jitter disabled) ──\n");
    IRSynthParams pA3 = pA;  IRSynthParams pB3 = pB;
    for (auto pp : { &pA3, &pB3 })
    {
        pp->diffusion  = 0.0;
        pp->organ_case = 0.0;
        pp->balconies  = 0.0;
        pp->vault_type = "None (flat)";
        pp->er_only    = false;
    }
    auto rA3 = IRSynthEngine::synthIR(pA3, progress);
    auto rB3 = IRSynthEngine::synthIR(pB3, progress);
    auto cmp = compare(rA3.iLL, rB3.iLR);
    std::printf("  iLL(A) vs iLR(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);
    cmp = compare(rA3.iLR, rB3.iLL);
    std::printf("  iLR(A) vs iLL(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);
    double erA3_L = rmsWindow(rA3.iLL, 0, er);
    double erA3_R = rmsWindow(rA3.iLR, 0, er);
    double erB3_L = rmsWindow(rB3.iLL, 0, er);
    double erB3_R = rmsWindow(rB3.iLR, 0, er);
    std::printf("  Scene A (jitter-off): L−R = %+.2f dB    Scene B: L−R = %+.2f dB    |A|−|B| = %+.2f dB\n",
                db(erA3_L) - db(erA3_R), db(erB3_L) - db(erB3_R),
                std::fabs(db(erA3_L) - db(erA3_R)) - std::fabs(db(erB3_L) - db(erB3_R)));

    // ── Small-room ER-only (matches IR_32 test setup) ─────────────────
    std::printf("\n── Small room (10×8×5 m) ER-only — matches IR_32 ──\n");
    // Use identical Vienna params but with the small dimensions, to isolate
    // any geometry-specific cause of asymmetry.
    IRSynthParams pSA = viennaLikeParams();
    pSA.width  = 10.0;
    pSA.depth  =  8.0;
    pSA.height =  5.0;
    pSA.er_only = true;
    pSA.source_lx = 0.30; pSA.source_ly = 0.50;
    pSA.source_rx = 0.30; pSA.source_ry = 0.50;
    IRSynthParams pSB = pSA;
    pSB.source_lx = 0.70; pSB.source_ly = 0.50;
    pSB.source_rx = 0.70; pSB.source_ry = 0.50;
    auto rSA = IRSynthEngine::synthIR(pSA, progress);
    auto rSB = IRSynthEngine::synthIR(pSB, progress);
    auto cmpS_LL_LR = compare(rSA.iLL, rSB.iLR);
    auto cmpS_LR_LL = compare(rSA.iLR, rSB.iLL);
    std::printf("  iLL(A) vs iLR(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d  onsetB=%d  peakA=%.4e  peakB=%.4e\n",
                cmpS_LL_LR.maxAbs, cmpS_LL_LR.rmsRel, cmpS_LL_LR.onsetA, cmpS_LL_LR.onsetB, cmpS_LL_LR.peakA, cmpS_LL_LR.peakB);
    std::printf("  iLR(A) vs iLL(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d  onsetB=%d  peakA=%.4e  peakB=%.4e\n",
                cmpS_LR_LL.maxAbs, cmpS_LR_LL.rmsRel, cmpS_LR_LL.onsetA, cmpS_LR_LL.onsetB, cmpS_LR_LL.peakA, cmpS_LR_LL.peakB);

    // Find first divergence sample to identify the cause.
    int firstDiv = -1;
    for (int i = 0; i < rSA.irLen; ++i)
    {
        if (std::fabs(rSA.iLL[i] - rSB.iLR[i]) > 1e-9)
        {
            firstDiv = i;
            break;
        }
    }
    std::printf("  First divergence sample (iLL_A vs iLR_B, eps=1e-9): %d\n", firstDiv);
    if (firstDiv >= 0)
    {
        std::printf("    iLL_A[%d..%d] = ", firstDiv, firstDiv+5);
        for (int i = firstDiv; i < firstDiv + 6 && i < rSA.irLen; ++i)
            std::printf("%.6e ", rSA.iLL[i]);
        std::printf("\n    iLR_B[%d..%d] = ", firstDiv, firstDiv+5);
        for (int i = firstDiv; i < firstDiv + 6 && i < rSB.irLen; ++i)
            std::printf("%.6e ", rSB.iLR[i]);
        std::printf("\n");
    }

    // ── Diagnostic: ER-only mode (no FDN tail), with jitter on. Lets us see
    //    whether the asymmetry is concentrated in the deterministic ISM rays
    //    or in the FDN/diffuser stage. ER-only also disables the close-source
    //    coincident=true FDN seed scaling.
    std::printf("\n── ER-only (no FDN), jitter ON ──\n");
    IRSynthParams pA4 = pA;  pA4.er_only = true;
    IRSynthParams pB4 = pB;  pB4.er_only = true;
    auto rA4 = IRSynthEngine::synthIR(pA4, progress);
    auto rB4 = IRSynthEngine::synthIR(pB4, progress);
    cmp = compare(rA4.iLL, rB4.iLR);
    std::printf("  iLL(A) vs iLR(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);

    // ── Diagnostic: ER-only AND ts→0 — the deterministic-only configuration.
    //    Any remaining asymmetry here is from non-jitter sources (geometry,
    //    rounding in the dispatcher, or path-dependent code that branches on
    //    L vs R). This should be near bit-identical if the core image-source
    //    code is symmetric.
    std::printf("\n── ER-only + ts→0 (deterministic only) ──\n");
    IRSynthParams pA5 = pA3;  pA5.er_only = true;
    IRSynthParams pB5 = pB3;  pB5.er_only = true;
    auto rA5 = IRSynthEngine::synthIR(pA5, progress);
    auto rB5 = IRSynthEngine::synthIR(pB5, progress);
    cmp = compare(rA5.iLL, rB5.iLR);
    std::printf("  iLL(A) vs iLR(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);
    cmp = compare(rA5.iLR, rB5.iLL);
    std::printf("  iLR(A) vs iLL(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);
    double erA5_L = rmsWindow(rA5.iLL, 0, er);
    double erA5_R = rmsWindow(rA5.iLR, 0, er);
    double erB5_L = rmsWindow(rB5.iLL, 0, er);
    double erB5_R = rmsWindow(rB5.iLR, 0, er);
    std::printf("  Scene A: L−R = %+.2f dB    Scene B: L−R = %+.2f dB    |A|−|B| = %+.2f dB\n",
                db(erA5_L) - db(erA5_R), db(erB5_L) - db(erB5_R),
                std::fabs(db(erA5_L) - db(erA5_R)) - std::fabs(db(erB5_L) - db(erB5_R)));

    // ── Diagnostic: deterministic + decca_centre_gain = 0 (no centre fill).
    //    Strips the centre mic from the combine entirely, leaving a bare L/R
    //    spaced pair. Asymmetries here are pure outer-pair geometry residue.
    std::printf("\n── Deterministic + centre gain = 0 (no centre fill) ──\n");
    IRSynthParams pA6 = pA5;  pA6.decca_centre_gain = 0.0;
    IRSynthParams pB6 = pB5;  pB6.decca_centre_gain = 0.0;
    auto rA6 = IRSynthEngine::synthIR(pA6, progress);
    auto rB6 = IRSynthEngine::synthIR(pB6, progress);
    cmp = compare(rA6.iLL, rB6.iLR);
    std::printf("  iLL(A) vs iLR(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);
    cmp = compare(rA6.iLR, rB6.iLL);
    std::printf("  iLR(A) vs iLL(B)  maxAbs=%.6e  RMSrel=%.6f  onsetA=%d onsetB=%d  peakA=%.4e peakB=%.4e\n",
                cmp.maxAbs, cmp.rmsRel, cmp.onsetA, cmp.onsetB, cmp.peakA, cmp.peakB);

    return 0;
}
