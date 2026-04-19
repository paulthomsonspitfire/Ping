# P!NG — Multi-Mic IR Synthesis: Work Plan

**Branch:** `feature/multi-mic-paths`
**Source of truth for behaviour:** `Multi-Mic-Implementation-Brief.md`
**Source of truth for codebase conventions:** `CLAUDE.md`
**This document:** concrete task breakdown, checkpoint ordering, and test plan.

---

## Resolved decisions (ratified before work starts)

| # | Decision | Implication |
|---|---|---|
| D1 | **Belt-and-braces bit-identity.** The refactored `synthIR` must produce bit-identical MAIN output. Add a new test **IR_14** that captures the full IR (all 4 channels, every sample) hashed, then re-asserts it post-refactor. IR_11 continues unchanged. | Any non-trivial FP reordering in `synthIR` (including std::async-mediated side effects) must be avoided. The dispatcher calls its path lambdas in *predictable* order; result collection is sequential. |
| D2 | **DIRECT inherits MAIN mic pattern and mic angles.** `p.mic_pattern`, `p.micl_angle`, `p.micr_angle` are reused. No new params for DIRECT. | `synthDirectPath` passes the same mic pattern/angle to `calcRefs` as the MAIN path; only the reflection order (max 0) differs. |
| D3 | **Defer factory IR regeneration.** Phase 6 is removed from this branch. `Tools/generate_factory_irs.cpp` changes are out of scope. | Existing 27 factory IRs keep working unchanged (MAIN-only). OUTRIG/AMBIENT default off on every loaded factory IR. Installer size unchanged. |
| D4 | **`er_only` is global** — applies to MAIN, OUTRIG, AMBIENT uniformly. DIRECT is implicitly ER-only (order-0 only). | No `outrig_er_only` / `ambient_er_only` fields. Existing APVTS/state stays minimal. |
| D5 | **Hard HP toggle** (no crossfade). Users toggle in non-playing moments. | `HP2ndOrder::enabled` read per-block; no SmoothedValue. Filter state still runs continuously to avoid startup transients on re-enable. |

---

## Repository layout for this work

Additions (new files):
- `Source/MicMixerComponent.h`, `Source/MicMixerComponent.cpp` — 4-strip mixer, replaces `OutputLevelMeter` in the main UI.
- `Source/HP2ndOrder.h` — small header-only 2nd-order Butterworth high-pass (shared by all 4 strips).
- `Tests/PingMultiMicTests.cpp` — new Catch2 test file for IR_14 … IR_21 and DSP_15 … DSP_19 (see test plan below).

Modified:
- `Source/IRSynthEngine.{h,cpp}` — Phase 1.
- `Source/PluginProcessor.{h,cpp}` — Phase 2.
- `Source/FloorPlanComponent.{h,cpp}` — Phase 3.
- `Source/IRSynthComponent.{h,cpp}` — Phase 4.
- `Source/PluginEditor.{h,cpp}` — Phase 5.
- `CLAUDE.md` — final doc update once behaviour is locked.
- `CMakeLists.txt` — add new source + test files; bump project version to `2.6.0-dev`.

Unchanged (explicitly out of scope for this branch):
- `Tools/generate_factory_irs.cpp` and all `Installer/factory_irs/*.wav` content (D3).
- `Tools/trim_factory_irs.py` (already handles arbitrary `.wav` files).

---

## Phase 1 — IRSynthEngine

### 1.1 Extend `IRSynthParams`

Per brief §1a. Additions only, with defaults set to `*_enabled = false` so old sidecars with missing fields reproduce current behaviour.

### 1.2 Extend `IRSynthResult`

Per brief §1b. `MicIRChannels { LL, RL, LR, RR, irLen, synthesised }`. Existing `iLL/iRL/iLR/iRR` preserved as the MAIN channels.

### 1.3 Refactor `synthIR` into a path dispatcher

Introduce three private statics:

```cpp
static IRSynthResult synthMainPath  (const IRSynthParams& p, IRSynthProgressFn cb);
static MicIRChannels synthExtraPath (const IRSynthParams& p,
                                     double rlx, double rly, double rrx, double rry,
                                     double rz, double langle, double rangle,
                                     const std::string& pattern,
                                     uint32_t seedBase,
                                     IRSynthProgressFn cb);
static MicIRChannels synthDirectPath(const IRSynthParams& p);  // no progress — fast
```

`synthMainPath` is the **current `synthIR` body moved verbatim**, reading `p.receiver_*`, `p.micl_angle`, `p.micr_angle`, `p.mic_pattern`, `rz = std::min(3.0, He * 0.9)`. The existing seed set (42–45 for cross-paths, 100/101 for FDN) is retained.

`synthExtraPath` is the same body but reads the passed-in receiver geometry. Uses its own seed base (OUTRIG: 52/53/54/55 + 110/111; AMBIENT: 62/63/64/65 + 120/121) so MAIN output is unchanged and extra paths have distinct diffuse fields.

`synthDirectPath`:
- Takes `p.mic_pattern` and `p.micl_angle`/`p.micr_angle` (D2).
- Calls `calcRefs` four times with `maxOrder = 0`. **New param `maxOrder`** is added to `calcRefs` (default `INT_MAX` or unchanged behaviour if not supplied). At `maxOrder = 0`, only the direct ray survives (the nested image-source loops clamp their upper bound).
- For each cross-path, `renderCh` is called but with `diffusion = 0.0`, `freqScatterMs = 0.0`, and **`irLen` clamped to the minimum necessary** — `ceil(maxDirect_s * sr) + 512` samples (enough for the farthest direct arrival plus the 8-band bandpass-filter tail). This gives DIRECT IRs ~10–50 ms long instead of 8 s.
- Skip `applyModalBank` (no modes matter for an impulse) and the 500 ms end fade (IR is too short). Keep the +15 dB `gain15dB` scalar at the end so DIRECT levels match MAIN's convention.
- Returns a `MicIRChannels{ LL, RL, LR, RR, irLen, synthesised = true }`.

### 1.4 Parallel dispatcher

```cpp
IRSynthResult IRSynthEngine::synthIR (const IRSynthParams& p, IRSynthProgressFn cb)
{
    std::atomic<double> mainProg{0}, outrigProg{0}, ambientProg{0};
    std::mutex cbMutex;  // serialises the user's callback; deterministic output is unaffected

    auto updateProgress = [&](const std::string& msg) {
        const double w = 0.60 * mainProg.load()
                       + 0.18 * outrigProg.load()
                       + 0.18 * ambientProg.load();
        if (cb) { std::lock_guard<std::mutex> lk(cbMutex); cb(std::min(w + 0.04, 1.0), msg); }
    };

    auto mainFut = std::async(std::launch::async, [&]{
        return synthMainPath(p, [&](double f, const std::string& m){ mainProg = f; updateProgress(m); });
    });
    std::future<MicIRChannels> outrigFut, ambientFut, directFut;
    if (p.outrig_enabled)
        outrigFut = std::async(std::launch::async, [&]{
            return synthExtraPath(p, p.outrig_lx, p.outrig_ly, p.outrig_rx, p.outrig_ry,
                                  p.outrig_height, p.outrig_langle, p.outrig_rangle, p.outrig_pattern,
                                  /*seedBase*/ 52,
                                  [&](double f, const std::string& m){ outrigProg = f; updateProgress(m); });
        });
    if (p.ambient_enabled)
        ambientFut = std::async(std::launch::async, [&]{
            return synthExtraPath(p, p.ambient_lx, p.ambient_ly, p.ambient_rx, p.ambient_ry,
                                  p.ambient_height, p.ambient_langle, p.ambient_rangle, p.ambient_pattern,
                                  /*seedBase*/ 62,
                                  [&](double f, const std::string& m){ ambientProg = f; updateProgress(m); });
        });
    if (p.direct_enabled)
        directFut = std::async(std::launch::async, [&]{ return synthDirectPath(p); });

    IRSynthResult r = mainFut.get();      // MAIN is authoritative: rt60, irLen, sampleRate, success
    if (outrigFut.valid())  r.outrig  = outrigFut.get();
    if (ambientFut.valid()) r.ambient = ambientFut.get();
    if (directFut.valid())  r.direct  = directFut.get();
    if (cb) cb(1.0, "Done.");
    return r;
}
```

**Determinism note (D1):** each path's internal math is deterministic and receives the same seeds regardless of thread scheduling. The mutex only serialises user progress callbacks. MAIN output is bit-identical to the pre-refactor engine.

### 1.5 `makeWav` unchanged

`makeWav` is called per path in `IRSynthComponent` after synthesis completes (Phase 4.4). No engine changes here.

---

## Phase 2 — PluginProcessor (DSP + state)

### 2.1 `HP2ndOrder` (new header)

`Source/HP2ndOrder.h`, 2nd-order Butterworth HP at configurable `fc`. Members: `b0,b1,b2,a1,a2,x1[2],x2[2],y1[2],y2[2]`, `bool enabled`. Compute biquad coefficients in `prepare(fc, sr)` using the canonical RBJ formulas. `process(x, ch)` always runs through the biquad (state continuous); the `enabled` switch only selects the output — when disabled, return the input sample but continue to clock the biquad state. This eliminates clicks on re-enable.

### 2.2 New APVTS parameters

Per brief §2a. Added to `createParameterLayout()` **in the order listed in the brief**. All gains use `NormalisableRange<float>(-48.f, 6.f)`, pans `(-1.f, 1.f)`, bools default per D5. Use the `IDs::` namespace convention (add `mainGain`, `mainPan`, `mainMute`, `mainSolo`, `mainHPOn`, `mainOn`, plus `direct*`, `outrig*`, `ambient*` mirrors).

**Critical:** `mainOn`, `directOn`, `outrigOn`, `ambientOn` must **not** trigger `loadSelectedIR()` from `parameterChanged`. Only `stretch` and `decay` do. The existing listener in `PluginEditor::parameterChanged` already only reacts to those two; we just must not expand the list.

### 2.3 New convolvers

Exactly as the brief specifies (§2b): 4 DIRECT + 8 OUTRIG + 8 AMBIENT = 20 new convolvers, for 28 total. Declared in `PluginProcessor.h`.

**All 20 new convolvers must be:**
- `prepare()`'d in `prepareToPlay` (same spec as `tsErConvLL` etc.).
- `reset()` in both `releaseResources` and `prepareToPlay`.
- Included in the `audioEnginePrepared` / `deferConvolverLoad` guard (brief R4).

### 2.4 Per-path SmoothedValues and peaks

Per brief §2c, §2d. `mainGainSmooth`, `directGainSmooth`, `outrigGainSmooth`, `ambientGainSmooth` for gain (20 ms ramp); same for pan. Peaks are `std::atomic<float>` L/R pairs per path.

### 2.5 `MicPath` enum + `loadIRFromBuffer` extension

Per brief §2f. `MicPath::Main` is default and preserves existing behaviour. DIRECT bypasses silence-trim, decay envelope, stretch, reverse, and ER/Tail split — it's treated as a short specialised IR and loaded into all four DIRECT convolvers as-is. OUTRIG and AMBIENT go through the full pipeline identical to MAIN's.

New helper: `loadMicPathFromFile(const juce::File& baseIRFile, MicPath path)` that derives the suffix filename, checks `existsAsFile()`, and returns silently if absent (for backward compat).

### 2.6 Per-path `rawSynthBuffer`s

Per brief §2j. `rawSynthBuffer` (MAIN, unchanged), `rawSynthDirectBuffer`, `rawSynthOutrigBuffer`, `rawSynthAmbientBuffer`. Shared `rawSynthSampleRate`. `reloadSynthIR()` calls `loadIRFromBuffer` per non-empty buffer.

### 2.7 IR load fade — convert to sample-based

The brief (§2h) calls this out as a risk point. Replace:

```cpp
std::atomic<int> irLoadFadeBlocksRemaining { 0 };
static constexpr int kIRLoadFadeBlocks = 64;
```

with:

```cpp
std::atomic<int> irLoadFadeSamplesRemaining { 0 };
static constexpr int kIRLoadFadeSamples = 96000;   // ~2 s at 48 kHz
```

`processBlock` decrements by `numSamples`; fade is `1 - remaining / (float)kIRLoadFadeSamples`. Armed in `loadIRFromBuffer` **before the first `loadImpulseResponse` call**, regardless of which path is loading. This is the only block-count→sample-count migration; CLAUDE.md already documents that the sample-based approach is what we want.

### 2.8 `processBlock` — mixer insertion

The single biggest DSP risk. Current structure (abbreviated):

```
[Plate/Bloom/Cloud/Shimmer inject into buffer]
[tsEr* and tsTail* convolve buffer → lEr/rEr/lTail/rTail; ER/Tail crossfeed; sum with trueStereoWetGain, write back to buffer]
[EQ → decorrelation → LFO → Width → Chorus → post-conv Cloud → Shimmer (pre-conv) → Output Gain → spectrum → dry/wet → Bloom Volume]
```

After change:

```
[Plate/Bloom/Cloud/Shimmer] — unchanged
[Compute lIn/rIn once]
[MAIN path]
    tsEr*/tsTail* → erLevel/tailLevel → ER/Tail crossfeed → trueStereoWetGain → sum → mainPathBuf(L/R)
    mainHP.process (if mainHPOn) → mainGainSmooth → pan → mainPeakL/R
[DIRECT path] (if directOn)
    tsDirect* convolve lIn/rIn → directBuf(L/R) → directHP → directGain → pan → directPeakL/R
[OUTRIG path] (if outrigOn)
    tsOutrigEr*/tsOutrigTail* → outrigBuf(L/R) → outrigHP → outrigGain → pan → outrigPeakL/R
[AMBIENT path] (if ambientOn)
    tsAmbEr*/tsAmbTail* → ambientBuf(L/R) → ambientHP → ambientGain → pan → ambientPeakL/R
[Solo + Mute mask]
    anySolo = any*Solo; contributes[i] = onFlag && !mute && (!anySolo || solo)
[Sum contributing buffers into `buffer`]
[EQ → decorrelation → LFO → Width → Chorus → post-conv Cloud → Output Gain → spectrum → dry/wet → Bloom Volume]
```

**MAIN-path preservation** (brief R1): the code between "compute `lIn/rIn`" and "sum into `mainPathBuf`" is a one-for-one copy of the existing block, with the final `buffer.setSample(ch,i, eL+tL)` replaced by `mainPathBuf.setSample(ch,i, eL+tL)`. The `trueStereoWetGain = 2.0f`, `erLevelSmoothed`, `tailLevelSmoothed`, and both crossfeed paths stay in that block and apply to MAIN only. `erLevelPeakL/R` and `tailLevelPeakL/R` continue to be computed there (these feed the MAIN strip's sub-meters in the mixer UI).

**`convTmp` AudioBlock constructor** (brief R2): every new `juce::dsp::Convolution::process` call site uses the 3-argument form:

```cpp
juce::dsp::AudioBlock<float>(convTmp.getArrayOfWritePointers(), 1, (size_t)numSamples)
```

Pre-allocated scratch members for the new paths (`convTmp` is shared across paths since they run sequentially inside a single block): reuse the existing `convTmp` for all 20 new convolvers. Add `directBufL/R`, `outrigBufL/R`, `ambientBufL/R`, `mainPathBufL/R` as pre-allocated `juce::AudioBuffer<float>` members sized in `prepareToPlay`.

**Pan law:** constant-power per brief §2i. Centre (p=0) gives gain ≈ 0.707 each side — which is correct for stereo sources. Tested in DSP_15 (see test plan).

### 2.9 State persistence

Per brief §2k. `getStateInformation` strips and re-adds `irSynthParams` (already does) and `synthIR` (MAIN raw synth buffer only, already base64). **No** embedding of DIRECT/OUTRIG/AMBIENT raw buffers — they are always reconstructed from on-disk WAVs (or set to off if WAVs absent). Avoids the state-size regression that CLAUDE.md documents for `currentIRBuffer` → `rawSynthBuffer` migration.

Add the new `IRSynthParams` fields (outrig_*, ambient_*, direct_enabled) to the `irSynthParams` XML child. New `setStateInformation` reads them with defaults from the struct (i.e. all extra paths boot off for old sessions).

After `setStateInformation` loads MAIN (existing flow), call `loadMicPathFromFile(selectedIRFile, {Direct,Outrig,Ambient})`. Each returns silently if the file is absent (old presets, factory IRs).

### 2.10 `audioEnginePrepared` deferred-load integration

All 20 new convolvers' `loadImpulseResponse` calls must be deferred until `prepareToPlay` completes its `callAsync`. The existing pattern (saving `rawSynthBuffer` when `audioEnginePrepared == false`) is extended to save all 4 raw buffers; the `callAsync` then calls `reloadSynthIR()` (which hits all 4 paths) and `loadMicPathFromFile` for each extra path if loading from file.

---

## Phase 3 — FloorPlanComponent

### 3.1 Extend `TransducerState`

`cx[8]`, `cy[8]`, `angle[8]`. Indices: 0/1 speakers, 2/3 MAIN mics, 4/5 OUTRIG mics, 6/7 AMBIENT mics. Defaults per brief §3a.

**⚠ Binary compatibility:** `TransducerState` is a value type used in UI state, but it is **not** serialised to preset XML (only `IRSynthParams` is, and that holds the same positions). So extending the struct won't break state persistence. However, `IRSynthComponent` and any other code that passes `TransducerState` by value must be re-checked — an 8-slot version is ~50 bytes larger.

### 3.2 Drawing + hit-testing

Per brief §3c. `outrigVisible` / `ambientVisible` flags from `IRSynthComponent`. Extra pairs get distinct colours (soft purple for OUTRIG `0xffb09aff`, amber `0xffcfa95e` for AMBIENT); dim + hollow when `*Visible == false`. Labels "M" / "O" / "A" next to each non-speaker dot at small font.

`transducerHitTest` range expanded to 0–7; invisible pairs skipped from hit-testing.

### 3.3 Callbacks

Replace single `onPlacementChanged` with three per-group callbacks **and** keep the old name as a catch-all that fires all three (for `IRSynthComponent` simplicity). `mouseUp` dispatches based on `dragIndex`:
- 0/1/2/3 → `onMainPlacementChanged` (speakers fire this too, matching today's behaviour)
- 4/5 → `onOutrigPlacementChanged`
- 6/7 → `onAmbientPlacementChanged`

---

## Phase 4 — IRSynthComponent

### 4.1 Three collapsible-section controls in left column

Below the existing "Options" section, add:
- **Direct path** section: header + power-button toggle (`directEnabledToggle`), no other controls.
- **Outrigger mics** section: toggle + `outrig_pattern` combo + `outrig_height` slider (0.5–8.0 m, default 3.0).
- **Ambient mics** section: toggle + `ambient_pattern` combo + `ambient_height` slider (1.0–12.0 m, default 6.0).

Each section's child controls grey out when its toggle is off. All three toggles and both height sliders call `onParamModifiedFn()` to dirty the IR. Combos go through `comboBoxChanged`; they must be registered with the existing `ComboBox::Listener`.

Layout lives in `layoutControls()` — append three sections after "Options". Section header bounds stored as new members (`directHeaderBounds`, `outrigHeaderBounds`, `ambientHeaderBounds`) and drawn in `paint()` using the same small-caps + underline style.

### 4.2 `getParams` / `setParams`

Extend both to cover the new fields. The existing **three-layer async suppression pattern** (CLAUDE.md "IRSynthComponent::setParams suppresses dirty notifications") must be extended to cover the new toggles, sliders, and combos — failing to add them will cause spurious dirty indicators on plugin instantiation.

Specifically: the new toggle `onClick` handlers must check `suppressingParamNotifications`; new combo selection changes must do the same inside `comboBoxChanged`; and `setParams` must wrap the writes to new controls in the suppression flag. The async re-apply of `irSynthDirty` at the end of `setParams` must remain a single `callAsync` (no change).

### 4.3 FloorPlan sync

On every `getParams()` readout (UI edit), copy `outrig_enabled`/`ambient_enabled` into `floorPlan.outrigVisible`/`ambientVisible` and `floorPlan.repaint()`. Register `floorPlan.onOutrigPlacementChanged` / `onAmbientPlacementChanged` handlers that write back to the new position fields and fire `onParamModifiedFn()`.

### 4.4 Post-synthesis: per-path WAV + raw buffer storage

In the synthesis completion handler, for each `result.{direct,outrig,ambient}.synthesised == true`:
1. Call `IRSynthEngine::makeWav(LL,RL,LR,RR,sr)` to produce a WAV byte buffer.
2. When the user saves the synth IR (`finishSaveSynthIR`), write each WAV to disk with the appropriate suffix (`_direct`, `_outrig`, `_ambient`). One `.ping` sidecar covers all (brief §1d / §4e).
3. Store the buffer in the corresponding `rawSynthDirectBuffer` / `rawSynthOutrigBuffer` / `rawSynthAmbientBuffer` on the processor so `reloadSynthIR` reloads all paths after Reverse/Stretch/Decay changes.

### 4.5 `.ping` sidecar JSON extension

Add all new fields to `writeIRSynthParamsSidecar` and `loadIRSynthParamsFromSidecar`. Missing fields use `IRSynthParams` defaults → old sidecars load without error (D5 / brief §10 backward compat).

---

## Phase 5 — MicMixerComponent + PluginEditor integration

### 5.1 Component skeleton

`Source/MicMixerComponent.{h,cpp}`. Takes `PingProcessor&` in constructor. Four vertical strips (DIRECT, MAIN, OUTRIG, AMBIENT). Each strip:

1. Fader readout label (top, dB text, timer-updated).
2. Fader + meter column (JUCE `LinearBarVertical` slider in front of a painted meter bar; meter value from atomic peak, fader value from APVTS gain).
3. Label ("DIRECT"/"MAIN"/"OUTRIG"/"AMBIENT"), 10 pt small-caps.
4. Power toggle (round icon, `PingLookAndFeel::drawToggleButton`).
5. M / S buttons side by side (pill-style via `PingLookAndFeel`).
6. Pan knob (small rotary, 70% of `outputGainKnobSize`, centre detent).
7. HP toggle (small labelled pill "HP").

All bound via `SliderAttachment`, `ButtonAttachment` etc. Peak values pulled in a 24 Hz timer.

### 5.2 PluginEditor integration

- Remove the `OutputLevelMeter` member and all associated code; delete the old meter bar layout constants (`kMeterBarsH`, `kMeterBarOffset`).
- Add `MicMixerComponent micMixer { pingProcessor }`.
- In `resized()`, place `micMixer` where the old meter was:

```cpp
const int mixerW = 310;
const int mixerH = 160;
const int mixerX = rowStartX;
const int mixerY = h - 38 - mixerH + 15;  // same bottom anchor
micMixer.setBounds(mixerX, mixerY, mixerW, mixerH);
```

Any other code that referenced `outputLevelMeter` (`setBounds`, look-and-feel propagation) must be updated or removed.

Licence and version labels stay at `(12, h-18)` / `(w/2, h-18)` — unchanged.

### 5.3 `phantomWaveCentreY` anchor check

The DRY/WET stack's Y anchor (`cy`) uses `phantomWaveCentreY` which is derived from old waveform dimensions, not the current meter. So replacing the meter does **not** move the DRY/WET knob. Confirm this empirically with a screenshot comparison during development.

---

## Test Plan

Two new test files:

- `Tests/PingMultiMicTests.cpp` — engine-level tests for the new paths (IR_14 … IR_21).
- New DSP tests appended to existing `Tests/PingDSPTests.cpp` (DSP_15 … DSP_19) or a sibling file — see note below.

All new tests compile under `PING_TESTING_BUILD` with no JUCE dependency. They use `TestHelpers.h` (add new helpers there if shared across files).

### Engine-level tests — additions

| ID | Guards against | Description |
|----|---|---|
| **IR_14** | D1 (bit-identity of MAIN after refactor). The regression net that IR_11's 30-sample golden can't catch. | Runs `synthIR` with **default small-room params** (`smallRoomParams()`). Captures SHA-256 of each of `iLL`/`iRL`/`iLR`/`iRR` over the full length. Asserts against frozen digests. Run pre-refactor to capture; then again post-refactor. Same pattern as IR_11 regen: `./PingTests "[capture]" -s` prints digests on failure, paste into the test. |
| **IR_15** | OUTRIG/AMBIENT path structure. | With `outrig_enabled = true` and a non-coincident OUTRIG geometry (0.15/0.8, 0.85/0.8 at 3 m — the default), `synthIR` returns `result.outrig.synthesised == true`, all 4 channels have `irLen == result.irLen`, no NaN/Inf, peak < 10.0. AMBIENT same with default ambient params. |
| **IR_16** | Determinism under parallel dispatch. | `outrig_enabled = ambient_enabled = direct_enabled = true`. Run `synthIR` twice. Assert all 4 paths (MAIN, DIRECT, OUTRIG, AMBIENT) are bit-identical across runs. Guards against any non-deterministic progress-callback shuffling leaking into the IRs. |
| **IR_17** | DIRECT path = order-0 only. | With `direct_enabled = true`, `result.direct.LL`'s non-zero samples are clustered within **one window** around the direct-arrival sample (speaker→mic distance / SPEED × sr). There are **no** samples above −40 dB of peak after that window (no wall bounces). Window width: 128 samples of the 8-band bandpass-filter impulse response. |
| **IR_18** | DIRECT path timing correctness. | Direct arrival sample = round(dist(slx, sly, sz → rlx, rly, rz) / 343 × 48000). Assert `result.direct.LL` peak is within ±5 samples of that predicted index. Same check for `RL`, `LR`, `RR` using the appropriate speaker-mic pair distances. |
| **IR_19** | DIRECT path polar colouration (D2). | Set `p.mic_pattern = "figure8"`. A figure-8 mic points toward the speaker on one axis and nulls the other. With `micl_angle` pointing away from the speaker (e.g. 0 rad pointing right when speaker is to the left), assert `result.direct.LL` peak level is **at least 20 dB below** the same synthesis with `micl_angle` set to point at the speaker. Confirms the frequency-dependent polar map is invoked for order-0. |
| **IR_20** | OUTRIG/AMBIENT independence from MAIN. | Run with outrig positions differing from main positions. Assert `result.iLL != result.outrig.LL` by L2 difference > 1% of `‖iLL‖`. Then switch `outrig_*` to **exactly** `receiver_*` and `outrig_pattern = p.mic_pattern`, `outrig_height = 3.0` — but with a different seed base (52 vs 42) internally, the diffuse field should still differ from MAIN. That's asserted too: `l2diff(iLL, outrig.LL) > 1e-6`. (Sanity: if we accidentally reused seed 42, they'd be bit-identical and the test would fail.) |
| **IR_21** | `er_only` global behaviour (D4). | With `er_only = true` + `outrig_enabled = ambient_enabled = true`: OUTRIG and AMBIENT results must have no late energy past 200 ms (−60 dB gate, analogous to IR_06). DIRECT is already order-0 by construction; also gated. |

### DSP-level tests — additions

Appended to `Tests/PingMultiMicTests.cpp` (same file — they test hostless DSP that is easiest to define alongside the engine tests). Each defines a local struct mirroring production so they're self-contained.

| ID | Guards against | Description |
|----|---|---|
| **DSP_15** | Pan law. | For pan values {−1, −0.5, 0, +0.5, +1}, assert `panL*panL + panR*panR == 1.0 ± 1e-6` (constant power). At p=0, both outputs = √0.5. At p=±1, one output is 1.0 and the other is 0.0 (within 1e-6). |
| **DSP_16** | `HP2ndOrder` correctness. | 1. Impulse response is causal (output[i]==0 for i<0). 2. DC gain (sum of impulse response) ≈ 0 → confirms it's a high-pass. 3. At `fc = 110 Hz`, a sine wave at 50 Hz is attenuated by ≥ 6 dB vs the same sine at 1 kHz (frequency response sanity). 4. `enabled=false` returns input; `enabled=true` returns filtered output; state continues to update in both modes. 5. 10 s of −12 dBFS random input produces no NaN/Inf. |
| **DSP_17** | HP toggle click-free on re-enable. | `enabled=true` for 5000 samples with a DC ramp input; `enabled=false` for 5000 samples; `enabled=true` again. The discontinuity at each toggle boundary must be ≤ 2× the expected filter-state value (i.e. state was continuous across the toggle — no click impulse). |
| **DSP_18** | Solo logic. | 4 paths with various on/mute/solo combinations. Verify the `contributes` mask for: (1) no solo → all unmuted paths contribute; (2) one solo → only that path; (3) two solos → only those two; (4) solo + mute on same path → no contribution (mute wins); (5) solo with path off → no contribution. |
| **DSP_19** | Path summation preserves MAIN-only path (backward compat). | Feed DIRECT/OUTRIG/AMBIENT with silent output (simulate `*On = false`). MAIN path only. Assert the summed output is **bit-identical** (within 1e-6) to the pre-refactor processBlock output for the same input and same IR. This is the processBlock analogue of IR_14 — a golden capture that guards against processBlock restructure regressions. |

### Test execution integration

- Add new source files to `PingTests` in `CMakeLists.txt` (one or two new lines).
- Run `ctest --output-on-failure` after each phase to confirm no regression.
- DSP_19 requires a lightweight mock `PingProcessor` or a header-only port of the processBlock mixer logic. Given the full processBlock has JUCE dependencies, the test will be structured as: (a) a header-only `MicPathSummer` struct that takes pre-computed per-path L/R buffers + gain/pan/mute/solo state and produces the summed bus; (b) the test feeds synthetic buffers to both the new summer and a reference "MAIN-only pass-through" and asserts equality.

### Pre-existing test sanity

Before any refactor, run `ctest` on `main` and record the baseline: **all IR_01 … IR_13 and DSP_01 … DSP_14 pass**. Run again after each phase.

After Phase 1 commit: IR_14 captures the golden digests; IR_11 unchanged (still passes with the same onset index 482 and same 30 samples).

After Phase 2 commit: DSP_19 validates that processBlock didn't regress MAIN-only output.

---

## Commit checkpoints

Ordering designed so that main branch CI always builds and passes tests after each step. Each checkpoint is a single commit on `feature/multi-mic-paths`.

1. **C1 — IR_14 regression capture (pre-refactor).** Add IR_14 with captured digests against the *current* engine. All tests pass.
2. **C2 — Phase 1.1/1.2 struct extensions.** `IRSynthParams` new fields (with defaults), `MicIRChannels` struct, `IRSynthResult::{direct,outrig,ambient}`. No behavioural change. Tests still pass. IR_14 unchanged.
3. **C3 — Phase 1.3/1.4 synthIR refactor.** Extract `synthMainPath`, add `synthExtraPath`, `synthDirectPath`. `synthIR` becomes the dispatcher. **IR_14 must still pass bit-identically** — if it doesn't, stop and investigate before continuing.
4. **C4 — Phase 1 multi-path tests.** Add IR_15 … IR_21. Run them against the new engine. Commit the captured values / fixed assertions.
5. **C5 — Phase 2.1 HP filter + DSP_16/17 tests.** `HP2ndOrder.h` lands with its own tests. No integration yet. All other tests pass.
6. **C6 — Phase 2.2/2.3/2.4/2.6 APVTS + convolvers + raw buffers.** New params, new convolvers declared and `prepareToPlay`'d, new `rawSynth*Buffer`s. `processBlock` unchanged. Plugin still behaves identically (new convolvers silent). DSP_19 golden is captured from this state.
7. **C7 — Phase 2.5 MicPath enum + loadIRFromBuffer extension + loadMicPathFromFile.** Can now manually load OUTRIG/AMBIENT WAVs into the new convolvers, but they aren't in the mixer yet. Verify via a test that loading a file IR still works (existing `loadIRFromBuffer` behaviour preserved).
8. **C8 — Phase 2.7 sample-based IR load fade.** Atomic counter migration. `kIRLoadFadeSamples = 96000`. Test: existing IR load still fades in correctly; fade is now 2 s regardless of buffer size.
9. **C9 — Phase 2.8 processBlock mixer insertion.** The biggest commit. DSP_19 must pass after. Existing preset files play back correctly. MAIN path output unchanged when all other paths are off.
10. **C10 — Phase 2.9/2.10 state persistence + deferred load.** Presets round-trip with new params. `loadMicPathFromFile` called from `setStateInformation`. Test backward compat: load a v2.5.0 preset → all new paths off, no errors.
11. **C11 — Phase 3 FloorPlan extensions.** `TransducerState` → 8 points, per-group callbacks. `IRSynthComponent` wires extra callbacks (no-op path to extra positions until Phase 4). Manual sanity test: drag MAIN mics still works; extra points don't appear yet.
12. **C12 — Phase 4 IRSynthComponent UI.** Direct/Outrig/Ambient sections appear. Users can enable, position, and synthesise multi-path IRs. Per-path WAVs written to disk. Sidecar extended. Manual test: synthesise, save, reload, round-trip.
13. **C13 — Phase 5 MicMixerComponent + editor wiring.** Mixer UI replaces `OutputLevelMeter`. End-to-end manual test of full feature.
14. **C14 — CLAUDE.md documentation update.** Add a "Multi-mic paths" section covering the 4 strips, APVTS params, signal flow, persistence, and the new test IDs.
15. **C15 — Version bump to 2.6.0.** Both `CMakeLists.txt` and `Installer/build_installer.sh`.

---

## Risks and watch-points

**R1 — IR_11 and IR_14 after C3.** If IR_11 breaks after the synthIR refactor, STOP. The refactor should be pure code-motion. Suspect: inadvertent FP reordering in the dispatcher (e.g. using `result = outrigFut.get(); r.outrig = result;` instead of `r.outrig = outrigFut.get();`). Scalar code must be line-by-line moved, not re-expressed.

**R2 — `convTmp` AudioBlock constructor** (brief R2, CLAUDE.md's own history note). All 20 new convolvers use the 3-arg form. Grep for `AudioBlock<float>(convTmp)` after C6 to confirm no regressions.

**R3 — 28 convolvers × NUPC background threads.** Preset switching now spawns up to 28 background FFT-prep threads. The existing `isRestoringState` + `callAsync` triple-load guard must be intact (no new per-param-listener paths added for `*On` / `*Mute` / `*Solo` / `*HPOn`). Only `stretch` and `decay` trigger reload.

**R4 — State size regression.** The brief warns `currentIRBuffer` vs `rawSynthBuffer` duplication grew XML state unboundedly in v2.3.x. The new code must *not* embed `rawSynthDirectBuffer` / `rawSynthOutrigBuffer` / `rawSynthAmbientBuffer` in state. Only the on-disk WAVs + the MAIN raw buffer.

**R5 — FloorPlan binary size.** `TransducerState` grows from ~96 bytes to ~192 bytes. If passed by value frequently, watch the profile. Pass by const-ref where possible.

**R6 — `fdnMaxRefCut` per path.** Each extra path computes its own `fdnMaxRefCut` inside `synthExtraPath`. This is correct: each path's geometry and direct-arrival time differ, so each FDN warmup window differs. No cross-contamination.

**R7 — Parallel `makeAllpassDiffuser` / FDN seed thread safety.** These helpers are already static and stateless. Confirmed by inspection.

**R8 — HP filter at 110 Hz in the bass.** Sub-bass below 80 Hz is already attenuated by the tail — the 110 Hz HP on OUTRIG/AMBIENT at default removes the low-mid rumble that's typical in ambient-mic placements. Test DSP_16 confirms the attenuation shape is plausible.

**R9 — Future multi-mic factory IRs (out of scope, documented).** Once the feature is merged and tuned, a follow-up PR can add Phase 6. Until then, factory `.wav`s load into MAIN only and all extra paths stay off.

---

## Open questions for later (not blocking)

- **Pan AutoGain / centre attenuation.** Constant-power pan gives ≈ 0.707 at centre. The existing single-bus path has no such attenuation, so users who are used to the old sound may perceive a 3 dB level drop. Consider adding a +3 dB compensating offset to all strip pan curves, or a separate "centre attenuation" toggle. Left as a polish item.

- **Mic pattern per extra path from the main mic combo.** Currently OUTRIG/AMBIENT have independent `*_pattern` combos. If users expect OUTRIG to follow MAIN's pattern, consider a "Link to MAIN" toggle. Out of scope for v2.6.0.

- **Altiverb-style dedicated DIRECT mic at different position.** Brief D2 ties DIRECT to MAIN mics. Phase 7 work.
