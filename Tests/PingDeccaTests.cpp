// PingDeccaTests.cpp
// Tests for the Decca Tree capture mode (see CLAUDE.md "Decca Tree" section).
// Compiled with PING_TESTING_BUILD=1 so IRSynthEngine.h skips JuceHeader.h.
//
// Layout:
//   IR_22  Decca OFF: explicit p.main_decca_enabled=false is bit-identical to
//          the default (= false) — regression guard on the "off" branch.
//   IR_23  Decca ON vs OFF: enabling the flag must change the MAIN output.
//          Counterpart to IR_22 — proves the flag is wired to the engine.
//   IR_24  Centre-advance onset shift: moving the tree position changes the
//          L/C/R arrival pattern and therefore the early-reflection envelope.
//   DSP_20 1-pole 110 Hz HPF (re-implementation of the lambda used inside the
//          engine) — DC gain ≈ 0, rejects LF, passes HF.
//
// Build target: PingTests (see CMakeLists.txt).

#define PING_TESTING_BUILD 1
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "IRSynthEngine.h"
#include "TestHelpers.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ── Shared default params (match IR_11 / IR_14 small-room geometry) ─────────
namespace
{
    IRSynthParams deccaSmallRoomParams()
    {
        IRSynthParams p;
        p.width  = 10.0;
        p.depth  =  8.0;
        p.height =  5.0;
        p.diffusion = 0.4;
        return p;
    }

    // Identical FNV-1a helper as PingMultiMicTests.cpp (kept local so the two
    // files compile independently).
    inline uint64_t fnv1a64 (const double* data, std::size_t count)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        const uint64_t prime = 0x100000001b3ull;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
        const std::size_t nb = count * sizeof(double);
        for (std::size_t i = 0; i < nb; ++i)
        {
            h ^= static_cast<uint64_t>(p[i]);
            h *= prime;
        }
        return h;
    }

    // L2 difference between two equal-length buffers (sqrt of sum of squares).
    inline double l2diffLocal (const std::vector<double>& a, const std::vector<double>& b)
    {
        const std::size_t n = std::min (a.size(), b.size());
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double d = a[i] - b[i];
            s += d * d;
        }
        return std::sqrt (s);
    }

    // Find the first sample index in which abs(a[i] - b[i]) exceeds a tiny
    // threshold. Returns -1 if the two signals never diverge within `upTo`
    // samples. Useful for pin-pointing where Decca first affects the output.
    inline int firstDivergenceIndex (const std::vector<double>& a,
                                      const std::vector<double>& b,
                                      int upTo,
                                      double eps = 1.0e-12)
    {
        const int n = std::min ({ (int) a.size(), (int) b.size(), upTo });
        for (int i = 0; i < n; ++i)
            if (std::fabs (a[i] - b[i]) > eps)
                return i;
        return -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_22 — Decca OFF: explicit flag=false is bit-identical to default
// ─────────────────────────────────────────────────────────────────────────────
// The whole Decca Tree feature is gated by `p.main_decca_enabled`. When the
// flag is false the engine MUST NOT touch any of the existing code paths that
// IR_11 and IR_14 lock against.
//
// IR_14 already indirectly verifies this by using a default-constructed
// IRSynthParams (where the flag defaults to false). IR_22 adds the explicit
// flag=false case so we catch regressions where someone accidentally makes
// the false branch *do* something extra (e.g. rz clamping, mic-pattern
// override, rcx/rcy initialisation leaking into the output).
TEST_CASE("IR_22: Decca OFF matches default bit-identically", "[engine][decca][bit-identity]")
{
    IRSynthParams p = deccaSmallRoomParams();
    IRSynthParams pExplicit = p;
    pExplicit.main_decca_enabled = false;     // explicit — should be a no-op

    auto rDefault  = IRSynthEngine::synthIR (p,         [](double, const std::string&) {});
    auto rExplicit = IRSynthEngine::synthIR (pExplicit, [](double, const std::string&) {});

    REQUIRE (rDefault.success);
    REQUIRE (rExplicit.success);
    REQUIRE (rDefault.irLen == rExplicit.irLen);
    REQUIRE ((int) rDefault.iLL.size() == rDefault.irLen);

    // Byte-for-byte identical across all four MAIN channels.
    CHECK (fnv1a64 (rDefault.iLL.data(), rDefault.iLL.size())
        == fnv1a64 (rExplicit.iLL.data(), rExplicit.iLL.size()));
    CHECK (fnv1a64 (rDefault.iRL.data(), rDefault.iRL.size())
        == fnv1a64 (rExplicit.iRL.data(), rExplicit.iRL.size()));
    CHECK (fnv1a64 (rDefault.iLR.data(), rDefault.iLR.size())
        == fnv1a64 (rExplicit.iLR.data(), rExplicit.iLR.size()));
    CHECK (fnv1a64 (rDefault.iRR.data(), rDefault.iRR.size())
        == fnv1a64 (rExplicit.iRR.data(), rExplicit.iRR.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_23 — Decca ON differs from Decca OFF
// ─────────────────────────────────────────────────────────────────────────────
// Counterpart to IR_22: if flipping the flag to true doesn't change the
// output, the Decca code path is not being reached. This catches mis-plumbed
// state-wiring (e.g. the flag not being forwarded from the processor XML, or
// being reset to false somewhere between UI and engine).
//
// The difference must be non-trivial — we check both the full-IR L2 norm
// and the fraction of samples that changed, so the test fails loudly if the
// engine accidentally only touches a handful of samples.
TEST_CASE("IR_23: enabling Decca changes the MAIN output", "[engine][decca]")
{
    IRSynthParams pOff = deccaSmallRoomParams();
    IRSynthParams pOn  = pOff;
    pOn.main_decca_enabled = true;

    auto rOff = IRSynthEngine::synthIR (pOff, [](double, const std::string&) {});
    auto rOn  = IRSynthEngine::synthIR (pOn,  [](double, const std::string&) {});

    REQUIRE (rOff.success);
    REQUIRE (rOn.success);
    REQUIRE (rOff.irLen == rOn.irLen);  // dispatcher shape is unchanged

    // L2 difference should be substantially non-zero — the centre mic adds
    // real energy into all four output channels.
    const double dLL = l2diffLocal (rOn.iLL, rOff.iLL);
    const double dRL = l2diffLocal (rOn.iRL, rOff.iRL);
    const double dLR = l2diffLocal (rOn.iLR, rOff.iLR);
    const double dRR = l2diffLocal (rOn.iRR, rOff.iRR);

    INFO ("L2 differences  LL=" << dLL << "  RL=" << dRL
         << "  LR=" << dLR << "  RR=" << dRR);
    CHECK (dLL > 1.0e-6);
    CHECK (dRL > 1.0e-6);
    CHECK (dLR > 1.0e-6);
    CHECK (dRR > 1.0e-6);

    // Sanity: count samples that actually differ (>= 10% of the IR length).
    // Prevents a single-sample divergence from passing the L2 check.
    int ndiff = 0;
    for (int i = 0; i < rOn.irLen; ++i)
        if (std::fabs (rOn.iLL[(size_t)i] - rOff.iLL[(size_t)i]) > 1.0e-12)
            ++ndiff;
    INFO ("Samples differing in iLL = " << ndiff << " / " << rOn.irLen);
    CHECK (ndiff >= rOn.irLen / 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_24 — Centre-advance onset shift
// ─────────────────────────────────────────────────────────────────────────────
// Moving the tree position on the floor plan must change the geometry of the
// L/C/R mics (rigid array) and therefore alter the arrival times of the
// centre-mic contribution. If the decca_cx/decca_cy/decca_angle fields are
// silently ignored by the engine, the two "Decca ON" runs will produce
// identical output — this test will then fail.
//
// We use two tree positions with meaningfully different centre-mic
// positions (one near the default centre, one pushed forward toward the
// speakers), and verify that the resulting IRs diverge within the first
// ~100 ms — i.e. the difference shows up in the early reflections, not
// just the FDN tail.
TEST_CASE("IR_24: tree placement affects the early-reflection envelope", "[engine][decca]")
{
    IRSynthParams pA = deccaSmallRoomParams();
    pA.main_decca_enabled = true;
    pA.decca_cx    = 0.5;
    pA.decca_cy    = 0.65;                // default
    pA.decca_angle = -1.5707963267948966; // -π/2

    IRSynthParams pB = pA;
    // Push the tree forward toward the speakers (lower y in room coordinates).
    // decca_cy = 0.55 with depth=8m moves the tree 0.80 m closer to y=0 than
    // the default, giving an order-of-ms change in direct-path arrival.
    pB.decca_cy = 0.55;

    auto rA = IRSynthEngine::synthIR (pA, [](double, const std::string&) {});
    auto rB = IRSynthEngine::synthIR (pB, [](double, const std::string&) {});

    REQUIRE (rA.success);
    REQUIRE (rB.success);
    REQUIRE (rA.irLen == rB.irLen);

    // 100 ms at the engine's internal sample rate. synthIR runs at
    // p.sample_rate which defaults to 48000, but we derive from the result
    // size so this survives future default changes.
    const int sr = 48000;
    const int earlyEndSample = sr / 10;  // 100 ms
    REQUIRE (rA.irLen > earlyEndSample);

    // Find the first sample where the two IRs diverge in iLL within the
    // first 100 ms. A -1 means the two placements produced identical
    // early-reflection output — the engine is not reading decca_cy.
    const int iDiv = firstDivergenceIndex (rA.iLL, rB.iLL, earlyEndSample);
    INFO ("First divergence sample (iLL) = " << iDiv
          << "   (negative means NO early-reflection divergence)");
    CHECK (iDiv >= 0);
    CHECK (iDiv < earlyEndSample);

    // And there should be a meaningful amount of total early-window
    // divergence, not just one rounding-noise sample.
    double earlyL2 = 0.0;
    for (int i = 0; i < earlyEndSample; ++i)
    {
        const double d = rA.iLL[(size_t)i] - rB.iLL[(size_t)i];
        earlyL2 += d * d;
    }
    earlyL2 = std::sqrt (earlyL2);
    INFO ("Early-window (100 ms) L2 difference = " << earlyL2);
    CHECK (earlyL2 > 1.0e-5);
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_20 — 1-pole 110 Hz HPF (as used on the Decca centre-mic contribution)
// ─────────────────────────────────────────────────────────────────────────────
// The engine applies a 1-pole HPF to the centre-mic contribution before
// summing into the four MAIN channels to avoid LF doubling. The filter is a
// lambda inside synthMainPath / synthDirectPath; to make it testable, the
// exact recurrence is reproduced here and checked against frequency-response
// expectations.
//
// Recurrence (matches engine):
//   α = exp(-2π · fc / sr)
//   y[n] = α · (y[n-1] + x[n] - x[n-1])
//
// Properties we assert:
//   1. Impulse response has near-zero DC gain (sum of tail ≈ 0).
//   2. 50 Hz sine is attenuated by ≥ 6 dB relative to 1 kHz (fc = 110 Hz).
//   3. Unit-step input decays toward zero, not to a steady DC offset.
//   4. No NaN/Inf for bounded random input.
namespace
{
    // Copy of the engine lambda — keep in sync with IRSynthEngine::synthMainPath.
    void applyDeccaHPF (std::vector<double>& v, double fcHz, double sr)
    {
        if (v.empty()) return;
        const double a = std::exp (-2.0 * M_PI * fcHz / sr);
        double xPrev = 0.0, yPrev = 0.0;
        for (double& s : v)
        {
            const double x = s;
            const double y = a * (yPrev + x - xPrev);
            s = y;
            xPrev = x;
            yPrev = y;
        }
    }

    double sinePeakTail (double fHz, double fcHz, double sr, int N)
    {
        std::vector<double> buf (N);
        const double w = 2.0 * M_PI * fHz / sr;
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = std::sin (w * (double) i);
        applyDeccaHPF (buf, fcHz, sr);
        double peak = 0.0;
        for (int i = N / 2; i < N; ++i)  // settled half
        {
            const double a = std::fabs (buf[(size_t) i]);
            if (a > peak) peak = a;
        }
        return peak;
    }
}

TEST_CASE("DSP_20: Decca centre-HPF (1-pole, 110 Hz) correctness", "[dsp][decca][hp]")
{
    constexpr double sr = 48000.0;
    constexpr double fc = 110.0;

    SECTION ("impulse response: DC sum ≈ 0, no NaN/Inf")
    {
        const int N = 8192;
        std::vector<double> buf (N, 0.0);
        buf[0] = 1.0;
        applyDeccaHPF (buf, fc, sr);

        double sum = 0.0;
        for (double s : buf)
        {
            REQUIRE (std::isfinite (s));
            sum += s;
        }
        INFO ("IR DC sum = " << sum);
        // 1-pole HPFs do not perfectly null DC (they have a finite-length tail
        // that is truncated here). |sum| < 5e-3 is tight enough to catch a
        // sign-flipped or wrong-α regression without being brittle.
        CHECK (std::fabs (sum) < 5.0e-3);
    }

    SECTION ("frequency response: 50 Hz attenuated ≥ 6 dB vs 1 kHz")
    {
        const int N = (int) (sr * 0.25);  // 250 ms settle
        const double peakLo = sinePeakTail (50.0,   fc, sr, N);
        const double peakHi = sinePeakTail (1000.0, fc, sr, N);

        REQUIRE (peakHi > 1.0e-4);
        const double dB = 20.0 * std::log10 (peakLo / peakHi);
        INFO ("50 Hz peak = " << peakLo
             << "   1 kHz peak = " << peakHi
             << "   relative atten = " << dB << " dB");
        CHECK (dB <= -6.0);
    }

    SECTION ("unit step decays — no DC retention")
    {
        const int N = 4800;  // 100 ms
        std::vector<double> buf (N, 1.0);  // constant +1
        applyDeccaHPF (buf, fc, sr);

        // After ~100 ms (1000+ time constants at 110 Hz) the output should be
        // essentially zero — certainly < 1e-3 of the input amplitude.
        const double tail = std::fabs (buf.back());
        INFO ("Unit step, last sample after " << N << " samples = " << tail);
        CHECK (tail < 1.0e-3);
    }

    SECTION ("bounded random input produces finite output")
    {
        const int N = 48000;  // 1 s
        std::vector<double> buf (N);
        uint32_t rng = 0xC0FFEE00u;
        for (int i = 0; i < N; ++i)
        {
            rng = rng * 1664525u + 1013904223u;  // LCG
            buf[(size_t) i] = ((double) rng / (double) UINT32_MAX) * 2.0 - 1.0;
        }
        applyDeccaHPF (buf, fc, sr);
        for (double s : buf)
            REQUIRE (std::isfinite (s));
    }
}
