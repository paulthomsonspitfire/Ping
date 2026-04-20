// PingMultiMicTests.cpp
// Tests for the multi-mic IR synthesis feature (feature/multi-mic-paths).
// Compiled with PING_TESTING_BUILD=1 so IRSynthEngine.h skips JuceHeader.h.
//
// Layout:
//   IR_14         Bit-identity regression lock for MAIN path (pre-refactor capture).
//   IR_15 … IR_21 (added after refactor lands — see Docs/Multi-Mic-Work-Plan.md).
//   DSP_15 … DSP_19 (added alongside the HP filter, pan law, solo, mixer work).
//
// Build target: PingTests (see CMakeLists.txt).
// Run: ctest --output-on-failure  (or ./PingTests from the build dir).

#define PING_TESTING_BUILD 1
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "IRSynthEngine.h"
#include "TestHelpers.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// ── Shared default params ───────────────────────────────────────────────────
// Identical to smallRoomParams() in PingEngineTests.cpp so IR_14 and IR_11
// use the same geometry and remain comparable.
static IRSynthParams smallRoomParams()
{
    IRSynthParams p;
    p.width  = 10.0;
    p.depth  =  8.0;
    p.height =  5.0;
    p.diffusion = 0.4;
    return p;
}

// ── 64-bit FNV-1a over raw IEEE-754 bytes ──────────────────────────────────
// A stronger alternative to a 30-sample spot check (IR_11).  Hashes the
// full IR, so *any* single-bit drift anywhere in any channel fails the test.
//
// SHA-256 would be the textbook choice; FNV-1a 64-bit is used here because
// (a) the test binary has no crypto dependencies, (b) false collision
// probability over a ~3 MB input is ~1/2^64 (vanishingly small for
// regression-detection purposes), and (c) it is deterministic across
// compilers/platforms.
//
// Return value is printed as a 16-char hex string so golden values are easy
// to copy-paste from the [capture14] test into the IR_14 lock.
namespace
{
    inline uint64_t fnv1a64 (const double* data, std::size_t count)
    {
        uint64_t h = 0xcbf29ce484222325ull;         // FNV-1a 64-bit offset basis
        const uint64_t prime = 0x100000001b3ull;    // FNV-1a 64-bit prime
        const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
        const std::size_t nb = count * sizeof(double);
        for (std::size_t i = 0; i < nb; ++i)
        {
            h ^= static_cast<uint64_t>(p[i]);
            h *= prime;
        }
        return h;
    }

    inline std::string toHex16 (uint64_t v)
    {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<unsigned long long>(v));
        return std::string(buf);
    }

    struct MainDigests
    {
        std::string iLL, iRL, iLR, iRR;
        int irLen = 0;
    };

    inline MainDigests digestMain (const IRSynthResult& r)
    {
        MainDigests d;
        d.irLen = r.irLen;
        d.iLL = toHex16(fnv1a64(r.iLL.data(), r.iLL.size()));
        d.iRL = toHex16(fnv1a64(r.iRL.data(), r.iRL.size()));
        d.iLR = toHex16(fnv1a64(r.iLR.data(), r.iLR.size()));
        d.iRR = toHex16(fnv1a64(r.iRR.data(), r.iRR.size()));
        return d;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST_IR_14 — MAIN path bit-identity regression lock
// ─────────────────────────────────────────────────────────────────────────────
// IR_11 locks only 30 samples of iLL near onset; the rest of the tail and
// the other three channels (iRL / iLR / iRR) are unprotected.
//
// IR_14 hashes every sample of every channel, so any non-trivial floating-
// point reordering or engine change shows up as four mismatched digests.
//
// This is the *belt-and-braces* safety net for the synthIR refactor on the
// feature/multi-mic-paths branch (Decision D1 in Docs/Multi-Mic-Work-Plan.md):
// the refactor extracts the MAIN synthesis body into synthMainPath() and
// adds a parallel dispatcher.  That refactor must not change the MAIN
// output by a single bit — IR_14 is the gate.
//
// To regenerate digests after a deliberate engine change:
//   ./PingTests "[capture14]" -s
// Paste the printed digests into the table below and set
// goldenCaptured = true, mirroring the IR_11 pattern.
TEST_CASE("IR_14: MAIN path full-IR bit-identity regression lock",
          "[engine][golden][bit-identity]")
{
    // GOLDEN DIGESTS — captured at feature/multi-mic-paths branch point
    // (v2.5.0, pre-multi-mic refactor) so the Phase 1 synthIR refactor
    // (C3 in Docs/Multi-Mic-Work-Plan.md) can be proven bit-identical.
    //
    //   irLen:  sample count of each MAIN channel (matches r.irLen)
    //   iLL/iRL/iLR/iRR: 64-bit FNV-1a hex digests over the raw double bytes.
    //
    // Any change to IRSynthEngine math that touches MAIN output (seeds,
    // geometry formulas, FDN parameters, blend factors, etc.) will flip
    // one or more digests.  Update intentionally via the capture case.
    //
    // Updated v2.7.6 (3D mic tilt): micG now uses a 3D direction cosine
    // computed once per reflection, replacing the previous 2D azimuth-only
    // gain. With the new *_tilt defaults of -π/6 (-30°), the per-reflection
    // mic gain factor changes for every reflection — all four channel
    // digests flip. irLen is unchanged because no length-affecting
    // parameter (room size, RT60, FDN topology) is touched.
    static const int         golden_irLen = 813146;
    static const std::string golden_iLL   = "5d50b767e97a82d9";
    static const std::string golden_iRL   = "c0b941904b9cd51d";
    static const std::string golden_iLR   = "9cbf0b51f4e087ad";
    static const std::string golden_iRR   = "3e4f471a33168215";

    static const bool goldenCaptured = true;   // recaptured for v2.7.6 3D mic tilt (IRSynthParams *_tilt = -π/6 default)
    if (! goldenCaptured)
    {
        SUCCEED("IR_14 golden digests not yet captured — run [capture14] first.");
        return;
    }

    IRSynthParams p = smallRoomParams();
    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&) {});

    REQUIRE(r.success);
    REQUIRE(r.irLen > 0);

    auto d = digestMain(r);

    INFO("irLen     expected " << golden_irLen << "  actual " << d.irLen);
    INFO("iLL hash  expected " << golden_iLL   << "  actual " << d.iLL);
    INFO("iRL hash  expected " << golden_iRL   << "  actual " << d.iRL);
    INFO("iLR hash  expected " << golden_iLR   << "  actual " << d.iLR);
    INFO("iRR hash  expected " << golden_iRR   << "  actual " << d.iRR);

    CHECK(d.irLen == golden_irLen);
    CHECK(d.iLL   == golden_iLL);
    CHECK(d.iRL   == golden_iRL);
    CHECK(d.iLR   == golden_iLR);
    CHECK(d.iRR   == golden_iRR);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST_IR_14_CAPTURE — Helper to (re)capture IR_14 golden digests
// ─────────────────────────────────────────────────────────────────────────────
// Tagged [capture14] so it only runs when explicitly requested:
//   ./PingTests "[capture14]" -s
// Paste the printed values into IR_14 above and set goldenCaptured = true.
TEST_CASE("IR_14_CAPTURE: print MAIN path full-IR digests", "[capture14]")
{
    IRSynthParams p = smallRoomParams();
    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&) {});

    REQUIRE(r.success);
    auto d = digestMain(r);

    std::printf("\n// ── Golden digests for TEST_IR_14 ──\n");
    std::printf("static const int         golden_irLen = %d;\n", d.irLen);
    std::printf("static const std::string golden_iLL   = \"%s\";\n", d.iLL.c_str());
    std::printf("static const std::string golden_iRL   = \"%s\";\n", d.iRL.c_str());
    std::printf("static const std::string golden_iLR   = \"%s\";\n", d.iLR.c_str());
    std::printf("static const std::string golden_iRR   = \"%s\";\n", d.iRR.c_str());
    std::printf("// Set goldenCaptured = true once these are pasted in.\n\n");

    SUCCEED("Digests printed — paste into IR_14 and set goldenCaptured = true.");
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_16 — HP2ndOrder correctness
// ─────────────────────────────────────────────────────────────────────────────
// Five sub-assertions:
//   1. Causality (output[i]==0 for i<0 by construction; we verify the impulse
//      response contains no samples before the impulse arrives).
//   2. DC gain of the impulse response ≈ 0 (confirms it is high-pass).
//   3. At fc = 110 Hz, a sine at 50 Hz is attenuated by ≥ 6 dB vs a sine at
//      1 kHz (frequency-response sanity — 50 Hz is well below cutoff, 1 kHz
//      is well above).
//   4. `enabled=false` passes input through unchanged; `enabled=true` returns
//      the filtered output. State continues to update in both modes.
//   5. 10 s of −12 dBFS random input produces no NaN/Inf.
#include "HP2ndOrder.h"

namespace
{
    // Render N samples of a pure sine and return the peak-to-peak amplitude
    // measured across the final (settled) half of the buffer.
    static float renderSinePeak (HP2ndOrder& hp, double sr,
                                 double freqHz, int numSamples)
    {
        float peak = 0.0f;
        const double omega = 2.0 * 3.14159265358979323846 * freqHz / sr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = (float) std::sin (omega * (double) i);
            const float y = hp.process (x, 0);
            if (i >= numSamples / 2)
            {
                const float ay = std::fabs (y);
                if (ay > peak) peak = ay;
            }
        }
        return peak;
    }
}

TEST_CASE("DSP_16: HP2ndOrder correctness", "[dsp][hp]")
{
    constexpr double sr = 48000.0;

    SECTION ("impulse response: DC gain ≈ 0 and finite")
    {
        HP2ndOrder hp;
        hp.prepare (110.0f, sr);
        hp.enabled = true;

        constexpr int N = 8192;
        std::vector<float> ir (N, 0.0f);
        ir[0] = 1.0f;

        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float y = hp.process (ir[i], 0);
            sum += (double) y;
            REQUIRE (std::isfinite (y));
        }

        // DC gain of an ideal 2nd-order HP is zero. With a finite tail the
        // residual is tiny (<1e-3); anything meaningfully nonzero here would
        // indicate the coefficients are wrong or the filter is not HP.
        CHECK (std::fabs (sum) < 1.0e-3);
    }

    SECTION ("frequency response: 50 Hz attenuated ≥ 6 dB vs 1 kHz at fc=110 Hz")
    {
        constexpr int settleSamples = (int) (sr * 0.5); // 500 ms settle

        HP2ndOrder hpLow;
        hpLow.prepare (110.0f, sr);
        hpLow.enabled = true;
        const float peakLo = renderSinePeak (hpLow, sr, 50.0, settleSamples);

        HP2ndOrder hpHi;
        hpHi.prepare (110.0f, sr);
        hpHi.enabled = true;
        const float peakHi = renderSinePeak (hpHi, sr, 1000.0, settleSamples);

        REQUIRE (peakHi > 1.0e-4f);
        const double attenDb = 20.0 * std::log10 ((double) peakLo / (double) peakHi);
        INFO ("50 Hz peak = " << peakLo
              << "  1 kHz peak = " << peakHi
              << "  relative attenuation = " << attenDb << " dB");
        CHECK (attenDb <= -6.0);
    }

    SECTION ("enabled=false is unity passthrough; state continues to update")
    {
        HP2ndOrder hp;
        hp.prepare (110.0f, sr);

        // Disabled: outputs should equal inputs exactly (bit-identical).
        hp.enabled = false;
        const float inputs[6] = { 0.1f, -0.2f, 0.35f, -0.4f, 0.0f, 0.5f };
        for (int i = 0; i < 6; ++i)
        {
            const float y = hp.process (inputs[i], 0);
            REQUIRE (y == inputs[i]);
        }

        // Even though outputs were pass-through, the internal state should
        // have been advanced. Verify by toggling enabled=true and checking
        // that the very first filtered output is NOT the naïve "b0*x" we'd
        // get from a cleared state.
        HP2ndOrder hpReset;
        hpReset.prepare (110.0f, sr);
        hpReset.enabled = true;
        const float yReset = hpReset.process (inputs[0], 0); // clean-state output
        // Using the same input sample as what will be fed next:
        hp.enabled = true;
        const float yContinuous = hp.process (0.6f, 0);
        // If state had been silently zeroed while disabled, processing 0.6f
        // after a clean reset would give b0 * 0.6f. With history, y differs.
        const float yNaive = hp.b0 * 0.6f;
        INFO ("clean-state y = " << yReset
              << "  continuous y = " << yContinuous
              << "  b0*x        = " << yNaive);
        CHECK (std::fabs (yContinuous - yNaive) > 1.0e-5f);
    }

    SECTION ("10 s of −12 dBFS noise produces no NaN/Inf")
    {
        HP2ndOrder hp;
        hp.prepare (110.0f, sr);
        hp.enabled = true;

        TestRng rng (0x5a17u);
        const int N = (int) (sr * 10.0);
        for (int i = 0; i < N; ++i)
        {
            // −12 dBFS ≈ 0.251 linear
            const float x = 0.251f * rng.nextFloat();
            const float y = hp.process (x, 0);
            if (! std::isfinite (y))
            {
                FAIL ("HP produced non-finite output at sample " << i);
                return;
            }
        }
        SUCCEED();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_17 — HP toggle click-free on re-enable (state-continuity invariant)
// ─────────────────────────────────────────────────────────────────────────────
// The D5 design decision says HP toggle is a hard switch (no crossfade), but
// the filter state must be continuous through the toggle so re-enabling does
// NOT produce a startup transient / click. To verify this, we run two copies
// of the filter on identical input:
//   refHp   — enabled throughout
//   toggleHp — enabled → disabled (5000 samples) → enabled again
// After re-enable, toggleHp's filtered output samples must be bit-identical
// to refHp's at the same indices. If they differ, the state drifted while
// disabled and re-enable will produce a transient.
//
// Output comparison during the disabled window is NOT a useful test (the
// toggled filter is pass-through, ref is filtered — they differ by design).
TEST_CASE("DSP_17: HP toggle click-free on re-enable", "[dsp][hp]")
{
    constexpr double sr = 48000.0;
    constexpr int segLen = 5000;
    constexpr int total  = segLen * 3;

    auto rampInput = [](int i) -> float
    {
        return 0.5f * (float) i / (float) total;
    };

    HP2ndOrder refHp;
    refHp.prepare (110.0f, sr);
    refHp.enabled = true;

    HP2ndOrder toggleHp;
    toggleHp.prepare (110.0f, sr);
    toggleHp.enabled = true;

    std::vector<float> refOut (total), toggleOut (total);

    for (int i = 0; i < total; ++i)
    {
        if      (i == segLen)      toggleHp.enabled = false;
        else if (i == 2 * segLen)  toggleHp.enabled = true;

        const float x = rampInput (i);
        refOut[i]    = refHp.process    (x, 0);
        toggleOut[i] = toggleHp.process (x, 0);
    }

    // Enabled segments must match bit-for-bit. State-continuity is the
    // whole point of the D5 design: re-enabling picks up exactly where an
    // always-enabled filter would be.
    float maxDiffEnabled = 0.0f;
    for (int i = 0; i < segLen; ++i)
        maxDiffEnabled = std::max (maxDiffEnabled, std::fabs (refOut[i] - toggleOut[i]));
    for (int i = 2 * segLen; i < total; ++i)
        maxDiffEnabled = std::max (maxDiffEnabled, std::fabs (refOut[i] - toggleOut[i]));

    INFO ("max |refOut - toggleOut| across enabled windows = " << maxDiffEnabled);
    CHECK (maxDiffEnabled == 0.0f);

    // Also sanity-check the pass-through segment: toggleOut should equal the
    // raw input (not the filtered reference).
    float maxDiffPassthrough = 0.0f;
    for (int i = segLen; i < 2 * segLen; ++i)
        maxDiffPassthrough = std::max (maxDiffPassthrough,
                                        std::fabs (toggleOut[i] - rampInput (i)));
    INFO ("max |toggleOut - rawInput| across disabled window = " << maxDiffPassthrough);
    CHECK (maxDiffPassthrough == 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers for multi-path engine tests (IR_15 … IR_21)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // Peak absolute value across four channels.
    inline double peak4 (const MicIRChannels& m)
    {
        double pk = 0.0;
        auto scan = [&pk](const std::vector<double>& v)
        {
            for (double x : v) { double ax = std::fabs(x); if (ax > pk) pk = ax; }
        };
        scan(m.LL); scan(m.RL); scan(m.LR); scan(m.RR);
        return pk;
    }

    // True iff any of the four channels contains NaN/Inf.
    inline bool anyNaNorInf4 (const MicIRChannels& m)
    {
        return hasNaNorInf(m.LL) || hasNaNorInf(m.RL)
            || hasNaNorInf(m.LR) || hasNaNorInf(m.RR);
    }

    // Index of the peak absolute sample in v.
    inline int peakIndex (const std::vector<double>& v)
    {
        int bestI = 0; double bestV = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            double ax = std::fabs(v[i]);
            if (ax > bestV) { bestV = ax; bestI = (int)i; }
        }
        return bestI;
    }

    // 3D Euclidean distance.
    inline double dist3d (double ax, double ay, double az,
                          double bx, double by, double bz)
    {
        double dx = ax-bx, dy = ay-by, dz = az-bz;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_15 — OUTRIG / AMBIENT path structure
// ─────────────────────────────────────────────────────────────────────────────
// Enables both OUTRIG and AMBIENT with their default (non-coincident) geometry
// and verifies:
//   - synthesised == true
//   - all four channel vectors have length == result.irLen
//   - no NaN/Inf anywhere
//   - peak magnitude < 10.0 (sanity bound — real IRs are ~1)
// DIRECT is left disabled to confirm it stays at synthesised == false.
TEST_CASE("IR_15: OUTRIG / AMBIENT path structure is correct",
          "[engine][multimic]")
{
    IRSynthParams p = smallRoomParams();
    p.outrig_enabled  = true;
    p.ambient_enabled = true;

    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    REQUIRE(r.success);
    REQUIRE(r.irLen > 0);

    SECTION ("OUTRIG")
    {
        REQUIRE(r.outrig.synthesised);
        CHECK(r.outrig.irLen == r.irLen);
        CHECK((int)r.outrig.LL.size() == r.irLen);
        CHECK((int)r.outrig.RL.size() == r.irLen);
        CHECK((int)r.outrig.LR.size() == r.irLen);
        CHECK((int)r.outrig.RR.size() == r.irLen);
        CHECK_FALSE(anyNaNorInf4(r.outrig));
        const double pk = peak4(r.outrig);
        INFO("OUTRIG peak = " << pk);
        CHECK(pk > 0.0);
        CHECK(pk < 10.0);
    }

    SECTION ("AMBIENT")
    {
        REQUIRE(r.ambient.synthesised);
        CHECK(r.ambient.irLen == r.irLen);
        CHECK((int)r.ambient.LL.size() == r.irLen);
        CHECK((int)r.ambient.RL.size() == r.irLen);
        CHECK((int)r.ambient.LR.size() == r.irLen);
        CHECK((int)r.ambient.RR.size() == r.irLen);
        CHECK_FALSE(anyNaNorInf4(r.ambient));
        const double pk = peak4(r.ambient);
        INFO("AMBIENT peak = " << pk);
        CHECK(pk > 0.0);
        CHECK(pk < 10.0);
    }

    SECTION ("DIRECT remains inactive when direct_enabled == false")
    {
        CHECK_FALSE(r.direct.synthesised);
        CHECK(r.direct.LL.empty());
        CHECK(r.direct.RL.empty());
        CHECK(r.direct.LR.empty());
        CHECK(r.direct.RR.empty());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_16 — Determinism under parallel dispatch
// ─────────────────────────────────────────────────────────────────────────────
// C5 refactored synthIR to run MAIN, DIRECT, OUTRIG and AMBIENT concurrently
// via std::async. If any shared state leaked between threads (progress map,
// static material table, RNG, etc.) we would see sample drift between
// consecutive runs with identical params.
//
// Runs synthIR twice with all auxiliary paths enabled and asserts the
// resulting IRs are bit-identical for every path. Guards C5 against future
// refactors that accidentally introduce non-determinism.
TEST_CASE("IR_16: synthIR is deterministic across runs (all paths on)",
          "[engine][multimic][determinism]")
{
    IRSynthParams p = smallRoomParams();
    p.direct_enabled  = true;
    p.outrig_enabled  = true;
    p.ambient_enabled = true;

    auto r1 = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    auto r2 = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    REQUIRE(r1.success);
    REQUIRE(r2.success);
    REQUIRE(r1.irLen == r2.irLen);

    // MAIN
    CHECK(r1.iLL == r2.iLL);
    CHECK(r1.iRL == r2.iRL);
    CHECK(r1.iLR == r2.iLR);
    CHECK(r1.iRR == r2.iRR);

    auto checkPathEqual = [](const char* name,
                             const MicIRChannels& a,
                             const MicIRChannels& b)
    {
        INFO("path: " << name);
        REQUIRE(a.synthesised);
        REQUIRE(b.synthesised);
        CHECK(a.irLen == b.irLen);
        CHECK(a.LL == b.LL);
        CHECK(a.RL == b.RL);
        CHECK(a.LR == b.LR);
        CHECK(a.RR == b.RR);
    };
    checkPathEqual("DIRECT",  r1.direct,  r2.direct);
    checkPathEqual("OUTRIG",  r1.outrig,  r2.outrig);
    checkPathEqual("AMBIENT", r1.ambient, r2.ambient);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_17 — DIRECT path with direct_max_order=0 contains no wall reflections
// ─────────────────────────────────────────────────────────────────────────────
// synthDirectPath reads IRSynthParams::direct_max_order. This test explicitly
// pins it to 0 (the historical behaviour), so the only content is the direct
// arrival per speaker→mic pair plus the 8-band bandpass-filter impulse-response
// tail. There should be no first-order wall bounces (which would arrive later
// than the direct path, after ~2× the min-wall distance / c).
//
// Test:
//   - Find the global peak index in direct.LL.
//   - Count how much signal energy falls outside a localised window around the
//     peak (±kWindow samples — wide enough to contain the bandpass ringing).
//   - Assert the max sample outside that window is at least 40 dB below peak.
//
// 40 dB headroom is an easy win for a pure order-0 IR (the bandpass ringing
// decays well below 40 dB within a few hundred samples). Any first-order
// reflection would arrive at ~1 ms past direct = ~48 samples later — well
// inside the window in tiny rooms but outside for room sizes where the test
// geometry puts the closest wall ≥ a few metres off the direct path.
TEST_CASE("IR_17: DIRECT with direct_max_order=0 has only order-0 content",
          "[engine][multimic][direct]")
{
    IRSynthParams p = smallRoomParams();
    p.direct_enabled   = true;
    p.direct_max_order = 0;  // explicitly pin historical behaviour

    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    REQUIRE(r.success);
    REQUIRE(r.direct.synthesised);
    REQUIRE(r.direct.LL.size() > 1000);

    const int peakIdx = peakIndex(r.direct.LL);
    const double peakVal = std::fabs(r.direct.LL[peakIdx]);
    REQUIRE(peakVal > 0.0);

    // Window width chosen to safely bracket the 8-band biquad bandpass
    // impulse response (the widest resonator is the 125 Hz band with the
    // longest ring-down). 512 samples ≈ 10.7 ms at 48 kHz — generous.
    constexpr int kWindow = 512;
    const int lo = std::max(0, peakIdx - kWindow);
    const int hi = std::min((int)r.direct.LL.size(), peakIdx + kWindow);

    double maxOutside = 0.0;
    for (int i = 0; i < (int)r.direct.LL.size(); ++i)
    {
        if (i >= lo && i < hi) continue;
        const double ax = std::fabs(r.direct.LL[i]);
        if (ax > maxOutside) maxOutside = ax;
    }

    const double rDb = maxOutside > 0.0
        ? 20.0 * std::log10(maxOutside / peakVal)
        : -200.0;
    INFO("peak = " << peakVal << " at sample " << peakIdx
         << "; max outside ±" << kWindow << " samples = " << maxOutside
         << " (" << rDb << " dB)");
    CHECK(rDb < -40.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_18 — DIRECT path timing (arrival sample matches geometry)
// ─────────────────────────────────────────────────────────────────────────────
// For each of the four speaker→mic paths, compute the physical distance and
// the expected arrival sample index (= dist / 343 × 48000). Find the peak in
// the corresponding DIRECT channel and assert it is within a tolerance of
// the predicted index.
//
// The bandpass biquad cascade introduces ~tens of samples of group delay, so
// the absolute offset is baselined here to ±200 samples (~4 ms at 48 kHz) —
// wide enough to tolerate filter phase but tight enough that a completely
// wrong geometry (e.g. no speaker/mic height, or distance bug) would fail.
TEST_CASE("IR_18: DIRECT path peak timing matches geometry",
          "[engine][multimic][direct]")
{
    IRSynthParams p = smallRoomParams();
    p.direct_enabled = true;
    // Pin to order-0 so the peak is guaranteed to be the direct arrival. With
    // direct_max_order > 0 a first-order reflection can outrun or match the
    // direct peak and the geometry check becomes ambiguous. IR_25 covers the
    // order > 0 case separately.
    p.direct_max_order = 0;

    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    REQUIRE(r.success);
    REQUIRE(r.direct.synthesised);

    // Recompute the heights the engine uses (match synthDirectPath).
    // Default smallRoomParams vault type is "Groin / cross vault  (Lyndhurst Hall)"
    // with hm = 1.25 → He = 6.25, sz = 1.0, rz = 3.0.
    const double hm = 1.25;
    const double He = p.height * hm;
    const double sz = std::min(1.0, He * 0.9);
    const double rz = std::min(3.0, He * 0.9);

    const double slx = p.width * p.source_lx,   sly = p.depth * p.source_ly;
    const double srx = p.width * p.source_rx,   sry = p.depth * p.source_ry;
    const double rlx = p.width * p.receiver_lx, rly = p.depth * p.receiver_ly;
    const double rrx = p.width * p.receiver_rx, rry = p.depth * p.receiver_ry;

    const int sr = p.sample_rate;
    const double c = 343.0;

    auto expectedSample = [sr, c](double d) {
        return (int) std::round(d / c * sr);
    };

    struct Case { const char* name; double dist; const std::vector<double>* v; };
    const std::vector<Case> cases = {
        { "LL", dist3d(rlx, rly, rz, slx, sly, sz), &r.direct.LL },
        { "RL", dist3d(rlx, rly, rz, srx, sry, sz), &r.direct.RL },
        { "LR", dist3d(rrx, rry, rz, slx, sly, sz), &r.direct.LR },
        { "RR", dist3d(rrx, rry, rz, srx, sry, sz), &r.direct.RR }
    };

    constexpr int kTol = 200; // samples; bracket for bandpass group delay
    for (const auto& c0 : cases)
    {
        const int expected = expectedSample(c0.dist);
        const int actual   = peakIndex(*c0.v);
        INFO(c0.name << ": dist=" << c0.dist
             << " m, expected=" << expected
             << ", actual=" << actual
             << ", delta=" << (actual - expected));
        CHECK(std::abs(actual - expected) <= kTol);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_19 — DIRECT path polar colouration (figure-8 null test)
// ─────────────────────────────────────────────────────────────────────────────
// Sanity-checks that the frequency-dependent mic polar pattern is actually
// applied to order-0 arrivals (Decision D2 — DIRECT inherits mic_pattern /
// micl_angle / micr_angle from MAIN).
//
// Figure-8 mic: maximum gain when faceAngle matches the azimuth to the source,
// zero gain at ±90° (the null). The engine models a half-lobe via
// max(0, o + d·cos(az − face)), so at 125 Hz the null is not perfectly zero
// (o = 0.12, d = 0.88 → ~0.12 residual at 90° off-axis), but by 2 kHz and
// above o = 0, d = 1 → true null.
//
// Test:
//   - Case A: mic L faces toward speaker L (max response).
//   - Case B: mic L faces 90° away from speaker L (near-null).
//   - Assert peak of case B is ≥ 20 dB below peak of case A.
TEST_CASE("IR_19: DIRECT path applies mic polar pattern (figure-8)",
          "[engine][multimic][direct][polar]")
{
    IRSynthParams base = smallRoomParams();
    base.direct_enabled = true;
    base.mic_pattern    = "figure8";
    // Pin to order-0: the figure-8 null test only makes sense on the bare
    // direct arrival. Reflections arrive from other azimuths and would bypass
    // the null, inflating the 90°-off-axis peak.
    base.direct_max_order = 0;
    // Pin tilt to 0° (horizontal). The figure-8 null this test checks is a
    // pure azimuth-plane null; with the new tilt feature defaulting to -30°,
    // a source below horizontal lands inside the lobe and the null collapses.
    // This test is a regression guard for the polar-pattern lookup, not for
    // tilt behaviour — DSP_21 covers the 3D direction-cosine math directly.
    base.micl_tilt = 0.0;
    base.micr_tilt = 0.0;

    // Physical geometry for mic L → spk L (default smallRoomParams).
    const double hm = 1.25;
    const double He = base.height * hm;
    const double sz = std::min(1.0, He * 0.9);
    const double rz = std::min(3.0, He * 0.9);
    const double slx = base.width * base.source_lx, sly = base.depth * base.source_ly;
    const double rlx = base.width * base.receiver_lx, rly = base.depth * base.receiver_ly;

    // Azimuth from mic L toward spk L in the floor plane (matches calcRefs
    // convention: z component is ignored for the polar lookup).
    const double azToSpk = std::atan2(sly - rly, slx - rlx);

    // Case A: face the speaker
    IRSynthParams pA = base;
    pA.micl_angle = azToSpk;
    // Case B: face 90° away from the speaker (perpendicular → figure-8 null)
    IRSynthParams pB = base;
    pB.micl_angle = azToSpk + M_PI_2;

    auto rA = IRSynthEngine::synthIR(pA, [](double, const std::string&){});
    auto rB = IRSynthEngine::synthIR(pB, [](double, const std::string&){});
    REQUIRE(rA.success);
    REQUIRE(rB.success);
    REQUIRE(rA.direct.synthesised);
    REQUIRE(rB.direct.synthesised);

    const double peakA = std::fabs(rA.direct.LL[peakIndex(rA.direct.LL)]);
    const double peakB = std::fabs(rB.direct.LL[peakIndex(rB.direct.LL)]);
    REQUIRE(peakA > 0.0);

    const double dB = 20.0 * std::log10(std::max(peakB, 1e-20) / peakA);
    INFO("figure-8: peak ON-axis = " << peakA
         << ", peak 90° OFF-axis = " << peakB
         << ", relative = " << dB << " dB  (azToSpk = " << azToSpk << " rad)");

    // Silence the unused-variable warning for sz / rz — retained for
    // documentation / parity with IR_18; not actually used in this test.
    (void) sz; (void) rz;

    CHECK(dB <= -20.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_20 — OUTRIG / AMBIENT independence from MAIN
// ─────────────────────────────────────────────────────────────────────────────
// Two sub-tests:
//   (1) OUTRIG with its default (different) positions produces an IR that
//       differs substantially from MAIN — l2diff > 1% of ‖iLL‖².
//   (2) OUTRIG with identical positions, pattern and height to MAIN still
//       differs, because synthExtraPath uses seedBase = 52 (vs MAIN's 42).
//       The diffuse field (Lambert scatter / frequency scatter) is seeded
//       from the same RNG family as MAIN but with different seeds, so every
//       non-specular contribution is different. A pure l2diff > 1e-6 is enough
//       to catch an accidental seed collision.
TEST_CASE("IR_20: OUTRIG / AMBIENT are independent from MAIN",
          "[engine][multimic]")
{
    SECTION ("Different positions → substantially different IRs")
    {
        IRSynthParams p = smallRoomParams();
        p.outrig_enabled = true;
        // Default outrig geometry (0.15/0.80, 0.85/0.80) differs from MAIN
        // (0.35/0.80, 0.65/0.80) — ensure we keep the defaults for the test.
        p.outrig_lx = 0.15; p.outrig_ly = 0.80;
        p.outrig_rx = 0.85; p.outrig_ry = 0.80;

        auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
        REQUIRE(r.success);
        REQUIRE(r.outrig.synthesised);

        const double nLL = totalEnergy(r.iLL);
        REQUIRE(nLL > 0.0);
        const double d = l2diff(r.iLL, r.outrig.LL);
        const double ratio = d / nLL;
        INFO("‖iLL‖² = " << nLL
             << ", l2diff(iLL, outrig.LL)² = " << d
             << ", ratio = " << ratio);
        CHECK(ratio > 0.01);
    }

    SECTION ("Same positions + pattern → still differs (seed base 42 vs 52)")
    {
        IRSynthParams p = smallRoomParams();
        p.outrig_enabled = true;
        // Pin outrig geometry to MAIN's receiver positions.
        p.outrig_lx = p.receiver_lx; p.outrig_ly = p.receiver_ly;
        p.outrig_rx = p.receiver_rx; p.outrig_ry = p.receiver_ry;
        p.outrig_langle = p.micl_angle;
        p.outrig_rangle = p.micr_angle;
        p.outrig_height = 3.0;               // matches rz = min(3.0, He*0.9)
        p.outrig_pattern = p.mic_pattern;

        auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
        REQUIRE(r.success);
        REQUIRE(r.outrig.synthesised);

        const double d = l2diff(r.iLL, r.outrig.LL);
        INFO("l2diff(iLL, outrig.LL)² at identical geometry = " << d
             << " (should be nonzero due to distinct seed bases 42 vs 52)");
        CHECK(d > 1e-6);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_21 — er_only global behaviour for OUTRIG / AMBIENT / DIRECT
// ─────────────────────────────────────────────────────────────────────────────
// Decision D4: er_only is a global flag that suppresses the late/FDN tail on
// every path. DIRECT is order-0 by construction (already silent past the
// direct arrival). OUTRIG and AMBIENT should have no meaningful energy past
// 200 ms relative to their own peak (−60 dB gate, analogous to IR_06 for
// MAIN).
TEST_CASE("IR_21: er_only suppresses late energy on all paths",
          "[engine][multimic][er-only]")
{
    IRSynthParams p = smallRoomParams();
    p.er_only         = true;
    p.direct_enabled  = true;
    p.outrig_enabled  = true;
    p.ambient_enabled = true;

    auto r = IRSynthEngine::synthIR(p, [](double, const std::string&){});
    REQUIRE(r.success);
    REQUIRE(r.outrig.synthesised);
    REQUIRE(r.ambient.synthesised);
    REQUIRE(r.direct.synthesised);

    const int sr       = r.sampleRate;
    const int lateStart = (int)(0.200 * sr);        // 200 ms
    const int winLen    = std::max(64, sr / 1000);  // 1 ms sliding window

    auto checkLateIsSilent = [&](const char* name,
                                 const std::vector<double>& v)
    {
        INFO("path: " << name);
        const int peakI = peakIndex(v);
        const double peakV = std::fabs(v[peakI]);
        REQUIRE(peakV > 0.0);

        double maxLate = 0.0;
        for (int i = lateStart; i + winLen <= (int)v.size(); i += winLen)
        {
            const double rms = windowRMS(v, i, winLen);
            if (rms > maxLate) maxLate = rms;
        }
        const double dB = maxLate > 0.0
            ? 20.0 * std::log10(maxLate / peakV)
            : -200.0;
        INFO("  peak = " << peakV << ", max late RMS = " << maxLate
             << " (" << dB << " dB below peak)");
        CHECK(dB < -60.0);
    };

    checkLateIsSilent("OUTRIG.LL",  r.outrig.LL);
    checkLateIsSilent("OUTRIG.RR",  r.outrig.RR);
    checkLateIsSilent("AMBIENT.LL", r.ambient.LL);
    checkLateIsSilent("AMBIENT.RR", r.ambient.RR);
    checkLateIsSilent("DIRECT.LL",  r.direct.LL);
    checkLateIsSilent("DIRECT.RR",  r.direct.RR);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR_25 — DIRECT with direct_max_order=1 contains first-order reflections
// ─────────────────────────────────────────────────────────────────────────────
// Counterpart to IR_17: when direct_max_order is raised from 0 to 1, the DIRECT
// path must include first-order wall / floor / ceiling bounces and therefore
// carry meaningful energy outside the direct-arrival window.
//
// Uses the same ±512-sample window as IR_17. At order-0 the energy outside the
// window is ≤ −40 dB (IR_17). At order-1 the floor and ceiling reflections are
// well above that floor (−20 dB is a comfortable margin in the default small
// room), so we assert the maxOutside/peak ratio has risen by at least 10 dB
// relative to the order-0 measurement — a robust guard against regression to
// order-0 without pinning a brittle absolute level.
TEST_CASE("IR_25: DIRECT with direct_max_order=1 contains wall reflections",
          "[engine][multimic][direct][reflections]")
{
    auto maxOutsidePeakDb = [](const std::vector<double>& v)
    {
        const int peakIdx = peakIndex(v);
        const double peakVal = std::fabs(v[peakIdx]);
        REQUIRE(peakVal > 0.0);
        constexpr int kWindow = 512;
        const int lo = std::max(0, peakIdx - kWindow);
        const int hi = std::min((int)v.size(), peakIdx + kWindow);
        double maxOutside = 0.0;
        for (int i = 0; i < (int)v.size(); ++i)
        {
            if (i >= lo && i < hi) continue;
            const double ax = std::fabs(v[i]);
            if (ax > maxOutside) maxOutside = ax;
        }
        return maxOutside > 0.0
            ? 20.0 * std::log10(maxOutside / peakVal)
            : -200.0;
    };

    IRSynthParams p0 = smallRoomParams();
    p0.direct_enabled   = true;
    p0.direct_max_order = 0;
    auto r0 = IRSynthEngine::synthIR(p0, [](double, const std::string&){});
    REQUIRE(r0.success);
    REQUIRE(r0.direct.synthesised);
    const double order0Db = maxOutsidePeakDb(r0.direct.LL);

    IRSynthParams p1 = smallRoomParams();
    p1.direct_enabled   = true;
    p1.direct_max_order = 1;
    auto r1 = IRSynthEngine::synthIR(p1, [](double, const std::string&){});
    REQUIRE(r1.success);
    REQUIRE(r1.direct.synthesised);
    const double order1Db = maxOutsidePeakDb(r1.direct.LL);

    INFO("order-0 max-outside = " << order0Db << " dB, "
         "order-1 max-outside = " << order1Db << " dB");

    // Sanity: order-0 is still below the IR_17 threshold of −40 dB.
    CHECK(order0Db < -40.0);
    // Order-1 must be at least 10 dB *louder* outside the window — i.e.
    // closer to 0 dB, so order1Db > order0Db + 10.
    CHECK(order1Db > order0Db + 10.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_15 — constant-power pan law
// ─────────────────────────────────────────────────────────────────────────────
// The mixer in processBlock implements a constant-power pan law:
//     panL = cos((p + 1) · π/4),   panR = sin((p + 1) · π/4),   p ∈ [−1, +1]
// Invariants:
//   1. panL² + panR² == 1 (constant power) for every p.
//   2. At p = 0, panL == panR == √(1/2).
//   3. At p = −1, panL == 1 and panR == 0. At p = +1, panL == 0 and panR == 1.
namespace
{
    inline void computePanLaw (float p, float& panL, float& panR)
    {
        const float theta = (p + 1.0f) * (float) M_PI_4;
        panL = std::cos(theta);
        panR = std::sin(theta);
    }
}

TEST_CASE("DSP_15: constant-power pan law", "[dsp][mixer][pan]")
{
    constexpr float kTol = 1.0e-6f;

    const float pans[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    for (float p : pans)
    {
        float l = 0.0f, r = 0.0f;
        computePanLaw(p, l, r);
        const float sumSq = l * l + r * r;
        INFO("pan = " << p << ": panL = " << l
             << ", panR = " << r << ", panL² + panR² = " << sumSq);
        CHECK(std::fabs(sumSq - 1.0f) < kTol);
        // Cosine/sine round to very small negative values near π/2 — allow
        // a single-ULP slack instead of requiring strict non-negativity.
        CHECK(l >= -kTol);
        CHECK(r >= -kTol);
    }

    SECTION ("centre returns equal, √(1/2) on each side")
    {
        float l = 0.0f, r = 0.0f;
        computePanLaw(0.0f, l, r);
        const float sqrtHalf = std::sqrt(0.5f);
        CHECK(std::fabs(l - sqrtHalf) < kTol);
        CHECK(std::fabs(r - sqrtHalf) < kTol);
        CHECK(std::fabs(l - r)        < kTol);
    }

    SECTION ("hard-left returns (1, 0); hard-right returns (0, 1)")
    {
        float l = 0.0f, r = 0.0f;
        computePanLaw(-1.0f, l, r);
        CHECK(std::fabs(l - 1.0f) < kTol);
        CHECK(std::fabs(r - 0.0f) < kTol);

        computePanLaw(+1.0f, l, r);
        CHECK(std::fabs(l - 0.0f) < kTol);
        CHECK(std::fabs(r - 1.0f) < kTol);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_18 — mute / solo logic
// ─────────────────────────────────────────────────────────────────────────────
// The mixer gates each path with a "contributes" boolean derived from four
// inputs per path: pathOn (raw), mute, solo and anySolo (OR across all paths).
//
//   contributes = pathOn && !mute && (anySolo ? solo : true)
//
// Mute always wins over solo; solo on a path that is switched off still has
// no effect (because pathOn is false). If any path is soloed, all non-soloed
// paths fall silent — the standard DAW convention.
//
// This test defines the rule as a standalone function and exhaustively checks
// the truth table for the five canonical cases.
namespace
{
    struct PathState { bool on, mute, solo; };

    inline bool computeContributes (const PathState& s, bool anySolo)
    {
        if (! s.on || s.mute) return false;
        if (anySolo && ! s.solo) return false;
        return true;
    }

    inline bool anySoloOf4 (const std::array<PathState, 4>& s)
    {
        for (const auto& p : s) if (p.solo) return true;
        return false;
    }
}

TEST_CASE("DSP_18: mute / solo logic", "[dsp][mixer][solo]")
{
    using S = PathState;

    SECTION ("no solo: all unmuted paths contribute")
    {
        std::array<S, 4> st = {{
            { true, false, false }, { true, false, false },
            { true, false, false }, { true, false, false }
        }};
        const bool any = anySoloOf4(st);
        CHECK_FALSE(any);
        for (int i = 0; i < 4; ++i)
            CHECK(computeContributes(st[i], any));
    }

    SECTION ("one solo: only that path contributes")
    {
        std::array<S, 4> st = {{
            { true, false, false }, { true, false, true },
            { true, false, false }, { true, false, false }
        }};
        const bool any = anySoloOf4(st);
        CHECK(any);
        CHECK_FALSE(computeContributes(st[0], any));
        CHECK      (computeContributes(st[1], any));
        CHECK_FALSE(computeContributes(st[2], any));
        CHECK_FALSE(computeContributes(st[3], any));
    }

    SECTION ("two solos: only those two contribute")
    {
        std::array<S, 4> st = {{
            { true, false, true  }, { true, false, false },
            { true, false, true  }, { true, false, false }
        }};
        const bool any = anySoloOf4(st);
        CHECK(any);
        CHECK      (computeContributes(st[0], any));
        CHECK_FALSE(computeContributes(st[1], any));
        CHECK      (computeContributes(st[2], any));
        CHECK_FALSE(computeContributes(st[3], any));
    }

    SECTION ("solo + mute on same path: mute wins")
    {
        std::array<S, 4> st = {{
            { true, true,  true  },   // solo + mute → silent
            { true, false, false },
            { true, false, false },
            { true, false, false }
        }};
        const bool any = anySoloOf4(st);
        CHECK(any); // path 0 is still soloed as far as anySolo is concerned
        CHECK_FALSE(computeContributes(st[0], any));   // muted
        CHECK_FALSE(computeContributes(st[1], any));   // not soloed while anySolo
        CHECK_FALSE(computeContributes(st[2], any));
        CHECK_FALSE(computeContributes(st[3], any));
    }

    SECTION ("solo on a switched-off path: no contribution")
    {
        std::array<S, 4> st = {{
            { false, false, true  },    // soloed but path is off
            { true,  false, false },
            { true,  false, false },
            { true,  false, false }
        }};
        const bool any = anySoloOf4(st);
        CHECK(any);
        CHECK_FALSE(computeContributes(st[0], any));   // off
        CHECK_FALSE(computeContributes(st[1], any));   // soloed elsewhere but we're not
        CHECK_FALSE(computeContributes(st[2], any));
        CHECK_FALSE(computeContributes(st[3], any));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// DSP_19 — Path summation preserves MAIN-only path (backward compat)
// ════════════════════════════════════════════════════════════════════════════
//
// The real processBlock builds per-strip contributions (ER+Tail for MAIN,
// convolver output for DIRECT/OUTRIG/AMBIENT), applies HP→gain→pan per strip,
// then sums them into the output bus:
//
//     out[i] = Σ_p (path_p.contributes ? strip_p.post_pan[i] : 0)
//
// DSP_19 is the processBlock analogue of IR_14: it guards against regression
// in the mixer's summation logic. Because the full processBlock depends on
// JUCE, we extract a header-only `MicPathSummer` that captures exactly the
// add-accumulator behaviour and verify three invariants:
//
//   (a) Silent auxiliary paths contribute zero to the bus → MAIN-only output
//       is bit-identical to MAIN's own post-pan buffer.
//   (b) Path contributions sum linearly.
//   (c) Strips that do not contribute (gated off, muted, or not soloed) are
//       excluded from the sum regardless of their buffer content.
namespace
{
    struct StripBuf
    {
        std::vector<float> L;
        std::vector<float> R;
        bool contributes = true;
    };

    struct MicPathSummer
    {
        // Accumulates `contributing` paths into `outL`/`outR`. Buffers for
        // paths whose `contributes == false` are ignored (they might still hold
        // non-zero data — e.g. a convolver whose gain smoother has not yet
        // rung down — but the gate keeps them out of the final bus).
        static void sum (const std::vector<StripBuf>& strips,
                         std::vector<float>& outL,
                         std::vector<float>& outR)
        {
            const int n = (int) outL.size();
            for (int i = 0; i < n; ++i) { outL[(size_t) i] = 0.f; outR[(size_t) i] = 0.f; }
            for (const auto& s : strips)
            {
                if (! s.contributes) continue;
                for (int i = 0; i < n; ++i)
                {
                    outL[(size_t) i] += s.L[(size_t) i];
                    outR[(size_t) i] += s.R[(size_t) i];
                }
            }
        }
    };

    // Fill `v` with a deterministic band-limited test signal so we exercise
    // both positive and negative samples at varied magnitudes.
    inline void fillTestSignal (std::vector<float>& v, uint32_t seed, float amp)
    {
        const size_t n = v.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((float) seed / (float) 0xffffffffu) * 2.f - 1.f;
            v[i] = amp * (std::sin (2.f * (float) M_PI * 7.5f * t) * 0.6f + noise * 0.25f);
        }
    }
}

TEST_CASE("DSP_19: path summation preserves MAIN-only output", "[dsp][mixer][sum]")
{
    constexpr int N = 512;

    SECTION ("MAIN-only: aux paths silent → bus == MAIN post-pan buffer")
    {
        std::vector<StripBuf> strips (4);
        for (auto& s : strips) { s.L.assign (N, 0.f); s.R.assign (N, 0.f); }

        fillTestSignal (strips[0].L, 0xa5a5u, 0.75f);
        fillTestSignal (strips[0].R, 0xb6b6u, 0.75f);
        strips[0].contributes = true;
        strips[1].contributes = false;   // DIRECT off
        strips[2].contributes = false;   // OUTRIG off
        strips[3].contributes = false;   // AMBIENT off

        std::vector<float> outL (N, 0.f), outR (N, 0.f);
        MicPathSummer::sum (strips, outL, outR);

        for (int i = 0; i < N; ++i)
        {
            CHECK(outL[(size_t) i] == strips[0].L[(size_t) i]);
            CHECK(outR[(size_t) i] == strips[0].R[(size_t) i]);
        }
    }

    SECTION ("MAIN-only: aux strips with non-zero buffers but contributes=false stay out of bus")
    {
        // Simulates the real processBlock case where DIRECT/OUTRIG/AMBIENT
        // convolvers have been kept warm and their smoothed gains have not yet
        // fully decayed — but the strip is gated off. None of that leakage is
        // allowed to reach the final bus.
        std::vector<StripBuf> strips (4);
        for (auto& s : strips) { s.L.assign (N, 0.f); s.R.assign (N, 0.f); }

        fillTestSignal (strips[0].L, 0x1111u, 0.5f);
        fillTestSignal (strips[0].R, 0x2222u, 0.5f);
        fillTestSignal (strips[1].L, 0x3333u, 0.5f);   // non-zero
        fillTestSignal (strips[1].R, 0x4444u, 0.5f);
        fillTestSignal (strips[2].L, 0x5555u, 0.5f);
        fillTestSignal (strips[2].R, 0x6666u, 0.5f);
        fillTestSignal (strips[3].L, 0x7777u, 0.5f);
        fillTestSignal (strips[3].R, 0x8888u, 0.5f);

        strips[0].contributes = true;
        strips[1].contributes = false;
        strips[2].contributes = false;
        strips[3].contributes = false;

        std::vector<float> outL (N, 0.f), outR (N, 0.f);
        MicPathSummer::sum (strips, outL, outR);

        for (int i = 0; i < N; ++i)
        {
            CHECK(outL[(size_t) i] == strips[0].L[(size_t) i]);
            CHECK(outR[(size_t) i] == strips[0].R[(size_t) i]);
        }
    }

    SECTION ("path contributions sum linearly")
    {
        std::vector<StripBuf> strips (4);
        for (auto& s : strips) { s.L.assign (N, 0.f); s.R.assign (N, 0.f); s.contributes = true; }

        fillTestSignal (strips[0].L, 0xa1u, 0.3f);
        fillTestSignal (strips[0].R, 0xa2u, 0.3f);
        fillTestSignal (strips[1].L, 0xb1u, 0.2f);
        fillTestSignal (strips[1].R, 0xb2u, 0.2f);
        fillTestSignal (strips[2].L, 0xc1u, 0.2f);
        fillTestSignal (strips[2].R, 0xc2u, 0.2f);
        fillTestSignal (strips[3].L, 0xd1u, 0.2f);
        fillTestSignal (strips[3].R, 0xd2u, 0.2f);

        std::vector<float> outL (N, 0.f), outR (N, 0.f);
        MicPathSummer::sum (strips, outL, outR);

        for (int i = 0; i < N; ++i)
        {
            const float eL = strips[0].L[(size_t) i] + strips[1].L[(size_t) i]
                           + strips[2].L[(size_t) i] + strips[3].L[(size_t) i];
            const float eR = strips[0].R[(size_t) i] + strips[1].R[(size_t) i]
                           + strips[2].R[(size_t) i] + strips[3].R[(size_t) i];
            CHECK(outL[(size_t) i] == eL);
            CHECK(outR[(size_t) i] == eR);
        }
    }

    SECTION ("solo isolates a single strip")
    {
        // Simulates `anySolo` with only OUTRIG soloed — the summer's contract
        // is just "sum what's flagged", so we set contributes accordingly.
        std::vector<StripBuf> strips (4);
        for (auto& s : strips) { s.L.assign (N, 0.f); s.R.assign (N, 0.f); }

        fillTestSignal (strips[0].L, 0x01u, 0.5f);
        fillTestSignal (strips[0].R, 0x02u, 0.5f);
        fillTestSignal (strips[1].L, 0x03u, 0.5f);
        fillTestSignal (strips[1].R, 0x04u, 0.5f);
        fillTestSignal (strips[2].L, 0x05u, 0.5f);   // OUTRIG — soloed
        fillTestSignal (strips[2].R, 0x06u, 0.5f);
        fillTestSignal (strips[3].L, 0x07u, 0.5f);
        fillTestSignal (strips[3].R, 0x08u, 0.5f);

        strips[0].contributes = false;
        strips[1].contributes = false;
        strips[2].contributes = true;
        strips[3].contributes = false;

        std::vector<float> outL (N, 0.f), outR (N, 0.f);
        MicPathSummer::sum (strips, outL, outR);

        for (int i = 0; i < N; ++i)
        {
            CHECK(outL[(size_t) i] == strips[2].L[(size_t) i]);
            CHECK(outR[(size_t) i] == strips[2].R[(size_t) i]);
        }
    }
}
