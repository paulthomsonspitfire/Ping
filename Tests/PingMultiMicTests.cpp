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
