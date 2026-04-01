# P!NG — CLAUDE.md

Developer context for AI-assisted work on this codebase.

---

## What this project is

**P!NG** (`PRODUCT_NAME "P!NG"`) is a stereo reverb plugin for macOS (AU + VST3) built with JUCE. It convolves audio with impulse responses (IRs) and also includes a from-scratch IR synthesiser that simulates room acoustics using the image-source method + a 16-line FDN.

**Current version:** 1.9.7 (see `CMakeLists.txt`)
**Minimum macOS:** 13.0 Ventura
**Formats:** AU (primary, for Logic Pro) + VST3

---

## Build

Prerequisites: Xcode, CMake, libsodium (`brew install libsodium`).
JUCE is fetched automatically by CMake on first configure.

```bash
cd "/Users/paulthomson/Cursor wip/Ping"
cmake -B build -S .
cmake --build build --config Release
```

Built artefacts land in `build/Ping_artefacts/Release/` (AU: `AU/P!NG.component`, VST3: `VST3/P!NG.vst3`).

**Install to system library (intentional — plugins install system-wide for all users):**
```bash
cp -R build/Ping_artefacts/Release/AU/P!NG.component /Library/Audio/Plug-Ins/Components/
cp -R build/Ping_artefacts/Release/VST3/P!NG.vst3 /Library/Audio/Plug-Ins/VST3/
```

> Note: CMake sets `JUCE_AU_COPY_DIR` and `JUCE_VST3_COPY_DIR` to `/Library/...` (system-wide), not `~/Library/...`. This is deliberate — the .pkg installer requires admin and deploys for all users on the machine. Don't change this to `~/Library`.

**Build installer .pkg:**
```bash
cmake --build build --target installer
```
(Requires the `Installer/build_installer.sh` script and that `Ping_AU` + `Ping_VST3` are already built.)

---

## Tests

The test suite lives in `Tests/` and is compiled by CMake into the `PingTests` binary (no JUCE dependency — pure C++17 + Catch2, fetched automatically).

```bash
# Build and run (from repo root)
cmake -B build -S .
cmake --build build --target PingTests
cd build && ctest --output-on-failure
```

Catch2 is fetched by CMake into `build/_deps/catch2-src/`. If you need to compile the tests standalone (e.g. on Linux for CI without a macOS toolchain), use the amalgamated build:

```bash
CATCH2=build/_deps/catch2-src
g++ -std=c++17 -O2 \
    -DPING_TESTING_BUILD=1 \
    -I${CATCH2}/src -I${CATCH2}/extras \
    -I build/_deps/catch2-build/generated-includes \
    -I Source -I Tests \
    ${CATCH2}/extras/catch_amalgamated.cpp \
    Source/IRSynthEngine.cpp \
    Tests/PingEngineTests.cpp \
    Tests/PingDSPTests.cpp \
    -o PingTests -lm
./PingTests
```

### Test files

| File | What it tests |
|------|--------------|
| `Tests/PingEngineTests.cpp` | `IRSynthEngine` — determinism, output dimensions, NaN/Inf, RT60 plausibility, channel distinctness, ER-only silence, FDN decay, golden regression lock |
| `Tests/PingDSPTests.cpp` | Hybrid-mode DSP building blocks — `SimpleAllpass` (Plate/Bloom), Cloud LFO rate spacing, Shimmer grain engine pitch and stability |
| `Tests/TestHelpers.h` | Shared utilities: `TestRng`, `windowRMS`, `hasNaNorInf`, `l2diff`, `peakFrequencyHz` |

### Test inventory

**Engine tests (IR_01 – IR_11)** — test existing `IRSynthEngine` code, all should pass on every commit.

| ID | Description |
|----|-------------|
| IR_01 | `synthIR` is deterministic (bit-identical across two runs) |
| IR_02 | Output dimensions are self-consistent (irLen, sampleRate, rt60 size) |
| IR_03 | No NaN or Inf in any output channel |
| IR_04 | RT60 values are physically plausible (0.05–30 s, LF ≥ HF) |
| IR_05 | All four IR channels are distinct (no copy bug) |
| IR_06 | ER-only mode has no late energy after 200 ms (−60 dB gate) |
| IR_07 | FDN tail energy decreases monotonically over time (250 ms windows) |
| IR_08 | Full-reverb tail has energy after ER crossover (FDN seed check) |
| IR_09 | No NaN/Inf with extreme parameters (tiny room, coincident speakers, high diffusion) |
| IR_10 | Measured RT60 within 2× of Eyring-predicted value (lower bound 0.4×, upper 2×) |
| IR_11 | Golden regression lock — 30 samples from onset at index 371, locked to 17 sig-fig |

**DSP tests (DSP_01 – DSP_11)** — test the hybrid-mode DSP building blocks. DSP_01–DSP_02 and DSP_10 cover `SimpleAllpass` (Plate), which is now implemented in `PluginProcessor.h`. The remaining tests (Bloom, Cloud, Shimmer) are self-contained reference specs for blocks not yet implemented in production code. All tests define their own structs locally and must stay in sync with any production implementation.

| ID | Description |
|----|-------------|
| DSP_01 | Plate allpass cascade is truly all-pass (energy preserved to ±1% after drain) |
| DSP_02 | Plate at density=0 is bit-identical to bypass (IEEE-754 collapse) |
| DSP_03 | Bloom allpass is causal — no echo before first delay drains |
| DSP_04 | Bloom feedback is stable at maximum setting (0.65) — output decays after input stops (bloomTime = 300 ms) |
| DSP_05 | Cloud LFO rates have no rational beat frequencies (p,q ≤ 6 check) |
| DSP_06 | Cloud does not amplify signal (RMS out ≤ RMS in × 1.05) |
| DSP_07 | Shimmer 12-semitone shift produces ~750 Hz from 375 Hz input |
| DSP_08 | Shimmer feedback decays after input stops (stability check) |
| DSP_09 | Shimmer semitone-to-ratio formula is correct for key musical intervals |
| DSP_10 | Plate `plateSize=2.0` doubles the effective allpass delay — first-return peak moves from sample ~691 to ~1382 |
| DSP_11 | Bloom feedback stable at minimum bloomTime (50 ms) — most stressful case (~20 recirculations/s) |

### Golden regression lock (IR_11)

IR_11 locks 30 samples of `iLL` starting at the first non-silent sample (onset index 371 for the 10×8×5 m small-room params). To update after a deliberate engine change:

```bash
./PingTests "[capture]" -s    # prints new onset index + 30 sample values
```

Paste the printed `onset_offset` and `golden_iLL[30]` values into `IR_11` in `PingEngineTests.cpp`, set `goldenCaptured = true`, and commit with a note explaining why the engine output changed.

### Key test design decisions

- **`PING_TESTING_BUILD=1`** — defined for the `PingTests` target; gates out `#include <JuceHeader.h>` in `IRSynthEngine.h`, making the engine compilable with no JUCE dependency.
- **Small-room params** — `width=10, depth=8, height=5 m`. Generates a ~2 s IR instead of the default 25 s, keeping individual test runtime under 4 s.
- **DSP tests are self-contained** — `SimpleAllpass`, the Cloud LFO, and the grain engine are defined locally in `PingDSPTests.cpp`. The `SimpleAllpass` production implementation now lives in `PluginProcessor.h`; the struct definition there must stay in sync with the one in the tests exactly. Cloud LFO and grain engine are not yet in production code.
- **DSP_07 uses 375 Hz input** (not 440 Hz) — 375 × 512/48000 = 4 exact integer cycles per grain, making grain resets phase-coherent. 440 Hz gives 4.69 cycles/grain, producing phase discontinuities that shift the DFT peak ~65 Hz below the true pitch-shifted frequency.
- **DSP_04 feedback delay** — the Bloom feedback circular buffer must be read from `fbWp` (the oldest slot, = full `fbLen` samples of delay), not `(fbWp-1)` (1-sample IIR). The same convention applies in the production implementation. DSP_04 tests bloomTime = 300 ms (mid-range); DSP_11 tests the 50 ms minimum.
- **DSP_05 rational-beat bound** — p,q ≤ 6 covers all perceptible simple LFO beat ratios. The original p,q ≤ 32 flagged 11/15, whose beat period at these rates would be many minutes — completely inaudible.
- **DSP_10 first-return peak test** — allpass filters are IIR and never fully "drain," so drain-energy comparisons are unsuitable for testing `effLen`. Instead, DSP_10 exploits the fact that a single allpass of delay d, fed an impulse, produces its first large positive output exactly at sample d: `out[d] = 1 - g² ≈ 0.51`. At `plateSize=2.0`, d doubles from 691 to 1382 samples, making the peak position a direct, unambiguous observable of the `effLen` mechanism.
- **DSP_11 short bloomTime is the worst case** — At 50 ms (minimum bloomTime) there are ~20 feedback round-trips per second. More trips per second means more opportunities for energy to accumulate if the loop gain is near 1. DSP_04 (300 ms) and DSP_11 (50 ms) together bound the stability guarantee across the full 50–500 ms range.
- **`SimpleAllpass` struct must stay in sync** — The `effLen` field (default 0 = use `buf.size()`) must be present in both `PingDSPTests.cpp` and `PluginProcessor.h`. Plate stages set `effLen` each block via `plateSize` (4× alloc, range 0.5–4.0). Bloom stages also now set `effLen` each block via `bloomSize` (2× alloc, range 0.25–2.0). Plate buffers at 4× base primes; Bloom buffers at 2× base primes.

---

## Source files

```
Source/
  PluginProcessor.h/.cpp   — Audio engine, APVTS parameters, IR loading, processBlock
  PluginEditor.h/.cpp      — Main UI, IR list, IR Synth panel host
  IRManager.h/.cpp         — Scans ~/Documents/P!NG/IRs for .wav/.aiff files
  IRSynthEngine.h/.cpp     — Room acoustic simulation (image-source + FDN)
  IRSynthComponent.h/.cpp  — UI panel for the IR Synth
  FloorPlanComponent.h/.cpp— 2D floor-plan visualiser for speaker/mic placement
  EQGraphComponent.h/.cpp  — 5-band EQ graph + rotary knob controls (low shelf, 3× peak, high shelf)
  WaveformComponent.h/.cpp — IR waveform thumbnail display (dB logarithmic scale)
  PingLookAndFeel.h/.cpp   — Custom JUCE LookAndFeel (colours, knob style)
  PresetManager.h/.cpp     — Save/load named presets
  LicenceScreen.h          — Floating activation window
  LicenceVerifier.h        — Ed25519 serial decode + verify (libsodium)
  OutputLevelMeter.h       — L/R peak meter widget
Resources/
  ping_logo.png, spitfire_logo.png, reverse_button.png, reverse_button_glow.png
Tools/
  keygen.cpp               — Serial number generator (developer only, never shipped)
```

---

## WaveformComponent display

The waveform panel (top-right of the main UI) shows the currently-loaded IR using a **dB logarithmic amplitude scale**. This is essential because synthesised IRs can be 4–30 seconds long (8× the longest RT60, see IR Synth engine section), but the vast majority of that length is an exponentially decaying tail that is 60+ dB quieter than the onset. A linear amplitude scale shows only a spike at t≈0; the dB scale reveals the full "ski-slope" decay shape for any room size.

### Display algorithm

1. **True-peak scan** — finds the global maximum absolute value across all samples in all displayed channels. This is the 0 dB reference (`peakSample`). A floor of `1e-6` prevents divide-by-zero on silence.
2. **Per-pixel peak envelope** — for each horizontal pixel, all samples in that pixel's time range are scanned and the maximum absolute value is recorded. This avoids the subsampling aliasing that occurs when only one sample per pixel is used (at 48 kHz / 200 px each pixel covers 700+ samples, so stride-based lookup routinely misses the actual peak).
3. **dB conversion** — `peakToFrac`: `dB = 20 × log10(pk / peakSample)`, clamped to `[dBFloor, 0]`, then linearly mapped to `[0, 1]`. `dBFloor = −60.0f` (one full RT60 of range). The result is `0` at −60 dB and `1` at 0 dB.
4. **Symmetric fill** — a filled `juce::Path` is drawn symmetrically above and below the centre line (forward pass builds the top edge; reverse pass mirrors it to the bottom edge), with a separate `topPath` for the white outline stroke.

True-stereo 4-channel IRs are displayed as two channels (L = ch0+ch1 averaged, R = ch2+ch3 averaged), matching the convolution output path.

The **Reverse Trim** handle overlay is drawn on top when Reverse is engaged.

---

## UI layout notes

The editor (`PluginEditor.cpp` `resized()`) uses a content-area offset: `leftPad = w/6`, `cw = w - leftPad`, `ch = h - h/6`. All proportional constants use `cw`/`ch`. The window is divided into **left-side rows** (small knobs, left-justified from `rowStartX`) and **right-side rows** (small knobs, right-justified to `b.getRight()`), with a **centre column** for the DRY/WET knob stack.

### Left-side rows (Rows 1–4)

All left-side rows use `rowKnobSize = (int)(sixKnobSize * 0.6f)`, `rowStep = rowKnobSize + rowGap`, and `rowStartX = max(8, w/128) + 5`. Row Y positions use absolute anchors (see Key Design Decisions) to avoid JUCE `removeFromTop` clamping.

**Row 1 — IR Input + IR Controls (5 knobs)**
`mainArea.removeFromTop(rowTotalH + groupLabelH)` reserves the strip (no extra +4 gap — the extra gap was moved to the Row 2→3 boundary). A 5 px extra gap before index 2 splits into two groups:
- **IR Input**: knob 0 = GAIN (`irInputGainSlider`), knob 1 = DRIVE (`irInputDriveSlider`)
- **IR Controls**: knob 2 = PREDELAY, knob 3 = DAMPING, knob 4 = STRETCH

`rowY = topKnobRow.getY() + groupLabelH - 10 - rowShiftUp`. With `rowShiftUp = 30 - rowKnobSize` (a negative value), the net effect is that all rows sit one `rowKnobSize` lower than the original 30 px upward shift. Group header bounds: `irInputGroupBounds`, `irControlsGroupBounds`.

**Row 2 — ER Crossfade + Tail Crossfade (4 knobs + 2 pill switches)**
Same 5 px gap before index 2 splits into two groups:
- **ER Crossfade**: knob 0 = DELAY, knob 1 = ATT, pill switch `erCrossfeedOnButton`
- **Tail Crossfade**: knob 2 = DELAY, knob 3 = ATT, pill switch `tailCrossfeedOnButton`

Group header bounds: `erCrossfadeGroupBounds`, `tailCrossfadeGroupBounds`. All controls are live/real-time — no IR recalculation.

`row2AbsY = topKnobRow.getBottom()`. `row3AbsY = row2AbsY + row2TotalH_ + 4` — the **+4 extra gap sits between Rows 2 and 3** (not 1 and 2). This gives a visual breathing space between the Crossfade row and the Plate row, matching the equivalent gap on the right side between Cloud (R1) and Shimmer (R2).

**Row 3 — Plate pre-diffuser (4 knobs + 1 switch)**
No extra inter-group gap — single **"Plate pre-diffuser"** group: DIFFUSION, COLOUR, SIZE, IR FEED, pill switch `plateOnButton` right-aligned. Group header bounds: `plateGroupBounds`.

**Row 4 — Bloom hybrid (5 knobs + 1 switch)**
Single **"Bloom hybrid"** group: SIZE, FEEDBACK, TIME, IR FEED, VOLUME, pill switch `bloomOnButton` right-aligned. Group header bounds: `bloomGroupBounds`.

### Right-side rows (Rows R1–R3)

All right-side rows share `placeRightRowKnob` / `placeR3Knob` lambdas. Knobs are placed right-to-left from `b.getRight()`: `cx = b.getRight() - (4 - idx) * rowStep - rowKnobSize / 2`. The rightmost knob's right edge aligns with `b.getRight()`.

**Row R1 — Cloud multi-LFO (5 knobs + 1 switch)** — vertically aligned with Row 1 (`rowY`).
Single **"Clouds post convolution"** group: DEPTH, RATE, SIZE, IR FEED, VOLUME, pill switch `cloudOnButton`. Group header Y = `topKnobRow.getY() - 10 - rowShiftUp`. Group header bounds: `cloudGroupBounds`.

**Row R2 — Shimmer (5 knobs + 1 switch)** — vertically aligned with Row 2 (`row2KnobY`).
Single **"Shimmer"** group: PITCH, SIZE, COLOUR, IR FEED, VOLUME, pill switch `shimOnButton`. Group header Y = `row2AbsY - rowShiftUp`. Group header bounds: `shimGroupBounds`.

**Row R3 — Tail AM mod + Tail Frq mod (5 knobs, no switch)** — vertically aligned with Row 3 (`row3KnobY`).
A 5 px extra gap before index 2 splits into two groups (mirroring the IR Input / IR Controls split):
- **Tail AM mod**: knob 0 = LFO DEPTH (`modDepthSlider`), knob 1 = LFO RATE (`modRateSlider`)
- **Tail Frq mod**: knob 2 = TAIL MOD (`tailModSlider`), knob 3 = DELAY DEPTH (`delayDepthSlider`), knob 4 = RATE (`tailRateSlider`)

Formula: `cx = b.getRight() - (4 - idx) * rowStep - rowKnobSize / 2 - (idx < 2 ? 5 : 0)`. Group header Y = `row3AbsY - rowShiftUp`. Group header bounds: `tailAMModGroupBounds`, `tailFrqModGroupBounds`. No toggle pills — these controls are always active.

These five knobs were previously part of the large-knob grid at the bottom of the UI. They were moved to Row R3 to co-locate modulation controls with the left-side rows they interact with.

### Centre column — DRY/WET stack

The DRY/WET knob and all controls below it are horizontally centred at `w / 2` (the full window centre, not `b`'s centre). Their vertical anchor `cy` is computed from phantom waveform dimensions (see below) plus a +40 px downward offset:

```cpp
int cy = phantomWaveCentreY - (dryWetKnobSize + labelH + readoutH + 4 + irComboH + 6) / 2 + 40;
```

From top to bottom, all items derive their X from `dryWetSlider.getBounds().getCentreX()` (= `w/2`) and their Y from `cy` or from the control above:

1. **DRY/WET knob** (`dryWetSlider`): `dryWetKnobSize = bigKnobSize * 1.05f` (70% of the old size), Y = `cy + 10`.
2. **IR combo + IR Synth button** (`irCombo`, `irSynthButton`): centred at `w/2`, Y = `irRowY`. "IR preset" label (`irComboLabel`) to the left of the combo.
3. **Reverse button** (`reverseButton`): right-aligned to the waveform's right edge, immediately below the IR combo.
4. **Waveform display** (`waveformComponent`): centred at `w/2`, immediately below the reverse button. Size: `wavePanelW = max(165, 0.27f × cw)`, `wavePanelH = max(54, wavePanelW × 0.36f)` — 25% smaller than the old right-column dimensions.

The preset combo (`presetCombo`), save button, and "Preset" label are **not** part of this centre-column stack — they live in the top bar (see P!NG logo section).

**Phantom waveform anchor:** `cy` is derived from `phantomWaveCentreY`, which uses the *old* right-column waveform dimensions (`0.36f × cw` wide) rather than the current smaller waveform. This preserves the DRY/WET knob's visual height even though the waveform has moved and shrunk. Do not replace `phantomWavePanelW/H` with the current `wavePanelW/H` — doing so would shift `cy` and misalign the DRY/WET knob. Note: the preset combo is no longer anchored to `cy`; it lives in the top bar.

### Wet Output Gain position

Wet Output Gain (`outputGainKnobSize = smallKnobSize * 1.5f`) is positioned to the **right** of the DRY/WET knob, horizontally centred on the Save button X and vertically centred on the DRY/WET knob Y:

```cpp
const int outputGainCenterX = w / 2 + irComboW / 2 + 4 + saveButtonW / 2;
// vertically centred on dryWetTrueCentreY = cy + 10 + dryWetKnobSize / 2
```

ER and Tail level knobs (same size as WET OUTPUT) mirror this to the **left** of DRY/WET at the same horizontal distance (`erTailCenterX = w/2 - (irComboW/2 + 4 + saveButtonW/2)`), stacked ER above and Tail below the DRY/WET centre Y.

### P!NG logo

Reduced to 75% of original size: `rightLogoW = min(68, 0.075f × cw)`. Placed horizontally centred in the window top bar:

```cpp
pingBounds = juce::Rectangle<int> (w / 2 - rightLogoW / 2, topRow.getY() + 2, rightLogoW, topRow.getHeight() - 4);
```

`topRow.removeFromRight(rightLogoW)` is still called (result discarded) so that `presetArea` (= `topRow.reduced(4)`) ends at `w - rightLogoW - 4` — the right-aligned preset combo and save button sit flush against the P!NG logo slot. In `paint()`, the logo is drawn at 2× `pingBounds` size centred on `pingBounds.getCentre()`.

### EQ section (bottom of window)

The `EQGraphComponent` occupies the **bottom-right** of the editor, pinned to the full window bottom edge. Its bounds extend into the `h/6` bottom margin so the knob strip sits flush with the window bottom:

```cpp
const int eqTotalH = eqHeight + (h - ch);
eqGraph.setBounds (eqRect.getX(), eqRect.getY(), eqRect.getWidth(), eqTotalH);
```

`eqHeight = max(225, 0.3375 × ch)` and `eqWidth = max(315, 0.465 × cw)` — both 75% of the former values. The minimum `eqMinH = 225` is sized to fit the 32 px knob strip (estimated `ctrlH ≈ 188`).

#### EQGraphComponent layout

**Graph area** — upper portion; displays spectrum analyser, grid lines, frequency-response curve, and draggable band handles. Top edge trimmed 60 px downward (`b.withTrimmedTop(60)`).

**Control strip** — reserved via `b.removeFromBottom(ctrlH - 75)`. Three rows × five bands:

| Row | Parameter | DX / DY fine-tune | Notes |
|-----|-----------|-------------------|-------|
| Row 1 | FREQ | `freqDX = −8`, `freqDY = −5` | DX scaled to 75%; DY unchanged (cancels the −75 ctrlArea shift) |
| Row 2 | GAIN | `gainDX = +15`, `gainDY = −45` | DX scaled to 75%; DY unchanged |
| Row 3 | Q / SLOPE | `qDX = −8`, `qDY = −65` | DX scaled to 75%; DY unchanged |

Knob size is **32 px** (was 42 px). All DY constants are intentionally NOT scaled — they cancel the structural `ctrlH - 75` offset, which is independent of knob size. Do not scale DY when changing knob size.

Band order (left→right): LOW (low shelf, purple) · MID 1 (peak, blue) · MID 2 (peak, amber) · MID 3 (peak, green) · HIGH (high shelf, red).

#### EQ DSP (PluginProcessor)

Five `ProcessorDuplicator` instances in series: `lowShelfBand → lowBand → midBand → highBand → highShelfBand`. Updated in `updateEQ()` called from `parameterChanged`. Filter types:

- `lowShelfBand` — `makeLowShelf` (b3); `highShelfBand` — `makeHighShelf` (b4)
- `lowBand / midBand / highBand` — `makePeakFilter` (b0 / b1 / b2)

**Backward compatibility:** b0–b2 keep their original IDs. b3/b4 default to 0 dB gain so existing presets missing these keys are unaffected.

### Large-knob grid (bottom of left column)

Only **Width** (`widthSlider`) remains in the large-knob grid at `row6AbsY + row6TotalH_ + 70`. LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to Row R3 on the right side.

### Version label

Displayed in the bottom strip under the Tail Rate knob (`tailRateSlider.getX()`). Must be placed **after** `tailRateSlider.setBounds()` in `resized()`.

---

## Reverse for synthesised IRs

### Why it needs special handling

`loadSelectedIR()` (PluginEditor.cpp) is the single entry point used by the Reverse button click handler and the waveform Trim-changed callback. For file-based IRs it calls `loadIRFromFile()` → `loadIRFromBuffer()`, which physically reverses the buffer if `reverse == true`. For the synth IR slot (`irCombo` id=1, idx=−1) it previously returned immediately, so toggling Reverse or moving the Trim handle had no effect on synth IRs.

### Fix

`PluginProcessor` stores a **raw (pre-processing) copy** of the last synthesised buffer. When `loadIRFromBuffer` is called with `fromSynth=true`, two steps run in order inside the `fromSynth` block before any other transforms:

**Step 1 — Auto-trim trailing silence.** `synthIR()` always allocates `8 × max_RT60` (up to 30 s), but the reverb signal decays to below −80 dB well before the end. The trim scans all channels from the end backwards for the last sample above −80 dB below peak, then keeps that point plus a 200 ms safety tail (minimum 300 ms). This runs *before* `rawSynthBuffer` is saved so the stored copy is already silence-free.

```cpp
// threshold = peak × 1e-4  (−80 dB)
// newLen    = lastSignificant + 200 ms safety tail, min 300 ms
```

**Step 2 — Save raw copy.** After silence trim, the buffer is saved as `rawSynthBuffer` before any reversal/trim/stretch/decay transforms:

```cpp
rawSynthBuffer = buffer;       // already silence-trimmed, before any other transforms
rawSynthSampleRate = bufferSampleRate;
```

`reloadSynthIR()` re-calls `loadIRFromBuffer` with that stored raw copy, picking up the current `reverse`, `reversetrim`, `stretch`, and `decay` values:

```cpp
void PingProcessor::reloadSynthIR()
{
    if (rawSynthBuffer.getNumSamples() > 0)
        loadIRFromBuffer (rawSynthBuffer, rawSynthSampleRate, true);
}
```

`loadSelectedIR()` now calls `reloadSynthIR()` instead of silently returning:

```cpp
if (idx < 0)
{
    pingProcessor.reloadSynthIR();
    return;
}
```

The +15 dB output trim applied by `synthIR()` is already baked into the result vectors before `loadIRFromBuffer` is ever called, so it is preserved in `rawSynthBuffer` and survives every subsequent `reloadSynthIR()` call without double-boosting.

### Stretch and Decay for synthesised IRs

`parameterChanged` (PluginEditor.cpp) fires whenever the Stretch or Decay knob changes and must trigger an IR reload so the new parameter value is baked into the convolver. The original implementation called `pingProcessor.loadIRFromFile(pingProcessor.getLastLoadedIRFile())` directly, which unconditionally reloaded the last file-based IR — clobbering any active synth IR, because `lastLoadedIRFile` is never cleared when a synth IR is loaded.

**Fix:** `parameterChanged` now calls `loadSelectedIR()` instead:

```cpp
void PingEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "stretch" || parameterID == "decay")
        loadSelectedIR();
}
```

`loadSelectedIR()` is the single-entry-point for all IR reloads and already routes correctly: synth IR (combo idx < 0) → `reloadSynthIR()`; file IR (combo idx ≥ 0) → `loadIRFromFile()`. The previous `&& pingProcessor.getLastLoadedIRFile().existsAsFile()` guard was removed — both paths guard themselves internally.

---

## Parameters (APVTS)

All parameters live in `PingProcessor::apvts` (an `AudioProcessorValueTreeState`). String IDs are in the `IDs` namespace at the top of `PluginProcessor.cpp`. Always use `IDs::xyz` to reference them — never raw string literals.

| ID | Name | Range | Default |
|----|------|-------|---------|
| `drywet` | Dry/Wet | 0–1 | 0.3 |
| `predelay` | Predelay (ms) | 0–500 | 0 |
| `decay` | Damping | 0–1 | 1.0 |
| `stretch` | Stretch | 0.5–2.0 | 1.0 |
| `width` | Width | 0–2 | 1.0 |
| `moddepth` | LFO Depth | 0–1 | 0 |
| `modrate` | LFO Rate | 0.01–2 | 0.5 |
| `tailmod` | Tail Modulation | 0–1 | 0 |
| `delaydepth` | Delay Depth (ms) | 0.5–8 | 2.0 |
| `tailrate` | Tail Rate (Hz) | 0.05–3 | 0.5 |
| `inputGain` | IR Input Gain (dB) | -24–12 | 0 |
| `outputGain` | Wet Output (dB) | -24–12 | 0 |
| `irInputDrive` | IR Input Drive | 0–1 | 0 |
| `erLevel` | Early Reflections (dB) | -48–6 | 0 |
| `tailLevel` | Tail (dB) | -48–6 | 0 |
| `erCrossfeedOn` | ER Crossfeed On | bool | false |
| `erCrossfeedDelayMs` | ER Crossfeed Delay (ms) | 5–15 | 10 |
| `erCrossfeedAttDb` | ER Crossfeed Att (dB) | -24–0 | -6 |
| `tailCrossfeedOn` | Tail Crossfeed On | bool | false |
| `tailCrossfeedDelayMs` | Tail Crossfeed Delay (ms) | 5–15 | 10 |
| `tailCrossfeedAttDb` | Tail Crossfeed Att (dB) | -24–0 | -6 |
| `plateOn` | Plate On | bool | false |
| `plateDiffusion` | Plate Diffusion | 0.30–0.88 | 0.40 |
| `plateColour` | Plate Colour | 0–1 | 0.5 |
| `plateSize` | Plate Size | 0.5–4.0 | 1.0 |
| `plateIRFeed` | Plate IR Feed | 0–1 | 0 |
| `bloomOn` | Bloom On | bool | false |
| `bloomSize` | Bloom Size | 0.25–2.0 | 1.0 |
| `bloomFeedback` | Bloom Feedback | 0–0.65 | 0.25 |
| `bloomTime` | Bloom Time (ms) | 50–500 | 200 |
| `bloomIRFeed` | Bloom IR Feed | 0–1 | 0 |
| `bloomVolume` | Bloom Volume | 0–1 | 0 |
| `cloudOn` | Cloud On | bool | false |
| `cloudDepth` | Cloud Depth | 0–1 | 0.3 |
| `cloudRate` | Cloud Rate | 0.1–4.0 | 1.0 |
| `cloudSize` | Cloud Size (ms) | 1–40 | 5.0 |
| `cloudVolume` | Cloud Volume | 0–1 | 0 |
| `cloudIRFeed` | Cloud IR Feed | 0–1 | 0 |
| `shimOn` | Shimmer On | bool | false |
| `shimPitch` | Shimmer Pitch | −24–+24 semitones (integer steps) | +12 |
| `shimSize` | Shimmer Size | 0.5–4.0 | 1.0 |
| `shimColour` | Shimmer Colour | 0–1 | 0.7 |
| `shimIRFeed` | Shimmer IR Feed | 0–1 | 0.5 |
| `shimVolume` | Shimmer Volume | 0–1 | 0 |
| `reversetrim` | Reverse Trim | 0–0.95 | 0 |
| `b3freq/b3gain/b3q` | Low Shelf EQ | 20–1200 Hz / ±12 dB / slope 0.3–2.0 | 200 Hz, 0 dB, 0.707 |
| `b0freq/b0gain/b0q` | Peak 1 EQ | 50–16k Hz / ±12 dB / Q 0.3–10 | 400 Hz, 0 dB, 0.707 |
| `b1freq/b1gain/b1q` | Peak 2 EQ | — | 1000 Hz, 0 dB, 0.707 |
| `b2freq/b2gain/b2q` | Peak 3 EQ | — | 4000 Hz, 0 dB, 0.707 |
| `b4freq/b4gain/b4q` | High Shelf EQ | 2000–20k Hz / ±12 dB / slope 0.3–2.0 | 8000 Hz, 0 dB, 0.707 |

`LFO Rate` is display-inverted: `period_seconds = 2.01 - rawValue`. So UI left = slow, UI right = fast.

All level-changing parameters use `SmoothedValue` (20 ms ramp) to prevent zipper noise on knob moves.

---

## Signal flow (processBlock)

```
Input (stereo)
  │
  ├──────────────────────────────► dryBuffer (copy, held for final blend)
  │
  ▼
[Licence gate — clears buffer and returns if unlicensed]
  │
  ▼
Predelay (0–500 ms, circular buffer up to 2 s)
  │
  ▼
Input Gain (dB, smoothed)
  │
  ▼
Harmonic Saturator (cubic soft-clip: x − x³/3, inflection ±√3, drive mix + compensation)
  │
  ▼
[Plate: parallel path — 6-stage allpass diffuser (g=diffusion) + 1-pole colour LP]  (if plateOn)
  │   plateBuffer stored; irFeed portion added to convolver input
  │
  ▼
[Bloom Insertion 1: 6-stage allpass cascade (g=0.35 hardcoded, delays scaled by bloomSize) on (input + feedbackTap)]  (if bloomOn)
  │   bloomBuffer stored; bloomIRFeed portion added to convolver input
  │   feedback tap: bloomFbBufs[ch][wp] = cascade output (NOT wet signal) — written here, same sample loop
  │
  ▼
[Cloud Insertion 1: previous block's cloudBuffer × cloudIRFeed injected into main signal]  (if cloudOn && cloudIRFeed > 0)
  │   1-block deferred: cloudBuffer written at Insertion 2, read here next block
  │
  ▼
[Shimmer Insertion 1: previous block's shimBuffer × shimIRFeed injected into main signal]  (if shimOn && shimIRFeed > 0)
  │   1-block deferred: shimBuffer written at Insertion 2, read here next block
  │
  ▼
Convolution  ── see "Convolution modes" below
  │  (optional crossfeed: ER and Tail each get L↔R delayed/attenuated copy when on, then ER/Tail mix)
  ▼
5-band EQ: low shelf → peak 1 → peak 2 → peak 3 → high shelf (JUCE makeLowShelf / makePeakFilter / makeHighShelf, per-band ProcessorDuplicator)
  │
  ▼
Stereo decorrelation allpass (R channel only: 2-stage, 7.13 ms + 14.27 ms, g=0.5 — preserves mono sum, widens tail)
  │
  ▼
LFO Modulation (optional — wet gain × (1 + depth × sin(2π·phase)))
  │
  ▼
Stereo Width (M/S: S × width)
  │
  ▼
Tail Chorus Modulation (optional — variable delay ±depthMs, skipped for ER-only IRs)
  │
  ▼
[Cloud Insertion 2: 8-line LFO delay bank on wet signal]  (if cloudOn)
  │   cloudBuffer ← lineSum/8; wet signal += cloudBuffer × cloudVolume (if cloudVolume > 0)
  │   LFO phases advanced per-sample; R channel uses π phase offset for decorrelation
  │
  ▼
[Shimmer Insertion 2: two-grain Hann-windowed pitch shifter on wet signal]  (if shimOn)
  │   shimBuffer ← grain output × shimColour LP; wet signal += shimBuffer × shimVolume (if shimVolume > 0)
  │   Pitch ratio = 2^(shimPitch/12); grain length = kShimGrainLen × shimSize
  │
  ▼
Output Gain (dB, smoothed)
  │
  ▼
[Spectrum analysis push — lock-free FIFO for GUI EQ display]
  │
  ▼
Dry/Wet blend: out = wet × √(mix) + dry × √(1−mix)   [constant-power crossfade]
  │
  ▼
[Bloom Volume: bloomBuffer × bloomVolume added here]  (if bloomOn — independent of dry/wet control)
  │
  ▼
Output (stereo) + L/R peak meter update
```

---

## Convolution modes

The mode is set by `useTrueStereo` (bool, set when loading the IR).

### Stereo mode (2-channel IR)
- `erConvolver` (stereo) + `tailConvolver` (stereo).
- Both process the same input. Output: `ER × erLevel + Tail × tailLevel`.
- Output is scaled ×0.5 to compensate for stereo convolution gain doubling.

### True-stereo mode (4-channel IR — from IR Synth)
- 4 ER mono convolvers: `tsErConvLL`, `tsErConvRL`, `tsErConvLR`, `tsErConvRR`.
- 4 Tail mono convolvers: same naming pattern.
- `lIn → LL + RL → lEr`;  `rIn → LR + RR → rEr`.
- Tail channels are summed the same way then mixed back.

### Post-convolution crossfeed (ER and Tail)

After convolution and **before** the ER/tail mix (and before EQ), each path can add a **delayed** and **attenuated** copy of the opposite channel (L→R and R→L) to improve stereo imaging and pan-tracking (e.g. tight early image with wider tail, or vice versa).

- **Two independent paths:** **ER crossfeed** (early reflections only) and **Tail crossfeed** (tail only), each with its own on/off switch, delay (5–15 ms), and attenuation (−24–0 dB). When a path’s switch is off, that path’s crossfeed is skipped; delay/att knobs have no “off” meaning.
- **Processing:** For each path that is on, the corresponding buffer(s) are processed in place: L += gain × delayed(R), R += gain × delayed(L), using circular delay lines (max 15 ms at current sample rate). Same logic for both true-stereo (separate lEr/rEr and lTail/rTail) and stereo 2ch (buffer = ER, tailBuffer = tail).
- **State:** Four delay lines in `PluginProcessor` (`crossfeedErBufRtoL`, `crossfeedErBufLtoR`, `crossfeedTailBufRtoL`, `crossfeedTailBufLtoR`), initialised in `prepareToPlay()`, used only in `processBlock()`. No IR Synth engine changes.
- **UI (main editor, Row 2):** Two groups of two rotary knobs each, directly below the IR Controls row in the main editor. **ER Crossfade**: DELAY + ATT knobs, pill-style on/off switch (`erCrossfeedOnButton`, component ID `ERCrossfeedSwitch`) centred below the pair, group header "ER Crossfade" drawn above. **Tail Crossfade**: same layout with `tailCrossfeedOnButton` (ID `TailCrossfeedSwitch`) and header "Tail Crossfade". Controls live in `PluginEditor` (not `IRSynthComponent`) and are wired directly to APVTS in the editor constructor. The switches use `PingLookAndFeel` pill-switch drawing. Knobs use `rotarySliderFillColourId = accent`. Controls are purely live/real-time — they do not trigger any IR recalculation.

---

## Plate onset (Feature 1 — implemented)

### What it does

Replaces the "room geometry in the first 50 ms" feel with a dense, immediate diffuse wash — the defining acoustic property of a physical plate reverb. Rather than discrete early reflections that reveal room shape, the onset blooms instantly into an undifferentiated density.

### How it works

A cascade of 6 allpass filters applied to the **input signal after the saturator and before convolution** acts as a pre-diffuser. Every incoming transient is scattered across neighbouring samples. When that cloud passes through the convolution IR, the early reflections are blurred together into something that sounds and feels like the fast-building onset of a plate.

### DSP state (`PluginProcessor.h`)

```cpp
struct SimpleAllpass {
    std::vector<float> buf;
    int   ptr    = 0;
    int   effLen = 0;   // 0 = use buf.size(); set each block for plateSize scaling
    float g      = 0.7f;
    float process (float x) noexcept { ... }
};
static constexpr int kNumPlateStages = 6;
std::array<std::array<SimpleAllpass, kNumPlateStages>, 2> plateAPs; // [ch][stage]
std::array<float, 2> plateShelfState { 0.f, 0.f };  // 1-pole lowpass state per channel
```

The `SimpleAllpass` struct in `PingDSPTests.cpp` must stay **exactly in sync** with this definition (field names, field order, `effLen` semantics, `process()` logic). The DSP tests define it locally and serve as the spec.

### `prepareToPlay`

Base prime delays at 48 kHz: `{ 24, 71, 157, 293, 431, 691 }` samples. Buffers allocated at **4× base primes** so the full `plateSize` 0.5–4.0 range needs no reallocation. All g values set to 0.70. `plateShelfState` filled to zero. `plateBuffer` sized to `(2, samplesPerBlock)` and cleared.

### `processBlock` insertion point

After the saturator, **before** the convolution block. At `density = 0` the blend formula `in * (1 - density) + shaped * density` collapses to the raw input, so no audible effect even if `plateOn = true`. At `plateOn = false` the block is skipped entirely — zero DSP overhead.

`plateColour` sets the 1-pole lowpass cutoff applied to the diffused signal: 0 → 2 kHz (warm, dark — EMT 140 character), 1 → 8 kHz (bright — AMS RMX16 character). The `effLen` for each stage is computed once per block (not per sample) as `round(platePrimes[s] * sr / 48000 * plateSize)`.

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `plateOn` | bool | false | Enables the cascade; zero overhead when off |
| `plateDiffusion` | 0.30–0.88 | 0.70 | Allpass g coefficient applied to all 6 stages each block; lower = gentle scatter, higher = very dense diffusion |
| `plateColour` | 0–1 | 0.5 | 1-pole LP cutoff applied to the diffused signal: 0 → 2 kHz (warm), 1 → 8 kHz (bright). Readout displays the actual cutoff in kHz. |
| `plateSize` | 0.5–4.0 | 1.0 | Scales all 6 allpass delays; readout shows the largest delay time in ms (prime 691 × size / 48000 × 1000). At size=1.0 → ~14.4 ms, size=4.0 → ~57.6 ms. Buffers allocated at 4× base primes to cover the full range without reallocation. |
| `plateIRFeed` | 0–1 | 0 | Adds the processed plate signal into the IR convolver input (on top of the main signal). At 0: plate has no effect; at 1: full plate signal added to convolver input. |

---

## Bloom hybrid (Feature 2 — implemented)

### What it does

Creates a progressively-building, self-diffusing signal from the input. Think of it as a guitar pedal: it has its own internal feedback loop that builds a dense, swelling texture, with two independent outputs — one that feeds into the IR convolver (adding reverb on top of the bloom) and one that goes directly to the final output. The reverb has no return path into Bloom.

Two elements work together:

- **Pre-convolution allpass cascade** — 6 stages with short delays (~5–40 ms per channel), creating a textured diffuse swirl rather than discrete echoes. Separate L/R prime sets produce genuinely independent stereo textures.
- **Self-contained cascade feedback** — a fraction of the cascade's own output is fed back into its input (`bloomTime` ms later), causing density to build with each recirculation. The convolved wet signal is **not** part of this loop.

### Architecture — Bloom as a self-contained pedal upstream of the reverb

The main signal into the convolver is **not modified** by the cascade itself. The cascade output is stored in `bloomBuffer` and delivered through two independent output paths, both defaulting to 0 (consistent with `plateIRFeed = 0`):

- **`bloomIRFeed`** — adds `bloomBuffer * irFeed` additively to the convolver input (on top of the main signal). Like plugging a Bloom pedal into the reverb's input jack.
- **`bloomVolume`** — adds `bloomBuffer * volume` directly to the **final output after the dry/wet blend**, so it is heard regardless of the wet/dry setting.

At both defaults = 0, Bloom has no audible effect until the user raises at least one output control.

### DSP state (`PluginProcessor.h`)

```cpp
static constexpr int kNumBloomStages     = 6;
static constexpr int kBloomFeedbackMaxMs = 500;
std::array<std::array<SimpleAllpass, kNumBloomStages>, 2> bloomAPs; // [ch][stage]
std::array<std::vector<float>, 2> bloomFbBufs;   // 500 ms circular feedback buffer
std::array<int, 2>                bloomFbWritePtrs { 0, 0 };
juce::AudioBuffer<float>          bloomBuffer;   // per-block bridge: Insertion 1 → Volume output
```

Bloom reuses the existing `SimpleAllpass` struct (defined for Plate). The `PingDSPTests.cpp` definition of `SimpleAllpass` must stay in sync.

### `prepareToPlay`

Separate L/R prime delay sets at 48 kHz — L: `{ 241, 383, 577, 863, 1297, 1913 }` (~5–40 ms), R: `{ 263, 431, 673, 1049, 1531, 2111 }` (~5.5–44 ms). Incommensurate across channels to produce genuinely independent L/R textures after feedback cycles. Buffers allocated at **2× base primes** so bloomSize 0.25–2.0 needs no reallocation. `effLen` set each block via `bloomSize` (same pattern as `plateSize`). g = 0.35f hardcoded (transparent scatter). Feedback buffer allocated at `500 ms × sampleRate`. `bloomBuffer` sized to `(2, samplesPerBlock)`.

### `processBlock` insertion points (2 locations)

**Insertion 1 — after Plate block, before convolution (the full Bloom cascade block):**
- Read feedback tap (`bloomTime` ms back in `bloomFbBufs`), add to input, run through 6-stage cascade (L/R independent primes, `effLen` set per-block via `bloomSize`), store in `bloomBuffer`.
- Add `bloomBuffer * bloomIRFeed` to the main signal before the convolver.
- At the bottom of the same per-sample loop: write the cascade output (`diff`) into `bloomFbBufs[ch][wp]` and advance the write pointer. This is the feedback tap — it uses the cascade output, **not** the convolved wet signal, keeping Bloom's loop entirely self-contained.
- `bloomOn = false` skips entirely — zero overhead.

**Insertion 2 — after dry/wet blend, before peak meter:**
- Add `bloomBuffer * bloomVolume` to the final output buffer.
- Because this is after the dry/wet blend, `bloomVolume` is audible regardless of the plugin's wet/dry setting — it behaves like the direct output of a pedal in parallel with the reverb.

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `bloomOn` | bool | false | Enables Bloom; zero overhead when off |
| `bloomSize` | 0.25–2.0 | 1.0 | Scales all 6 allpass delay times (like `plateSize` for Plate). At 1.0 the L delays span ~5–40 ms (textured, dense); at 2.0 they span ~10–80 ms (more spacious); at 0.25 ~1.25–10 ms (very dense, clangorous). Readout shows multiplier (e.g. "1.00×"). |
| `bloomFeedback` | 0–0.65 | 0.25 | Wet→input feedback amount; safety-clamped at 0.65. Higher = more self-sustaining swell |
| `bloomTime` | 50–500 ms | 200 ms | Feedback tap delay — how far back in the wet signal the feedback reads. Short = fast rhythmic bloom, long = slow expansive sustain. Readout displays integer ms. |
| `bloomIRFeed` | 0–1 | 0 | How much bloom cascade output injects additively into the convolver input |
| `bloomVolume` | 0–1 | 0 | How much bloom cascade output is added to the final output after dry/wet blend (independent of wet/dry) |

### UI layout (Row 4 — "Bloom hybrid")

Immediately after Row 3 (Plate), same `rowKnobSize`/`rowStep`/`rowStartX` constants. Single group "Bloom hybrid" with 5 knobs + pill switch (`bloomOnButton`, component ID `BloomSwitch`) right-aligned in the group header. Group header bounds stored as `bloomGroupBounds`. Editor height bumped from 600 → **672 px**. Row 4 uses `row4AbsY = row3AbsY + row3TotalH_` (absolute anchor, same pattern as Rows 2/3). The 6-knob grid (LFO Depth/Width/etc.) is now anchored at `row4AbsY + row4TotalH_ + 70` (was `row3AbsY + row3TotalH_ + 70`).

---

## Cloud Multi-LFO (Feature 3 — implemented)

### What it does

Eight independently-paced LFO-modulated delay lines applied to the wet reverb tail, after Tail Chorus. Each line animates the tail with subtle pitch and time variation; together they produce a shimmering, living quality — widening and enlivening long reverb tails without discrete echoes. The R channel uses a π phase offset on all LFO phases for free stereo decorrelation.

Unlike Plate (pre-conv diffusion) and Bloom (pre-conv feedback texture), Cloud is purely post-convolution. It does not have its own feedback loop — any "re-reverberation" effect must flow through the convolver via `cloudIRFeed`.

### Architecture — post-convolution tail animator

Cloud runs in two insertion points within `processBlock`:

- **Cloud Insertion 1** (pre-convolution, before the convolver block): Reads `cloudBuffer` from the *previous* block and injects it × `cloudIRFeed` into the main signal before convolution. This implements a 1-block deferred IR feed — necessary because Cloud runs post-convolution but its output needs to enter the convolver.
- **Cloud Insertion 2** (post-Tail Chorus, before Output Gain): Runs the 8-line LFO delay bank on the current wet signal. Stores `lineSum / 8` in `cloudBuffer` (the bridge to Insertion 1 next block). Adds `cloudBuffer × cloudVolume` to the wet signal if volume > 0.

`cloudBuffer` is a persistent member variable that naturally bridges the two blocks. No separate deferred-write buffer is needed.

### LFO rate design

Geometric spacing: `r_i = 0.04 × k^i` Hz where `k = pow(0.35/0.04, 1/(kNumCloudLines-1)) ≈ 1.3165`, giving 8 lines from 0.04 Hz to 0.35 Hz. This ensures no two lines share a rational beat frequency (same principle as the FDN LFO spacing). LFO phases are staggered at `i × π/4` on initialisation.

### DSP state (`PluginProcessor.h`)

```cpp
static constexpr int   kNumCloudLines    = 8;
static constexpr float kCloudBaseDepthMs = 3.0f;
static constexpr float kCloudSizeMaxMs   = 40.0f;
static constexpr float kCloudBufMs       = 45.0f;  // sizeMax + depthMax + 2 ms margin

std::array<std::array<std::vector<float>, kNumCloudLines>, 2> cloudBufs;       // [ch][line]
std::array<std::array<int,               kNumCloudLines>, 2> cloudWritePtrs {};
std::array<float, kNumCloudLines> cloudLfoPhases    {};
std::array<float, kNumCloudLines> cloudLfoBaseRates {};
juce::AudioBuffer<float> cloudBuffer;     // bridge: Insertion 2 → Insertion 1 (next block)
int                      cloudLastBlockSize = 0;
```

### `prepareToPlay`

Buffer size = `ceil(45.0 × sampleRate / 1000)` samples per line per channel (covers full `cloudSize` + `cloudDepth` range). Geometric LFO rates computed once. Phases staggered at `i × π/4`. `cloudBuffer` sized to `(2, samplesPerBlock)` and cleared.

### `processBlock` details

**Insertion 1** reads `cloudBuffer` (from previous block, `cloudLastBlockSize` samples) and adds `× cloudIRFeed` to the main input buffer before the convolver. On the first block, `cloudBuffer` is zeroed and has no effect.

**Insertion 2** iterates per-sample:
- Each LFO phase advances by `baseRate × cloudRate / sampleRate` (2π radians per cycle normalised).
- Modulation depth per line = `cloudDepth × kCloudBaseDepthMs × sampleRate / 1000` samples.
- Read position = `writePtr - cloudSize_samples - depth × sin(phase)`, with linear interpolation.
- R channel: same rates/depths but `phase + π` for decorrelation.
- `lineSum` averages all 8 lines; `cloudBuffer[ch][i] = lineSum / 8`.
- If `cloudVolume > 0`: wet signal += `cloudBuffer[ch][i] × cloudVolume`.

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `cloudOn` | bool | false | Enables Cloud; zero overhead when off |
| `cloudDepth` | 0–1 | 0.3 | LFO modulation depth — scales `kCloudBaseDepthMs` (3 ms). At 0: static delay (no shimmer). At 1: ±3 ms swing. Readout shows 0.00–1.00. |
| `cloudRate` | 0.1–4.0 | 1.0 | Global rate multiplier applied to all 8 geometric LFO rates. At 1.0: lines span 0.04–0.35 Hz. At 4.0: 0.16–1.4 Hz (faster shimmer). Readout shows multiplier (e.g. "1.00×"). |
| `cloudSize` | 1–40 ms | 5.0 ms | Base delay time shared by all 8 lines. Longer = more spacious, more pitched. Shorter = subtle smear. Readout shows ms (1 decimal). |
| `cloudVolume` | 0–1 | 0 | Adds `cloudBuffer × cloudVolume` to the wet signal at Insertion 2. Audible at any dry/wet setting. Default 0 = no direct output until user raises it. |
| `cloudIRFeed` | 0–1 | 0 | Injects previous-block `cloudBuffer × cloudIRFeed` into the convolver input (Insertion 1). Creates a "re-reverberated shimmer" — the Cloud output gets convolved again. Loop gain = `cloudIRFeed × convolver_gain` stays < 1 for real room IRs. Default 0 = no feed. |

### UI layout (Row 5 — "Cloud multi-LFO")

Immediately after Row 4 (Bloom), same `rowKnobSize`/`rowStep`/`rowStartX` constants. Single group "Clouds post convolution" with 5 knobs (DEPTH, RATE, SIZE, VOLUME, IR FEED) + pill switch (`cloudOnButton`, component ID `CloudSwitch`) right-aligned in the group header. Group header bounds stored as `cloudGroupBounds`. Editor height bumped from 672 → **744 px**. Row 5 uses `row5AbsY = row4AbsY + row4TotalH_` (absolute anchor). The 6-knob grid (LFO Depth/Width/etc.) is now anchored at `row5AbsY + row5TotalH_ + 70`.

---

## Shimmer (Feature 4 — implemented)

### What it does

A two-grain Hann-windowed pitch shifter applied to the post-convolution wet signal. Pitch-shifted output is fed back into the convolver input (via the same 1-block deferred bridge pattern as Cloud), producing the characteristic harmonically-rising shimmer wash associated with the Eventide H3000. A 1-pole lowpass (`shimColour`) controls warmth. Unlike Bloom and Cloud, `shimIRFeed` defaults to **0.5** — the effect is immediately audible when enabled.

### Architecture

Identical bridge pattern to Cloud: `shimBuffer` persists between `processBlock` calls.

- **Shimmer Insertion 1** (pre-convolution, after Cloud Insertion 1): reads previous block's `shimBuffer` × `shimIRFeed`, injects into main signal before convolver.
- **Shimmer Insertion 2** (post-Cloud Insertion 2, before Output Gain): runs two-grain pitch shifter on wet signal, applies `shimColour` 1-pole LP, stores in `shimBuffer`. Adds `× shimVolume` to wet signal if `shimVolume > 0`.

### Grain engine

Two grains offset by half a grain length (phase 0 and 0.5), Hann windowed, summed and normalised by 0.5. Read pointer advances `pitchRatio` samples per input sample. On phase rollover the read head is snapped to `writePtr − effGrainLen` (one full grain behind the write head), ensuring it always reads real signal rather than zeroes. This is the exact spec verified by DSP_07–DSP_09.

`effGrainLen = round(kShimGrainLen × shimSize)`. Buffer allocation is fixed at `kShimBufLen = 8192` — no reallocation needed at runtime.

### DSP state (`PluginProcessor.h`)

```cpp
static constexpr int kShimGrainLen = 512;
static constexpr int kShimBufLen   = 8192;

struct ShimmerVoice {
    std::vector<float> grainBuf;   // kShimBufLen samples, circular
    int   writePtr    = 0;
    float readPtrA    = 0.f;
    float readPtrB    = 0.f;       // initialised to kShimGrainLen/2 in prepareToPlay
    float grainPhaseA = 0.f;
    float grainPhaseB = 0.5f;
};

std::array<ShimmerVoice, 2> shimVoices;
std::array<float, 2>        shimColourState { 0.f, 0.f };
juce::AudioBuffer<float>    shimBuffer;
int                         shimLastBlockSize = 0;
```

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `shimOn` | bool | false | Enables Shimmer; zero overhead when off |
| `shimPitch` | −24–+24 st (integer steps) | +12 | Semitone interval for pitch shift. +12 = octave up. Readout: "+12 st". Integer NormalisableRange enforces musical intervals. |
| `shimSize` | 0.5–4.0 | 1.0 | Scales grain length (effGrainLen = kShimGrainLen × shimSize). Longer = smoother pitch at cost of more smear. Readout: multiplier "1.00×". |
| `shimColour` | 0–1 | 0.7 | 1-pole LP on shifter output: 0 → 2 kHz (warm/dark), 1 → 20 kHz (full brightness). Readout: cutoff in Hz. |
| `shimIRFeed` | 0–1 | **0.5** | Injects shimBuffer into convolver input (the pitch-shifted re-reverberation path). Default 0.5 — audible immediately on enable. |
| `shimVolume` | 0–1 | 0 | Adds shimBuffer directly to wet signal before output gain, independent of dry/wet. |

### UI layout (Row 6 — "Shimmer")

Immediately after Row 5 (Cloud), same `rowKnobSize`/`rowStep`/`rowStartX` constants. Single group "Shimmer" with 5 knobs (PITCH, SIZE, COLOUR, IR FEED, VOLUME) + pill switch (`shimOnButton`, component ID `ShimmerSwitch`) right-aligned in the group header. Group header bounds stored as `shimGroupBounds`. Editor height bumped from 744 → **816 px**. Row 6 uses `row6AbsY = row5AbsY + row5TotalH_` (absolute anchor). The 6-knob grid (LFO Depth/Width/etc.) is now anchored at `row6AbsY + row6TotalH_ + 70`.

---

## IR loading pipeline

Called from the **message thread** only (`loadIRFromFile` / `loadIRFromBuffer`). Never call from the audio thread.

Steps applied in order:
1. **Reverse** (if `reverse == true`) — sample order flipped per channel. Reverse Trim then skips `trimFrac × length` samples from the start of the reversed IR.
2. **Stretch** — linear-interpolation time-scale to `stretchFactor × originalLength`.
3. **Decay envelope** — exponential fade: `env(t) = exp(−decayParam × 6 × t)`, where `decayParam = 1 − UI_decay`.
4. **ER / Tail split at 80 ms** — crossover at `0.080 × sr` samples, with a 10 ms fade-out on ER and 10 ms fade-in on Tail. Each half is loaded into its respective convolver pair.

> **Known inconsistency:** The file-load IR split uses **80 ms** but the IR Synth engine's internal crossover uses **85 ms** (`ec = 0.085 × sr`). These should ideally be unified. Don't introduce a third value.

---

## IR Synth engine (IRSynthEngine.cpp)

Synthesises physically-modelled room IRs. Called from a background thread; reports progress via a callback. Produces a 4-channel 48 kHz 24-bit WAV (channels: iLL, iRL, iLR, iRR).

**Default ceiling material:** `"Painted plaster"` (low absorption, RT60 ≈ 12 s for the default room). Changed from the previous `"Acoustic ceiling tile"` default which was far too absorptive for a realistic room.

### Character tab defaults

The following defaults are applied consistently in three places: `IRSynthComponent.cpp` (UI combo/slider initial values), `IRSynthParams` in `IRSynthEngine.h` (struct member defaults), and `PluginProcessor.cpp` (XML/sidecar load fallbacks). When adding or changing a Character tab default, update all three locations.

| Parameter | Old default | New default | Notes |
|-----------|-------------|-------------|-------|
| Walls | `Painted plaster` | `Concrete / bare brick` (combo id 1) | More reflective; suited to reverberant spaces |
| Windows | 0 % | 27 % (0.27) | Partial glass blend on wall material |
| Audience | 0 | 0.45 | Mid-level occupancy absorption |
| Diffusion | 0.4 | 0.40 | Unchanged |
| Vault | `None (flat)` | `Groin / cross vault (Lyndhurst Hall)` (combo id 4) | Adds vault height multiplier and HF absorption |
| Organ case | 0 | 0.59 | Significant scattering and y-wall opening |
| Balconies | 0 | 0.54 | Added balcony absorption area |

Floor, ceiling, room shape, room dimensions, and all other parameters are unchanged from their previous defaults.

**Placement tab defaults** (kept in sync in three places: `FloorPlanComponent.h` `TransducerState::cx`, `IRSynthEngine.h` `IRSynthParams`, and `PluginProcessor.cpp` XML/sidecar fallbacks): **Speakers** at room centre in depth (y = 0.5), **25% and 75%** across the width (facing down). **Microphones** at 1/5 up from bottom (y = 0.8), **35% and 65%** across the width; L mic faces up-left (−135°), R mic faces up-right (−45°).

Pipeline:
1. **calcRT60** — Eyring formula at 8 octave bands (125 Hz – 16 kHz) + air absorption correction.
2. **calcRefs** × 4 — Image-source method for each of the 4 cross-channel paths (seeds 42–45). Accepts `minJitterMs` and `highOrderJitterMs` parameters for order-dependent time jitter (see close-speaker handling below). Speaker directivity fades to omnidirectional with bounce order: order 0–1 use full cardioid, order 2 blends 50/50 cardioid+omni, order 3+ are fully omnidirectional. Mic polar pattern applied per-reflection. Reflection order `mo` is sized by the **original RT60-based formula**: `mo = min(60, floor(RT60_MF × 343 / minDim / 2))`. No distance gate applied in full-reverb mode — sources decay naturally through cumulative surface absorption. **ER-only mode** (`eo = true`): sources with `t >= ec` (arrival time ≥ 85 ms after all jitter) are skipped via `continue`; this is the primary gate that keeps late periodic reflections out of ER-only output. **Feature A — Lambert diffuse scatter** (enabled via `#if 1` in `calcRefs`): for each specular Ref of order 1–3 when `ts > 0.05`, `N_SCATTER = 2` secondary Refs are spawned with random azimuth and 0–4 ms extra delay; amplitude = specular × `(ts * 0.08) / N_SCATTER * 0.6^(order-1)` (~3% at order 1 at default ts). Fills in comb-like gaps between specular spikes. Scatter arrivals are also gated (`scatterT >= ec` in eo mode) since the +0–4 ms offset can push a near-boundary parent over the ec edge.
3. **renderCh** × 4 — Accumulate into per-band buffers (8 bands), bandpass-filter each band, sum. **Temporal smoothing** was tried (5 ms moving average from 60 ms onward) but disabled: replacing each sample with the window mean attenuated content after 60 ms by ~200× and caused “extremely quiet reverb”; the block is commented out in `renderCh`. Then apply deferred allpass diffusion (pure-dry 0–65 ms, crossfade 65–85 ms, fully diffused 85 ms+). Accepts `reflectionSpreadMs` (always 0 — disabled) and **Feature C — frequency-dependent scatter**: when `freqScatterMs > 0` (= `ts × 0.5`), bands 1–7 get a deterministic hash-based time offset (0 at 125 Hz, max ±freqScatterMs at 16 kHz). Simulates surface micro-structure without changing the Ref struct.
4. **renderFDNTail** × 2 (L seed 100, R seed 101) — 16-line FDN with Hadamard mixing, prime delay times derived from mean free path, LFO-modulated with per-line depth scaled by the line's HF attenuation (see frequency-dependent modulation depth below), 1-pole LP per line for frequency-dependent decay from RT60. FDN seeding starts at `t_first` (min direct-path arrival sample), so the full 85 ms warmup is spent on real signal. FDN output starts at `ecFdn = t_first + ec`. Seeding continues through `fdnMaxRefCut = (ecFdn + fdnMaxMs) × 1.1` before free-running. Before seeding, `eL` and `eR` are run through a three-stage allpass pre-diffusion cascade (short → long → short) to spread the seed spike over 50+ ms and prevent any single FDN delay line from producing a coherent repeating echo. **LFO rate spacing is geometric, not linear** — rates follow `r_i = 0.07 × k^i` Hz where `k = (0.45/0.07)^(1/15) ≈ 1.1321`, spanning 0.07–0.45 Hz over 16 lines. This ensures no two lines share a rational beat frequency; linear spacing (previously `0.07 + i × 0.025`) created a worst-case beat of 0.375 Hz (~2.7 s period) audible as periodic density fluctuation in long tails. **The 1-pole LP HF reference uses `rt60s[7]` (16 kHz RT60)**, not the previous `rt60s[5]` (4 kHz), giving more aggressive high-frequency damping that correctly reflects dominant air absorption above 8 kHz.
5. **Blend** — Two separate paths depending on `eo`:
   - **Full-reverb** (`eo = false`): FDN fade-in `tf` (linear 0→1) and ER fade-out `ef` (cosine 1→`erFloor`) run simultaneously over the same 20 ms window `[ecFdn−xfade, ecFdn]`. After `ecFdn`, `ef` holds at `erFloor = 0.05` — the already-diffuse late ISM continues at 5% amplitude, decaying naturally at the room RT60 without any extra envelope. A dynamic per-channel gain (`fdnGainL`/`fdnGainR`) is computed just before the blend loop: ER reference is the windowed-RMS of `eLL+eRL` / `eLR+eRR` over `[ecFdn−xfade, ecFdn]` (the last 20 ms before the crossfade, reliably in the allpass-diffused zone); FDN reference is the windowed-RMS of `tL`/`tR` over `[ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade]` (after the longest delay line has completed its first full recirculation cycle and the FDN is near quasi-steady-state), multiplied by the `0.5` blend factor. Gain is clamped to `[1.0, kMaxGain=16]` — the FDN is only ever boosted, never attenuated. Formula: `out = ER * bakedErGain * ef + FDN * 0.5 * bakedTailGain * fdnGain * tf`.
   - **ER-only** (`eo = true`): No FDN. A 10 ms cosine taper (`erFade`) runs over `[ec−10ms, ec]`, fading to zero at `ec` to avoid a hard silence edge. Formula: `out = ER * bakedErGain * erFade`.
6. **Feature B — Modal bank** — After the blend, all 4 channels are processed by `applyModalBank`: a parallel bank of high-Q IIR resonators (via `bpFQ`) tuned to axial modes f_n = c·n/(2L) for n=1..4 (10–250 Hz). Q from 125 Hz RT60; gain 0.18, normalised by mode count. Adds standing-wave ringing the ISM under-represents. For the default large room (e.g. 28×16×12 m) all axial modes fall below ~57 Hz (below the 125 Hz synthesis band), so the feature is effectively a no-op there; it becomes audible in smaller rooms or if the mode range were extended.
7. **Post-process** — LP@18kHz, HP@20Hz, 500ms cosine end fade. **No peak normalisation** — amplitude scales naturally with the 1/r law (proximity effect preserved).
8. **Output level trim (+15 dB)** — After all processing, a fixed `+15 dB` gain (`pow(10, 15/20) ≈ 5.6234`) is applied to all four channels. This corrects for the observed ~15 dB shortfall in synthesised IR output level without touching any synthesis calculations. Applied as the very last step in `synthIR()`, immediately before moving results into `IRSynthResult`.

### Close / coincident speaker handling

When the two speakers are close together, perfectly periodic image-source reflections (especially floor-ceiling bounces at 2H/c intervals) can produce audible repeating delays. Two distance thresholds are checked against the speaker separation `srcDist`:

- **Close** (`srcDist < 30% of min room dimension`): order-dependent jitter is applied in `calcRefs` — 0.25 ms on order 0–1 (keeps direct and first-order tight), 2.5 ms on order 2+ (scrambles the periodic pattern). In ER-only mode, diffusion 0.50 is also applied even though ER-only normally uses no diffusion.
- **Coincident** (`srcDist < 10% of min room dimension`): additionally, `eL`/`eR` are scaled by 0.8 before FDN seeding to prevent the doubled direct-path spike from overloading the delay lines.

`reflectionSpreadMs` was also explored (smearing each reflection across a time window in `renderCh`) but caused the early-reflection pattern to collapse and is disabled (set to 0).

### FDN seeding (start at first arrival, warmup + delay-line coverage)

The seeding start is deferred to `t_first` — the sample at which the nearest speaker-to-mic direct path arrives (min of the 4 distances). Before `t_first`, `eL`/`eR` are silent; seeding silence wastes the warmup window and leaves the FDN delay lines under-loaded.

`renderFDNTail` is called with `erCut = ecFdn = t_first + ec`. This uses a three-phase approach:

| Phase | Samples | FDN input | Output captured |
|-------|---------|-----------|----------------|
| 1 — warmup | 0 → ecFdn | `eL[i]` (real signal from t_first onwards) | No |
| 2 — seed + output | ecFdn → fdnMaxRefCut | `eL[i]` (ongoing) | Yes |
| 3 — free-running | fdnMaxRefCut → irLen | 0 (silent) | Yes |

`fdnMaxRefCut = (ecFdn + fdnMaxMs) × 1.1`. Any spike at time `T` in `eL` echoes at `T + fdnMaxMs`. The worst spike is the direct-path at `t_first`, which echoes at `t_first + fdnMaxMs = ecFdn - ec + fdnMaxMs`. This is always less than `fdnMaxRefCut` (since `fdnMaxMs < fdnMaxMs × 1.1 + ecFdn × 1.1 - ecFdn`), so it is always absorbed. The formula is placement-independent: centred speakers (t_first ≈ 5 ms) give fdnMaxRefCut ≈ 259 ms; far-wall speakers (t_first ≈ 70 ms) give ≈ 335 ms.

The combination loop fade-in also uses `ecFdn` (not the fixed `ec`) so the FDN is smoothly ramped in over 20 ms as it starts outputting.

The FDN provides smooth, diffuse background energy. The image-source field is the primary carrier of the reverberant tail throughout the full IR length.

### IR amplitude and normalisation

Peak normalisation has been removed. IR amplitude now scales as `1/max(dist, 0.5)` — the natural 1/r law. Speakers placed close to a mic produce a louder wet signal; speakers placed further away produce a quieter one. This preserves the physical proximity effect as a meaningful user-controllable parameter. Clipping can only occur at distances below ~85 cm (cardioid default), which is not a realistic placement.

Sidecar files: `.ping` (JSON) stored alongside each synthesised WAV, containing the `IRSynthParams` used to generate it. Loaded by `loadIRSynthParamsFromSidecar()` to recall IR Synth panel state.

---

## Licence system

- **Algorithm:** Ed25519 (libsodium). Public key embedded in `LicenceVerifier.h`.
- **Serial format:** Base32-encoded signed payload `normalisedName|tier|expiry`.
- **Tiers:** `demo`, `standard`, `pro`. Expiry `9999-12-31` = perpetual.
- **Storage:** Checked in order: `~/Library/Audio/Ping/P!NG/licence.xml`, then `/Library/Application Support/Audio/Ping/P!NG/licence.xml`.
- **In processBlock:** Checked first — clears buffer and returns immediately if unlicensed.

To generate serials (developer only — never commit `private_key.bin`):
```bash
cd Tools && g++ -std=c++17 -o keygen keygen.cpp -lsodium
./keygen --name "Customer Name" --tier pro --expiry 2027-12-31
```

---

## File locations (runtime)

| Purpose | Path |
|---------|------|
| IR folder | `~/Documents/P!NG/IRs/` |
| Licence (user) | `~/Library/Audio/Ping/P!NG/licence.xml` |
| Licence (system) | `/Library/Application Support/Audio/Ping/P!NG/licence.xml` |
| IR Synth sidecar | `<irname>.wav.ping` (same folder as the WAV) |

---

## Debugging: very quiet reverb

If the user reports **very quiet reverb** or **only the direct signal audible**:

- **Already fixed in this codebase:** A 5 ms **temporal moving average** in `renderCh` (from 60 ms onward) was attenuating content after 60 ms by ~200×. That block is **commented out** in `IRSynthEngine.cpp`. Do not re-enable it without redesigning (e.g. envelope-based or very short window) so peak level is preserved.
- **True-stereo wet gain:** The true-stereo (4ch / IR Synth) path applies a compensation gain (`trueStereoWetGain = 2.0f` in `PluginProcessor::processBlock`) so that un-normalised synthesised IRs (1/r level) are in a usable range at default ER/Tail. Tune this constant if reverb is still too quiet or too hot with synth IRs.
- **If the problem persists**, things to check:
  1. **PluginProcessor:** ER/Tail level sliders (`erLevel`, `tailLevel`) — default 0 dB; if they were changed or mis-loaded they could be very low. True-stereo path does *not* apply the 0.5f trim (only the 2‑channel stereo path does after convolution).
  2. **IR Synth output level:** No peak normalisation; level follows 1/r (proximity). Default room and placement may produce a relatively low-level IR; that’s intentional. Check whether the issue is only with **synthesised** IRs or also with **loaded** stereo WAVs.
  3. **Gains in synthIR:** `bakedErGain` / `bakedTailGain` (default 1.0 unless bake balance is on), FDN tail blend uses `0.5 * bakedTailGain * fdnGainL/R` (intentional: each L/R output gets half of each of the two tails; `fdnGain` applies the level-match calibration — log it to verify it falls in range 1–16 and isn't clamped).
  4. **Any other smoothing or scaling** in `renderCh` or `synthIR` that could reduce level (e.g. a re-introduced moving average or an extra gain factor).
  5. **FDN quiet-tail (resolved):** root cause was a level calibration problem, not a seeding gap. The FDN IS seeded by the full ER (`eL = eLL + eRL`). The three-stage pre-diffusion cascade reduces immediate per-sample amplitude to ~0.85% of the seed (energy is preserved but spread over 50+ ms), and the FDN delay lines haven't all completed a first cycle by `ecFdn`. The ER boundary fix (`ef=0` after `ecFdn`) exposed this by removing the late-ISM direct contribution that was previously masking the low FDN onset level. Fix: `erFloor=0.05` residual + dynamic `fdnGain` level-match (see pipeline step 5 above). **Three bugs were found and fixed in the fdnGain calculation** (a first attempt silenced the tail completely): (a) ER reference window `[ecFdn-2·xfade, ecFdn-xfade]` straddled the sparse-ISM / allpass-onset boundary and gave near-zero RMS — fixed by shifting to `[ecFdn-xfade, ecFdn]`; (b) FDN measurement window `[ecFdn, ecFdn+xfade]` captured the initial energy burst from 99 ms of warmup accumulation, which can exceed the ER snapshot and give `fdnGain < 1` — fixed by measuring at `[ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade]` after the FDN settles; (c) the `0.5` blend factor was missing from the fdnRms denominator, making `fdnGain` 2× too small — now included.

Starting a **new chat** and referencing **@CLAUDE.md** is a good way to give the agent full project and reverb-pipeline context for a fresh pass at this issue.

---

## Key design decisions to be aware of

- **Character tab defaults live in three places** — `IRSynthComponent.cpp` (UI initial values), `IRSynthEngine.h` (`IRSynthParams` struct defaults), and `PluginProcessor.cpp` (XML/sidecar load fallbacks). All three must be kept in sync. Missing a location means the UI shows the right default but a freshly constructed `IRSynthParams` (or a preset missing the attribute) will use the old value.
- **Placement tab defaults (speaker/mic positions) live in three places** — `FloorPlanComponent.h` (`TransducerState::cx`), `IRSynthEngine.h` (`IRSynthParams` source_* / receiver_*), and `PluginProcessor.cpp` (XML `getDoubleAttribute` fallbacks for slx/sly/srx/sry, rlx/rly/rrx/rry). Speakers at **25% and 75%** (y = 0.5), mics at **35% and 65%** (y = 0.8). Keep all three in sync when changing placement defaults.
- **`/Library` install path** — Intentional. The .pkg installer deploys system-wide. Don't change copy dirs to `~/Library`.
- **Speaker directivity in image-source** — `spkG()` is applied using the real source→receiver angle, not the image-source position. This is deliberate: image-source positions aren't physical emitters and using them would give wrong distance-dependent directivity results.
- **Deferred allpass diffusion** — The allpass starts at 65 ms (not sample 0) to prevent early-reflection spikes from creating audible 17ms-interval echoes in the tail.
- **Stereo decorrelation allpass (R only)** — After EQ and before Width, the **right** channel of the wet buffer is passed through a 2-stage allpass (7.13 ms, 14.27 ms, g=0.5). Delays are incommensurate with FDN/diffuser times. Allpass has unity gain so the mono sum L+R is unchanged; the phase/time difference on R alone reduces stereo collapse at strong FDN modes and makes the tail feel more spacious. Implemented in `PluginProcessor` (decorrDelays, decorrBufs, decorrPtrs, decorrG); initialised in `prepareToPlay()`, processed in `processBlock()` before `applyWidth()`.
- **Post-convolution crossfeed (ER and Tail)** — After convolution, before ER/tail mix: when **ER crossfeed on** or **Tail crossfeed on**, the corresponding buffer(s) get a delayed (5–15 ms) and attenuated (−24–0 dB) copy of the opposite channel (L→R, R→L). Four delay lines (two per path), on/off switch per path. Params: `erCrossfeedOn`, `erCrossfeedDelayMs`, `erCrossfeedAttDb`, `tailCrossfeedOn`, `tailCrossfeedDelayMs`, `tailCrossfeedAttDb`. UI: **main editor Row 2** (see UI layout notes) — controls live in `PluginEditor`, not `IRSynthComponent`. Purely live/real-time; no IR recalculation on any crossfeed parameter change. No IR Synth engine changes.
- **Constant-power dry/wet** — `√(mix)` / `√(1−mix)` crossfade. Don't change to linear without a reason.
- **SmoothedValue everywhere** — All parameters that scale audio use `SmoothedValue` (20 ms). Any new audio-scaling parameter should do the same.
- **loadIR from message thread only** — Convolver loading is not real-time safe. Always call `loadIRFromFile` / `loadIRFromBuffer` from the message thread (UI callbacks, timer, not processBlock).
- **IR Synth runs on a background thread** — `synthIR()` is blocking and can take several seconds. Never call it from the audio or message thread directly; always dispatch to a background thread.
- **No IR peak normalisation** — Removed intentionally. IR amplitude follows the 1/r law so speaker–mic distance affects wet level naturally. Don't add normalisation back without considering that it would flatten the proximity effect.
- **Image-source order is RT60-based, not distance-gated (full-reverb mode)** — `mo = min(60, floor(RT60_MF × 343 / minDim / 2))`. In full-reverb mode, no arrival-time gate is applied in `calcRefs`; all sources within `mo` reflections are used and they decay naturally. Do not introduce an artificial distance cutoff in the non-eo path — it creates a perceptible character change where the image-source field cuts off and the FDN takes over alone. In **ER-only mode** (`eo = true`), sources with `t >= ec` *are* gated (`if (eo && t >= ec) continue`) — this is what keeps late periodic floor-ceiling bounces out of the ER output and is the correct behaviour.
- **FDN seeding starts at `t_first`, not sample 0** — `t_first` is the sample of the minimum direct-path arrival across all four speaker-mic pairs. `ecFdn = t_first + ec` is the warmup-end / output-start sample, ensuring the full 85 ms warmup is spent on real reverberant signal. `fdnMaxRefCut = (ecFdn + fdnMaxMs) × 1.1` guarantees the direct-path echo (which would arrive at `t_first + fdnMaxMs`) is absorbed by Hadamard mixing within Phase 2. This is placement-independent: the formula works for both centred speakers and mics-on-far-wall layouts without adjustment.
- **FDN seed pre-diffusion — three-stage cascade** — `eL`/`eR` are run through three AllpassDiffuser stages in sequence (short [17 ms spread, g=0.75] → long [35 ms spread, delays 31.7/17.3/11.2/5.7 ms, g=0.62] → short again) before FDN seeding. This smears the seed over 50+ ms so no single FDN delay line gets a clean isolated spike, eliminating repeating pings regardless of speaker placement. The ER output buffers (`eLL`, `eRL`, etc.) are not pre-diffused.
- **Speaker directivity fades to omni with bounce order** — order 0–1 use full cardioid, order 2 blends 50/50, order 3+ are fully omnidirectional. This is more physically correct: by order 3 the energy has scattered from multiple surfaces and arrives at the source position from all directions. Applying full cardioid to high-order reflections over-attenuates the late reverb tail.
- **Close-speaker jitter is order-dependent** — when `srcDist < 30% minDim`, order 0–1 get ±0.25 ms jitter (preserves discrete early reflections) and order 2+ get ±2.5 ms jitter (breaks the periodic floor-ceiling flutter without smearing the early field). `reflectionSpreadMs` in `renderCh` is disabled — it smeared individual reflections in a way that caused the ER pattern to collapse.
- **ER/FDN crossfade uses cosine×linear envelopes with an erFloor residual** — The 20 ms window `[ecFdn−xfade, ecFdn]` has a linear `tf` FDN fade-in and a cosine `ef` ER fade-out (from 1.0 to `erFloor = 0.05`, not to 0). After `ecFdn`, `ef` holds at `erFloor = 0.05`; the fully-diffuse late-ISM contributes a natural 5% undertone that decays at the room RT60. A dynamic `fdnGainL/R` calibrates the FDN level. **Critical window choices:** ER reference = `[ecFdn-xfade, ecFdn]` (last 20 ms before crossfade, in the allpass diffusion zone — do NOT shift earlier into the sparse ISM zone or RMS collapses to near-zero). FDN measurement = `[ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade]` after the FDN settles (do NOT measure at onset `[ecFdn, ecFdn+xfade]` — the burst from 99 ms of accumulated warmup energy exceeds the ER snapshot and produces `fdnGain < 1`, silencing the tail). The `0.5` blend factor must be included in the fdnRms denominator. `fdnGain` is clamped to `[kMinGain, kMaxGain]` = `[1/16, 16]` (±24 dB) — the tail can be boosted **or attenuated** to maintain consistent ER/tail balance as speaker-to-mic distance changes. The previous floor of `1.0` meant the tail was never attenuated, causing the tail-to-ER ratio to shift for far-speaker placements. `kMinGain = 1.0 / kMaxGain`. The ER-only path uses a separate 10 ms cosine taper `erFade` at `ec`; the `erFloor`/`fdnGain` block is inside `if (!eo)` and does not affect ER-only mode.
- **Feature A (Lambert scatter) is conservative** — `scatterWeight = (ts * 0.08) / N_SCATTER * 0.6^(order-1)`; at default ts≈0.74, order 1 scatter is ~3% of specular, decaying 0.6× per order. Keeps scattered energy from dominating. Toggle with `#if 1` / `#if 0` in `calcRefs`.
- **Temporal smoothing is disabled** — A 5 ms moving average from 60 ms onward was tried to soften periodic peaks; it replaced each sample with the window mean and cut level after 60 ms by ~200× (“extremely quiet reverb”). The block is commented out in `renderCh`. Any future smoothing should preserve peak level (e.g. very short window or envelope-based).
- **Feature C (frequency-dependent scatter) uses a deterministic hash** — `renderCh` has no RNG. Hash of `(r.t + 1) * 2654435769 XOR b * 1234567891` gives per-band, per-reflection offsets; `freqScatterMs = ts * 0.5` (≈0.37 ms at default), so ±~18 samples at 4 kHz.
- **Feature B (modal bank) gain is fixed** — `modalGain = 0.18`; normalisation by mode count keeps level consistent. For large default rooms, axial modes sit below the 125 Hz band so the effect is inaudible there.
- **FDN LFO rates use geometric spacing** — rates follow `r_i = 0.07 × k^i` Hz where `k ≈ 1.1321`, giving 16 values from 0.07 Hz to 0.45 Hz. The irrational ratio means no two delay lines share a rational beat frequency, eliminating the periodic density flutter (~2.7 s period) that the previous linear spacing (`0.07 + i × 0.025` Hz) produced. Do not revert to arithmetic spacing. Phase offsets remain `i × π/4` (commensurable, but dominated by the incommensurable rates).
- **FDN LFO modulation depth is frequency-dependent (per line)** — Each of the 16 delay lines has its own modulation depth, linearly interpolated from its 1-pole LP filter coefficient `lpAlpha[i]`: `depthMs = 0.3 + (1.2 − 0.3) × alpha`. Lines with `alpha ≈ 1.0` (LF-dominated, low HF attenuation) get the maximum ±1.2 ms — they carry mostly low-frequency energy and can tolerate large delay excursions without perceptible pitch modulation. Lines with lower `alpha` (HF-attenuated, rapid HF decay) get shallower depth down to ±0.3 ms — those lines still carry treble content and uniform ±1.2 ms would cause audible chorus artefacts on high-frequency transients. The buffer allocation uses `LFO_DEPTH_MAX_MS = 1.2` throughout to guarantee no overrun regardless of per-line depth. Do not revert to a uniform scalar depth.
- **Absorption model uses 8 octave bands (125 Hz – 16 kHz)** — `N_BANDS = 8`, `BANDS[] = {125, 250, 500, 1k, 2k, 4k, 8k, 16k}`. The original 6-band model (125–4 kHz) under-represented air absorption above 4 kHz; extending to 8k and 16k gives physically correct HF decay in large rooms. `AIR[6] = 0.066` dB/m (8 kHz), `AIR[7] = 0.200` dB/m (16 kHz) per ISO 9613-1 at 20°C, 50% RH. Material absorption data at 8k/16k is from ISO 354 reference tables. The `N_BANDS` constant gates all band loops — do not hardcode `6` anywhere. `IRSynthResult::rt60` is now 8 elements. The FDN HF LP reference was updated from `rt60s[5]` (4 kHz) to `rt60s[7]` (16 kHz) to match. The frequency-scatter and lateral-cue scale factors in `renderCh` were updated from `b/5.0` to `b/(N_BANDS-1)` so the 0→1 normalisation still spans the full band range.
- **Waveform display uses dB scale, not linear** — Synthesised IRs are 8× the longest RT60 in length (up to 30 s). On a linear scale the entire tail collapses to a flat line 60+ dB below the onset spike. `dBFloor = −60.0f` maps the full decay range to [0, 1] so all room sizes produce a visible "ski-slope" shape. Do not revert to linear scale. The per-pixel peak-envelope scan (scanning every sample in each pixel's time range) is also essential — stride-based single-sample lookup misses the true peak because each pixel covers hundreds of samples.
- **Version label is derived from `ProjectInfo::versionString`** — Do not hard-code the version string in `PluginEditor.cpp`. JUCE generates `ProjectInfo::versionString` automatically from `project(Ping VERSION x.y.z)` in `CMakeLists.txt`, so the label is always in sync with the build. Update `CMakeLists.txt` version when cutting a release; do not update `PluginEditor.cpp` separately.
- **Installer version lives in `Installer/build_installer.sh`** — The package filename/version passed to `pkgbuild` is controlled by the script `VERSION` variable. When cutting a release, bump both `project(Ping VERSION x.y.z)` in `CMakeLists.txt` and `VERSION` in `Installer/build_installer.sh` to keep the generated `.pkg` name/version in sync.
- **IR Input Gain and IR Input Drive live in Row 1; Wet Output Gain sits near `irKnobsCenterX`** — IR Input Gain (GAIN) and IR Input Drive (DRIVE) were moved to the small-knob Row 1 strip at the top of the main area. Wet Output Gain is positioned at `outputGainCenterX = irKnobsCenterX + smallKnobSize / 2 + controlShift` (controlShift = 50). `outputGainY = dryWetCenterY − smallKnobSize − irKnobGap` moves with `cy`. Do not re-introduce the old `irGainShift` formula — it was removed when the gain knobs moved to Row 1.
- **Trailing silence is auto-trimmed from synth IRs at load time** — `synthIR()` allocates `8 × max_RT60` (up to 30 s) but the signal decays to below −80 dB well before the end. `loadIRFromBuffer` trims to the last sample above `peak × 1e-4` plus a 200 ms safety tail (min 300 ms), before saving `rawSynthBuffer`. This keeps the convolvers lean and makes the Reverse Trim handle span actual signal. File-based IRs are not affected. Do not move this trim to after the `rawSynthBuffer` save — `reloadSynthIR()` would then re-introduce silence on every Reverse or Trim interaction.
- **`loadSelectedIR()` is the single entry point for all IR reloads — never call `loadIRFromFile()` or `reloadSynthIR()` directly from parameter listeners** — `parameterChanged` (Stretch, Decay) and all UI callbacks must go through `loadSelectedIR()`, which routes to `reloadSynthIR()` for synth IRs and `loadIRFromFile()` for file IRs. Bypassing this (e.g. calling `loadIRFromFile(getLastLoadedIRFile())` directly) will clobber any active synth IR because `lastLoadedIRFile` is never cleared when a synth IR is loaded.
- **Preset and IR save overwrite prompts are selection-based** — Save actions only prompt when the typed name matches the currently selected existing item in the corresponding editable combo (`presetCombo` or IR synth `irCombo`) and the target file already exists. Typing a different name saves directly as a new file. Use async JUCE dialogs (`AlertWindow::showAsync`) in plugin UI; avoid blocking modal loops.
- **`rawSynthBuffer` stores the pre-processing synth IR — do not save it after any transforms** — It must be saved as the very first thing inside the `fromSynth` block of `loadIRFromBuffer`, before reverse/trim/stretch/decay are applied. If it were saved after transforms, `reloadSynthIR()` would double-apply them on every Reverse or Trim interaction.
- **+15 dB output trim is intentional** — `synthIR()` applies a fixed `+15 dB` scalar (`gain15dB = pow(10, 15/20)`) to all four IR channels as the very last step, correcting for the observed output level shortfall. Do not remove this. It does not affect the synthesis calculations, RT60, ER/tail balance, or FDN gain calibration — it is a pure post-process scalar applied after everything else.
- **Plate pre-diffuser is a pure convolver pre-feed** — The Plate DSP runs in `processBlock` after the saturator. It processes the post-saturator signal through the allpass cascade and colour LP, storing the result in `plateBuffer`. **IR FEED** adds `plateBuffer * irFeed` to the convolver input before convolution — the diffused signal feeds the IR alongside the main signal. The only output is via the convolver; there is no direct parallel output. At irFeed=0 the plate has no effect. Plate parameters never trigger an IR recalculation. The IR output (and therefore all IR_01–IR_11 test golden values) is unchanged.
- **Plate `effLen` and `g` are set per block, not per sample** — `plateSize` is read once, the 6 effective delay lengths are computed, and all `effLen` fields written before the sample loop. `plateDiffusion` is also read once and written to all `plateAPs[ch][s].g` before the sample loop. This avoids redundant computation while keeping the sample loop tight.
- **Plate `plateColour` is a 1-pole lowpass, not a high-shelf** — A simple 1-pole lowpass applied to the diffused signal before feeding to the convolver. At `colour = 0` the cutoff is 2 kHz (warm, dark — EMT 140 character); at `colour = 1` it is 8 kHz (bright — AMS RMX16 character). Do not replace it with a true biquad shelf — the 1-pole is intentional.
- **Plate signal path: pre-diffuser into convolver only** — The sample loop processes the input through the allpass cascade + colour LP, stores the result in `plateBuffer`, and adds `plate[i] * irFeed` to the main buffer. The convolver receives the main signal plus the diffused plate signal. There is no direct output path. `plateOn = false` skips the block entirely — zero overhead.
- **`plateDiffusion` sets g on all 6 stages simultaneously** — The g coefficient is written to all `plateAPs[ch][s].g` once per block before the sample loop. Range 0.30–0.88 keeps the filter stable and well below the unit-circle limit. Lower g = gentle, transparent scatter; higher g = very dense, metallic diffusion. Default 0.40 gives a gentle, transparent scatter suitable for a pre-diffuser.
- **Default editor height is now 816 px** — bumped from 528 → 600 (Row 3 Plate) → 672 (Row 4 Bloom) → 744 (Row 5 Cloud) → 816 (Row 6 Shimmer). The resize minimum (`minH`) remains 528 so users can still shrink the window. Both constants are at the top of the `PingEditor` constructor in `PluginEditor.cpp`.
- **EQ is pinned to the window bottom** — `eqGraph.setBounds(x, y, w, eqHeight + (h−ch))` extends the component into the `h/6` bottom margin, so the knob strip sits flush with the window bottom edge. Do not revert to `eqGraph.setBounds(eqRect)` — that clips the control strip at `ch` and leaves a dead strip below.
- **EQ has 5 bands: low shelf, 3 peaks, high shelf** — DSP order: `lowShelfBand → lowBand → midBand → highBand → highShelfBand` (all `ProcessorDuplicator`). IDs are `b3`/`b0`/`b1`/`b2`/`b4` (b3 and b4 are the shelves; b0–b2 are the original peaks preserved for preset backward-compat). Frequency response in `EQGraphComponent::getResponseAt()` uses a tanh-sigmoid approximation for shelves and a Gaussian for peaks — close enough for display, avoids needing DSP coefficient access at paint time.
- **EQ control strip uses a row-per-parameter layout** — FREQ knobs all share the same Y (Row 1); GAIN knobs are shifted right by `colW/5 + gainDX` (Row 2, zig-zag); Q/SLOPE knobs return to the FREQ X column below GAIN (Row 3). Each row has independent `DX`/`DY` fine-tuning constants (`freqDX/DY`, `gainDX/DY`, `qDX/DY`) in `EQGraphComponent::resized()`. The control strip is reserved with `removeFromBottom(ctrlH - 75)` — the -75 shifts the entire strip 75 px lower and extends the chart bottom by 75 px; the `DY` constants counteract this shift, so net knob positions equal the intended row offsets. Knob size is **32 px** (reduced 25% from 42 px). The `DX` constants were scaled proportionally (`freqDX/qDX: −10→−8`, `gainDX: +20→+15`). The `DY` constants are intentionally NOT scaled — they cancel the structural `ctrlH - 75` offset which is independent of knob size. Do not scale DY when resizing knobs. The graph area top is trimmed 60 px downward. Each knob has an accent-orange live readout above its grey parameter label.
- **Row Y positions in `PluginEditor` use absolute anchors to avoid JUCE `removeFromTop` clamping** — `removeFromTop(n)` silently clamps `n` to the rectangle's remaining height, so when `mainArea` has shrunk (due to large `eqMinH`) to less than the combined row heights, subsequent rows land in wrong positions. Row 2–6 Y positions are computed as `row2AbsY = topKnobRow.getBottom()`, `row3AbsY = row2AbsY + row2TotalH_`, etc. — independent of whatever height remains in `mainArea`. All group-header bounds, toggle LED Y positions, and knob Y positions for all rows use these absolute anchors. Right-side rows (R1/R2/R3) share the same Y anchors as their corresponding left-side rows (Rows 1/2/3).
- **`rowShiftUp = 30 - rowKnobSize` — all knob rows are shifted down by one knob height** — `rowShiftUp` is subtracted from every row Y position, so making it smaller (by `rowKnobSize`) pushes all 9 rows (left-side 1–4 and right-side R1–R3) down by exactly `rowKnobSize` pixels simultaneously. The original `rowShiftUp = 30` provided a 30 px upward nudge; the current `30 - rowKnobSize` gives a net downward displacement from the natural `removeFromTop` position. Do not revert to a fixed positive value without updating all group-header Y positions accordingly.
- **Preset combo + save button are right-aligned in the top bar, level with the Spitfire logo** — placed via `presetArea.getRight()` (= `w - rightLogoW - 4`): save button flush to the right edge, combo immediately to its left, "Preset" label further left. There is no second placement block lower in `resized()` — the top-bar placement is the only one. Do not add a repositioning block that moves the preset combo below the top bar.
- **Only Width remains in the large-knob grid at `row6AbsY + row6TotalH_ + 70`** — LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to right-side Row R3. The +70 px offset below the last small-knob row is unchanged. Do not revert the anchor back to any earlier row.
- **Bloom has two independent output paths (`bloomIRFeed` and `bloomVolume`), both defaulting to 0** — consistent with `plateIRFeed = 0`. The main signal is not modified by the bloom cascade; only additive injection via `bloomIRFeed` into the convolver and `bloomVolume` into the final output. At both defaults = 0, Bloom has zero effect when switched on.
- **`bloomBuffer` bridges Insertion 1 (pre-conv) and the post-dry/wet Volume injection within the same processBlock call** — it is not a feedback buffer. Populated at Insertion 1, read after the dry/wet blend. `bloomBuffer.clear()` at the top of Insertion 1 ensures no stale data. A reallocation guard (`if (numSamples > bloomBuffer.getNumSamples())`) handles hosts that exceed `maximumExpectedSamplesPerBlock`.
- **Bloom feedback tap is written inside Insertion 1's per-sample loop** — immediately after computing `diff` (the cascade output), `bloomFbBufs[ch][wp] = diff` is written and the pointer advanced. The convolved wet signal is **never** written to `bloomFbBufs`. This makes the feedback loop entirely self-contained: Bloom → `bloomFbBufs` → Bloom. The old architecture wrote the post-EQ wet signal to `bloomFbBufs` (old Insertion 3), which put the convolver inside the loop and caused feedback explosions with stereo IRs where the LL convolver path had significantly higher gain than the RL/LR paths.
- **`bloomVolume` is injected after the dry/wet blend** — it is added to the final output buffer after `buffer.addFrom(dryBuffer)`. This means `bloomVolume` is audible at any dry/wet setting, including fully dry. It behaves like the direct output level of a Bloom pedal sitting in parallel with the reverb unit. The old architecture injected before EQ, making it subject to both EQ and the dry/wet control.
- **Bloom g is hardcoded at 0.35 (transparent)** — `bloomDiffusion` was removed. g=0.35 was chosen to keep individual delay taps transparent (each tap contributes ~35% reflection vs 65% pass-through), so the character comes from the delay times and feedback density rather than allpass coloration. Unlike Plate where g is user-adjustable for density vs transparency, Bloom's character is defined by its delay structure. Do not add a diffusion control back.
- **Bloom uses separate L/R prime sets for genuine stereo independence** — L primes `{241, 383, 577, 863, 1297, 1913}` and R primes `{263, 431, 673, 1049, 1531, 2111}` are incommensurate (no shared factors). After several feedback cycles the L and R textures diverge into genuinely different patterns, filling the stereo field without any explicit width/decorrelation DSP. This is the primary source of Bloom's wide stereo character. Do not unify L/R primes or the stereo independence is lost.
- **Bloom delay range is ~5–40 ms (at size=1.0)** — this is deliberately below the ~30 ms threshold at which allpass delays become audible as discrete echoes. The previous {~39–300 ms} primes were all above this threshold, making the individual taps clearly audible as fragments. At 5–40 ms the allpass stages act as diffusers rather than distinct echoes, producing the "textured swirl" character. `bloomSize` scales linearly — at size=2.0 delays reach ~10–80 ms (more spacious), at size=0.5 ~2.5–20 ms (very dense).
- **Bloom feedback is safety-clamped at 0.65** — the loop gain is `bloomFeedback` alone (the convolver is no longer in the loop). The clamp provides a hard stability bound independent of IR content or signal level.
- **Cloud uses a 1-block deferred IR feed via `cloudBuffer`** — Cloud runs post-convolution, but its output needs to reach the convolver input. The bridge is `cloudBuffer` (a member variable): Insertion 2 writes to it; Insertion 1 reads it the following block. This introduces exactly 1 block of latency on the IR feed path, which is inaudible at any normal buffer size. No separate circular deferred-write buffer is needed. `cloudLastBlockSize` tracks the previous block's sample count so Insertion 1 reads exactly the right number of samples.
- **Cloud `cloudVolume` and `cloudIRFeed` both default to 0** — consistent with Bloom and Plate. Cloud has zero audible effect at defaults even when `cloudOn = true`. Users raise at least one output to hear it.
- **Cloud `cloudBuffer` bridges Insertion 2 → Insertion 1 across blocks, not within the same block** — unlike `bloomBuffer` which is written and read within the same processBlock call, `cloudBuffer` is written at Insertion 2 and consumed at Insertion 1 of the *next* block. Do not clear `cloudBuffer` at the start of each block — it must survive between blocks.
- **Cloud LFO rates use geometric spacing (0.04–0.35 Hz)** — same principle as FDN LFO. `k = pow(0.35/0.04, 1/(kNumCloudLines-1)) ≈ 1.3165`. No two lines share a rational beat frequency. LFO phases staggered at `i × π/4`. Do not change to arithmetic spacing.
- **Cloud R-channel decorrelation uses a π phase offset** — all 8 LFO phases for the R channel are offset by π relative to L. This gives free stereo width without any extra DSP: at any given moment L lines are near their maximum delay excursion when R lines are near minimum, and vice versa. No separate delay line set needed (unlike Bloom which uses separate L/R prime arrays).
- **Cloud has no self-feedback loop** — unlike Bloom, Cloud's delay lines do not feed back into themselves. The only recirculation path is via `cloudIRFeed` → convolver → wet signal → Cloud Insertion 2. The convolver is inherently attenuating for real room IRs, so loop gain < 1 for any physical IR. Very high-gain IR presets with boosted `outputGain` are the only edge case to be aware of.
- **Cloud `cloudDepth` scales `kCloudBaseDepthMs = 3.0f`** — the maximum ±3 ms swing was chosen to keep modulation below the ~5 ms threshold where pitch variation becomes audible as vibrato on sustained tones. At `cloudDepth = 0` the delay lines are static (no modulation), which gives a comb-filter character rather than shimmer.
- **Width is the only remaining large knob in the bottom grid** — LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to right-side Row R3 (aligned with Plate / Row 3). The grid anchor `row6AbsY + row6TotalH_ + 70` is unchanged.
- **Shimmer uses the same 1-block deferred bridge pattern as Cloud** — `shimBuffer` persists between `processBlock` calls. Insertion 2 writes; Insertion 1 of the next block reads. Do not clear `shimBuffer` at block start. `shimLastBlockSize` tracks the previous block's sample count.
- **Shimmer grain engine matches DSP_07–DSP_09 exactly** — two grains at phase 0 and 0.5, Hann windowed, read pointer advances `pitchRatio` per sample, grain reset snaps to `writePtr − effGrainLen`. Fixed buffers: `kShimGrainLen = 512`, `kShimBufLen = 8192`. The production code must stay in sync with these test specs.
- **`shimPitch` uses integer NormalisableRange (step=1)** — fractional semitones produce detuned output not aligned to musical intervals. The parameter layout uses `juce::NormalisableRange<float>(-24.f, 24.f, 1.f)`.
- **`shimIRFeed` defaults to 0.5** — unlike every other IR-feed parameter (Plate, Bloom, Cloud all default to 0), Shimmer defaults to 0.5 so the effect is immediately audible when enabled. This matches expected shimmer plugin UX: you enable it and it shimmers.
- **`shimColour` is a 1-pole LP on the grain shifter output** — cutoff = `2000 + shimColour × 18000` Hz. Applied per-sample inside Insertion 2 before storing to `shimBuffer`. `shimColourState[ch]` holds the per-channel filter state; reset to zero in `prepareToPlay`.
- **EQ response curve is display-only** — `getResponseAt()` is an approximation. The actual audio uses JUCE biquad coefficients. The two will not match exactly at steep slopes or very high/low frequencies, but are visually representative for a mixing EQ.
- **DRY/WET knob is centred at `w / 2` (full window centre, not `b` centre)** — `b` starts at `leftPad = w/6`, so `b.getCentreX() = 7w/12 ≠ w/2`. Using `w/2` ensures the knob, IR combo, and waveform are visually centred in the window. The preset combo is **not** centred here — it lives right-aligned in the top bar. The DRY/WET knob size is `bigKnobSize * 1.05f` (70% of the original `1.5f` multiplier). Do not revert to the old relative formula that positioned the knob between `stretchSlider` and `outputGainSlider`.
- **`cy` uses a phantom waveform anchor — do not replace it with the current waveform dimensions** — `cy` is computed from `phantomWaveCentreY`, which uses the *old* waveform dimensions (`0.36f × cw` wide, not the current `0.27f × cw`). This keeps the DRY/WET knob at the correct visual height. The +40 px offset (`cy + 40`) shifts the entire centre-column stack (DRY/WET → IR combo → waveform) downward by 40 px. If you change the +40 offset, all four items move together. The preset combo is no longer in this stack — it is right-aligned in the top bar, independent of `cy`.
- **Right-side rows R1/R2/R3 share Y anchors with left-side Rows 1/2/3** — Cloud knobs (R1) use `rowY`; Shimmer knobs (R2) use `row2KnobY`; Tail AM/Frq mod knobs (R3) use `row3KnobY`. Their group header Y positions match their left-side counterparts exactly. The `mainArea.removeFromTop()` calls for Rows 5 and 6 (Cloud/Shimmer) are kept to preserve `mainArea` state even though the actual knob Y values come from the absolute anchors.
- **Row R3 (Tail AM/Frq mod) uses an extra-gap split identical to Row 1** — `extraGap = (idx < 2) ? 5 : 0` places the gap between the AM pair (idx 0,1) and the Frq triple (idx 2,3,4). The formula `cx = b.getRight() - (4-idx)*rowStep - rowKnobSize/2 - extraGap` keeps the rightmost knob flush with `b.getRight()`. `tailAMModGroupBounds` and `tailFrqModGroupBounds` are stored as member variables and drawn in `paint()` with `drawGroupHeader`. No toggle pills — these are always-active controls.
