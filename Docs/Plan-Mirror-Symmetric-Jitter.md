# Plan — Mirror-symmetric ER jitter (Fix 1)

Status: **Proposed**, awaiting user approval to implement.
Owner: engine
Targets: `Source/IRSynthEngine.cpp` (only), plus regression-lock updates.

---

## 1. Background

The investigation in the chat dated 2026-04-29 traced an L/R imaging asymmetry on the Decca tree (and to a lesser extent on the spaced pair) to the per-reflection time jitter in `IRSynthEngine::calcRefs` and `calcRefsPolygon`.

The image-source method, polar patterns, distances, the Decca combine, and the centre-fill mix are all mathematically mirror-symmetric. Verified by `Tools/symmetry_probe.cpp`: with `er_only = true` and `ts ≤ 0.05` (no jitter active) the L mic IR for a left-source scene matches the R mic IR for the mirrored right-source scene to **1e-14** relative error.

The asymmetry is entirely in the random rolls inside `calcRefs` / `calcRefsPolygon`:

| Roll | Where | Range | When active |
|---|---|---|---|
| ts-driven scatter time-jitter | line 580 (rect), 992 (poly) | `±ts·4` ms | `ts > 0.05` |
| Order-dependent close-source jitter | line 585 (rect), 995 (poly) | `±jitterMs` | speakers within 30% of `min(W,D)` |
| Lambert scatter ray azimuth | line 649 (rect), 1063 (poly) | `[-π, π]` | order ∈ [1,3] rect / [1,5] poly |
| Lambert scatter ray time | line 650 (rect), 1064 (poly) | `[0, 4]` ms | same as above |

All four rolls share a single `Rng rng = mkRng(seed)` per `calcRefs` call. The seed is currently a per-`(speaker, mic)` constant:

| Path | rLL | rRL | rLR | rRR | rLC | rRC |
|---|---|---|---|---|---|---|
| MAIN (synthMainPath) | 42 | 43 | 44 | 45 | 46 | 47 |
| OUTRIG (synthExtraPath, seedBase=52) | 52 | 53 | 54 | 55 | – | – |
| AMBIENT (synthExtraPath, seedBase=62) | 62 | 63 | 64 | 65 | – | – |
| DIRECT (synthDirectPath) | 72 | 73 | 74 | 75 | 76 | 77 |

So the L mic IR uses one RNG sequence and the R mic IR uses a *different* RNG sequence. Even with mirror-symmetric geometry, the scatter pattern differs between mics → different ER peak structure → asymmetric perceived localisation.

Probe results (Vienna-like 49×19×18 m, Decca, mono source, speaker forward, source at `sx = 0.30` vs mirrored `sx = 0.70`):

| Scenario | iLL(A) peak | iLR(B) peak (mirror counterpart) | maxAbs |
|---|---|---|---|
| Full engine | 0.269 | 0.178 | 0.27 |
| Jitter off (`ts → 0`) | 0.238 | 0.238 | 8.5e-3 (FDN seed only) |
| ER-only + jitter off | 0.238 | 0.238 | **1e-14** |

The ~3.6 dB peak difference at the direct/ER cluster is entirely seed-jitter.

---

## 2. Goal

Replace the per-`calcRefs`-call sequential RNG with a **deterministic hash keyed on the image source identity**, so:

1. The same image source produces the same time-jitter / scatter values **at every mic** that observes it. This is also more physically correct — surface scatter is a property of the wall, not of the listener.
2. Under `x → W − x` mirror of source positions, the per-image-source rolls are *unchanged* (the image-source identity `(nx, ny, nz)` does not depend on x), so the mirrored geometry produces a mirrored IR exactly. Strict mirror symmetry by construction for rectangular rooms.
3. Different speakers still produce decorrelated jitter (otherwise stereo sources collapse), and Lambert scatter rays within one image source are still independently randomised.
4. Run-to-run determinism (`IR_01`) is preserved.

Non-goal: full mirror invariance for polygon rooms. `makeWalls2D` produces walls in CCW order, and mirroring x flips the winding to CW, which would relabel walls. Rectangular is the dominant case (Vienna, the user's screenshot, and the rectangular-default factory IRs all sit here). For polygons the new scheme still gives intra-speaker mic symmetry (L mic and R mic from one speaker get identical jitter for the same image source), which is the bigger fix; full polygon mirror invariance is a follow-up nice-to-have.

---

## 3. Design

### 3.1 Hash function

Add a private, file-static helper in `IRSynthEngine.cpp` (no header change):

```cpp
// SplitMix64-style avalanche mixer.
static inline uint64_t mix64 (uint64_t x) noexcept
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

// Deterministic uniform [0, 1) double from an image source identity.
//   isHash : 64-bit identity of the image source (encodes (nx,ny,nz) for
//            rectangular, or hash(wallPath) ^ nz for polygon)
//   salt   : per-speaker salt (different for L vs R speaker so stereo
//            sources don't lock-step their scatter; SAME for L mic and
//            R mic from the same speaker so they share scatter timing)
//   kind   : roll-index within the image source (0 = ts-jitter,
//            1 = order-jitter, 2+2k = scatter[k] azimuth, 3+2k = scatter[k] time)
static inline double hashU01 (uint64_t isHash, uint32_t salt, uint32_t kind) noexcept
{
    uint64_t h = isHash;
    h = mix64 (h ^ ((uint64_t) salt << 32));
    h = mix64 (h ^ (uint64_t) kind);
    // top 53 bits → uniform [0,1) double
    return (double) (h >> 11) * (1.0 / (double) (1ULL << 53));
}

static inline double hashRange (uint64_t isHash, uint32_t salt, uint32_t kind,
                                double lo, double hi) noexcept
{
    return lo + (hi - lo) * hashU01 (isHash, salt, kind);
}
```

Properties:

- Avalanche-quality mixing (SplitMix64 is widely vetted).
- Two `mix64` calls per roll ≈ ~6 ns on Apple Silicon — comparable to or cheaper than the existing `Rng::next()` (which itself does two multiply-XOR rounds).
- Pure function. No hidden state. Bit-deterministic across builds and platforms.
- Mirror-invariant input: `(nx, ny, nz)` does not change under `x → W − x` (the same lattice index points to the mirrored position), so `isHash` is identical for the original and mirrored geometry.

### 3.2 Image source identity

**Rectangular path (`calcRefs`).** Identity = `(nx, ny, nz)` packed into a 64-bit value:

```cpp
auto isIdentityRect = [] (int nx, int ny, int nz) noexcept -> uint64_t
{
    // Bias signed → unsigned with +mo offset is unnecessary because we
    // mix the cast below; mix64 absorbs the sign bit fine.
    return mix64 (((uint64_t) (uint32_t) nx)
                ^ ((uint64_t) (uint32_t) ny << 21)
                ^ ((uint64_t) (uint32_t) nz << 42));
};
```

**Polygon path (`calcRefsPolygon`).** Identity = hash of the wall path plus nz:

```cpp
auto isIdentityPoly = [] (const std::vector<int>& wallPath, int nz) noexcept -> uint64_t
{
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (int wi : wallPath)
        h = mix64 (h ^ (uint64_t) (uint32_t) wi);
    return mix64 (h ^ ((uint64_t) (uint32_t) nz << 32));
};
```

Both versions are deterministic and order-sensitive (so wallPath `{0,2,1}` and `{0,1,2}` get different hashes — correct, those are different image sources).

### 3.3 Roll-kind enumeration

Stable integer codes, captured in a comment block at the top of `calcRefs`:

```
kind 0  — ts-driven scatter time jitter  (only when ts > 0.05)
kind 1  — order-dependent close-source jitter (only when jitterMs > 0)
kind 2  — Lambert scatter[0] azimuth
kind 3  — Lambert scatter[0] time
kind 4  — Lambert scatter[1] azimuth
kind 5  — Lambert scatter[1] time
... up to kind (2 + 2*N_SCATTER - 1)
```

Polygon uses the same kind numbering. N_SCATTER differs (rect=2, poly=4) so the polygon path consumes kinds 2…9.

### 3.4 Salt convention

Each path has a single L-speaker and R-speaker salt, both shared across all mics:

| Path | L spk salt | R spk salt | Notes |
|---|---|---|---|
| MAIN | 42 | 43 | rLL/rLR/rLC use 42; rRL/rRR/rRC use 43 |
| OUTRIG | 52 | 53 | seedBase + 0/1 |
| AMBIENT | 62 | 63 | seedBase + 0/1 |
| DIRECT | 72 | 73 | rLL/rLR/rLC use 72; rRL/rRR/rRC use 73 |

In mono mode the R-speaker copy paths (`rRL := rLL`, `rRR := rLR`) still happen at the call-site level — no engine change needed there.

### 3.5 Refactor outline

`calcRefs` body:

```cpp
// REMOVE: Rng rng = mkRng(seed);

const uint32_t saltSrc = seed;   // arg now interpreted as per-speaker salt

for (nx, ny, nz) {
    // ... existing image-source position / dist / cull code ...

    const uint64_t is = isIdentityRect (nx, ny, nz);

    int t = (int) std::floor (dist / SPEED * sr);
    if (ts > 0.05)
        t = std::max (1, t + (int) std::floor (
            hashRange (is, saltSrc, 0, -ts * 4.0, ts * 4.0) * sr / 1000.0));
    const double jitterMs = (totalBounces >= 2) ? highOrderJitterMs : minJitterMs;
    if (jitterMs > 0.0)
        t = std::max (1, t + (int) std::floor (
            hashRange (is, saltSrc, 1, -jitterMs, jitterMs) * sr / 1000.0));

    // ... ER gate, polar pattern, amps[] computation unchanged ...

    refs.push_back ({ t, amps, az });

    if (p.lambert_scatter_enabled && ts > 0.05 && totalBounces >= 1 && totalBounces <= 3)
    {
        constexpr int N_SCATTER = 2;
        const double scatterWeight =
            (ts * 0.08) / (double) N_SCATTER * std::pow (0.6, totalBounces - 1);
        for (int s = 0; s < N_SCATTER; ++s)
        {
            const double scatterAz = hashRange (is, saltSrc, 2 + 2*s, -M_PI, M_PI);
            int scatterT = t + (int) std::round (
                hashRange (is, saltSrc, 3 + 2*s, 0.0, 4.0) * sr / 1000.0);
            scatterT = std::max (1, scatterT);
            if (eo && scatterT >= ec) continue;
            std::array<double,8> scatterAmps;
            for (int b = 0; b < N_BANDS; ++b) scatterAmps[b] = amps[b] * scatterWeight;
            refs.push_back ({ scatterT, scatterAmps, scatterAz });
        }
    }
}
```

`calcRefsPolygon` is mechanically identical except the identity is `isIdentityPoly(is.wallPath, nz)` and N_SCATTER = 4 (so kinds 2…9 are used).

`Rng` and `mkRng` / `rU` stay (used by `renderFDNTail` and possibly elsewhere) — `grep` confirms `mkRng` has uses outside `calcRefs`. Only the two `calcRefs` functions stop using it.

### 3.6 Call-site changes

Six call sites updated, all in `IRSynthEngine.cpp`:

```cpp
// synthMainPath (lines 1976–1999):
//   42, 43, 44, 45, 46, 47   →   42, 43, 42, 43, 42, 43
// (L spk paths get 42; R spk paths get 43; L vs R mic no longer differ.)

// synthExtraPath (lines 2526–2533) — seedBase 52 (OUTRIG) / 62 (AMBIENT):
//   seedBase+0, +1, +2, +3   →   seedBase+0, +1, +0, +1

// synthDirectPath (lines 2960–2973):
//   72, 73, 74, 75, 76, 77   →   72, 73, 72, 73, 72, 73
```

The `seedBase + 58 / 59` FDN seed slots in `synthExtraPath` are unchanged (FDN is unaffected by this work).

---

## 4. Side effects and decisions

### 4.1 Bit-identity regression locks break

The following tests will fail and need their golden values regenerated:

- **IR_11** — golden 30-sample lock on `iLL` at sample 482. Re-run `./PingTests "[capture]" -s` and paste new values into `Tests/PingEngineTests.cpp`.
- **IR_14** — multi-mic bit-identity lock in `Tests/PingMultiMicTests.cpp`. Same procedure.
- **IR_15..IR_21** (aux-path tests in `PingMultiMicTests.cpp`) — likely unaffected if they only test structural properties, but verify after the change.

Re-locking is a single commit ("regenerate IR_11/IR_14 after mirror-symmetric jitter fix") with a clear changelog note.

### 4.2 New mirror-symmetry test (proposed: `IR_22`)

Add a permanent regression in `Tests/PingEngineTests.cpp` so this can never silently break again:

```cpp
TEST_CASE ("IR_22: engine is x-mirror-symmetric for rectangular rooms",
           "[engine][symmetry]")
{
    IRSynthParams pA = smallRoomParams();
    pA.source_lx = 0.30; pA.source_rx = 0.30;   // mono-equivalent: same L/R puck
    pA.source_ly = 0.50; pA.source_ry = 0.50;
    pA.spkl_angle = M_PI_2; pA.spkr_angle = M_PI_2;  // forward, no rotation asymmetry
    pA.mono_source = true;
    pA.diffusion = 0.5;  // ts > 0.05 — guarantees jitter is active

    IRSynthParams pB = pA;
    pB.source_lx = 0.70; pB.source_rx = 0.70;

    auto rA = IRSynthEngine::synthIR (pA, [](double, const std::string&){});
    auto rB = IRSynthEngine::synthIR (pB, [](double, const std::string&){});

    REQUIRE (rA.success); REQUIRE (rB.success);
    REQUIRE (rA.irLen == rB.irLen);

    // Under mirror, scene B's iLR == scene A's iLL.
    // (FDN tail uses per-channel seeds 100/101, so the late tail will not
    // match. Restrict the comparison to the ER region [0, ec_samples].)
    const int erEnd = (int) std::floor (0.085 * rA.sampleRate);
    double maxAbs = 0.0;
    for (int i = 0; i < erEnd; ++i)
        maxAbs = std::max (maxAbs, std::fabs (rA.iLL[i] - rB.iLR[i]));

    CHECK (maxAbs < 1e-12);
}
```

(The FDN tail still has its own seed-dependent decorrelation. That's fine — the perceptual asymmetry the user reported is in the ER region. The test asserts ER mirror symmetry; FDN tail mirror symmetry is a separate, smaller issue that can be addressed independently if needed.)

### 4.3 Listening A/B before committing

Recommend extending `Tools/symmetry_probe.cpp` (or a new `Tools/jitter_ab_render.cpp`) to write four `.wav` files:

1. Old jitter, source on left
2. Old jitter, source on right
3. New jitter, source on left
4. New jitter, source on right

Convolve each with a transient impulse (clave, hi-hat) in your DAW. The new pair should sound like a clean mirror; the old pair should sound asymmetric. If the new jitter introduces any unexpected character change vs the old (e.g. dense ER feeling more "static" because L and R mics share scatter timing), we hear it before locking the change in.

This is a quick add — perhaps 1 hour of work — and worth doing before regenerating the regression locks.

### 4.4 Factory IR rebake — separate decision

Every shipped factory `.wav` is bit-identical to a particular ER scatter pattern. After the fix every `.wav` will change slightly. Per the agent guardrails in `CLAUDE.md`:

> Never regenerate factory IRs without explicit per-run confirmation.

The fix can ship to the engine without rebaking — the existing factory IRs still play correctly, they just don't reflect the new symmetric jitter. Whether to rebake is the user's decision, separate from this plan. If the answer is "yes, ship the rebake in the same release":

1. Run `build/rebake_factory_irs` (the sidecar-driven path — never overwrites `.ping`).
2. Audit the diff (file sizes, RT60 measurements) before committing.
3. Tag the version bump appropriately.

If the answer is "no, defer rebake to a later release", that is also fine and the plan is unaffected.

### 4.5 Polygon mirror symmetry is partial

For polygon rooms (`Fan / Shoebox`, `Octagonal`, `Circular Hall`, `Cathedral`), `makeWalls2D` produces walls in CCW order. Mirroring x flips this to CW, which would relabel the wall indices and therefore change `hashWallPath()`. So the L mic IR and R mic IR for one speaker ARE symmetric (same image-source identity → same hash → same jitter), but the two pucks-in-mirrored-positions case is NOT bit-symmetric for polygons.

This is acceptable for the user's reported issue (Vienna is rectangular). A follow-up task could canonicalise wall ordering (e.g. always order walls by lexicographically smallest endpoint) so polygons inherit full mirror invariance too. Tracked but not blocking.

### 4.6 Per-mic ER decorrelation

Today the L mic IR and R mic IR have **independent** ER scatter realisations. After the fix they share scatter timing for each image source. Their IRs still differ in:

- Direct-path arrival time (different mic positions)
- Per-band polar pattern (different face angles)
- Per-image-source `dist`, `az`, `el` (geometry)
- Lambert scatter ray azimuth — the *azimuth* of each scatter ray is shared between mics (it represents a virtual reflection direction, a property of the wall) but the *gain* applied at each mic is `micG(b, micPat, cosTh3D)` evaluated at that ray's azimuth, which depends on the mic face angle. So scatter rays still produce different gains at L vs R mic, just at the *same* time.

Net effect on perceived stereo width: probably negligible. The dominant ER decorrelator is geometric (different distances, different polar gains), not the scatter timing. The listening A/B in §4.3 confirms.

---

## 5. Step-by-step implementation order

Each step is independently committable; merge as one PR or four small PRs at your preference.

1. **Add `mix64` / `hashU01` / `hashRange` helpers** to `IRSynthEngine.cpp` (file-static, no header change). Compile + run existing tests; nothing should change yet (helpers are unused). [≈10 min]

2. **Refactor `calcRefs`** (rectangular path): replace the four `rU(rng, ...)` calls with `hashRange(is, salt, kind, ...)`. Build + run `PingTests`. IR_11 / IR_14 will fail; capture the new golden values and update them in `PingEngineTests.cpp` and `PingMultiMicTests.cpp`. Update `IR_11`'s onset comment. [≈45 min]

3. **Refactor `calcRefsPolygon`** (polygon path): same change with polygon image-source identity. Verify polygon-specific tests in `PingPolygonTests.cpp` still pass (they're structural, not bit-identity, so should be unaffected). [≈30 min]

4. **Update call sites** in `synthMainPath` / `synthExtraPath` / `synthDirectPath`: collapse the 6 / 4 / 6 mic-specific seeds to 2 per-speaker salts. [≈15 min]

5. **Add IR_22 mirror-symmetry test** to `PingEngineTests.cpp`. Run it and confirm `maxAbs < 1e-12`. [≈20 min]

6. **Update CLAUDE.md** — add a note in the engine section about the hash-based jitter scheme, the kind enumeration, and the salt convention. Add `IR_22` to the test inventory table. [≈15 min]

7. **(Optional) Listening A/B** per §4.3 — render four IRs, compare in DAW. [≈1 h]

8. **(Separate user decision)** Factory IR rebake per §4.4.

Total engine-only effort: ~2 hours (steps 1–6). Ship listening A/B and rebake on user approval.

---

## 6. Rollback plan

If the listening A/B reveals an unwanted character change:

- The hash scheme can be made **opt-in via a parameter** (`IRSynthParams::deterministic_jitter`, default `false`) so the old behaviour stays the default while the new one is available for users who care about symmetry. The Decca-tree code path could opt in unconditionally inside `synthMainPath` (since that's the path with the audible asymmetry) while leaving non-Decca paths on the legacy RNG.
- Or revert the four commits cleanly — the four-step structure above is designed for that.

---

## 7. Files touched

| File | Change | Lines (approx) |
|---|---|---|
| `Source/IRSynthEngine.cpp` | hash helpers + `calcRefs` + `calcRefsPolygon` + 3 call sites | +~80 net |
| `Tests/PingEngineTests.cpp` | regen IR_11 golden + add IR_22 | +~40, ±~30 |
| `Tests/PingMultiMicTests.cpp` | regen IR_14 golden | ±~30 |
| `CLAUDE.md` | doc update | +~25 |
| `Docs/Plan-Mirror-Symmetric-Jitter.md` | this file | +~280 |

No header changes. No public-API changes. No XML/preset migration.

---

## 8. Verification checklist before merge

- [ ] All Catch2 tests pass (`ctest --output-on-failure` from `build/`).
- [ ] `Tools/symmetry_probe` reports `maxAbs < 1e-12` for `iLL(A) vs iLR(B)` in the full engine + jitter case (was `0.27` before).
- [ ] `IR_22` passes.
- [ ] IR_11 onset comment matches the new captured value.
- [ ] CLAUDE.md test inventory updated.
- [ ] No regression in compilation warnings.
- [ ] (If listening A/B done) the new IRs sound subjectively as good as or better than the old, with the L/R imbalance gone.
- [ ] User has explicitly approved the factory IR rebake (or explicitly declined and accepted that factory IRs lag the engine for this release).
