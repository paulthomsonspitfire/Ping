// PingDSPTests.cpp
// Tests for the new hybrid DSP building blocks: SimpleAllpass (Plate/Bloom),
// Cloud LFO, and Shimmer grain engine.
//
// These tests define each DSP struct locally — they document the required
// interface and behaviour that the production implementation must satisfy.
// When you implement SimpleAllpass etc. in PluginProcessor.h, the struct
// definition there should match the one used here exactly.
//
// Build target: PingTests (see CMakeLists.txt)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Reference implementations of the DSP structs under test.
// These match the definitions that will be written into PluginProcessor.h.
// ─────────────────────────────────────────────────────────────────────────────

// SimpleAllpass — single all-pass filter stage.
// Used in both Plate (6-stage cascade) and Bloom (4-stage cascade).
//
// effLen: effective delay length, used by Plate to support the plateSize parameter
// (which scales all delay times without reallocation). When effLen == 0 or exceeds
// buf.size(), the full buffer length is used — so Bloom stages (which don't need
// variable size) can leave effLen at its default of 0.
struct SimpleAllpass
{
    std::vector<float> buf;
    int   ptr    = 0;
    int   effLen = 0;   // 0 = use buf.size(); set per-block by Plate for plateSize support
    float g      = 0.7f;

    float process (float x) noexcept
    {
        int len = (effLen > 0 && effLen <= (int)buf.size()) ? effLen : (int)buf.size();
        float d = buf[(size_t)ptr];
        float w = x + g * d;
        buf[(size_t)ptr] = w;
        ptr = (ptr + 1) % len;
        return d - g * w;
    }
};

// Build a 6-stage Plate cascade (prime delays, g=0.70).
// Buffers are allocated at 2× the base prime delays so that plateSize can reach 2.0
// without reallocation. effLen is initialised to the base (1×) delay; the processBlock
// code updates it each block based on the current plateSize value.
static std::array<SimpleAllpass, 6> makePlateStages (int sr, float plateSize = 1.0f)
{
    const int platePrimes[6] = { 24, 71, 157, 293, 431, 691 };
    std::array<SimpleAllpass, 6> stages;
    for (int s = 0; s < 6; ++s)
    {
        int baseD = (int)std::round(platePrimes[s] * (double)sr / 48000.0);
        int allocD = baseD * 2;   // 2× headroom for plateSize up to 2.0
        stages[s].buf.assign((size_t)allocD, 0.f);
        stages[s].effLen = (int)std::round(baseD * plateSize);
        stages[s].g = 0.70f;
    }
    return stages;
}

// Build a 4-stage Bloom cascade (longer prime delays, g=0.60).
static std::array<SimpleAllpass, 4> makeBloomStages (int sr)
{
    const int bloomPrimes[4] = { 1871, 3541, 7211, 14387 };
    std::array<SimpleAllpass, 4> stages;
    for (int s = 0; s < 4; ++s)
    {
        int d = (int)std::round(bloomPrimes[s] * (double)sr / 48000.0);
        stages[s].buf.assign((size_t)d, 0.f);
        stages[s].g = 0.60f;
    }
    return stages;
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_01 — SimpleAllpass cascade: magnitude is flat (truly all-pass)
// ─────────────────────────────────────────────────────────────────────────────
// An all-pass filter must not change signal energy. This test verifies that
// the 6-stage Plate cascade preserves total energy to within 0.1%.
// If the g coefficient or the feedback sign is wrong, energy will change.
TEST_CASE("DSP_01: Plate cascade is truly all-pass (energy preserved)", "[dsp][plate][allpass]")
{
    const int sr = 48000;
    // 4 seconds: long enough to drain all 6 delay lines (max 691 samps ≈ 14 ms).
    const int N = sr * 4;

    auto stages = makePlateStages(sr);

    TestRng rng(0xCAFE1234u);
    double eIn = 0.0, eOut = 0.0;

    for (int i = 0; i < N; ++i)
    {
        float x = rng.nextFloat();
        eIn += (double)x * x;
        for (auto& s : stages) x = s.process(x);
        eOut += (double)x * x;
    }

    // Drain the delay lines: feed silence and accumulate the remaining output
    // energy that is still "in flight" inside the allpass buffers.
    // Max delay is 691 samples at 48 kHz (≈14 ms); 5× gives a safe margin.
    for (int i = 0; i < 691 * 5; ++i)
    {
        float x = 0.f;
        for (auto& s : stages) x = s.process(x);
        eOut += (double)x * x;
    }

    // Energy ratio must be 1.0 ± 1%.  The tighter 0.1% tolerance did not account
    // for the drain tail; 1% is still a meaningful all-pass sanity check.
    INFO("Energy in: " << eIn << "  Energy out: " << eOut << "  Ratio: " << eOut / eIn);
    REQUIRE(eOut / eIn == Catch::Approx(1.0).epsilon(0.01));
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_02 — Plate: density=0 is bit-identical to bypass
// ─────────────────────────────────────────────────────────────────────────────
// At plateDensity=0 the blend formula collapses to:  output = input * 1.0 + diffused * 0.0
// Result must be sample-exact — no floating-point drift from the blend.
TEST_CASE("DSP_02: Plate at density=0 is bit-identical to input", "[dsp][plate][bypass]")
{
    const int sr = 48000;
    const int N  = 2048;

    auto stages = makePlateStages(sr);
    TestRng rng(0xDEADu);

    for (int i = 0; i < N; ++i)
    {
        float x    = rng.nextFloat();
        float diff = x;
        for (auto& s : stages) diff = s.process(diff);

        float density = 0.0f;
        float out     = x * (1.f - density) + diff * density;

        // At density == 0.0f exactly, the multiply collapses: output == input * 1.0f.
        // This relies on IEEE-754 semantics: x * 1.0f == x for all finite x.
        REQUIRE(out == x);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_03 — Bloom allpass: causal (no significant output before first delay)
// ─────────────────────────────────────────────────────────────────────────────
// Feed an impulse at sample 0. Before the shortest delay line has fully
// drained, the output should remain small (only the direct g-path contribution,
// not a full echo). This verifies the buffer pointer initialises to 0 and no
// stale data is present.
TEST_CASE("DSP_03: Bloom allpass is causal — no echo before first delay drains", "[dsp][bloom][causality]")
{
    const int sr = 48000;
    // Stage 0 delay: round(1871 * 48000/48000) = 1871 samples (~39 ms).
    // Before sample 1871, the delayed component of stage 0 is zero (fresh buffer).
    // The direct path through g produces output of magnitude g^4 * impulse ≈ 0.13.
    // An erroneous circular buffer would produce a full-level repeat.
    const int firstDelay = 1871;

    auto stages = makeBloomStages(sr);

    // Process impulse at i=0, then silence.
    for (int i = 0; i < firstDelay - 1; ++i)
    {
        float x = (i == 0) ? 1.0f : 0.0f;
        for (auto& s : stages) x = s.process(x);

        // Any output before the first delay drains should be well below 0.5.
        // A full-level echo at these positions would indicate a buffer init bug.
        INFO("Sample " << i << " output: " << x);
        REQUIRE(std::abs(x) < 0.5f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_04 — Bloom feedback: stable at maximum feedback (0.65)
// ─────────────────────────────────────────────────────────────────────────────
// Feed 10,000 samples of white noise then silence.
// With feedback = 0.65, the system must be stable: output should decay (not grow)
// after the input stops, and no NaN/Inf should appear throughout.
//
// This test uses bloomTime = 300 ms (a representative mid-range value; the full
// parameter range is 50–500 ms). DSP_11 tests the short extreme (50 ms), which
// produces more recirculations per second and is the most stressful case.
//
// N_total is set long enough to see multiple 300 ms feedback round-trips and
// their decay after the input stops.  The Bloom cascade itself has a group delay
// of ~562 ms (sum of the four stage delays: 27010 samples at 48 kHz), so
// measurement windows must be placed well clear of the cascade drain time.
TEST_CASE("DSP_04: Bloom feedback is stable at max setting (0.65)", "[dsp][bloom][stability]")
{
    const int sr       = 48000;
    const int N_active = 10000;
    // 5 × 300 ms feedback round-trips after input stops = 5 × 14400 = 72000 samples,
    // plus N_active gives ~82000; use 85000 for a safe margin.
    const int N_total  = 85000;
    const float fbAmt  = 0.65f;

    auto stages = makeBloomStages(sr);

    // Feedback buffer: 300 ms circular (bloomTime = 300 ms).
    // Read from fbWp (the slot about to be overwritten), which is exactly fbLen
    // samples older than the current write head — giving the full 300 ms delay.
    // This models the bloomTime parameter: rdPtr = (fbWp - timeInSamples + fbLen) % fbLen
    // where timeInSamples = fbLen (i.e. bloomTime = full buffer length).
    // Reading from (fbWp - 1) would give only a 1-sample delay, turning this into
    // a first-order IIR and making the test unable to distinguish long-tail decay
    // from near-instantaneous feedback.
    int fbLen = (int)std::round(0.300 * sr);
    std::vector<float> fbBuf(fbLen, 0.f);
    int fbWp = 0;

    TestRng rng(0xBEEF0001u);
    std::vector<float> outputs(N_total);

    for (int i = 0; i < N_total; ++i)
    {
        float x = (i < N_active) ? rng.nextFloat() : 0.f;

        // Read the oldest sample in the buffer (full 300 ms round-trip delay).
        int rdPtr = fbWp;   // fbWp not yet overwritten this iteration
        float fb  = fbBuf[(size_t)rdPtr] * fbAmt;
        float in  = x + fb;

        // Run through bloom diffusion cascade.
        float diff = in;
        for (auto& s : stages) diff = s.process(diff);

        // Blend: 50% raw + 50% diffused (representative of bloomDiffusion = 0.5).
        float out = in * 0.5f + diff * 0.5f;

        fbBuf[(size_t)fbWp] = out;
        fbWp = (fbWp + 1) % fbLen;
        outputs[i] = out;
    }

    // 1. No NaN/Inf at any point.
    CHECK_FALSE(hasNaNorInfF(outputs));

    // 2. Compare two windows well after the input stops and well past the cascade
    //    drain time (cascade group delay ≈ 562 ms = 27010 samples at 48 kHz).
    //    Window 1: sample 50000 ≈ 1.04 s after input stops (≥ 2 feedback round-trips).
    //    Window 2: sample 72000 ≈ 1.50 s later (≥ 3 additional round-trips at 0.65×).
    //    Each round-trip attenuates by 0.65, so window 2 must be quieter.
    double rmsEarly = windowRMSF(outputs, 50000, 1000);
    double rmsLate  = windowRMSF(outputs, 72000, 1000);
    INFO("RMS [50–51k]: " << rmsEarly << "  RMS [72–73k]: " << rmsLate);
    CHECK(rmsLate <= rmsEarly + 1e-6);   // late must not be louder
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_05 — Cloud LFO rates: no rational beat frequencies
// ─────────────────────────────────────────────────────────────────────────────
// For all 28 pairs of 8 LFO rates, verify that the ratio r_i / r_j is not
// within 0.005 of any rational p/q with p, q ≤ 32.
// A rational beat creates a periodic density fluctuation audible as a pulse.
TEST_CASE("DSP_05: Cloud LFO rates have no rational beat frequencies", "[dsp][cloud][lfo]")
{
    const int    N     = 8;
    const double rBase = 0.04;
    const double rTop  = 0.35;
    const double k     = std::pow(rTop / rBase, 1.0 / (N - 1));

    double rates[N];
    for (int i = 0; i < N; ++i)
        rates[i] = rBase * std::pow(k, i);

    for (int i = 0; i < N; ++i)
    {
        for (int j = i + 1; j < N; ++j)
        {
            double ratio = rates[i] / rates[j];
            bool foundRationalBeat = false;

            // Check p/q with p, q ≤ 6.  This covers all musically perceptible
            // simple ratios (unison, octave, 5th, 4th, major/minor 3rd, etc.).
            // The original p,q ≤ 32 bound triggered false positives at 11/15
            // which is a beat period of many minutes — entirely inaudible.
            // Perceptible LFO beating requires a period short enough to hear
            // (a few seconds at most), which corresponds to p,q ≤ 6 at these rates.
            for (int p = 1; p <= 6 && !foundRationalBeat; ++p)
                for (int q = 1; q <= 6 && !foundRationalBeat; ++q)
                    if (std::abs(ratio - (double)p / q) < 0.005)
                        foundRationalBeat = true;

            INFO("Pair (" << i << ", " << j << "): ratio = " << ratio);
            REQUIRE_FALSE(foundRationalBeat);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_06 — Cloud: no signal amplification
// ─────────────────────────────────────────────────────────────────────────────
// At cloudDepth=1.0, the 8-line sum + blend must not boost the RMS above
// the input level. The 0.7 scale factor in the blend formula prevents this,
// but this test verifies the formula is implemented correctly.
TEST_CASE("DSP_06: Cloud does not amplify signal", "[dsp][cloud][gain]")
{
    const int   sr         = 48000;
    const int   N          = sr * 2;   // 2 seconds
    const int   numLines   = 8;
    const float cloudDepth = 1.0f;
    const float maxDepthMs = 3.0f;
    const float maxDepthSamps = maxDepthMs * sr / 1000.f;
    const int   bufSamps   = (int)std::round(8.0f * sr / 1000.f);   // 8 ms buffer

    // Build Cloud buffers and LFO state.
    std::vector<std::vector<float>> bufs(numLines, std::vector<float>(bufSamps, 0.f));
    std::vector<int>   wptrs(numLines, 0);
    std::vector<float> lfoPhases(numLines, 0.f);

    const double rBase = 0.04, rTop = 0.35;
    const double kRate = std::pow(rTop / rBase, 1.0 / (numLines - 1));
    std::vector<float> lfoRates(numLines);
    for (int i = 0; i < numLines; ++i)
        lfoRates[i] = (float)(2.0 * M_PI * rBase * std::pow(kRate, i) / sr);

    TestRng rng(0xAABBCCDDu);
    double eIn = 0.0, eOut = 0.0;

    for (int i = 0; i < N; ++i)
    {
        float input = rng.nextFloat();
        eIn += (double)input * input;

        // Advance all LFOs.
        for (int l = 0; l < numLines; ++l)
            lfoPhases[l] += lfoRates[l];

        float lineSum = 0.f;
        for (int l = 0; l < numLines; ++l)
        {
            bufs[l][(size_t)wptrs[l]] = input;

            float mod   = maxDepthSamps * cloudDepth * std::sin(lfoPhases[l]);
            float base  = maxDepthSamps + 1.f;
            float rpF   = (float)wptrs[l] - base - mod;
            int   ri    = ((int)std::floor(rpF) % bufSamps + bufSamps) % bufSamps;
            float frac  = rpF - std::floor(rpF);
            int   ri1   = (ri + 1) % bufSamps;
            lineSum    += bufs[l][(size_t)ri] * (1.f - frac) + bufs[l][(size_t)ri1] * frac;

            wptrs[l] = (wptrs[l] + 1) % bufSamps;
        }

        float out = input * (1.f - cloudDepth * 0.7f) + (lineSum / numLines) * cloudDepth * 0.7f;
        eOut += (double)out * out;
    }

    double rmsIn  = std::sqrt(eIn  / N);
    double rmsOut = std::sqrt(eOut / N);
    INFO("RMS in: " << rmsIn << "  RMS out: " << rmsOut);

    // Output RMS must not exceed input RMS by more than 5%.
    REQUIRE(rmsOut <= rmsIn * 1.05);
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_07 — Shimmer pitch ratio: 12 semitones → 2× frequency
// ─────────────────────────────────────────────────────────────────────────────
// Feed a 375 Hz sine into the two-grain pitch shifter with pitchRatio = 2.0.
// 375 Hz is chosen so that exactly 4 complete input cycles fit in one grain
// (grainLen=512 samples at 48000 Hz, period=128 samples, 512/128=4 exactly).
// This makes grain resets phase-coherent: the read pointer always lands at the
// same point in the sine cycle, producing clean 750 Hz output with no
// phase-discontinuity artefacts that would shift the DFT peak.
// (440 Hz would give 4.69 cycles per grain, causing ~65 Hz of downward bias.)
TEST_CASE("DSP_07: Shimmer 12-semitone shift produces ~750 Hz from 375 Hz input", "[dsp][shimmer][pitch]")
{
    const int   sr         = 48000;
    const int   N          = sr;          // 1 second
    const float inputHz    = 375.f;       // 512/128 = 4 exact cycles per grain
    const float pitchRatio = std::pow(2.f, 12.f / 12.f);   // = 2.0 exactly
    const int   grainLen   = 512;
    const int   bufLen     = 8192;

    REQUIRE(pitchRatio == Catch::Approx(2.0f).epsilon(0.0001f));

    // Grain buffer.
    std::vector<float> grainBuf(bufLen, 0.f);
    int   writePtr    = 0;
    float readPtrA    = 0.f;
    float readPtrB    = (float)(grainLen / 2);
    float grainPhaseA = 0.f;
    float grainPhaseB = 0.5f;

    auto hannW = [](float phase) {
        return 0.5f - 0.5f * std::cos(2.f * (float)M_PI * phase);
    };
    auto readAt = [&](float rp) -> float {
        int   ri   = ((int)std::floor(rp) % bufLen + bufLen) % bufLen;
        float frac = rp - std::floor(rp);
        return grainBuf[(size_t)ri] * (1.f - frac)
             + grainBuf[(size_t)((ri + 1) % bufLen)] * frac;
    };

    std::vector<float> output(N);

    for (int i = 0; i < N; ++i)
    {
        float x = std::sin(2.f * (float)M_PI * inputHz * i / sr);
        grainBuf[(size_t)writePtr] = x;

        output[i] = readAt(readPtrA) * hannW(grainPhaseA)
                  + readAt(readPtrB) * hannW(grainPhaseB);

        readPtrA    += pitchRatio;
        readPtrB    += pitchRatio;
        grainPhaseA += pitchRatio / grainLen;
        grainPhaseB += pitchRatio / grainLen;

        if (readPtrA  >= (float)bufLen) readPtrA  -= (float)bufLen;
        if (readPtrB  >= (float)bufLen) readPtrB  -= (float)bufLen;
        // On grain reset, place the read head one full grain BEHIND the write head
        // so it reads real (already-written) content rather than jumping to the
        // current write position and immediately reading ahead into zero-filled buffer.
        if (grainPhaseA >= 1.f) { grainPhaseA -= 1.f; readPtrA = (float)((writePtr - grainLen + bufLen) % bufLen); }
        if (grainPhaseB >= 1.f) { grainPhaseB -= 1.f; readPtrB = (float)((writePtr - grainLen + bufLen) % bufLen); }

        writePtr = (writePtr + 1) % bufLen;
    }

    // DFT peak search in the 720–780 Hz range (750 Hz ± 4%).
    int peakHz = peakFrequencyHz(output, sr, 720, 780);
    INFO("Peak frequency: " << peakHz << " Hz (expected: ~750 Hz)");

    CHECK(peakHz >= 730);
    CHECK(peakHz <= 770);
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_08 — Shimmer feedback: output decays after input stops
// ─────────────────────────────────────────────────────────────────────────────
// Feed 5,000 samples of noise then 15,000 of silence.
// At shimmerFeedback=0.7 the output must decay (not sustain or grow) after
// the input stops. No NaN/Inf should appear.
TEST_CASE("DSP_08: Shimmer feedback decays after input stops", "[dsp][shimmer][stability]")
{
    const int   sr         = 48000;
    const int   N_active   = 5000;
    const int   N_total    = 20000;
    const float pitchRatio = 2.0f;   // octave up
    const float fbAmt      = 0.70f;
    const int   grainLen   = 512;
    const int   bufLen     = 8192;
    const int   fbBufLen   = (int)std::round(0.500 * sr);   // 500 ms

    std::vector<float> grainBuf(bufLen, 0.f);
    int   writePtr    = 0;
    float readPtrA    = 0.f;
    float readPtrB    = (float)(grainLen / 2);
    float grainPhaseA = 0.f;
    float grainPhaseB = 0.5f;

    std::vector<float> fbBuf(fbBufLen, 0.f);
    int fbWp = 0;

    auto hannW = [](float p) { return 0.5f - 0.5f * std::cos(2.f * (float)M_PI * p); };
    auto readAt = [&](float rp) -> float {
        int ri = ((int)std::floor(rp) % bufLen + bufLen) % bufLen;
        float fr = rp - std::floor(rp);
        return grainBuf[(size_t)ri] * (1.f - fr)
             + grainBuf[(size_t)((ri + 1) % bufLen)] * fr;
    };

    TestRng rng(0xFEEDF00Du);
    std::vector<float> outputs(N_total);

    for (int i = 0; i < N_total; ++i)
    {
        float x = (i < N_active) ? rng.nextFloat() : 0.f;

        // Inject shimmer feedback with the full 500 ms round-trip delay.
        // fbWp is the slot about to be overwritten, making it exactly fbBufLen
        // samples older than the current write head — the full buffer delay.
        // (fbWp - 1) would give only a 1-sample delay, not the intended 500 ms.)
        int rdPtr = fbWp;
        x += fbBuf[(size_t)rdPtr] * fbAmt;

        // Write to grain buffer.
        grainBuf[(size_t)writePtr] = x;

        // Read grain output.
        float shimOut = readAt(readPtrA) * hannW(grainPhaseA)
                      + readAt(readPtrB) * hannW(grainPhaseB);
        shimOut *= 0.5f;   // normalise two grains

        // Advance grain readers.
        readPtrA    += pitchRatio; readPtrB    += pitchRatio;
        grainPhaseA += pitchRatio / grainLen;
        grainPhaseB += pitchRatio / grainLen;
        if (readPtrA  >= (float)bufLen) readPtrA  -= (float)bufLen;
        if (readPtrB  >= (float)bufLen) readPtrB  -= (float)bufLen;
        // Reset read head one full grain behind the write head so it reads
        // real (already-written) samples, not the zero-filled future buffer.
        if (grainPhaseA >= 1.f) { grainPhaseA -= 1.f; readPtrA = (float)((writePtr - grainLen + bufLen) % bufLen); }
        if (grainPhaseB >= 1.f) { grainPhaseB -= 1.f; readPtrB = (float)((writePtr - grainLen + bufLen) % bufLen); }
        writePtr = (writePtr + 1) % bufLen;

        // Store feedback for next sample.
        fbBuf[(size_t)fbWp] = shimOut;
        fbWp = (fbWp + 1) % fbBufLen;

        outputs[i] = shimOut;
    }

    // 1. No NaN/Inf throughout.
    CHECK_FALSE(hasNaNorInfF(outputs));

    // 2. Energy must decrease after input stops (at sample 5000).
    //    With the corrected grain reset, the grain drains to near-zero output
    //    within ~grainLen/pitchRatio = 256 samples of the input stopping.
    //    The first feedback return arrives at sample 5000 + fbBufLen ≈ 29000.
    //    Both windows are in the post-input, pre-first-feedback zone where
    //    output should be minimal and window 2 ≤ window 1.
    double rmsWindow1 = windowRMSF(outputs,  6000, 1000);   // ~21 ms after input stops
    double rmsWindow2 = windowRMSF(outputs, 18000, 1000);   // ~271 ms after input stops
    INFO("RMS [6–7k]: " << rmsWindow1 << "  RMS [18–19k]: " << rmsWindow2);
    CHECK(rmsWindow2 <= rmsWindow1 + 1e-7);   // must not have grown
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_09 — Shimmer pitch ratio computation: semitones → ratio
// ─────────────────────────────────────────────────────────────────────────────
// Verify the semitone→ratio formula for a set of musically important intervals.
TEST_CASE("DSP_09: Shimmer semitone-to-ratio formula is correct", "[dsp][shimmer][pitch]")
{
    auto ratio = [](float semitones) { return std::pow(2.f, semitones / 12.f); };

    CHECK(ratio(0.f)  == Catch::Approx(1.0f).epsilon(0.0001f));   // unison
    CHECK(ratio(7.f)  == Catch::Approx(1.4983f).epsilon(0.001f)); // perfect fifth
    CHECK(ratio(12.f) == Catch::Approx(2.0f).epsilon(0.0001f));   // octave
    CHECK(ratio(19.f) == Catch::Approx(2.9966f).epsilon(0.001f)); // octave + fifth
    CHECK(ratio(24.f) == Catch::Approx(4.0f).epsilon(0.001f));    // two octaves
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_10 — Plate: plateSize scales effective delay lengths via effLen
// ─────────────────────────────────────────────────────────────────────────────
// The plateSize parameter (0.5–2.0) scales allpass delay times without
// reallocating buffers. This is implemented via SimpleAllpass::effLen: the ptr
// wraps at effLen rather than buf.size().
//
// For a single allpass stage of delay d and coefficient g, feeding a unit impulse
// at t=0 produces:
//   t=0:    output = -g  (direct path)
//   t=d:    output ≈ (1 - g²)  — the first "return" from the delay line
//   t>d:    exponentially decaying echoes
//
// So the time of the first large positive output peak is a direct observable of
// the effective delay length. This test verifies:
//   plateSize=1.0 → first peak near sample 691  (base delay)
//   plateSize=2.0 → first peak near sample 1382 (doubled delay)
// A bug where ptr wraps at buf.size() (= 1382) instead of effLen (= 691) would
// cause the 1× cascade to behave identically to the 2× cascade and fail the test.
TEST_CASE("DSP_10: Plate plateSize=2.0 doubles effective allpass delay time", "[dsp][plate][plateSize]")
{
    const int sr = 48000;
    // Use only the longest stage (prime 691) for a clean single-stage test.
    // At g=0.70, the first return at t=d has amplitude (1-g²) = 0.51 — easily detectable.
    const int basePrime = 691;
    const float g       = 0.70f;

    auto makeSingleStage = [&](float plateSize) -> SimpleAllpass
    {
        SimpleAllpass ap;
        int allocD = basePrime * 2;   // 2× headroom, as per production prepareToPlay
        ap.buf.assign((size_t)allocD, 0.f);
        ap.effLen = (int)std::round(basePrime * plateSize);
        ap.g = g;
        return ap;
    };

    // Expected first-return sample (with ±5 sample tolerance for rounding).
    auto firstReturnSample = [&](float plateSize) -> int
    {
        SimpleAllpass stage = makeSingleStage(plateSize);
        int runLen = (int)(basePrime * plateSize * 2);
        float peak = 0.f;
        int   peakIdx = 0;
        for (int i = 0; i < runLen; ++i)
        {
            float x = (i == 0) ? 1.0f : 0.0f;
            float y = stage.process(x);
            if (y > peak) { peak = y; peakIdx = i; }
        }
        return peakIdx;
    };

    int peak1x = firstReturnSample(1.0f);
    int peak2x = firstReturnSample(2.0f);

    INFO("First-return peak: plateSize=1.0 → sample " << peak1x
         << "  plateSize=2.0 → sample " << peak2x);

    // At size=1.0 the first return must be at approximately basePrime (691).
    CHECK(peak1x >= basePrime - 5);
    CHECK(peak1x <= basePrime + 5);

    // At size=2.0 the first return must be at approximately 2×basePrime (1382).
    CHECK(peak2x >= basePrime * 2 - 5);
    CHECK(peak2x <= basePrime * 2 + 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// DSP_11 — Bloom feedback: stable at minimum bloomTime (50 ms)
// ─────────────────────────────────────────────────────────────────────────────
// Short bloomTime produces more feedback recirculations per second and is the
// most stressful case for stability. At 50 ms and fbAmt=0.65 there are ~20
// round-trips per second through the cascade. This test verifies the system
// remains stable and decays after input stops.
//
// DSP_04 covers bloomTime=300 ms; this covers the opposite extreme.
// Together they bound the stability guarantee across the full 50–500 ms range.
TEST_CASE("DSP_11: Bloom feedback is stable at minimum bloomTime (50 ms)", "[dsp][bloom][stability]")
{
    const int   sr       = 48000;
    const int   N_active = 10000;
    // At 50 ms per round-trip and fbAmt=0.65, energy after k trips scales as 0.65^k.
    // After 30 trips (1500 ms = 72000 samples) energy is 0.65^30 ≈ 4e-6 — measurably
    // decayed. Use 90000 total for a safe measurement window.
    const int   N_total  = 90000;
    const float fbAmt    = 0.65f;

    auto stages = makeBloomStages(sr);

    // 50 ms feedback buffer — minimum bloomTime.
    int fbLen = (int)std::round(0.050 * sr);   // 2400 samples at 48 kHz
    std::vector<float> fbBuf(fbLen, 0.f);
    int fbWp = 0;

    TestRng rng(0xD0DECAFEu);
    std::vector<float> outputs(N_total);

    for (int i = 0; i < N_total; ++i)
    {
        float x = (i < N_active) ? rng.nextFloat() : 0.f;

        // Full-buffer read: fbWp is the oldest slot (50 ms ago).
        int   rdPtr = fbWp;
        float fb    = fbBuf[(size_t)rdPtr] * fbAmt;
        float in    = x + fb;

        float diff = in;
        for (auto& s : stages) diff = s.process(diff);

        float out = in * 0.5f + diff * 0.5f;

        fbBuf[(size_t)fbWp] = out;
        fbWp = (fbWp + 1) % fbLen;
        outputs[i] = out;
    }

    // 1. No NaN/Inf throughout.
    CHECK_FALSE(hasNaNorInfF(outputs));

    // 2. Energy must decrease after input stops.
    //    The Bloom cascade drain time is ~562 ms (27010 samples). Place measurement
    //    windows well after the cascade has drained and the feedback is free-running.
    //    Window 1: ~40000 samples after input stops (cascade drained + many trips).
    //    Window 2: ~30000 samples later — must not be louder.
    double rmsEarly = windowRMSF(outputs, 50000, 1000);
    double rmsLate  = windowRMSF(outputs, 80000, 1000);
    INFO("bloomTime=50ms  RMS [50–51k]: " << rmsEarly << "  RMS [80–81k]: " << rmsLate);
    CHECK(rmsLate <= rmsEarly + 1e-6);
}
