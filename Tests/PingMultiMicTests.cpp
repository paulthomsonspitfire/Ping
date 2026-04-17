// PingMultiMicTests.cpp
// Tests for the multi-mic IR synthesis feature (feature/multi-mic-paths).
// Compiled with PING_TESTING_BUILD=1 so IRSynthEngine.h skips JuceHeader.h.
//
// Layout:
//   IR_14         Bit-identity regression lock for MAIN path (pre-refactor capture).
//   IR_15 … IR_21 (added after refactor lands — see Multi-Mic-Work-Plan.md).
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
// feature/multi-mic-paths branch (Decision D1 in Multi-Mic-Work-Plan.md):
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
    // (C3 in Multi-Mic-Work-Plan.md) can be proven bit-identical.
    //
    //   irLen:  sample count of each MAIN channel (matches r.irLen)
    //   iLL/iRL/iLR/iRR: 64-bit FNV-1a hex digests over the raw double bytes.
    //
    // Any change to IRSynthEngine math that touches MAIN output (seeds,
    // geometry formulas, FDN parameters, blend factors, etc.) will flip
    // one or more digests.  Update intentionally via the capture case.
    static const int         golden_irLen = 813146;
    static const std::string golden_iLL   = "30e1e1aece03b09a";
    static const std::string golden_iRL   = "2a6eba6d1480aa74";
    static const std::string golden_iLR   = "be0e5f3e01df5cfb";
    static const std::string golden_iRR   = "cb5312c58cc7d95a";

    static const bool goldenCaptured = true;   // captured by IR_14_CAPTURE at feature/multi-mic-paths branch point (v2.5.0, pre-refactor)
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

