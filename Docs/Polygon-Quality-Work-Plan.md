# Polygon Room Quality — Work Plan

**Target audience:** Cursor agent (Claude Opus) picking up the implementation.
**Status:** Design agreed; ready to implement. Incremental work items, each independently shippable.
**Owner:** TBD
**Created:** 2026-04-23

---

## 1. Problem statement

Since v2.8.0 shipped polygon room geometry (Cathedral, Fan / Shoebox, Circular Hall, Octagonal) alongside the original Rectangular shape, two symptoms have emerged:

1. **Non-rectangular shapes render noticeably faster than Rectangular.** This is the inverse of what you'd expect — polygon chain validation per image is more expensive than rectangular's analytic loop, so polygon should be slower *per image*. The speed gap means polygon is producing far fewer images.
2. **Polygon IRs sound subjectively less defined, more phasey, less crisp and present** than Rectangular IRs rendered with comparable room parameters.

A feature-by-feature audit of `calcRefs` vs `calcRefsPolygon` (see `Source/IRSynthEngine.cpp`) confirmed that the two paths are identical on: Lambert scatter (Feature A), speaker directivity fade-to-omni, close-speaker jitter, 3D mic directivity + tilt, frequency-dependent scatter (Feature C), deferred allpass diffusion in `renderCh`, FDN seeding path, and RNG seeds. The two real differences are:

- **Order caps are aggressive** (`orderLimitForShape`, lines 812–820). Cathedral and Circular Hall cap at **6**; Octagonal at **8**; Fan / Shoebox at **20**. Rectangular's RT60-based cap typically lands at 30–40 for a medium hall, sometimes higher. The accepted-image count gap is 1–2 orders of magnitude.
- **Modal bank is disabled for polygon** — `applyModalBank` early-returns when `shape != "Rectangular"` (see `IRSynthEngine.cpp` line ~1114 and the gate in the blend loop at lines ~2226–2229).

Everything else is identical. The density-and-definition gap is mechanical, not a missing feature.

### Documentation drift
`CLAUDE.md` under "Order caps" documents the caps as Cathedral=16, Fan=20, Circular=12, Octagonal=14. The actual code values are 6/20/6/8. The docs are stale; the code is tighter than intended or drifted during tuning. **Resolve this explicitly** — do not leave the two out of sync after this work lands.

---

## 2. Existing test guardrails

Any change in this workstream must respect these golden locks and regression guards (see `Tests/`):

| Test | What it guards | Action for this workstream |
|------|----------------|----------------------------|
| IR_11 | Rectangular MAIN path, 30-sample golden at onset 482 | **Must not change.** Rectangular is bit-identical to pre-2.8.0 and nothing here touches `calcRefs`. |
| IR_14 | MAIN path full-IR digest | **Must not change.** Same reasoning as IR_11. |
| IR_17, IR_18, IR_19 | DIRECT path behaviour, figure-8 null, etc. | **Must not change.** Polygon work doesn't touch `synthDirectPath` invariants. |
| IR_22 | Decca OFF = struct default | **Must not change.** |
| IR_23, IR_24, IR_25, IR_26 | Decca ON / toe-out / cx-shift / direct_max_order | **Must not change.** |
| IR_27 | Rectangular polygon-dispatch bit-identity lock | **Must not change.** The rectangular branch of the dispatch lambda must stay bit-identical, so no WI here is allowed to alter `calcRefs` itself. |
| IR_28 | Cathedral ≠ Rectangular | May need golden recapture after WI-1..WI-4. Confirm l2 > 0 still holds. |
| IR_29 | Fan taper=0 ≈ Rectangular; taper=0.5 ≠ | May need golden recapture. |
| IR_30 | Circular Hall geometry | May need golden recapture. |
| IR_31 | Octagonal regular-octagon side-length invariant | **Unchanged** — test is geometric, not acoustic. |
| DSP_22 | `reflect2D` / `rayIntersectsSegment` / `polygonArea` / `polygonPerimeter` / `makeWalls2D` | **Unchanged** — utilities are stable. |

### Re-capture discipline
IR_28/29/30 use coarse l2 / energy comparisons, not digest locks. If their assertions still pass after a WI lands, ship without touching the test. If a WI tightens or loosens output energy enough that the inequalities flip, update the assertion thresholds with a brief note in the test comment explaining why.

### Build / run tests
```bash
cd /Users/paulthomson/Cursor\ wip/Ping
cmake -B build -S .
cmake --build build --target PingTests
cd build && ctest --output-on-failure
```

---

## 3. Work items

Items are listed in priority order. WI-1 is the largest lever; it alone is expected to close most of the subjective gap. WI-2 through WI-4 are cheap force-multipliers or safety nets. WI-5 and WI-6 are exploratory. WI-7 is hygiene.

Each item is independently shippable. Batching WI-1..WI-4 into one release is sensible.

---

### WI-1 — Raise order caps with image-count budget

**Scope:** Replace the pure per-shape order cap with a combined (order cap, total-image budget) stop condition in `calcRefsPolygon`.

**Files:**
- `Source/IRSynthEngine.cpp` — `orderLimitForShape` (lines 812–820), `generateIS2D` (find via `grep -n generateIS2D`), `calcRefsPolygon` (lines 829+)

**Changes:**
1. Raise per-shape caps to the CLAUDE.md-documented values, which is where the design intent sat:
   - Fan / Shoebox: 20 → **24**
   - Octagonal: 8 → **14**
   - Circular Hall: 6 → **12**
   - Cathedral: 6 → **16**
2. Add an `acceptedImageBudget` parameter threaded through `calcRefsPolygon` → `generateIS2D`. Early-out when `refs.size() >= budget`. Suggested starting budget: **20_000** per channel-direction call (4 calls per MAIN → ~80k total horizontal images, still well within background-thread budget).
3. Measure the actual accepted-image count at order cap for each shape on the default room and log it at DEBUG. If any shape still hits the order cap with headroom under budget, consider raising further.

**Rationale:** Chain validation prunes so aggressively that worst-case `(n-1)^order` branching rarely materialises. A budget-based stop gives convex shapes (Fan, Octagonal) naturally deeper trees than concave (Cathedral) without hand-tuned per-shape caps being the sole lever. The order caps remain as a safety ceiling.

**Risk:** Synthesis time increases. Worst case is Cathedral at mo=16 on a small, long-RT60 room — estimate from a calibration run before committing final numbers. Target: polygon synthesis should be **slower than or comparable to** rectangular on the same room parameters, not faster.

**Test impact:** IR_28/29/30 may shift; re-verify thresholds still pass. IR_11/14/17/18/19/22/27 untouched.

**Acceptance:**
- Cathedral default-room synthesis time ≥ 80% of rectangular synthesis time on the same room (no longer "markedly faster").
- Listen test on a reference Cathedral preset: noticeably denser late ER, less audible periodic phasiness.
- All tests pass.

---

### WI-2 — Polygon-tuned Lambert scatter

**Scope:** Increase diffuse-fill density for polygon specifically, without touching the rectangular branch.

**Files:**
- `Source/IRSynthEngine.cpp` — the Lambert scatter block inside `calcRefsPolygon` (search for `N_SCATTER` — the match around line 992–997 is the polygon copy; the one at 634–637 is the rectangular copy and must not be touched).

**Changes:**
1. Raise polygon `N_SCATTER` from `2` to **`4`**. Leave rectangular at 2.
2. Widen polygon scatter order range from `[1, 3]` to **`[1, 5]`** (condition on `totalBounces`).
3. Keep the existing amplitude formula `(ts * 0.08) / N_SCATTER * 0.6^(order-1)` — the `/N_SCATTER` normalisation keeps per-scatter-ray amplitude constant, so total diffuse energy doubles vs the 2-ray version, which is what we want.

**Rationale:** Lambert scatter is the primary gap-filler between specular spikes. With polygon's lower specular density (even after WI-1), the gaps are wider and more exposed. Doubling scatter rays and extending to order 5 plugs those gaps directly. This is cheap CPU-wise because scatter rays bypass chain validation (they're spawned from already-accepted specular images).

**Risk:** Too much diffuse energy could muddy definition in the opposite direction. Listen test before and after; if the result sounds washy, dial N_SCATTER back to 3.

**Test impact:** IR_28/29/30 energies shift upward; thresholds probably still pass. Keep `lambert_scatter_enabled=true` pinned in IR tests that check engine-toggle behaviour (currently IR_11/14/17/18/19/22 pin this explicitly — no change needed).

**Acceptance:** Subjective density lift on Cathedral / Circular Hall. No muddying of HF definition.

---

### WI-3 — Flatten Lambert order decay for polygon

**Scope:** Tune the per-order decay factor on Lambert scatter for polygon's truncated order range.

**Files:**
- `Source/IRSynthEngine.cpp` — same scatter block in `calcRefsPolygon` as WI-2.

**Changes:**
- Change the `0.6` decay base to **`0.75`** in the polygon branch only. (Rectangular stays at 0.6.)

**Rationale:** The `0.6^(order-1)` decay is tuned for rectangular's deep order range where late diffuse contributions correctly fade into the FDN takeover. At polygon's order cap (even at the raised WI-1 values), you're cutting diffuse energy aggressively exactly where you most need fill. `0.75^(order-1)` preserves more scatter energy at orders 4–6 without disturbing the shape of the decay.

**Risk:** Minor. Total scatter energy rises modestly; perceptible density without over-egging.

**Test impact:** Combine with WI-2 in the same listen test. Tests likely pass without threshold adjustment.

**Acceptance:** Listen test on long-tail Cathedral preset shows smoother late ER → FDN transition.

---

### WI-4 — Relax chain-validation and planar-image tolerances

**Scope:** Widen the floating-point tolerances in polygon image validation to recover borderline-valid reflections.

**Files:**
- `Source/IRSynthEngine.cpp` — `validateChain2D` (the `t ∈ [1e-9, 1)` interval test) and `generateIS2D` (the `dot > -1e-9` cut-off at line ~784).

**Changes:**
1. Widen `validateChain2D` `t` interval from `[1e-9, 1)` to **`[1e-6, 1 + 1e-6)`**.
2. Widen `generateIS2D` planar-image cut-off from `dot > -1e-9` to **`dot > -1e-6`**.

**Rationale:** Both tests use nanounit tolerances, which is overkill for geometry at millimetre physical scale. Rays that clip wall corners or grazing source–receiver lines that are effectively coplanar with a wall get culled by rounding error. Widening to µm-scale tolerance is well inside the noise floor of the underlying room geometry (which the user specifies to 1 decimal metre) but recovers a non-trivial fraction of borderline images.

**Risk:** Very small. If phantom images slip through, they'd manifest as off-axis arrivals — but the chain validation still walks the full path, so a phantom would need to be consistent with every intermediate wall in the tree, which is mathematically unlikely.

**Test impact:** Instrument the image-count path in debug and compare before/after on each shape. Expect a 5–20% uplift in accepted images at the same order cap.

**Acceptance:** Accepted-image count rises measurably; no phantom artefacts in listen tests; tests pass.

---

### WI-5 — Equivalent-box modal bank for polygon (exploratory)

**Scope:** Re-enable `applyModalBank` for polygon shapes using an equivalent-volume box approximation of the polygon's bounding geometry.

**Files:**
- `Source/IRSynthEngine.cpp` — `applyModalBank` (line ~1114) and its call site in the blend loop (lines ~2226–2229).

**Changes:**
1. In `applyModalBank`, for non-rectangular shapes: compute `Wₑ = √(polygonArea)`, `Dₑ = polygonArea / Wₑ`, `Hₑ = height × hm` (already computed as `He` elsewhere). Feed these to the existing axial-mode formula `f_n = c × n / (2L)` for L ∈ {Wₑ, Dₑ, Hₑ}, n ∈ {1..4}.
2. Gate the new code path on a `#if` compile-time toggle (`PING_POLYGON_MODAL_BANK`) so it can be disabled trivially if the listen test reveals issues. Default **on**.

**Rationale:** The real reason modal bank is disabled for polygon is that exact axial modes aren't geometrically defined for non-rectangular plans. But the low-frequency tonal solidity the bank contributes is a *psychoacoustic* asset, not a physics-exact one — the brain hears "there's a low-frequency resonant presence" and accepts the room as weighty. An equivalent-box approximation at area-preserving dimensions places modes in roughly the right frequency range for the space's actual size, even if the exact frequencies aren't physical.

**Risk:** Moderate. If the approximation places modes badly, the result could be tonally off-colour rather than solid. The `#if` gate is the rollback.

**Test impact:** IR_28/29/30 energies shift in the sub-250 Hz band. Thresholds likely still pass. Add a sanity-check test asserting that the modal bank contribution is zero for the default Rectangular room (guards against accidentally changing rectangular behaviour).

**Acceptance:** Listen test on small Circular Hall / Octagonal presets shows added low-end solidity without booming or tonal colouration. If the result is ambiguous, ship with the `#if` gate off by default and leave as a documented opt-in.

---

### WI-6 — Raise FDN `kMaxGain` for polygon

**Scope:** Let the FDN level-match carry more of the tail energy when ER is sparse.

**Files:**
- `Source/IRSynthEngine.cpp` — `synthMainPath` blend loop, `kMaxGain` constant (currently 16, resulting in `kMinGain = 1/16`).

**Changes:**
- When `p.shape != "Rectangular"`, raise `kMaxGain` from **16 → 32** (+30 dB). `kMinGain` follows as `1/kMaxGain`.

**Rationale:** The `fdnGain` level-match formula measures ER RMS in `[ecFdn-xfade, ecFdn]`. With polygon's sparser late-ER, that RMS is lower, so the gain target to match ER↔FDN energy is higher. The `16` clamp binds more frequently for polygon, leaving the tail audibly thin relative to the ER onset. Doubling the ceiling gives the level-match more headroom without changing the formula.

**Risk:** Low. The FDN is stable to much higher gains; the clamp exists to prevent runaway from calibration edge cases.

**Test impact:** FDN-related tests (if any) unaffected. IR_28/29/30 may shift; verify.

**Acceptance:** Tail level on far-mic Cathedral / Circular Hall presets matches ER onset more convincingly.

---

### WI-7 — Documentation reconciliation

**Scope:** Bring `CLAUDE.md` and the code into sync on order caps and modal bank behaviour.

**Files:**
- `CLAUDE.md` — the "Order caps" table under the polygon section, and the "Shape-aware gates in the late-reverb path" section.

**Changes:**
1. Update the order-cap table to the WI-1 final values.
2. Document the WI-2 Lambert scatter polygon override.
3. Document WI-3's per-shape `0.75` decay base.
4. Document WI-4's tolerance widening.
5. If WI-5 ships, document the equivalent-box modal bank and the `PING_POLYGON_MODAL_BANK` gate.
6. If WI-6 ships, document the polygon `kMaxGain` override.
7. Add a brief "Why polygon used to sound sparser" note under "Key design decisions — polygon room geometry" referencing this work plan.

**Acceptance:** No drift between code and docs. Diff the updated CLAUDE.md section against the final code values as a review gate.

---

## 4. Suggested sequencing

**Batch 1 — ship together (low risk, high impact):**
- WI-1 (order caps + budget)
- WI-2 (polygon Lambert N_SCATTER + range)
- WI-3 (polygon Lambert decay)
- WI-4 (chain-validation tolerances)
- WI-7 (docs — partial, covering WI-1..WI-4)

**Batch 2 — ship after Batch 1 is validated:**
- WI-5 (equivalent-box modal bank) behind compile-time gate
- WI-6 (polygon FDN kMaxGain)
- WI-7 (docs — remaining)

Reasoning: Batch 1 addresses the primary mechanical gap (image density). Listen-test results from Batch 1 should inform whether WI-5 and WI-6 are necessary or risk over-correcting.

---

## 5. Validation protocol

For each batch:

1. **Build + test:**
   ```bash
   cd /Users/paulthomson/Cursor\ wip/Ping
   cmake -B build -S .
   cmake --build build --config Release
   cmake --build build --target PingTests
   cd build && ctest --output-on-failure
   ```
2. **Measure synthesis time** on the default Cathedral, Circular Hall, Octagonal, and Fan / Shoebox rooms. Record before/after. Expected result: polygon shapes are now ≥ 80% of rectangular synthesis time (no longer markedly faster).
3. **Measure accepted image count** per shape at the default room. Log at DEBUG. Record before/after.
4. **Regenerate factory IRs:**
   ```bash
   g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
       Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
       -o build/generate_factory_irs -lm
   ./build/generate_factory_irs Installer/factory_irs Installer/factory_presets
   python3 Tools/trim_factory_irs.py Installer/factory_irs
   ```
   Verify factory IRs still open cleanly (WAVE_FORMAT_EXTENSIBLE + `dwChannelMask=0x33` — see CLAUDE.md "`makeWav` must use WAVE_FORMAT_EXTENSIBLE" key decision).
5. **Listen-test matrix** — three presets per polygon shape, A/B against the pre-batch build:
   - Cathedral: small, medium, large rooms
   - Circular Hall: small, medium, large
   - Octagonal: small, medium, large
   - Fan / Shoebox: small, medium, large
6. **Rectangular regression listen test** — load three existing Rectangular factory presets and confirm no audible change (should be bit-identical by IR_11/14 guards, but listen anyway as a sanity check).

---

## 6. Release checklist

- [ ] All tests pass (`ctest --output-on-failure`)
- [ ] Factory IRs regenerated and trimmed
- [ ] Factory IRs verified to load in the plugin (waveform renders, no QuickTime rejection)
- [ ] Listen-test matrix completed with pass from user
- [ ] `CLAUDE.md` updated to match final code
- [ ] Version bumped in both `CMakeLists.txt` (`project(Ping VERSION ...)`) and `Installer/build_installer.sh` (`VERSION`)
- [ ] Installer built: `cmake --build build --target installer`
- [ ] Release note drafted mentioning: "Polygon room shapes (Cathedral, Circular Hall, Octagonal, Fan / Shoebox) now render with significantly denser and more defined early reflections, closer to the Rectangular shape's quality."

---

## 7. Rollback strategy

Each WI is independently revertible via `git revert` of its commit. WI-5 additionally has a compile-time gate (`PING_POLYGON_MODAL_BANK`). WI-1's order caps can be reverted in isolation from WI-2/3/4 without cross-dependency.

If Batch 1 ships and reveals an unexpected regression on a specific polygon shape, the fastest fix is to revert WI-1's per-shape cap for that shape while leaving WI-2/3/4 in place — density from scatter and recovered borderline images is preserved, and only the order-cap lever is tuned back.

---

## 8. Open questions / future work (not in scope)

These are documented here so they don't get lost but are **not** part of this workstream:

- **Distance-based pruning** instead of pure order cap — would let short-path high-order images in while still bounding work. More invasive; defer.
- **Statistical late-reverb fill** (random reflections with decay matching expected statistics) for orders above the cap — genuinely exploratory; defer.
- **Per-wall absorption differentiation in polygon** (currently all polygon walls share one material). Would require UI changes to pick per-wall materials; defer.
- **L-shaped room reintroduction** — dropped in v2.8.0 for CPU reasons. If WI-1's budget approach proves robust, L-shaped could return with the same budget guard.

---

## 9. References

- `Source/IRSynthEngine.cpp` — `calcRefs`, `calcRefsPolygon`, `generateIS2D`, `validateChain2D`, `orderLimitForShape`, `applyModalBank`, `renderFDNTail`, `synthMainPath`
- `Tests/PingPolygonTests.cpp` — DSP_22, IR_27..IR_31
- `Tests/PingEngineTests.cpp` — IR_11, IR_14
- `CLAUDE.md` — "Polygon room geometry (v2.8.0)" section
- `Docs/Polygon-Room-Geometry-Plan.md` — original v2.8.0 implementation plan (for context on design intent)
