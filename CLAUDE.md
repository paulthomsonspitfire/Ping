# P!NG — CLAUDE.md

Developer context for AI-assisted work on this codebase.

---

## What this project is

**P!NG** (`PRODUCT_NAME "P!NG"`) is a stereo reverb plugin for macOS (AU + VST3) built with JUCE. It convolves audio with impulse responses (IRs) and also includes a from-scratch IR synthesiser that simulates room acoustics using the image-source method + a 16-line FDN.

**Current version:** 2.4.3 (see `CMakeLists.txt`)
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
| IR_11 | Golden regression lock — 30 samples from onset at index 482, locked to 17 sig-fig |
| IR_12 | FDN LFO rates have no rational beat frequencies (p,q ≤ 6 check across all 120 pairs) |
| IR_13 | IR end-of-buffer is near-silent — last 500 ms below −60 dB of peak (silence-trim precondition) |

**DSP tests (DSP_01 – DSP_14)** — test the hybrid-mode DSP building blocks. DSP_01–DSP_02 and DSP_10/12 cover `SimpleAllpass` (Plate and Bloom). DSP_03/04/11 cover Bloom feedback stability. DSP_05/06/13/14 cover Cloud granular. DSP_07–09 cover Shimmer. All tests define their own structs locally and must stay in sync with any production implementation.

| ID | Description |
|----|-------------|
| DSP_01 | Plate allpass cascade is truly all-pass (energy preserved to ±1% after drain) |
| DSP_02 | Plate at density=0 is bit-identical to bypass (IEEE-754 collapse) |
| DSP_03 | Bloom allpass is causal — no echo before first delay drains |
| DSP_04 | Bloom feedback is stable at maximum setting (0.65) — output decays after input stops (bloomTime = 300 ms) |
| DSP_05 | Cloud granular output does not amplify signal (RMS out ≤ RMS in × 1.05) |
| DSP_06 | Cloud grain Hann window is applied per grain (near-zero onset/end, peak ≈ 1 at mid-grain) |
| DSP_07 | Shimmer 12-semitone shift produces ~750 Hz from 375 Hz input |
| DSP_08 | Shimmer feedback decays after input stops (stability check) |
| DSP_09 | Shimmer semitone-to-ratio formula is correct for key musical intervals |
| DSP_10 | Plate `plateSize=2.0` doubles the effective allpass delay — first-return peak moves from sample ~691 to ~1382 |
| DSP_11 | Bloom feedback stable at minimum bloomTime (50 ms) — most stressful case (~20 recirculations/s) |
| DSP_12 | Bloom `bloomSize=2.0` doubles the effective allpass delay — first-return peak moves from sample ~1913 to ~3826 (longest L-channel prime) |
| DSP_13 | Cloud grains scatter across full buffer depth — max lookback ≥ 60% of 3 s buffer, scatter range ≥ 70% (guards "do not revert to 1–2 grain lengths" regression) |
| DSP_14 | Cloud feedback stable at maximum setting (0.7) — no NaN/Inf, output decays after input stops |

### Golden regression lock (IR_11)

IR_11 locks 30 samples of `iLL` starting at the first non-silent sample (onset index 482 for the 10×8×5 m small-room params). To update after a deliberate engine change:

```bash
./PingTests "[capture]" -s    # prints new onset index + 30 sample values
```

Paste the printed `onset_offset` and `golden_iLL[30]` values into `IR_11` in `PingEngineTests.cpp`, set `goldenCaptured = true`, and commit with a note explaining why the engine output changed.

### Key test design decisions

- **`PING_TESTING_BUILD=1`** — defined for the `PingTests` target; gates out `#include <JuceHeader.h>` in `IRSynthEngine.h`, making the engine compilable with no JUCE dependency.
- **Small-room params** — `width=10, depth=8, height=5 m`. Generates a ~2 s IR instead of the default 25 s, keeping individual test runtime under 4 s.
- **DSP tests are self-contained** — `SimpleAllpass` and the grain engines are defined locally in `PingDSPTests.cpp`. The `SimpleAllpass` production implementation now lives in `PluginProcessor.h`; the struct definition there must stay in sync with the one in the tests exactly.
- **DSP_07 uses 375 Hz input** (not 440 Hz) — 375 × 512/48000 = 4 exact integer cycles per grain, making grain resets phase-coherent. 440 Hz gives 4.69 cycles/grain, producing phase discontinuities that shift the DFT peak ~65 Hz below the true pitch-shifted frequency.
- **DSP_04 feedback delay** — the Bloom feedback circular buffer must be read from `fbWp` (the oldest slot, = full `fbLen` samples of delay), not `(fbWp-1)` (1-sample IIR). The same convention applies in the production implementation. DSP_04 tests bloomTime = 300 ms (mid-range); DSP_11 tests the 50 ms minimum.
- **IR_12 rational-beat bound** — p,q ≤ 6 covers all perceptible simple beat ratios between FDN LFO pairs. **0.1% tolerance** (`TOL = 0.001`) is used: the nearest quasi-rational case in the geometric series is k^13 ≈ 5.016, which is 0.32% off from 5/1 — safely outside the threshold, with a drift period of ~888 s (completely inaudible). An earlier 1% tolerance incorrectly flagged the three pairs with Δ=13 (indices 0/13, 1/14, 2/15) as beats. The FDN now uses geometric spacing `r_i = 0.07 × k^i`; the previous linear spacing had 11/15 as an exact rational, producing a ~2.7 s beat.
- **DSP_10/12 first-return peak test** — allpass filters are IIR and never fully "drain," so drain-energy comparisons are unsuitable for testing `effLen`. Instead, DSP_10 (Plate) and DSP_12 (Bloom) exploit the fact that a single allpass of delay d, fed an impulse, produces its first large positive output exactly at sample d. At `plateSize=2.0`, d doubles from 691 to 1382; at `bloomSize=2.0`, d doubles from 1913 to 3826.
- **DSP_11 short bloomTime is the worst case** — At 50 ms (minimum bloomTime) there are ~20 feedback round-trips per second. More trips per second means more opportunities for energy to accumulate if the loop gain is near 1. DSP_04 (300 ms) and DSP_11 (50 ms) together bound the stability guarantee across the full 50–500 ms range.
- **DSP_13 full-buffer scatter is critical** — Cloud grains must look back across the full 3 s capture buffer (range [grainLen, 90% of buffer]), not just 1–2 grain lengths. Concurrent grains reading very different moments of audio history produce spectrally independent signals; limited scatter collapses them into near-identical content, creating comb filtering. The test uses 200 deterministic spawns (seed 12345u) to verify max lookback ≥ 60% and scatter range ≥ 70% of the buffer.
- **DSP_14 Cloud feedback uses 1/√N normalisation** — grain output is `sum / sqrt(activeCount)` (sqrt-power), not `sum / activeCount` (1/N). The existing DSP_05 gain test uses 1/N (an older design artefact); DSP_14 uses the correct 1/√N to match production. Both tests pass with either formula since 1/N is more conservative for stability.
- **`SimpleAllpass` struct must stay in sync** — The `effLen` field (default 0 = use `buf.size()`) must be present in both `PingDSPTests.cpp` and `PluginProcessor.h`. Plate stages set `effLen` each block via `plateSize` (14× alloc, range 0.5–14.0). Bloom stages also now set `effLen` each block via `bloomSize` (2× alloc, range 0.25–2.0). Plate buffers at 14× base primes; Bloom buffers at 2× base primes.
- **IR_10 onset detection** — IR_10 measures RT60 by scanning the IR for the first sample above a noise floor threshold (`onsetIdx`), then scanning forward from that point for the first window where energy drops 60 dB below peak. Without onset detection, the scan started at i=0 and the 10 ms smoothing window (480 samples at 48 kHz) could end before the direct-path arrival, finding silence and setting measuredRT60=0. Engine changes that shift the onset (e.g. changing `sz`/`rz` heights) move the onset but the scan self-corrects automatically. Do not revert IR_10 to a fixed-start scan.
- **IR_11 golden onset index** — currently 482 for the 10×8×5 m small-room params with speaker at 1 m / mic at 3 m. The onset shifted from 371 to 482 when `sz`/`rz` were changed from `He × 0.55` to fixed physical heights. If the engine geometry changes again, re-run `./PingTests "[capture]" -s` and paste the new values (see Golden regression lock section above).
- **IR_13 silence-trim precondition** — verifies the last 500 ms of the full-length synthIR output is below −60 dB of peak. The actual trim threshold in `loadIRFromBuffer` is `jmin(peak × 1e-4, 1e-4)` (−80 dB below peak, capped at −80 dBFS absolute); −60 dB is a conservative precondition check sufficient to catch FDN decay regressions without being brittle.

---

## Source files

```
Source/
  PluginProcessor.h/.cpp   — Audio engine, APVTS parameters, IR loading, processBlock
  PluginEditor.h/.cpp      — Main UI, IR list, IR Synth panel host
  IRManager.h/.cpp         — Scans ~/Library/Audio/Impulse Responses/Ping for .wav/.aiff files
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
  ping_logo.png, ping_logo_blue.png, spitfire_logo.png, reverse_button.png, reverse_button_glow.png, texture_bg.jpg
Tools/
  keygen.cpp               — Serial number generator (developer only, never shipped)
  generate_factory_irs.cpp — Batch factory IR + preset generator (developer only, never shipped)
  trim_factory_irs.py      — Trims trailing silence from factory IR .wav files
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

The editor (`PluginEditor.cpp` `resized()`) uses a content-area offset: `leftPad = w/6`, `cw = w - leftPad`, `ch = h - h/6`. All proportional constants use `cw`/`ch`. The window is divided into **left-side rows** (small knobs, left-justified from `rowStartX`) and **right-side rows** (small knobs, right-justified to `rightRowEdge`), with a **centre column** for the DRY/WET knob stack.

### Left-side rows (Rows 1–4)

All left-side rows use `rowKnobSize = (int)(sixKnobSize * 0.6f)`, `rowStep = rowKnobSize + rowGap`, and `rowStartX = max(8, w/128) + 10` (5 px further right than before). Row Y positions use absolute anchors (see Key Design Decisions) to avoid JUCE `removeFromTop` clamping.

**Row 1 — IR Input + IR Controls (5 knobs)**
`mainArea.removeFromTop(rowTotalH)` reserves the strip — **note: `groupLabelH` is NOT added here**. Removing it brings Rows 1 and 2 (and R1 and R2) closer together, giving tight within-group spacing. A 5 px extra gap before index 2 splits into two groups:
- **IR Input**: knob 0 = GAIN (`irInputGainSlider`), knob 1 = DRIVE (`irInputDriveSlider`)
- **IR Controls**: knob 2 = PREDELAY, knob 3 = DAMPING, knob 4 = STRETCH

`rowY = topKnobRow.getY() + groupLabelH - 10 - rowShiftUp`. With `rowShiftUp = 30 - rowKnobSize` (a negative value), the net effect is that all rows sit one `rowKnobSize` lower than the original 30 px upward shift. Group header bounds: `irInputGroupBounds`, `irControlsGroupBounds`.

**Row 2 — ER Crossfade + Tail Crossfade (4 knobs + 2 power-button switches)**
Same 5 px gap before index 2 splits into two groups:
- **ER Crossfade**: knob 0 = DELAY, knob 1 = ATT, power-button switch `erCrossfeedOnButton`
- **Tail Crossfade**: knob 2 = DELAY, knob 3 = ATT, power-button switch `tailCrossfeedOnButton`

Group header bounds: `erCrossfadeGroupBounds`, `tailCrossfadeGroupBounds`. All controls are live/real-time — no IR recalculation.

`row2AbsY = topKnobRow.getBottom() + 4`. `row3AbsY = row2AbsY + row2TotalH_ + 39` — the **+39 gap (14 base + 25 extra) between Rows 2 and 3** provides clear visual separation: **IR Input + ER/Tail Crossfade** (Rows 1–2, tight) vs **Plate + Bloom** (Rows 3–4, tight) on the left, and **Clouds + Shimmer** (R1–R2, tight) vs **Tail AM/Frq mod** (R3, separate) on the right. Because R3 shares the row3AbsY anchor, the same gap automatically appears between Shimmer and Tail on the right side. The +4 on row2AbsY ensures Row1→Row2 knob-top spacing matches the Row3→Row4 reference.

**Row 3 — Plate pre-diffuser (4 knobs + 1 switch)**
No extra inter-group gap — single **"Plate pre-diffuser"** group: DIFFUSION, COLOUR, SIZE, IR FEED, power-button switch `plateOnButton` right-aligned. Group header bounds: `plateGroupBounds`.

**Row 4 — Bloom hybrid (5 knobs + 1 switch)**
Single **"Bloom hybrid"** group: SIZE, FEEDBACK, TIME, IR FEED, VOLUME, power-button switch `bloomOnButton` right-aligned. Group header bounds: `bloomGroupBounds`.

### Right-side rows (Rows R1–R3)

All right-side rows share `placeRightRowKnob` / `placeR3Knob` lambdas. Knobs are placed right-to-left from `rightRowEdge = b.getRight() - 5` (5 px inset from the content-area right edge): `cx = rightRowEdge - (4 - idx) * rowStep - rowKnobSize / 2`. The rightmost knob's right edge aligns with `rightRowEdge`.

**Row R1 — Cloud multi-LFO (5 knobs + 1 switch)** — vertically aligned with Row 1 (`rowY`).
Single **"Clouds post convolution"** group: WIDTH, DENSITY, LENGTH, FEEDBACK, IR FEED, power-button switch `cloudOnButton`. Group header Y = `topKnobRow.getY() - 10 - rowShiftUp`. Group header bounds: `cloudGroupBounds`.

**Row R2 — Shimmer (5 knobs + 1 switch)** — vertically aligned with Row 2 (`row2KnobY`).
Single **"Shimmer"** group: PITCH, LENGTH, DELAY, IR FEED, FEEDBACK, power-button switch `shimOnButton`. Group header Y = `row2AbsY - rowShiftUp`. Group header bounds: `shimGroupBounds`.

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
3. **Reverse button** (`reverseButton`): right-aligned to the waveform's right edge, 10 px below the IR combo bottom.
4. **Waveform display** (`waveformComponent`): 300×`kMeterBarsH` (104 px) — centred at `w/2`, positioned 2 px below the reverse button bottom (`waveformY = revBtnY + revBtnH + 2`). The waveform sits directly below the IR combo stack, not at the old bottom-of-UI anchor.

`kMeterBarsH = 104` — `totalBarsH = 4*(2*9+2)+3*8`, spanning INPUT L top to TAIL R bottom. `kMeterBarOffset = 15` — centring gap from meter panel top to first bar.

The preset combo (`presetCombo`), save button, and "Preset" label are **not** part of this centre-column stack — they live in the top bar (see P!NG logo section).

**Phantom waveform anchor:** `cy` is derived from `phantomWaveCentreY`, which uses the *old* right-column waveform dimensions (`0.36f × cw` wide) rather than the current waveform (now a fixed 300×153 at the bottom of the UI). This preserves the DRY/WET knob's visual position. Do not replace `phantomWavePanelW/H` with the current `wavePanelW/H` (300/153) — doing so would shift `cy` and misalign the DRY/WET knob. Note: the preset combo is no longer anchored to `cy`; it lives in the top bar.

### Wet Output Gain position

All four level knobs (ER, Tail, WET OUT TRIM, Width) share the same size: `outputGainKnobSize = (int)(sixKnobSize * 0.6f)`.

**WET OUT TRIM** (`outputGainKnobSize`) is positioned to the **right** of the DRY/WET knob, horizontally centred on the Save button X and vertically centred on the DRY/WET knob Y:

```cpp
const int outputGainCenterX = w / 2 + irComboW / 2 + 4 + saveButtonW / 2;
// vertically centred on dryWetTrueCentreY = cy + 10 + dryWetKnobSize / 2
```

ER and Tail level knobs (same size) mirror this to the **left** of DRY/WET at the same horizontal distance (`erTailCenterX = w/2 - (irComboW/2 + 4 + saveButtonW/2)`), stacked ER above and Tail below the DRY/WET centre Y.

**Width** knob (same `outputGainKnobSize`) is stacked directly **below** WET OUT TRIM at the same X (`outputGainCenterX`), at `tailKnobY` (the same Y as the Tail knob). Width no longer lives in any separate large-knob grid — it has been moved to align horizontally with WET OUT TRIM.

Label text: `outputGainLabel` reads `"WET OUT TRIM"` at 12.0f (the shared default font — no per-knob override). Do not revert to `"WET OUTPUT"`, `"Wet Out trim"`, or set an explicit 9.0f override.

### Header panel

The full UI background is `Resources/texture_bg.jpg` (1200×800 JPEG, ~166 KB, embedded via `BinaryData::texture_bg_jpg`), a real brushed-steel photograph. It is drawn in `paint()` first, scaled to fill the entire 1104×786 editor using `RectanglePlacement::fillDestination`. An 85%-opaque dark overlay (`0xd8141414`) then covers everything below the header bar so the control area remains readable. The header bar itself receives only a light 38%-opaque tint (`0x60080a10`) so the steel texture remains visible there. A 1 px separator line (`0xff0a0c12`) runs along the very bottom of `headerPanelRect`.

`headerPanelRect` spans `(0, 0, w, topRow.getBottom())` — set in `resized()`, used in `paint()`. The `bgTexture` member (`juce::Image`) is loaded once in the constructor from binary data; the `brushedMetalTexture` member and its programmatic generation loop have been removed entirely.

Three elements sit inside the header, all vertically centred on `topRowH` and shifted 4 px upward (`- 4` in the Y formula):

- **Spitfire Audio logo** — left-anchored at x=14. `logoH = topRowH * 0.48f`; `logoW` derived from the 474:62 aspect ratio (no width cap). Drawn with `drawImageWithin(..., RectanglePlacement::centred)`.
- **P!NG logo** — uses `ping_logo_blue.png` (cyan glow, RGBA, 578×182 px). `pingBounds` sized by aspect ratio to fill `logoH`, centred at `w/2`. Drawn with `drawImageWithin(..., RectanglePlacement::centred)`.
- **Preset combo + Save button** — right-aligned at `w - 12` margin; preset combo fixed at 200 px wide, label reads **"PRESET"** at 14 pt with `pLabelW = 62` px (must be ≥ 62 to avoid horizontal compression of the text at 14 pt — do not reduce). See Preset section below.

Both logos are pre-processed in the constructor (ARGB conversion + alpha < 30 zeroed) to eliminate rectangular boundary artefacts visible against lighter backgrounds. The pre-processed images are stored in `spitfireLogoImage` and `pingLogoImage` members.

### EQ section (bottom of window)

The `EQGraphComponent` occupies the **bottom-right** of the editor, with its **left edge at `w/2`** (aligned with the DRY/WET knob centre). It is a **fixed 337 px tall** component bottom-anchored so the graph's visual bottom aligns with the meter bottom. The layout (knobs **above** graph) is:

- **Control strip** (top 207 px of component) — knobs fill the full EQ component width (`ctrlAreaXOffset = 0`).
- **Graph area** (bottom 130 px of component) — 25% taller than the original 104 px.

Positioning in `PluginEditor::resized()`:
```cpp
static constexpr int kEQComponentH = 337;
const int eqLeft   = w / 2;
const int eqWidth  = b.getRight() - eqLeft;
const int eqBottom = h - 34;   // graph visual bottom at h−38 (EQGraph reduced(4) internally)
const int eqTopY   = eqBottom - kEQComponentH;
eqGraph.ctrlAreaXOffset = 0;   // controls span full EQ width
eqGraph.setBounds(eqLeft, eqTopY, eqWidth, kEQComponentH);
```

`EQGraphComponent::resized()` applies `getLocalBounds().reduced(4)` internally, so the graph's rendered bottom edge = `eqBottom - 4 = h - 38`, matching the meter bottom exactly. The horizontal separator line below the graph has been removed.

#### EQGraphComponent layout

**Graph area** — bottom 130 px of the component (`b.removeFromBottom(130)`); displays spectrum analyser, grid lines, frequency-response curve, and draggable band handles.

**Control strip** — the full remaining area above the graph (`ctrlArea = b.withLeft(b.getX() + ctrlAreaXOffset)`). `ctrlAreaXOffset` is a public `int` member (default 0); setting it restricts knob columns to a sub-region of the EQ component width. Currently 0 so all five band columns span the full EQ width.

**Band-name labels** (LOW / MID 1 / MID 2 / MID 3 / HIGH) are **not** pinned to `ctrlArea.getY()`. They are anchored 3 px above the FREQ knob top: `bandNameLabel[i].setBounds(colX, freqRowY + freqDY - 3 - bandLblH, colW, bandLblH)`. This keeps the labels visually attached to their column regardless of DY shifts.

Three knob rows × five bands. DY values position the rows so the Q/SLOPE label bottoms land 10 px above the graph top:

| Row | Parameter | DX / DY fine-tune | Notes |
|-----|-----------|-------------------|-------|
| Row 1 | FREQ | `freqDX = −8`, `freqDY = +58` | Knobs pulled down close to graph; band labels above |
| Row 2 | GAIN | `gainDX = +15`, `gainDY = +23` | Zig-zag right offset |
| Row 3 | Q / SLOPE | `qDX = −8`, `qDY = +3` | Q label bottom = graph top − 10 px |

Knob size is **32 px**. Do not scale DY values when changing knob size — they are absolute offsets from the computed row base positions.

Band order (left→right): LOW (low shelf, purple) · MID 1 (peak, blue) · MID 2 (peak, amber) · MID 3 (peak, green) · HIGH (high shelf, red).

#### EQ DSP (PluginProcessor)

Five `ProcessorDuplicator` instances in series: `lowShelfBand → lowBand → midBand → highBand → highShelfBand`. Updated in `updateEQ()` called from `parameterChanged`. Filter types:

- `lowShelfBand` — `makeLowShelf` (b3); `highShelfBand` — `makeHighShelf` (b4)
- `lowBand / midBand / highBand` — `makePeakFilter` (b0 / b1 / b2)

**Backward compatibility:** b0–b2 keep their original IDs. b3/b4 default to 0 dB gain so existing presets missing these keys are unaffected.

### Level meter

`OutputLevelMeter` is positioned at:
```cpp
const int meterW = 300;
const int meterH = 153;
const int meterX = rowStartX;              // left-aligned with the knob rows
const int meterY = h - 38 - meterH + 15;  // = h − 176; bottom at h − 23
outputLevelMeter.setBounds(meterX, meterY, meterW, meterH);
```

The meter bottom (`h - 23`) sits 5 px above the licence label (`h - 18`). The meter background is **transparent** (no filled rounded rectangle).

The **licence label** is at `(12, h - 18, w/2 - 12, 16)` (bottom-left). The **version label** is at `(w/2, h - 18, w/2 - 12, 16)` (bottom-right, right-justified).

### Large-knob grid (bottom of left column)

Width (`widthSlider`) has been removed from any large-knob grid. It now sits directly below WET OUT TRIM at `outputGainCenterX, tailKnobY`. LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to Row R3 on the right side.

### Version label

Displayed in the bottom strip under the Tail Rate knob (`tailRateSlider.getX()`). Must be placed **after** `tailRateSlider.setBounds()` in `resized()`.

---

## Reverse for synthesised IRs

### Why it needs special handling

`loadSelectedIR()` (PluginEditor.cpp) is the single entry point used by the Reverse button click handler and the waveform Trim-changed callback. For file-based IRs it calls `loadIRFromFile()` → `loadIRFromBuffer()`, which physically reverses the buffer if `reverse == true`. For the synth IR slot (`irCombo` id=1, idx=−1) it previously returned immediately, so toggling Reverse or moving the Trim handle had no effect on synth IRs.

### Fix

`PluginProcessor` stores a **raw (pre-processing) copy** of the last synthesised buffer. When `loadIRFromBuffer` is called with `fromSynth=true`, two steps run in order inside the `fromSynth` block before any other transforms:

**Step 1 — Auto-trim trailing silence.** `synthIR()` always allocates `8 × max_RT60` (up to 30 s), but the reverb signal decays to below −80 dB well before the end. The trim scans all channels from the end backwards for the last sample above the threshold, then keeps that point plus a 200 ms safety tail (minimum 300 ms), then applies a cosine end-fade over those final 200 ms so the IR decays smoothly to silence. This runs *before* `rawSynthBuffer` is saved so the stored copy is already silence-free.

```cpp
// threshold = jmin(peak × 1e-4, 1e-4)  — −80 dB below peak, capped at −80 dBFS absolute
// newLen    = lastSignificant + 200 ms safety tail, min 300 ms
// end-fade  = cosine fade applied over the final 200 ms of newLen
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
| `plateSize` | Plate Size | 0.5–14.0 | 1.0 |
| `plateIRFeed` | Plate IR Feed | 0–1 | 0 |
| `bloomOn` | Bloom On | bool | false |
| `bloomSize` | Bloom Size | 0.25–2.0 | 0.77 |
| `bloomFeedback` | Bloom Feedback | 0–0.65 | 0.49 |
| `bloomTime` | Bloom Time (ms) | 50–500 | 290 |
| `bloomIRFeed` | Bloom IR Feed | 0–1 | 0.4 |
| `bloomVolume` | Bloom Volume | 0–1 | 0 |
| `cloudOn` | Cloud On | bool | false |
| `cloudDepth` | Cloud Width (UI: WIDTH) | 0–1 | 0.3 |
| `cloudRate` | Cloud Density (UI: DENSITY) | 0.1–4.0 | 2.0 |
| `cloudSize` | Cloud Length (UI: LENGTH) | 25–1000 ms | 200 ms |
| `cloudVolume` | Cloud Volume (unused — reserved) | 0–1 | 0 |
| `cloudFeedback` | Cloud Feedback (UI: FEEDBACK) | 0–0.7 | 0.3 |
| `cloudIRFeed` | Cloud IR Feed (UI: IR FEED) | 0–1 | 0.5 |
| `shimOn` | Shimmer On | bool | false |
| `shimPitch` | Shimmer Pitch | −24–+24 semitones (integer steps) | +12 |
| `shimSize` | Shimmer Length (ms) | 50–500 | 300 |
| `shimDelay` | Shimmer Delay / Onset Stagger (ms) | 0–1000 | 500 |
| `shimIRFeed` | Shimmer IR Feed | 0–1 | 0.5 |
| `shimVolume` | Shimmer Volume (unused — reserved) | 0–1 | 0 |
| `shimFeedback` | Shimmer Decay Time | 0–0.7 | 0.45 |
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
[Cloud Granular: grain output × cloudIRFeed injected into main signal inline (same block)]  (if cloudOn && cloudIRFeed > 0)
  │   Same-block injection: diffused grain sum written to cloudBuffer AND injected here in the same per-sample loop
  │
  ▼
[Shimmer 8-voice harmonic cloud: all 8 voices read clean pre-conv dry; sum × shimIRFeed injected]  (if shimOn)
  │   shimBuffer ← per-block sum of all 8 pitch-shifted, allpass-smeared grain voices
  │   Voices staggered onset: voice vi silent for (vi+1)×shimDelay ms after shimOn enabled
  │   No post-conv loopback — the IR provides all decay and smearing
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
[Cloud Granular: 3-s capture buffer + random grain playback, stores cloudBuffer]  (if cloudOn)
  │   Grains scatter across full 3-s buffer; sum normalised by sqrt(N); 4-stage allpass diffusion applied
  │   cloudFeedback mixes previous grain output back into capture buffer each sample
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

Base prime delays at 48 kHz: `{ 24, 71, 157, 293, 431, 691 }` samples. Buffers allocated at **14× base primes** so the full `plateSize` 0.5–14.0 range needs no reallocation (~200 ms max on prime 691). All g values set to 0.70. `plateShelfState` filled to zero. `plateBuffer` sized to `(2, samplesPerBlock)` and cleared.

### `processBlock` insertion point

After the saturator, **before** the convolution block. At `density = 0` the blend formula `in * (1 - density) + shaped * density` collapses to the raw input, so no audible effect even if `plateOn = true`. At `plateOn = false` the block is skipped entirely — zero DSP overhead.

`plateColour` sets the 1-pole lowpass cutoff applied to the diffused signal: 0 → 2 kHz (warm, dark — EMT 140 character), 1 → 8 kHz (bright — AMS RMX16 character). The `effLen` for each stage is computed once per block (not per sample) as `round(platePrimes[s] * sr / 48000 * plateSize)`.

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `plateOn` | bool | false | Enables the cascade; zero overhead when off |
| `plateDiffusion` | 0.30–0.88 | 0.70 | Allpass g coefficient applied to all 6 stages each block; lower = gentle scatter, higher = very dense diffusion |
| `plateColour` | 0–1 | 0.5 | 1-pole LP cutoff applied to the diffused signal: 0 → 2 kHz (warm), 1 → 8 kHz (bright). Readout displays the actual cutoff in kHz. |
| `plateSize` | 0.5–14.0 | 1.0 | Scales all 6 allpass delays; readout shows the largest delay time in ms (prime 691 × size / 48000 × 1000). At size=1.0 → ~14.4 ms, size=14.0 → ~201.8 ms. Buffers allocated at 14× base primes to cover the full range without reallocation. |
| `plateIRFeed` | 0–1 | 0 | Adds the processed plate signal into the IR convolver input (on top of the main signal). At 0: plate has no effect; at 1: full plate signal added to convolver input. |

---

## Bloom hybrid (Feature 2 — implemented)

### What it does

Creates a progressively-building, self-diffusing signal from the input. Think of it as a guitar pedal: it has its own internal feedback loop that builds a dense, swelling texture, with two independent outputs — one that feeds into the IR convolver (adding reverb on top of the bloom) and one that goes directly to the final output. The reverb has no return path into Bloom.

Two elements work together:

- **Pre-convolution allpass cascade** — 6 stages with short delays (~5–40 ms per channel), creating a textured diffuse swirl rather than discrete echoes. Separate L/R prime sets produce genuinely independent stereo textures.
- **Self-contained cascade feedback** — a fraction of the cascade's own output is fed back into its input (`bloomTime` ms later), causing density to build with each recirculation. The convolved wet signal is **not** part of this loop.

### Architecture — Bloom as a self-contained pedal upstream of the reverb

The main signal into the convolver is **not modified** by the cascade itself. The cascade output is stored in `bloomBuffer` and delivered through two independent output paths:

- **`bloomIRFeed`** — adds `bloomBuffer * irFeed` additively to the convolver input (on top of the main signal). Like plugging a Bloom pedal into the reverb's input jack. Default: 0.4 — audible immediately when Bloom is enabled.
- **`bloomVolume`** — adds `bloomBuffer * volume` directly to the **final output after the dry/wet blend**, so it is heard regardless of the wet/dry setting. Default: 0.

At `bloomVolume = 0` and `bloomIRFeed = 0` Bloom has no audible effect. With the default `bloomIRFeed = 0.4`, Bloom is audible as soon as it is switched on.

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
| `bloomSize` | 0.25–2.0 | 0.77 | Scales all 6 allpass delay times (like `plateSize` for Plate). At 1.0 the L delays span ~5–40 ms (textured, dense); at 2.0 they span ~10–80 ms (more spacious); at 0.25 ~1.25–10 ms (very dense, clangorous). Readout shows multiplier (e.g. "1.00×"). |
| `bloomFeedback` | 0–0.65 | 0.49 | Wet→input feedback amount; safety-clamped at 0.65. Higher = more self-sustaining swell |
| `bloomTime` | 50–500 ms | 290 ms | Feedback tap delay — how far back in the wet signal the feedback reads. Short = fast rhythmic bloom, long = slow expansive sustain. Readout displays integer ms. |
| `bloomIRFeed` | 0–1 | 0.4 | How much bloom cascade output injects additively into the convolver input |
| `bloomVolume` | 0–1 | 0 | How much bloom cascade output is added to the final output after dry/wet blend (independent of wet/dry) |

### UI layout (Row 4 — "Bloom hybrid")

Immediately after Row 3 (Plate), same `rowKnobSize`/`rowStep`/`rowStartX` constants. Single group "Bloom hybrid" with 5 knobs + pill switch (`bloomOnButton`, component ID `BloomSwitch`) right-aligned in the group header. Group header bounds stored as `bloomGroupBounds`. Editor height bumped from 600 → **672 px**. Row 4 uses `row4AbsY = row3AbsY + row3TotalH_` (absolute anchor, same pattern as Rows 2/3). The 6-knob grid (LFO Depth/Width/etc.) is now anchored at `row4AbsY + row4TotalH_ + 70` (was `row3AbsY + row3TotalH_ + 70`).

---

## Cloud Granular Delay (Feature 3 — implemented)

### What it does

A granular delay effect that captures a 3-second window of the input and plays back randomised grains from it. DENSITY controls spawn interval via an exponential curve: at low density grains are sparse (205–410 ms apart, 0–1 grains active), at mid-knob there are ~4 simultaneously overlapping grains, at maximum up to ~14 grains overlap. Crucially, grains are scattered across the **full 3-second buffer** — not just the most recent audio — so overlapping grains draw from very different moments in time, creating spectral richness rather than a phase-locked delay pattern. LENGTH sets grain length directly in ms (25–1000 ms), independently of DENSITY. WIDTH controls stereo spread and reverse-grain probability. FEEDBACK mixes grain output back into the capture buffer. A 4-stage all-pass diffusion cascade (Clouds-style) is applied to the grain sum before output, smoothing grain-boundary discontinuities into a continuous smear. IR FEED sends the diffused grain output into the convolver.

Unlike the original LFO delay-line design, Cloud is now a pre-convolution granular source. It has no post-convolution insertion point.

### Architecture — pre-convolution granular source with deferred IR feed

Cloud runs in two insertion points within `processBlock`:

- **Cloud Insertion 1** (pre-convolution, before the convolver block): Reads `cloudBuffer` from the *previous* block and injects it × `cloudIRFeed` into the main signal before convolution. This 1-block-deferred feed is necessary because Cloud writes `cloudBuffer` during its main block (which runs before the convolver), but the convolver has already run for the current block by the time Cloud finishes.
- **Cloud main block** (before Shimmer, before convolver): Per-sample: writes dry input + `cloudFeedback × cloudFbSamples[ch]` into a 3-second circular capture buffer; spawns grains (spawn interval driven by DENSITY, grain length set directly by LENGTH); advances active grains, accumulating output into `cloudBuffer`. WIDTH probability routes grains across channels and enables random reverse playback. `cloudFbSamples[ch]` updated to current grain output at end of each sample.

`cloudBuffer` is a persistent member variable that bridges the two blocks. `cloudFbSamples[2]` persists across blocks so feedback never resets.

### DENSITY → spawn interval; LENGTH → grain length

DENSITY (`cloudRate`, 0.1–4.0, default 2.0) is normalised to `t = (crate − 0.1) / (4.0 − 0.1)` and controls **only** spawn interval via an **exponential** curve:

```
tPow       = 0.02 ^ t            // 1.0 at t=0 → 0.02 at t=1
minSpawnMs = 200 × tPow + 5      // ~205 ms (t=0) → ~9 ms (t=1)
maxSpawnMs = 400 × tPow + 10     // ~410 ms (t=0) → ~18 ms (t=1)
```

| DENSITY knob | t | Spawn interval | Grains at 200 ms LENGTH |
|---|---|---|---|
| Min (0.1) | 0.0 | 205–410 ms | ~0–1 (sparse) |
| 25% (1.08) | 0.25 | 80–160 ms | ~1–2 |
| 50% / default (2.05) | 0.5 | 33–67 ms | ~4 |
| 75% (3.02) | 0.75 | 16–31 ms | ~8 |
| Max (4.0) | 1.0 | 9–18 ms | ~14 |

The exponential taper means the entire knob range is useful, unlike a linear formula where most of the travel produces no meaningful overlap.

LENGTH (`cloudSize`, 25–1000 ms, default 200 ms) sets grain length directly in ms, entirely independent of DENSITY. All grains within a block use the same length (no randomisation around the LENGTH value — variation comes from random read positions and randomised spawn timing).

### Grain position scatter — full buffer depth

Each new grain's read position is drawn uniformly from the range `[grainLen, 90% of buffer]` behind the write head (i.e., between one grain length and 2.7 seconds back). This is critical to the cloud sound: concurrent grains draw from very different moments in audio history and produce genuinely independent signals when summed. **Do not revert to 1–2 grain lengths of scatter** — that range causes all grains to read nearly the same audio content, producing comb filtering and a delay-like stutter rather than a cloud texture.

### Output normalisation and diffusion

After summing all active grains, the sum is scaled by `1 / √(activeCount)` (sqrt-power normalisation) before the diffusion cascade. This keeps perceived loudness consistent as density changes (`1/N` would make dense settings far too quiet).

The normalised grain sum then passes through a **4-stage all-pass diffusion cascade** (per channel) before being written to `cloudBuffer` and injected into the convolver:

| Stage | Delay (ms) | Delay @ 48 kHz |
|---|---|---|
| 1 | 13.7 ms | 658 samples |
| 2 | 7.3 ms | 350 samples |
| 3 | 4.1 ms | 197 samples |
| 4 | 1.7 ms | 82 samples |

g = 0.65f on all stages. `effLen = 0` (uses `buf.size()`). Buffers allocated in `prepareToPlay` at the exact delay size for the current sample rate. Reuses `SimpleAllpass` struct from Plate/Bloom. State stored in `cloudDiffuseAPs[2][4]`. This is the Mutable Instruments Clouds "TEXTURE" mechanism: all-pass filters smear grain boundaries at signal level without adding reverb tail or perceptible latency.

### DSP state (`PluginProcessor.h`)

```cpp
static constexpr int   kNumCloudGrains        = 40;      // max simultaneous grains (Clouds-style density)
static constexpr float kCloudCaptureBufMs     = 3000.0f; // 3-second capture buffer
static constexpr int   kNumCloudDiffuseStages = 4;       // all-pass diffusion cascade stages

struct CloudGrain {
    float readPos  = 0.f;   // fractional read position in capture buffer
    int   grainLen = 0;     // grain length in samples
    float phase    = 1.f;   // 0..1 through grain; ≥ 1.0 = inactive
    bool  reverse  = false; // play grain backwards
    int   srcCh    = -1;    // -1 = normal stereo, 0 = L-only, 1 = R-only
};

std::array<std::vector<float>, 2>       cloudCaptureBufs;         // [ch] circular capture
std::array<int, 2>                      cloudCaptureWritePtrs {};  // per-channel write heads
std::array<CloudGrain, kNumCloudGrains> cloudGrains;               // flat array, round-robin
float                                   cloudSpawnPhase    = 0.f;
float                                   cloudCurrentSpawnIntervalSamps = 24000.f;
int                                     cloudNextGrainSlot = 0;
uint32_t                                cloudSpawnSeed     = 12345u;
std::array<float, 2>                    cloudFbSamples { 0.f, 0.f };

// 4-stage all-pass diffusion — reuses SimpleAllpass; allocated in prepareToPlay
std::array<std::array<SimpleAllpass, kNumCloudDiffuseStages>, 2> cloudDiffuseAPs; // [ch][stage]

juce::AudioBuffer<float> cloudBuffer;  // same-block bridge: grain output for IR Feed + cloudVolume
```

### `prepareToPlay`

Capture buffer sized to `ceil(3000 × sampleRate / 1000)` samples per channel. All 40 grain structs reset (`phase = 1.f` = inactive). `cloudBuffer` sized to `(2, samplesPerBlock)` and cleared. `cloudFbSamples` filled to 0. Spawn phase and interval reset. Diffusion all-pass buffers allocated at their exact delay sizes (computed from the 4 delay constants scaled to current sample rate), g=0.65, zeroed.

### `processBlock` details

**Cloud main block** per-sample:
1. Write `dry[ch] + cloudFeedback × cloudFbSamples[ch]` into the capture buffer.
2. Advance spawn phase; when ≥ 1.0, spawn a new grain into the next round-robin slot: grain length from LENGTH knob, read position uniformly random across `[grainLen, 90% of buffer]` behind write head, reverse flag driven by WIDTH, srcCh driven by WIDTH.
3. For each of the 40 grain slots: if active (`phase < 1.0`), compute Hann window, linear-interpolate from capture buffer, accumulate into grain sum. Advance read position and phase. Count active grains.
4. Scale sum by `1 / √(activeCount)`. Apply 4-stage all-pass diffusion cascade per channel. Write to `cloudBuffer`.
5. If `cirFeed > 0`, inject diffused output into main buffer (same-block, into convolver input).
6. Store diffused output in `cloudFbSamples[ch]` for next sample's capture write.

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `cloudOn` | bool | false | Enables Cloud; zero overhead when off |
| `cloudDepth` | 0–1 | 0 | WIDTH — stereo spread probability. At 0: grains always read from same channel. At 1: maximum cross-channel sampling and reverse probability. UI label: WIDTH. |
| `cloudRate` | 0.1–4.0 | 2.0 | DENSITY — controls spawn interval via exponential curve (`200 × 0.02^t + 5` ms). Default 2.0 ≈ t=0.5 → ~4 grains overlapping at 200 ms LENGTH. |
| `cloudSize` | 25–1000 ms | 200 ms | LENGTH — grain length in ms, independent of DENSITY. All grains use this length directly. |
| `cloudFeedback` | 0–0.7 | 0 | FEEDBACK — mixes previous grain output back into capture buffer each sample. Safety-clamped at 0.7. Creates self-reinforcing, accumulating granular texture. |
| `cloudIRFeed` | 0–1 | 0 | IR FEED — injects `cloudBuffer × cloudIRFeed` into the convolver input (1-block deferred). Sends the granular output through the reverb. |
| `cloudVolume` | 0–1 | 0 | Reserved / unused — the old direct dry-output path has been removed. VOLUME slot is now occupied by IR FEED in the UI. |

### UI layout (Row R1 — "Clouds post convolution")

Right-side Row R1 (aligned with left-side Row 1). Same `rowKnobSize`/`rowStep` constants. Single group "Clouds post convolution" with 5 knobs: WIDTH (`cloudDepthSlider`), DENSITY (`cloudRateSlider`), LENGTH (`cloudSizeSlider`), FEEDBACK (`cloudIRFeedSlider` — attached to `cloudFeedback`), IR FEED (`cloudVolumeSlider` — attached to `cloudIRFeed`). Power-button switch `cloudOnButton` (component ID `CloudSwitch`) right-aligned in the group header. Group header bounds stored as `cloudGroupBounds`.

**Attachment remapping:** `cloudIRFeedSlider` / `cloudIRFeedAttach` are bound to `"cloudFeedback"` (UI slot 4 = FEEDBACK). `cloudVolumeSlider` / `cloudVolumeAttach` are bound to `"cloudIRFeed"` (UI slot 5 = IR FEED). The original VOLUME parameter (`cloudVolume`) has no knob; its APVTS entry is kept for preset backward-compatibility but has no UI attachment.

---

## Shimmer (Feature 4 — implemented)

### What it does

An 8-voice harmonic shimmer cloud. Every voice reads the **clean pre-conv dry signal**, pitch-shifts it by a fixed harmonic interval (see voice layout below), runs it through a 2-stage allpass for spectral smearing, and injects the sum into the convolver input via `shimIRFeed`. There is **no post-conv feedback loop** — the convolver IR provides all decay and smearing. Pitch never stacks beyond the fixed voice layout regardless of any knob setting.

LENGTH sets the grain length in milliseconds. DELAY controls the staggered voice onset: each voice waits `(voiceIndex + 1) × delay` ms before it starts outputting, so the shimmer cloud builds progressively from voice 0 upward. FEEDBACK controls the sustain/ring-out of the shimmer cloud: the previous block's summed shimmer output (`shimSelfFbBuf`) is mixed back into each voice's grain capture buffer, allowing the cloud to self-sustain and ring out after input stops. Higher FEEDBACK = longer ring-out (medium bloom ~2–5 s at max). Each voice's grain read position uses a small fixed base delay of 20 ms plus a ±5 ms per-voice LFO (0.5 Hz, 45° apart) for subtle chorus movement. `shimIRFeed` defaults to **0.5** — the effect is immediately audible when enabled.

### Architecture

Single pre-conv insertion point (before the convolver block). All 8 `shimVoicesHarm` voices run in parallel. Each voice:
1. Writes the incoming dry sample into its circular `grainBuf`.
2. Reads two Hann-windowed grains (phases 0 and 0.5), sums and scales by 0.5.
3. Passes the grain output through 2 per-voice allpass stages for spectral smearing.
4. Accumulates into `shimBuffer` — but only if that voice's `shimOnsetCounters[vi]` has counted down to zero.

After all voices have run, `shimBuffer × shimIRFeed × (1/√8)` is injected into the convolver input. The 1/√8 normalisation keeps perceived loudness consistent with a single voice.

There is no post-conv shimmer block.

### Voice layout

`shimPitch` = N semitones (integer steps, e.g. +12 = one octave).

| Voice | Pitch | Role |
|---|---|---|
| 0 | 0 st | Unshifted — body / unison reverb tail |
| 1 | +N st | Fundamental shimmer interval |
| 2 | +2N st | 2nd harmonic up |
| 3 | −N st | 1st harmonic down |
| 4 | +3N st | 3rd harmonic up |
| 5 | −2N st | 2nd harmonic down |
| 6 | 0 st + **3 cents fixed** | Chorus double of voice 0 |
| 7 | +N st + **6 cents fixed** | Chorus double of voice 1 |

Voices 6 and 7 provide gentle chorus beating against voices 0 and 1. The +3c/+6c detune is hard-coded in `semiOff[]` and is not knob-controllable.

### Grain engine

Two grains per voice per channel, offset by half a grain length (phases 0 and 0.5), Hann windowed, summed and scaled by 0.5. Read pointer advances `pitchRatio` samples per input sample. On phase rollover the read head snaps to `writePtr − effGrainLen − voiceDelaySamps`, where `voiceDelaySamps` is the LFO-modulated delay sampled once per block for this voice. ±25% LCG jitter (`shimRng`) is applied at each grain reset.

`effGrainLen = round(shimSize_ms × sampleRate / 1000)`. `kShimBufLen = 131072` (2.73 s at 48 kHz) — covers max grain (500 ms) + max modulated delay (750 ms) + jitter headroom.

### Grain read delay (subtle LFO)

Each voice uses a small fixed base of 20 ms plus a ±5 ms per-voice LFO sweep for subtle chorus movement:

```
mainLv          = 0.5 × (1 + sin(shimLfoPhase[vi]))       // [0,1]
voiceDelaySamps = round((20 + 5 × mainLv) × sr / 1000)    // 20–25 ms
```

This puts grains near-current audio (reads content recorded ~220–225 ms ago at default 200 ms grain) so short notes immediately fill the grain buffers. The previous 300 ms base was too far back and caused grains to read silence for any note shorter than ~500 ms.

LFO rate: 0.5 Hz. Phase per voice: `shimLfoPhase[vi] = vi × 2π/8` (45° apart). Phase advances by `2π × 0.5 / sr × numSamples` per block per voice.

### Per-voice delay lines (FEEDBACK knob — decay time)

After the 2-stage allpass, each voice's grain output feeds a per-channel circular delay line (`shimDelayBufs[voice][ch]`, `shimDelayPtrs[voice][ch]`). This is entirely separate from `grainBuf` — the delay output never re-enters the pitch-shifting path.

**Delay period:** Each voice has an independent period derived by applying a prime-scaled multiplier to the raw DELAY knob value: `voiceDelayPeriod = round(shimDelaySamps × kShimVoiceMultiplier[vi])`, then `voicePeriodSamps = max(effGrainLen, voiceDelayPeriod)`. At DELAY=0 `voiceDelayPeriod` is 0 and all voices fall back to `effGrainLen` (no spreading when delay is off). The multipliers span **0.4–1.6×** the set delay, derived from 8 primes (2, 3, 5, 7, 11, 13, 17, 19) mapped linearly to that range:

```
kShimVoiceMultiplier[8] = { 0.400, 0.471, 0.612, 0.753, 1.035, 1.176, 1.459, 1.600 }
```

Ratios between any pair are ratios of distinct primes (no common factors), so beat periods between voices are extremely long and inaudible. At 500 ms DELAY the eight voice periods are approximately 200, 235, 306, 376, 518, 588, 729, 800 ms — a 4× spread that eliminates the audible pulsing that occurs when all voices share the same period.

**Feedback coefficient:** Derived from the desired decay time `T` via an exponential mapping:
```
T      = 2 × (15/2)^(feedbackRaw / 0.7)            // feedbackRaw ∈ [0, 0.7]
shimFb = exp(-3 × voicePeriodSamps / sr / T)         // always < 1
```
| FEEDBACK | Decay T | shimFb (at 200 ms period) |
|---|---|---|
| 0.0 | ~2 s | 0.74 |
| 0.3 (default) | ~5.5 s | 0.89 |
| 0.7 (max) | 15 s | 0.96 |

**Per-sample delay processing (inside the grain loop):**
```
dReadPtr = (writePtr − voicePeriodSamps + bufLen) % bufLen
delayOut = dBuf[dReadPtr]
dBuf[writePtr] = grainOut + shimFb × delayOut    // internal feedback only — not into grainBuf
writePtr = (writePtr + 1) % bufLen
if (i >= onsetStartSample)
    shimBuffer[ch][i] += grainOut + delayOut      // immediate grain + decaying echoes
```

The delay runs every sample including during onset silence so state is stable at voice onset. Buffers allocated in `prepareToPlay` at `ceil(1700 ms × sr / 1000)` samples per voice per channel (~5.2 MB total at 48 kHz) — sized to cover the maximum voice period of 1.6 × 1000 ms = 1600 ms plus headroom. All buffers zeroed when `shimOn` is false.

### Per-voice allpass smearing

Each voice passes its grain output through 2 `SimpleAllpass` stages before accumulation. Delay lengths are slowly modulated at ~0.2 Hz:
- Stage 0: base 7 ms ± 3 ms → range 4–10 ms
- Stage 1: base 14 ms ± 5 ms → range 9–19 ms

Allpass LFO phase per voice: `shimApLfoPhase[vi] = vi × 2π/8 × 1.3` (offset from main LFO). Buffers allocated at 2× base (14 ms and 28 ms) so the full modulation range needs no reallocation. g = 0.5f on all stages. `effLen` updated once per block from allpass LFO.

### Staggered onset (DELAY knob)

When `shimOn` transitions false→true, each voice's countdown counter is armed:
```
shimOnsetCounters[vi] = (vi + 1) × shimDelaySamps
```
where `shimDelaySamps = round(shimDelay_ms × sr / 1000)`.

`onsetStartSample = min(numSamples, shimOnsetCounters[vi])` — the first sample within the current block at which voice `vi` contributes to `shimBuffer`. The grain engine (buffer writes, read-pointer advances, jitter) runs for all samples regardless of onset state so voices have real content at pickup. Counter decrements by `numSamples` each block, clamped at zero. At DELAY=0: all voices start simultaneously. At DELAY=100 ms: voice 0 at 100 ms, voice 1 at 200 ms, …, voice 7 at 800 ms.

### DSP state (`PluginProcessor.h`)

```cpp
static constexpr int kNumShimVoices = 8;
static constexpr int kShimGrainLen  = 9600;   // 200 ms at 48 kHz (legacy constant — effGrainLen computed from shimSize param)
static constexpr int kShimBufLen    = 131072;  // 2.73 s at 48 kHz

struct ShimmerVoice {
    std::vector<float> grainBuf;   // kShimBufLen samples, circular
    int   writePtr    = 0;
    float readPtrA    = 0.f;
    float readPtrB    = 0.f;       // initialised to kShimGrainLen/2 in prepareToPlay
    float grainPhaseA = 0.f;
    float grainPhaseB = 0.5f;
};

// [voice][ch] — 8 harmonic voices × 2 channels
std::array<std::array<ShimmerVoice, 2>, kNumShimVoices> shimVoicesHarm;
juce::AudioBuffer<float> shimBuffer;   // per-block scratch: sum of all voice outputs

uint32_t shimRng = 0x92d68ca2u;  // LCG for per-grain delay jitter

// Per-voice LFO state (main 0.5 Hz; allpass 0.2 Hz). Spread 45° (2π/8) per voice.
std::array<float, kNumShimVoices> shimLfoPhase   {};
std::array<float, kNumShimVoices> shimApLfoPhase {};

// 2-stage per-voice allpass: shimAPs[voice][stage][ch]
// Stage 0: 7 ms base (2× buf = 14 ms); Stage 1: 14 ms base (2× buf = 28 ms). g = 0.5f.
std::array<std::array<std::array<SimpleAllpass, 2>, 2>, kNumShimVoices> shimAPs;

// Staggered onset counters — armed on shimOn false→true, count down to zero.
std::array<int,  kNumShimVoices> shimOnsetCounters {};
bool shimWasEnabled = false;
```

### Controls

| Parameter ID | Range | Default | Effect |
|---|---|---|---|
| `shimOn` | bool | false | Enables Shimmer; zero overhead when off |
| `shimPitch` | −24–+24 st (integer steps) | +12 | Semitone interval N applied to voices 1–5 and 7. +12 = one octave. Integer NormalisableRange. |
| `shimSize` | 50–500 ms | 300 ms | LENGTH — grain length in ms. Longer = smoother, more sustained pitch with less smear. |
| `shimDelay` | 0–1000 ms | 500 ms | DELAY — dual role: (1) stagger interval for voice onsets (voice vi waits (vi+1) × delay ms after shimOn enabled); (2) sets the per-voice delay line period. At DELAY=0 the delay period falls back to `effGrainLen`. |
| `shimIRFeed` | 0–1 | **0.5** | IR FEED — `shimBuffer × shimIRFeed × (1/√8)` injected into convolver input (same-block, one-way). Default 0.5 — audible immediately on enable. |
| `shimVolume` | 0–1 | 0 | Reserved / unused. APVTS entry kept for preset backward-compatibility. |
| `shimFeedback` | 0–0.7 | 0.45 | FEEDBACK — decay time control for the per-voice delay lines. Exponential mapping: `T = 2 × (7.5)^(raw/0.7)`. At 0: T≈2 s. At 0.45 (default): T≈8.7 s. At 0.7: T=15 s. Feedback coefficient per voice: `shimFb = exp(-3 × period / sr / T)`, always < 1. |

### UI layout (Row R2 — "Shimmer")

Right-side Row R2 (aligned with left-side Row 2). Same `rowKnobSize`/`rowStep` constants. Single group "Shimmer" with 5 knobs: PITCH (`shimPitchSlider`), LENGTH (`shimSizeSlider`), DELAY (`shimColourSlider` — attached to `shimDelay`), IR FEED (`shimIRFeedSlider`), FEEDBACK (`shimVolumeSlider` — attached to `shimFeedback`). Power-button switch `shimOnButton` (component ID `ShimmerSwitch`) right-aligned in the group header. Group header bounds stored as `shimGroupBounds`.

**Attachment remapping:** `shimColourSlider` / `shimColourAttach` are bound to `"shimDelay"` (UI slot 3 = DELAY). `shimVolumeSlider` / `shimVolumeAttach` are bound to `"shimFeedback"` (UI slot 5 = FEEDBACK). The original `shimColour` APVTS parameter no longer exists; `shimVolume` is retained as a no-op for preset backward-compatibility.

---

## IR loading pipeline

Called from the **message thread** only (`loadIRFromFile` / `loadIRFromBuffer`). Never call from the audio thread.

Steps applied in order:
1. **Reverse** (if `reverse == true`) — sample order flipped per channel. Reverse Trim then skips `trimFrac × length` samples from the start of the reversed IR.
2. **Stretch** — linear-interpolation time-scale to `stretchFactor × originalLength`.
3. **Decay envelope** — exponential fade: `env(t) = exp(−decayParam × 6 × t)`, where `decayParam = 1 − UI_decay`.
4. **Trailing silence trim** — applied to **all** IRs (synth and file-based alike) after the decay envelope and before expansion. Scans all channels from the end to find the last sample above the threshold, keeps that point plus a 200 ms safety tail (minimum 300 ms), then applies a cosine end-fade over those final 200 ms so the IR decays smoothly to silence rather than cutting off abruptly. This is critical for factory IRs that were synthesised and saved as `.wav` files: `synthIR()` allocates `8 × max_RT60` (up to 60 s), but the actual reverb signal occupies only 8–15 s. Without trimming, the 8 NUPC tail convolvers each hold a 40–60 s IR; the background FFT thread cannot keep up at small buffer sizes (128–256 samples), causing persistent crackle on large/long factory presets. The trim brings factory file IRs down to the same effective length as freshly synthesised ones (which are trimmed in the `fromSynth` path before `rawSynthBuffer` is saved).

```cpp
// threshold = jmin(peak × 1e-4, 1e-4)  — −80 dB below peak, capped at −80 dBFS absolute
// newLen    = lastSignificant + 200 ms safety tail, min 300 ms
// end-fade  = cosine fade applied over the final 200 ms of newLen
```

5. **Mono/stereo → 4-channel expansion** (file IRs only, skipped for synth IRs which arrive as 4-channel) — a 2-channel stereo (or 1-channel mono) buffer is padded to 4 channels: iLL = ch0, iRR = ch1 (or ch0 for mono), cross-channels iRL/iLR zeroed. The two non-zero channels are scaled ×0.5 (cancels the `trueStereoWetGain = 2.0f` in `processBlock`). An additional **−15 dB** scalar (`juce::Decibels::decibelsToGain(-15.0f)`) is applied to the whole expanded buffer to compensate for the observed level excess of file-based IRs relative to synthesised IRs. Synth IRs bypass this block entirely because they arrive with `numCh == 4`.
6. **ER / Tail split at 80 ms** — crossover at `0.080 × sr` samples, with a 10 ms fade-out on ER and 10 ms fade-in on Tail. Each half is loaded into its respective convolver pair.

> **Known inconsistency:** The file-load IR split uses **80 ms** but the IR Synth engine's internal crossover uses **85 ms** (`ec = 0.085 × sr`). These should ideally be unified. Don't introduce a third value.

---

## IR Synth engine (IRSynthEngine.cpp)

Synthesises physically-modelled room IRs. Called from a background thread; reports progress via a callback. Produces a 4-channel 48 kHz 24-bit WAV (channels: iLL, iRL, iLR, iRR).

**Default ceiling material:** `"Painted plaster"` (low absorption, RT60 ≈ 12 s for the default room). Changed from the previous `"Acoustic ceiling tile"` default which was far too absorptive for a realistic room.

### IR Synth acoustic-character defaults

The following defaults are applied consistently in three places: `IRSynthComponent.cpp` (UI combo/slider initial values), `IRSynthParams` in `IRSynthEngine.h` (struct member defaults), and `PluginProcessor.cpp` (XML/sidecar load fallbacks). When adding or changing a default, update all three locations.

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

**Speaker/mic placement defaults** (kept in sync in three places: `FloorPlanComponent.h` `TransducerState::cx`, `IRSynthEngine.h` `IRSynthParams`, and `PluginProcessor.cpp` XML/sidecar fallbacks): **Speakers** at room centre in depth (y = 0.5), **25% and 75%** across the width (facing down). **Microphones** at 1/5 up from bottom (y = 0.8), **35% and 65%** across the width; L mic faces up-left (−135°), R mic faces up-right (−45°).

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
9. **`makeWav` writes WAVE_FORMAT_EXTENSIBLE (`0xFFFE`)** — The 4-channel WAV output uses a 40-byte extended `fmt` chunk with format tag `0xFFFE`, `cbSize=22`, `wValidBitsPerSample=24`, `dwChannelMask=0x33` (FL+FR+BL+BR quadraphonic), and the PCM SubFormat GUID `{00000001-0000-0010-8000-00AA00389B71}`. JUCE's `WavAudioFormat` silently refuses to create a reader for 4-channel files that use the plain PCM tag (`0x0001` with a 16-byte `fmt` chunk) — `createReaderFor` returns nullptr and the IR load fails without any error. `dwChannelMask=0` (unspecified) causes macOS QuickTime and CoreAudio to reject the file as incompatible, which also prevents the plugin from loading it on macOS. Always use `dwChannelMask=0x33` to match JUCE's own WAV writer. Do not revert to a minimal 16-byte `fmt` chunk or `dwChannelMask=0` for multichannel output.

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

### Speaker and microphone Z-heights (`sz` and `rz`)

`sz` (source/speaker height) and `rz` (receiver/mic height) are computed in `IRSynthEngine.cpp` from the effective ceiling height `He = p.height * hm` (where `hm` is the vault multiplier from `getVP()`):

```cpp
// Physical placement heights — clamped to 90% of He for low-ceiling rooms.
//   sz  = instrument / speaker at 1 m off the floor
//   rz  = Decca tree / outrigger mic at 3 m
double sz = std::min(1.0, He * 0.9);
double rz = std::min(3.0, He * 0.9);
```

These were previously `He * 0.55` for both (a symmetric geometric approximation). The 1 m / 3 m values model a standard orchestral recording setup: instruments on the floor at roughly seated/standing height, and a Decca tree or outrigger array at ~3 m above the floor. Changing these values shifts the direct-path distance and all reflection geometry, which moves the IR onset and changes IR_11's golden onset index — re-run `./PingTests "[capture]" -s` after any `sz`/`rz` change.

Sidecar files: `.ping` (JSON) stored alongside each synthesised WAV, containing the `IRSynthParams` used to generate it. Loaded by `loadIRSynthParamsFromSidecar()` to recall IR Synth panel state.

---

## Licence system

- **Algorithm:** Ed25519 (libsodium). Public key embedded in `LicenceVerifier.h`.
- **Serial format:** Base32-encoded signed payload `normalisedName|tier|expiry`.
- **Tiers:** `demo`, `standard`, `pro`. Expiry `9999-12-31` = perpetual.
- **Storage:** Checked in order (new paths first, old P!NG-nested paths as fallback with auto-migration): `~/Library/Audio/Ping/licence.xml` → `/Library/Application Support/Audio/Ping/licence.xml` → `~/Library/Audio/Ping/P!NG/licence.xml` → `/Library/Application Support/Audio/Ping/P!NG/licence.xml`.
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
| IR folder | `~/Library/Audio/Impulse Responses/Ping/` |
| Licence (user) | `~/Library/Audio/Ping/licence.xml` |
| Licence (system) | `/Library/Application Support/Audio/Ping/licence.xml` |
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

- **IR Synth is a single-page layout — no tabs** — `IRSynthComponent` no longer uses `TabbedComponent`. All acoustic-character controls (Surfaces, Contents, Interior, Options) and the room-geometry controls (Shape, Width/Depth/Height) are stacked in a left column (~35% width). `FloorPlanComponent` occupies the right column (~65% width) and is visible at all times — users can see speaker/mic placement while editing room character. `layoutControls(Rectangle<int> leftBounds)` handles the entire left column; section header bounds (`surfacesHeaderBounds` … `roomHeaderBounds`) are stored as members and drawn in `paint()` using the same small-caps + underline style as the main plugin's group headers. The background is the same `texture_bg.jpg` brushed-steel image as the main UI, with a `0xd4141414` dark overlay for readability.
- **IR Synth acoustic-character defaults live in three places** — `IRSynthComponent.cpp` (UI initial values), `IRSynthEngine.h` (`IRSynthParams` struct defaults), and `PluginProcessor.cpp` (XML/sidecar load fallbacks). All three must be kept in sync. Missing a location means the UI shows the right default but a freshly constructed `IRSynthParams` (or a preset missing the attribute) will use the old value.
- **Speaker/mic placement defaults live in three places** — `FloorPlanComponent.h` (`TransducerState::cx`), `IRSynthEngine.h` (`IRSynthParams` source_* / receiver_*), and `PluginProcessor.cpp` (XML `getDoubleAttribute` fallbacks for slx/sly/srx/sry, rlx/rly/rrx/rry). Speakers at **25% and 75%** (y = 0.5), mics at **35% and 65%** (y = 0.8). Keep all three in sync when changing placement defaults.
- **`/Library` install path** — Intentional. The .pkg installer deploys system-wide. Don't change copy dirs to `~/Library`.
- **Speaker directivity in image-source** — `spkG()` is applied using the real source→receiver angle, not the image-source position. This is deliberate: image-source positions aren't physical emitters and using them would give wrong distance-dependent directivity results.
- **Deferred allpass diffusion** — The allpass starts at 65 ms (not sample 0) to prevent early-reflection spikes from creating audible 17ms-interval echoes in the tail.
- **Stereo decorrelation allpass (R only)** — After EQ and before Width, the **right** channel of the wet buffer is passed through a 2-stage allpass (7.13 ms, 14.27 ms, g=0.5). Delays are incommensurate with FDN/diffuser times. Allpass has unity gain so the mono sum L+R is unchanged; the phase/time difference on R alone reduces stereo collapse at strong FDN modes and makes the tail feel more spacious. Implemented in `PluginProcessor` (decorrDelays, decorrBufs, decorrPtrs, decorrG); initialised in `prepareToPlay()`, processed in `processBlock()` before `applyWidth()`.
- **Post-convolution crossfeed (ER and Tail)** — After convolution, before ER/tail mix: when **ER crossfeed on** or **Tail crossfeed on**, the corresponding buffer(s) get a delayed (5–15 ms) and attenuated (−24–0 dB) copy of the opposite channel (L→R, R→L). Four delay lines (two per path), on/off switch per path. Params: `erCrossfeedOn`, `erCrossfeedDelayMs`, `erCrossfeedAttDb`, `tailCrossfeedOn`, `tailCrossfeedDelayMs`, `tailCrossfeedAttDb`. UI: **main editor Row 2** (see UI layout notes) — controls live in `PluginEditor`, not `IRSynthComponent`. Purely live/real-time; no IR recalculation on any crossfeed parameter change. No IR Synth engine changes.
- **Constant-power dry/wet** — `√(mix)` / `√(1−mix)` crossfade. Don't change to linear without a reason.
- **SmoothedValue everywhere** — All parameters that scale audio use `SmoothedValue` (20 ms). Any new audio-scaling parameter should do the same.
- **processBlock scratch buffers are pre-allocated members, not locals** — `dryBuffer`, `convLIn`, `convRIn`, `convTmp`, `convLEr`, `convREr`, `convLTail`, `convRTail` are `juce::AudioBuffer<float>` members sized to `samplesPerBlock` in `prepareToPlay`. This eliminates ~8 heap allocations per `processBlock` call, which caused timing spikes at small buffer sizes (128 samples = 2.67 ms budget on M-series). **Critical:** `convTmp` is the intermediate buffer passed to `juce::dsp::Convolution::process` via `AudioBlock`. It must be wrapped using the explicit three-argument form `juce::dsp::AudioBlock<float>(convTmp.getArrayOfWritePointers(), 1, (size_t)numSamples)` — **not** the single-argument form `AudioBlock<float>(convTmp)`. The single-argument constructor captures `buf.getNumSamples()` (= `samplesPerBlock`) regardless of how many samples were actually written; when the host sends a short block (playback start/stop, bounce), the convolvers process stale data from the previous block, corrupting their overlap-add state and causing persistent crackling. The explicit constructor pins the block to `numSamples` and is safe for all block sizes. **History of this bug:** The convolution block was originally written with per-block local allocations (`juce::AudioBuffer<float> tmp(1, numSamples)`), making the single-argument `AudioBlock(tmp)` safe because `tmp.getNumSamples()` was always exactly `numSamples`. When the locals were replaced with pre-allocated members (to remove the heap allocations), `AudioBlock<float>(convTmp)` was mistakenly left as the single-argument form. `convTmp` is sized to `samplesPerBlock`, so the AudioBlock told all 8 NUPC convolvers to process `samplesPerBlock` samples — but only `numSamples` had been written. The stale tail samples permanently corrupted the overlap-add partitioned-convolution state, producing distortion that persisted until the IR was fully reloaded. The symptom was distortion on a fresh load into a new Logic project, because Logic reliably sends a short first block. Fix: always use the 3-argument constructor so the AudioBlock length tracks `numSamples` exactly.
- **loadIR from message thread only** — Convolver loading is not real-time safe. Always call `loadIRFromFile` / `loadIRFromBuffer` from the message thread (UI callbacks, timer, not processBlock).
- **IR load crossfade guard prevents partial-swap distortion** — `loadIRFromBuffer` calls `loadImpulseResponse` on all 8 convolvers (`tsEr*` / `tsTail*`) sequentially. Each call triggers an asynchronous JUCE background thread that prepares the IR and atomically swaps it in during its own next `process()` call. The 8 swaps happen independently, so the audio thread can be summing e.g. `new_LL + old_RL + old_LR + old_RR` — wrong-level mixed IR output that sounds like distortion, especially when switching presets while audio is playing. The fix: `irLoadFadeSamplesRemaining` (`std::atomic<int>`, initialised to 0) is armed to `kIRLoadFadeSamples = 48000` (1 second at 48 kHz) in `loadIRFromBuffer` *before* the first `loadImpulseResponse` call. In `processBlock`, just before the dry/wet blend, if the counter is > 0 a linear fade-in is applied to the wet buffer (`buffer.applyGain(fadeIn)`) and the counter is decremented by `numSamples`. The counter is **sample-based, not block-based** — this is critical: a block-count of 64 was originally sized for 512-sample buffers (≈ 680 ms), but at 128-sample buffers it shrinks to only ≈ 170 ms, which is not long enough for JUCE's background thread to fully prepare all 8 convolvers for large/long IRs. At 128-sample buffers, large space IRs would crackle when selected from the preset list (but not when freshly synthesised, since synthesis itself takes several seconds and the convolvers are ready by the time audio is first heard). The sample-based approach gives a consistent 1-second fade at any buffer size. The dry signal is unaffected throughout. Do not remove this guard or move the `irLoadFadeSamplesRemaining.store()` call to after any `loadImpulseResponse` call — the counter must be set before any background thread is kicked off.
- **IR Synth runs on a background thread** — `synthIR()` is blocking and can take several seconds. Never call it from the audio or message thread directly; always dispatch to a background thread.
- **No IR peak normalisation** — Removed intentionally. IR amplitude follows the 1/r law so speaker–mic distance affects wet level naturally. Don't add normalisation back without considering that it would flatten the proximity effect.
- **`makeWav` must use WAVE_FORMAT_EXTENSIBLE with `dwChannelMask=0x33` for 4-channel output** — Two distinct bugs: (1) JUCE's `WavAudioFormat::createReaderFor` silently returns nullptr for 4-channel files using the plain PCM tag (`0x0001`, 16-byte `fmt` chunk) — always use `tag=0xFFFE` with a 40-byte `fmt` chunk. (2) `dwChannelMask=0` (unspecified) causes macOS QuickTime/CoreAudio to reject the file as incompatible even though the EXTENSIBLE tag is correct — use `dwChannelMask=0x33` (FL+FR+BL+BR = 0x1|0x2|0x10|0x20) to match JUCE's own WAV writer. Both bugs produce silent failure: the IR appears in the combo list but the waveform stays blank and audio is unchanged. Always verify a newly generated factory IR opens in QuickTime as a sanity check.
- **`sz` / `rz` are fixed physical heights, not a fraction of ceiling height** — `sz = min(1.0, He * 0.9)` (speaker/instrument at 1 m off the floor) and `rz = min(3.0, He * 0.9)` (mic at 3 m, Decca tree / outrigger height). Previously both were `He * 0.55` (symmetric, 55% of ceiling). The physical values model a standard orchestral session: instruments at floor level ~1 m, main mics at ~3 m. The `He * 0.9` clamp prevents invalid geometry in low-ceiling rooms (e.g. a 2 m vocal booth). **Any change to `sz`/`rz` shifts the direct-path arrival and all reflection geometry, moving IR_11's onset index** — re-run `./PingTests "[capture]" -s` and update the golden values.
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
- **Trailing silence is trimmed from ALL IRs at load time (universal trim)** — `loadIRFromBuffer` applies a silence trim to every IR — both freshly synthesised and loaded from file — after the decay envelope and before 4-channel expansion. The threshold is `jmin(peak × 1e-4, 1e-4)`: −80 dB below the IR's own peak, capped at −80 dBFS absolute. The cap matters because synth IRs receive a +15 dB boost and can have peaks well above 1.0; without the cap, `peak × 1e-4` could exceed 1e-4 in absolute terms, cutting the tail while it is still perceptibly loud. The trim keeps the last sample above the threshold plus a 200 ms safety tail (min 300 ms), then applies a cosine end-fade over those final 200 ms to smooth the transition to silence. This is critical for factory `.wav` IRs: `synthIR()` allocates `8 × max_RT60` (up to 60 s), but factory files were saved without trimming, so the 8 NUPC tail convolvers would each hold a 40–60 s IR. At small buffer sizes (128–256 samples), the NUPC background FFT thread cannot process a 50-second IR within the audio callback budget, causing persistent crackle on large-space presets ("Cello Epic Hall", "Large Concert", etc.). Freshly synthesised IRs are trimmed in the `fromSynth` path before `rawSynthBuffer` is saved; the universal trim ensures file IRs get the same treatment. Do not gate this trim with `if (fromSynth)` — that was the original bug.
- **`loadSelectedIR()` is the single entry point for all IR reloads — never call `loadIRFromFile()` or `reloadSynthIR()` directly from parameter listeners** — `parameterChanged` (Stretch, Decay) and all UI callbacks must go through `loadSelectedIR()`, which routes to `reloadSynthIR()` for synth IRs and `loadIRFromFile()` for file IRs. Bypassing this (e.g. calling `loadIRFromFile(getLastLoadedIRFile())` directly) will clobber any active synth IR because `lastLoadedIRFile` is never cleared when a synth IR is loaded.
- **`audioEnginePrepared` prevents the setStateInformation → prepareToPlay race condition — do not remove this flag** — JUCE `Convolution::loadImpulseResponse` spawns a NUPC background thread that prepares FFT partitions and atomically swaps them in on the next `process()` call. If `setStateInformation` calls `loadImpulseResponse` on all 9 true-stereo convolvers (9 threads) and then `prepareToPlay` calls `reset()` on those same convolvers while the threads are still active, there is a data race on NUPC internal state. The symptom is permanent memory corruption: distortion that persists after stop/restart, escalating crackling on each subsequent preset load, and crackle audible immediately when opening a saved Logic session. This started manifesting when the convolution block was replaced from 2 stereo convolvers to 9 mono convolvers (8 ts*Conv + 1 tailConvolver) — 9× more background threads means near-certain collision. **Fix:** `audioEnginePrepared` (`std::atomic<bool>`, starts `false`, set to `true` at the END of `prepareToPlay`) distinguishes two cases: (a) Initial session load (`audioEnginePrepared = false`): `setStateInformation` saves `rawSynthBuffer` via `loadIRFromBuffer(..., deferConvolverLoad=true)` (early-returns after the `fromSynth` block without calling `loadImpulseResponse`) or just notes `selectedIRFile`, then returns without spawning any background threads. `prepareToPlay` then does all `reset()` + `prepare()` calls cleanly, sets `audioEnginePrepared = true`, and posts a `callAsync` that fires `reloadSynthIR()` / `loadIRFromFile()` on the message thread after the audio engine is fully prepared. (b) Live preset switch (`audioEnginePrepared = true`): `setStateInformation` calls `loadIRFromFile` / `loadIRFromBuffer` immediately — no `prepareToPlay` follows, so there is nothing to race against. The `callAsync` in `prepareToPlay` also handles sample-rate changes: when the host calls `releaseResources → prepareToPlay` (no `setStateInformation`), `prepareToPlay` resets the convolvers and then reloads from the already-stored `selectedIRFile` or `rawSynthBuffer`.
- **`isRestoringState` prevents triple IR loading during preset changes — do not remove this flag** — `setStateInformation` calls `loadIRFromFile` once directly (Load #1). Then `apvts.replaceState()` queues async `parameterChanged` notifications for every changed parameter; the "stretch" and "decay" listeners each call `loadSelectedIR()` when they fire (Loads #2 and #3). 3 loads × 8 convolvers = 24 `loadImpulseResponse` calls in milliseconds. The NUPC background FFT thread is completely overwhelmed and cannot keep up → persistent crackling on any preset load, regardless of IR length. The fix: `isRestoringState` (`std::atomic<bool>` on `PingProcessor`) is set to `true` at the start of `setStateInformation`, then cleared via `juce::MessageManager::callAsync([this]() { isRestoringState.store(false); })` at the end. `callAsync` posts to the **end** of the message-thread FIFO, so it fires after all queued `parameterChanged` notifications have already been processed. `PingEditor::parameterChanged` checks `pingProcessor.getIsRestoringState()` (a public `const noexcept` getter wrapping the private atomic) and returns immediately if set. This is why recalculating the same IR in the IR Synth didn't crackle — synthesis takes ~30 s, only one load fires, and the NUPC thread is ready. Do not remove `isRestoringState` or replace `callAsync` with a synchronous clear — clearing before `parameterChanged` fires would defeat the guard.
- **Preset and IR save overwrite prompts are selection-based** — Save actions only prompt when the typed name matches the currently selected existing item in the corresponding editable combo (`presetCombo` or IR synth `irCombo`) and the target file already exists. Typing a different name saves directly as a new file. Use async JUCE dialogs (`AlertWindow::showAsync`) in plugin UI; avoid blocking modal loops.
- **`getStateInformation` must strip custom children from the APVTS state XML before adding fresh ones** — `setStateInformation` calls `apvts.replaceState(ValueTree::fromXml(*xml))`, which puts our custom child elements (`irSynthParams`, `synthIR`) into the APVTS state tree as child ValueTrees. On the next save, `apvts.copyState().createXml()` includes those children. If `getStateInformation` then adds new copies without removing the old ones, each save/load cycle accumulates duplicate children. The `synthIR` child contains a base64-encoded 4-channel IR buffer (~10–15 MB); after 2–3 cycles the state exceeds DAW size limits and Logic silently truncates it, causing `getXmlFromBinary` to return nullptr and all parameters to revert to defaults. Fix: `while (auto* old = xml->getChildByName("irSynthParams")) xml->removeChildElement(old, true);` (and same for `"synthIR"`) before adding fresh children.
- **`getStateInformation` saves `rawSynthBuffer`, not `currentIRBuffer`** — `currentIRBuffer` is the post-transform buffer (after reverse, trim, stretch, decay). Saving it in the state and then passing it through `loadIRFromBuffer(..., fromSynth=true)` on restore would double-apply all transforms (each save/load cycle compounds stretch, decay, and reverse-trim). `rawSynthBuffer` is the silence-trimmed raw synth output before any transforms; on restore, `loadIRFromBuffer` correctly applies the current APVTS parameter values (stretch, decay) and the `reverse` flag once. Old sessions saved with `currentIRBuffer` will still load (same double-processing as before — no regression) but will be fixed once re-saved with the new code.
- **`rawSynthBuffer` stores the pre-processing synth IR — do not save it after any transforms** — It must be saved as the very first thing inside the `fromSynth` block of `loadIRFromBuffer`, before reverse/trim/stretch/decay are applied. If it were saved after transforms, `reloadSynthIR()` would double-apply them on every Reverse or Trim interaction.
- **+15 dB output trim is intentional** — `synthIR()` applies a fixed `+15 dB` scalar (`gain15dB = pow(10, 15/20)`) to all four IR channels as the very last step, correcting for the observed output level shortfall. Do not remove this. It does not affect the synthesis calculations, RT60, ER/tail balance, or FDN gain calibration — it is a pure post-process scalar applied after everything else.
- **−15 dB file IR trim is intentional** — During the mono/stereo → 4-channel expansion step in `loadIRFromBuffer`, a `juce::Decibels::decibelsToGain(-15.0f)` scalar is applied to the whole expanded buffer immediately after the ×0.5 per-channel compensation. This corrects the observed ~15 dB level excess of file-based IRs relative to synthesised IRs on the shared true-stereo convolution path. It is a post-expansion scalar and does not affect the ×0.5 compensation logic or `trueStereoWetGain`. Synth IRs bypass this entirely (they arrive with `numCh == 4` and skip the expansion block). If tuning is needed, adjust the single `-15.0f` constant in the expansion block — do not touch `trueStereoWetGain` or the ×0.5 gains.
- **Plate pre-diffuser is a pure convolver pre-feed** — The Plate DSP runs in `processBlock` after the saturator. It processes the post-saturator signal through the allpass cascade and colour LP, storing the result in `plateBuffer`. **IR FEED** adds `plateBuffer * irFeed` to the convolver input before convolution — the diffused signal feeds the IR alongside the main signal. The only output is via the convolver; there is no direct parallel output. At irFeed=0 the plate has no effect. Plate parameters never trigger an IR recalculation. The IR output (and therefore all IR_01–IR_11 test golden values) is unchanged.
- **Plate `effLen` and `g` are set per block, not per sample** — `plateSize` is read once, the 6 effective delay lengths are computed, and all `effLen` fields written before the sample loop. `plateDiffusion` is also read once and written to all `plateAPs[ch][s].g` before the sample loop. This avoids redundant computation while keeping the sample loop tight.
- **Plate `plateColour` is a 1-pole lowpass, not a high-shelf** — A simple 1-pole lowpass applied to the diffused signal before feeding to the convolver. At `colour = 0` the cutoff is 2 kHz (warm, dark — EMT 140 character); at `colour = 1` it is 8 kHz (bright — AMS RMX16 character). Do not replace it with a true biquad shelf — the 1-pole is intentional.
- **Plate signal path: pre-diffuser into convolver only** — The sample loop processes the input through the allpass cascade + colour LP, stores the result in `plateBuffer`, and adds `plate[i] * irFeed` to the main buffer. The convolver receives the main signal plus the diffused plate signal. There is no direct output path. `plateOn = false` skips the block entirely — zero overhead.
- **`plateDiffusion` sets g on all 6 stages simultaneously** — The g coefficient is written to all `plateAPs[ch][s].g` once per block before the sample loop. Range 0.30–0.88 keeps the filter stable and well below the unit-circle limit. Lower g = gentle, transparent scatter; higher g = very dense, metallic diffusion. Default 0.40 gives a gentle, transparent scatter suitable for a pre-diffuser.
- **Editor size is fixed at 1104×786 px — user drag-resize is disabled** — `setResizable(false, false)` in the `PingEditor` constructor. The `minW/minH/maxW/maxH` constants have been removed. Because the window size is now a known constant, all proportional layout calculations (`cw`, `0.465f × cw`, etc.) resolve to exact integers, making pixel-precise layout straightforward. A % scaling option (e.g. 75 / 100 / 125 %) can be re-introduced later by calling `setSize(editorW * scale, editorH * scale)` — all proportional formulas will scale correctly.
- **EQ is a fixed 337 px tall component — knobs above, graph below** — `kEQComponentH = 337`. Graph occupies the bottom 130 px (`b.removeFromBottom(130)` in `EQGraphComponent::resized()`); control strip occupies the remaining 207 px above. Left edge at `w/2`; right edge at `b.getRight()`. Bottom-anchored at `eqBottom = h - 34` so the graph's rendered bottom (after `reduced(4)`) lands at `h - 38`, matching the meter bottom. `ctrlAreaXOffset = 0` — knob columns fill the full EQ component width. Do not revert to positioning by `meterBarTop`.
- **EQ has 5 bands: low shelf, 3 peaks, high shelf** — DSP order: `lowShelfBand → lowBand → midBand → highBand → highShelfBand` (all `ProcessorDuplicator`). IDs are `b3`/`b0`/`b1`/`b2`/`b4` (b3 and b4 are the shelves; b0–b2 are the original peaks preserved for preset backward-compat). Frequency response in `EQGraphComponent::getResponseAt()` uses a tanh-sigmoid approximation for shelves and a Gaussian for peaks — close enough for display, avoids needing DSP coefficient access at paint time.
- **EQ control strip uses a row-per-parameter layout** — FREQ knobs all share the same Y (Row 1); GAIN knobs are shifted right by `colW/5 + gainDX` (Row 2, zig-zag); Q/SLOPE knobs return to the FREQ X column below GAIN (Row 3). Each row has independent `DX`/`DY` fine-tuning constants (`freqDX/DY`, `gainDX/DY`, `qDX/DY`) in `EQGraphComponent::resized()`. `ctrlArea = b` (the region above the graph after `removeFromBottom(104)`). The DY values (−5, −45, −65) are offsets relative to the natural row Y positions (`freqRowY`, `gainRowY`, `qRowY`) which are themselves computed relative to `ctrlArea.getY()` — they produce a compact staggered layout within the 113 px control area regardless of where that area is positioned. Knob size is **32 px** (reduced 25% from 42 px). The `DX` constants were scaled proportionally (`freqDX/qDX: −10→−8`, `gainDX: +20→+15`). Do not scale DY when resizing knobs. Each knob has an accent-orange live readout above its grey parameter label.
- **Row Y positions in `PluginEditor` use absolute anchors to avoid JUCE `removeFromTop` clamping** — `removeFromTop(n)` silently clamps `n` to the rectangle's remaining height, so when `mainArea` has shrunk (due to large `eqMinH`) to less than the combined row heights, subsequent rows land in wrong positions. Row 2–6 Y positions are computed as `row2AbsY = topKnobRow.getBottom()`, `row3AbsY = row2AbsY + row2TotalH_`, etc. — independent of whatever height remains in `mainArea`. All group-header bounds, toggle LED Y positions, and knob Y positions for all rows use these absolute anchors. Right-side rows (R1/R2/R3) share the same Y anchors as their corresponding left-side rows (Rows 1/2/3).
- **`rowShiftUp = 30 - rowKnobSize` — all knob rows are shifted down by one knob height** — `rowShiftUp` is subtracted from every row Y position, so making it smaller (by `rowKnobSize`) pushes all 9 rows (left-side 1–4 and right-side R1–R3) down by exactly `rowKnobSize` pixels simultaneously. The original `rowShiftUp = 30` provided a 30 px upward nudge; the current `30 - rowKnobSize` gives a net downward displacement from the natural `removeFromTop` position. Do not revert to a fixed positive value without updating all group-header Y positions accordingly.
- **Preset combo + save button are right-aligned in the header panel** — save button at `w - 12 - 48`, preset combo (fixed 200 px wide) immediately to its left, "Preset" label further left. The combo uses the `PingLookAndFeel::drawComboBox` override which renders a semi-transparent fill (`0x18` alpha white), a soft rounded border (`0x38` alpha), and a subtle arrow — no hard JUCE default box. `positionComboBoxText` insets the label 6 px from the left. There is no second placement block lower in `resized()` — the header placement is the only one. Do not add a repositioning block that moves the preset combo below the top bar.
- **Width is positioned below WET OUT TRIM at `outputGainCenterX, tailKnobY`** — Width (`outputGainKnobSize`) was removed from any large-knob grid. It now stacks directly below "WET OUT TRIM" at the same X (`outputGainCenterX`), sharing the same Y as the Tail knob (`tailKnobY`). LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to right-side Row R3. Do not place Width at `row6AbsY + row6TotalH_ + 70` — that position is now unused.
- **Bloom has two independent output paths (`bloomIRFeed` and `bloomVolume`)** — `bloomIRFeed` defaults to 0.4 (audible immediately on enable via the convolver); `bloomVolume` defaults to 0. The main signal is not modified by the bloom cascade; only additive injection via `bloomIRFeed` into the convolver and `bloomVolume` into the final output. At `bloomVolume = 0` and `bloomIRFeed = 0` Bloom has zero effect — but with the default `bloomIRFeed = 0.4`, Bloom is audible as soon as it is switched on.
- **`bloomBuffer` bridges Insertion 1 (pre-conv) and the post-dry/wet Volume injection within the same processBlock call** — it is not a feedback buffer. Populated at Insertion 1, read after the dry/wet blend. `bloomBuffer.clear()` at the top of Insertion 1 ensures no stale data. A reallocation guard (`if (numSamples > bloomBuffer.getNumSamples())`) handles hosts that exceed `maximumExpectedSamplesPerBlock`.
- **Bloom feedback tap is written inside Insertion 1's per-sample loop** — immediately after computing `diff` (the cascade output), `bloomFbBufs[ch][wp] = diff` is written and the pointer advanced. The convolved wet signal is **never** written to `bloomFbBufs`. This makes the feedback loop entirely self-contained: Bloom → `bloomFbBufs` → Bloom. The old architecture wrote the post-EQ wet signal to `bloomFbBufs` (old Insertion 3), which put the convolver inside the loop and caused feedback explosions with stereo IRs where the LL convolver path had significantly higher gain than the RL/LR paths.
- **`bloomVolume` is injected after the dry/wet blend** — it is added to the final output buffer after `buffer.addFrom(dryBuffer)`. This means `bloomVolume` is audible at any dry/wet setting, including fully dry. It behaves like the direct output level of a Bloom pedal sitting in parallel with the reverb unit. The old architecture injected before EQ, making it subject to both EQ and the dry/wet control.
- **Bloom g is hardcoded at 0.35 (transparent)** — `bloomDiffusion` was removed. g=0.35 was chosen to keep individual delay taps transparent (each tap contributes ~35% reflection vs 65% pass-through), so the character comes from the delay times and feedback density rather than allpass coloration. Unlike Plate where g is user-adjustable for density vs transparency, Bloom's character is defined by its delay structure. Do not add a diffusion control back.
- **Bloom uses separate L/R prime sets for genuine stereo independence** — L primes `{241, 383, 577, 863, 1297, 1913}` and R primes `{263, 431, 673, 1049, 1531, 2111}` are incommensurate (no shared factors). After several feedback cycles the L and R textures diverge into genuinely different patterns, filling the stereo field without any explicit width/decorrelation DSP. This is the primary source of Bloom's wide stereo character. Do not unify L/R primes or the stereo independence is lost.
- **Bloom delay range is ~5–40 ms (at size=1.0)** — this is deliberately below the ~30 ms threshold at which allpass delays become audible as discrete echoes. The previous {~39–300 ms} primes were all above this threshold, making the individual taps clearly audible as fragments. At 5–40 ms the allpass stages act as diffusers rather than distinct echoes, producing the "textured swirl" character. `bloomSize` scales linearly — at size=2.0 delays reach ~10–80 ms (more spacious), at size=0.5 ~2.5–20 ms (very dense).
- **Bloom feedback is safety-clamped at 0.65** — the loop gain is `bloomFeedback` alone (the convolver is no longer in the loop). The clamp provides a hard stability bound independent of IR content or signal level.
- **Cloud is a Mutable Instruments Clouds-style granular processor** — the original 8-line LFO post-convolution design was replaced with a 3-second granular capture buffer. Key design properties: (a) DENSITY uses an **exponential** spawn-interval curve so the full knob range produces useful grain counts; (b) grains scatter across the **full 3-second buffer** (min lookback = grainLen, max = 90% of buffer) — this is the primary reason it sounds like a cloud rather than a delay; (c) output is normalised by `1/√(activeCount)` for consistent perceived loudness across density settings; (d) a **4-stage all-pass diffusion cascade** (`cloudDiffuseAPs[2][4]`, reusing `SimpleAllpass`) is applied per-sample to the grain sum, smearing grain boundaries (Clouds TEXTURE mechanism). DEFAULT cloudRate = 2.0 (≈ t=0.5, ~4 grains at 200 ms LENGTH). Max grain slots = 40.
- **Cloud IR FEED uses the old VOLUME knob slot; FEEDBACK uses the old IR FEED slot** — in the UI, `cloudIRFeedSlider` is attached to `"cloudFeedback"` (FEEDBACK) and `cloudVolumeSlider` is attached to `"cloudIRFeed"` (IR FEED). The original `cloudVolume` APVTS entry is retained for backward-compatibility but has no UI attachment or DSP path. Do not swap these attachments back.
- **Cloud `cloudBuffer` is a same-block bridge** — `cloudBuffer` is written and read within the same `processBlock` call. The diffused grain output is stored there and also injected inline into the main buffer (× `cloudIRFeed`) within the same per-sample loop — no cross-block deferral. `cloudFbSamples[2]` persists across blocks (per-sample feedback state updated at the end of each sample). `cloudBuffer` is cleared at the top of each Cloud block.
- **Cloud `cloudFbSamples` is per-sample, not per-block** — the feedback state is updated every sample inside the per-sample loop (`cloudFbSamples[ch] = currentGrainOutput`), not at block boundaries. This keeps feedback response immediate and avoids block-sized steps in the captured texture.
- **Cloud grain reset reads from source channel (WIDTH-driven)** — `srcCh = -1` means read from the same channel as the output; `srcCh = 0` reads exclusively from L; `srcCh = 1` reads exclusively from R. WIDTH probability determines the likelihood of a cross-channel grain. Reverse grains (`reverse = true`) decrement the read position each sample rather than incrementing it.
- **Width is the only remaining large knob in the bottom grid** — LFO Depth, LFO Rate, Tail Mod, Delay Depth, and Tail Rate were moved to right-side Row R3 (aligned with Plate / Row 3). The grid anchor `row6AbsY + row6TotalH_ + 70` is unchanged.
- **Shimmer is a pure pre-conv 8-voice harmonic cloud — no post-conv loopback** — All 8 `shimVoicesHarm[vi][ch]` read the clean pre-conv dry signal. `shimBuffer` is a same-block bridge written pre-conv and injected into the convolver within the same block (× `shimIRFeed × 1/√8`). There is no `shimFeedbackBuf`, no post-conv capture, and no cross-block feedback path. Pitch never stacks beyond the fixed 8-voice harmonic layout regardless of any parameter. The previous loopback architecture (shimVoices / shimVoicesVol / shimFeedbackBuf / kFbCeiling) has been completely removed.
- **`shimFeedback` controls decay time of the per-voice delay lines** — exponential mapping: `T = 2 × (7.5)^(raw/0.7)`. At 0: T≈2 s, at 0.45 (default): T≈8.7 s, at 0.7: T=15 s. Feedback coefficient per voice: `shimFb = exp(-3 × voicePeriodSamps / sr / T)`, always < 1. Do not revert to LFO-depth or shimSelfFbBuf approaches.
- **`shimDelay` has a dual role** — (1) onset stagger: on shimOn false→true, `shimOnsetCounters[vi] = (vi+1) × shimDelaySamps` arms each voice; (2) delay line period: each voice applies its own prime-derived multiplier (`kShimVoiceMultiplier[vi]`, 0.4–1.6×) to `shimDelaySamps`, then takes `max(effGrainLen, voiceDelayPeriod)`. At DELAY=0 onset stagger is simultaneous and all voices fall back to `effGrainLen` (no spreading). Do not revert to a shared `basePeriod` with a small `kShimVoiceSpread` — that caused audible pulsing at long delay settings because all voices echoed at nearly the same period.
- **`shimWasEnabled` detects the false→true transition** — stored as a member bool, updated each processBlock call after arming the counters. This means the staggered onset re-fires on every Shimmer enable. Do not move the transition check inside the `shimOn == true` branch — it must run unconditionally so `shimWasEnabled` is updated even when `shimOn` is false.
- **Shimmer per-voice LFO phases are spread 45° apart** — `shimLfoPhase[vi] = vi × 2π/8`. Allpass LFO phases use `shimApLfoPhase[vi] = vi × 2π/8 × 1.3` — the 1.3× multiplier decorrelates the allpass modulation from the main delay modulation so the two sweeps don't beat in sync. Both phase arrays advance by `numSamples × lfoInc` once per voice per block (after both channels finish), not per-sample.
- **Shimmer allpass effLen is updated once per block, not per sample** — `shimAPs[vi][0/1][ch].effLen` is computed from the allpass LFO and clamped to `[1, buf.size()]` at the start of the allpass setup loop, before the per-sample grain loop. This is safe because the LFO is ~0.2 Hz and changes negligibly within a single block. Do not move effLen updates into the per-sample loop.
- **Per-voice delay lines are separate from grainBuf — no re-entry into pitch shifting** — `shimDelayBufs[kNumShimVoices][2]` and `shimDelayPtrs[kNumShimVoices][2]` are the delay state. The delay write is `dBuf[writePtr] = grainOut + shimFb × dBuf[readPtr]`; the output `grainOut + delayOut` goes only to `shimBuffer`, never back to `grainBuf`. Allocated in `prepareToPlay` at `ceil(1100 ms × sr / 1000) + 4` samples per voice per channel. Cleared on `shimOn = false`. The per-voice spread constant `kShimVoiceSpread[8]` is a local `static constexpr` inside the processBlock shimmer block.
- **Shimmer voices 6 & 7 have fixed ±cents detune, not knob-controlled** — `semiOff[] = {0,0,0,0,0,0, 3.f/100.f, 6.f/100.f}` in semitones. This is a compile-time constant, not derived from any parameter. Voice 6 beats gently against voice 0; voice 7 beats against voice 1. Do not add a "chorus detune" knob or scale these values by shimFeedback.
- **Shimmer LENGTH engine: `shimSize` is direct milliseconds (50–500 ms), UI label LENGTH** — `effGrainLen = round(shimSize_ms × sampleRate / 1000)`. `kShimBufLen` is 131072 (2.73 s at 48 kHz) — covers max grain (500 ms) + max modulated delay (750 ms) + jitter headroom. DSP tests DSP_07–DSP_09 use old single-voice constants and have diverged from production on grain length formula and buffer size; the core two-grain / Hann window / phase advancement logic they cover still applies.
- **`shimPitch` uses integer NormalisableRange (step=1)** — fractional semitones produce detuned output not aligned to musical intervals. The parameter layout uses `juce::NormalisableRange<float>(-24.f, 24.f, 1.f)`.
- **`shimIRFeed` defaults to 0.5** — unlike every other IR-feed parameter (Plate, Bloom, Cloud all default to 0), Shimmer defaults to 0.5 so the effect is immediately audible when enabled. This matches expected shimmer plugin UX: you enable it and it shimmers.
- **`shimColour` parameter removed; DELAY knob slot now drives `shimDelay`** — `shimColourSlider` / `shimColourAttach` are bound to `"shimDelay"` with range 0–1000 ms. The old `shimColour` APVTS entry no longer exists.
- **FEEDBACK knob slot drives `shimFeedback`** — `shimVolumeSlider` / `shimVolumeAttach` are bound to `"shimFeedback"` (0–0.7). `shimVolume` APVTS entry is retained for preset backward-compatibility but has no DSP path.
- **EQ response curve is display-only** — `getResponseAt()` is an approximation. The actual audio uses JUCE biquad coefficients. The two will not match exactly at steep slopes or very high/low frequencies, but are visually representative for a mixing EQ.
- **EQ knob labels must have `setInterceptsMouseClicks(false, false)`** — JUCE `Label` components default to intercepting mouse events (`setInterceptsMouseClicks(true, true)`). In `EQGraphComponent`, labels are added after sliders and therefore sit in front (higher Z-order). Without the click-through flag, labels silently swallow mouse-down events and the knob beneath never receives them. This applied to all three label types: `knobLabels[b][k]`, `knobReadouts[b][k]`, and `bandNameLabel[b]`. The FREQ readout overlapped the upper portion of the GAIN knob; GAIN readout/label overlapped the Q knob area. Fix: call `setInterceptsMouseClicks(false, false)` on all three label arrays in the `EQGraphComponent` constructor.
- **Group header title font is 10.0f** — `drawGroupHeader` in `PluginEditor.cpp paint()` uses `g.setFont(juce::FontOptions(10.0f))` (increased from 9.0f). This applies to all group title labels: IR Input, IR Controls, ER Crossfade, Tail Crossfade, Plate pre-diffuser, Bloom hybrid, Clouds post convolution, Shimmer, Tail AM mod, Tail Frq mod, and the IR Preset label. Do not revert to 9.0f.
- **Header panel uses a real texture photograph — do not replace with a programmatic gradient** — `texture_bg.jpg` (Resources, 1200×800, 166 KB) is drawn scaled to fill the entire editor in `paint()`, then a dark overlay covers the main body area. The header shows the texture directly with only a light tint. The old procedural brushed-steel gradient + grain generation loop has been removed. `bgTexture` (`juce::Image`) is loaded once in the constructor from `BinaryData::texture_bg_jpg`. Do not reintroduce `brushedMetalTexture` or any per-pixel texture generation.
- **Accent colour is icy blue `0xff8cd6ef`, not orange** — all orange highlights (`0xffe8a84a`) have been replaced with `accentIce { 0xff8cd6ef }` / `accentLed { 0xffc4ecf8 }` across `PluginEditor.cpp`, `PingLookAndFeel.cpp`, `WaveformComponent.cpp`, `IRSynthComponent.cpp`, `EQGraphComponent.cpp`, and `LicenceScreen.h`. The EQ MID 2 band identifier (`0xffe8a84a` in `EQGraphComponent.cpp` line ~21) is intentionally left amber — it is a functional band colour code, not a UI accent. Red error and green success colours in `LicenceScreen.h` are also unchanged.
- **Both logos are pre-processed at construction time** — `spitfireLogoImage` and `pingLogoImage` are loaded via `ImageCache::getFromMemory`, converted to ARGB, and have all pixels with alpha < 30 zeroed. This prevents rectangular boundary artefacts visible against light-coloured or textured backgrounds. Do this once in the constructor; do not load raw from `ImageCache` directly in `paint()`.
- **Logo bounds use correct aspect-ratio-derived width** — Spitfire: `logoH = topRowH * 0.48f`, `logoW = logoH * 474.f / 62.f` (no jmin width cap). P!NG: same `logoH`, `logoW = logoH * 578.f / 182.f`. Both drawn with `drawImageWithin(..., RectanglePlacement::centred)`. Both Y positions subtract 4 px from the centred formula. Do not revert to `drawImage` with explicit bounds or the aspect ratio will be wrong.
- **Header panel spans the full window width** — `headerPanelRect = Rectangle(0, 0, w, topRow.getBottom())` is set in `resized()`. The texture background is painted first (full editor), then the main-body dark overlay, then logos. The old `spitfireBounds.setX(irInputGainSlider.getX())` re-alignment has been removed — do not add it back.
- **Row grouping gaps** — `topKnobRow = mainArea.removeFromTop(rowTotalH)` — **`groupLabelH` is NOT included** in this allocation. `row2AbsY = topKnobRow.getBottom() + 4` (the +4 makes Row1→Row2 knob spacing match the Row3→Row4 reference). `row3AbsY = row2AbsY + row2TotalH_ + 39` (14 base + 25 extra) — 25 px of additional clearance separates the Crossfade rows from Plate/Bloom and Shimmer from Tail mod. Two distinct visual groups per side: IR+Crossfade (Rows 1–2, tight) vs Plate+Bloom (Rows 3–4, tight) on the left; Cloud+Shimmer (R1–R2, tight) vs Tail mod (R3) on the right.
- **Level meter: 300×153 px, transparent background, left-aligned at `rowStartX`** — `OutputLevelMeter` background fill was removed (transparent). Bounds: `meterX = rowStartX`, `meterY = h - 38 - meterH + 15` (= h − 176, bottom at h − 23). Do not use `b.getX()` (= `leftPad ≈ 204 px`) for `meterX` — that positions the meter far from the knob rows; use `rowStartX` (≈ 13 px) to left-align with the knobs. The `+15` shifts the meter 15 px below the natural `h - 38 - meterH` position — do not remove it.
- **DRY/WET knob is centred at `w / 2` (full window centre, not `b` centre)** — `b` starts at `leftPad = w/6`, so `b.getCentreX() = 7w/12 ≠ w/2`. Using `w/2` ensures the knob, IR combo, and waveform are visually centred in the window. The preset combo is **not** centred here — it lives right-aligned in the top bar. The DRY/WET knob size is `bigKnobSize * 1.05f` (70% of the original `1.5f` multiplier). Do not revert to the old relative formula that positioned the knob between `stretchSlider` and `outputGainSlider`.
- **`cy` uses a phantom waveform anchor — do not replace it with the current waveform dimensions** — `cy` is computed from `phantomWaveCentreY`, which uses the *old* waveform dimensions (`0.36f × cw` wide, not the current `0.27f × cw`). This keeps the DRY/WET knob at the correct visual height. The +40 px offset (`cy + 40`) shifts the entire centre-column stack (DRY/WET → IR combo → waveform) downward by 40 px. If you change the +40 offset, all four items move together. The preset combo is no longer in this stack — it is right-aligned in the top bar, independent of `cy`.
- **Right-side rows R1/R2/R3 share Y anchors with left-side Rows 1/2/3** — Cloud knobs (R1) use `rowY`; Shimmer knobs (R2) use `row2KnobY`; Tail AM/Frq mod knobs (R3) use `row3KnobY`. Their group header Y positions match their left-side counterparts exactly. The `mainArea.removeFromTop()` calls for Rows 5 and 6 (Cloud/Shimmer) are kept to preserve `mainArea` state even though the actual knob Y values come from the absolute anchors.
- **Row R3 (Tail AM/Frq mod) uses an extra-gap split identical to Row 1** — `extraGap = (idx < 2) ? 5 : 0` places the gap between the AM pair (idx 0,1) and the Frq triple (idx 2,3,4). The formula `cx = rightRowEdge - (4-idx)*rowStep - rowKnobSize/2 - extraGap` keeps the rightmost knob flush with `rightRowEdge`. `tailAMModGroupBounds` and `tailFrqModGroupBounds` are stored as member variables and drawn in `paint()` with `drawGroupHeader`. No toggle switches — these are always-active controls.
- **On/off switches are circular power-button icons, not pills** — All six group toggles (`erCrossfeedOnButton`, `tailCrossfeedOnButton`, `plateOnButton`, `bloomOnButton`, `cloudOnButton`, `shimOnButton`) are drawn by `PingLookAndFeel::drawToggleButton` as circular buttons with a power symbol (arc-with-gap + vertical line). Button size: `ledH = groupLabelH` (14 px square) for all groups — `ledW = ledH`. Body: same radial knob-face gradient as `drawRotarySlider`. Off: dim grey icon + dim border. On: `accentIce` icon + bright ice-blue border + radial LED glow. Hover: slightly brighter border. Do not reinstate the pill-shaped drawing (rounded-rectangle fill + horizontal gradient) for these IDs.
- **`rowStartX` and `rightRowEdge` — left/right insets** — Left-side rows start at `rowStartX = max(8, w/128) + 10` (10 px, was 5). Right-side rows end at `rightRowEdge = b.getRight() - 5` (5 px inset). Both lambdas (`placeRightRowKnob`, `placeR3Knob`) use `rightRowEdge` instead of `b.getRight()`. Do not revert to `+5` / `b.getRight()` — they were changed to bring both sides symmetrically inward by 5 px each.
- **Waveform sits below the IR combo, not at the old bottom-of-UI anchor** — `reverseButton` is placed 10 px below `irCombo.getBottom()`; `waveformComponent` is 2 px below the reverse button. Both are centred on `w/2` (same as DRY/WET). Do not use `row4AbsY + row4TotalH_ + 29 + kMeterBarOffset` as the waveform Y — that was the old position when the waveform lived in the right column.
- **Licence and version labels are in the bottom corners** — `licenceLabel` at `(12, h-18, w/2-12, 16)` left-justified. `versionLabel` at `(w/2, h-18, w/2-12, 16)` right-justified. Both at the same Y (bottom strip). Do not place them under the Tail Rate knob or elsewhere in the control area.
- **IR Synth bottom bar layout (current)** — Save button is centred at `barCentreX`. The IR preset combo (`irComboW = min(140, 18% of barW)`) is placed 6 px to the left of the Save button's left edge; the `irPresetLabel` ("IR preset", 56 px wide, right-justified, 10 pt, `textDim`) sits 4 px further left of the combo. The **Calculate IR** button (`previewW = 100 px`) is 12 px to the right of Save. The **Main Menu** button (`doneW = 84 px`) is right-aligned at `barArea.getRight()`. The progress bar (`progW = min(200, rightX - leftX - 12)`) is placed with its right edge at `rightX - 30` — shifted 30 px left of the Main Menu button gap — to give breathing room for the wider button text. Do not revert button labels to "Preview" / "Done", reduce button widths below 100/84 px, or remove the 30 px progress bar offset.
- **IR Synth page background uses the right 60% of `texture_bg.jpg`** — `IRSynthComponent::paint()` draws `bgTexture` with source rect `(imgWidth * 0.4, 0, imgWidth * 0.6, imgHeight)` scaled to fill the full component, not the whole image. This brighter region of the steel photograph gives the IR Synth page a lighter, more readable background. The main editor `PluginEditor::paint()` still draws the full image. Do not change the IR Synth draw call to `RectanglePlacement::fillDestination` on the full image — that reverts the brighter look.
- **`PingLookAndFeel::drawComboBox` uses `backgroundColourId` for the fill** — the old hardcoded `0x18ffffff` fill has been replaced with `box.findColour(juce::ComboBox::backgroundColourId)`. All combo boxes in the plugin explicitly set `backgroundColourId = 0x1effffff` (12% opaque white) so the fill is consistent everywhere. The border is `0x44ffffff`. Do not hardcode the fill back to a fixed alpha — the per-combo colour is what allows uniform glassy appearance across both the lighter header background and the darker IR Synth page background.
- **All combo boxes share `backgroundColourId = 0x1effffff`** — set explicitly in both `PluginEditor` (on `presetCombo` and `irCombo`) and `IRSynthComponent` (on `irCombo`, `shapeCombo`, `floorCombo`, `ceilingCombo`, `wallCombo`, `vaultCombo`, `micPatternCombo`, `sampleRateCombo`). Do not revert any of these to the old opaque `0xff2a2a2a` — that value is ignored by `drawComboBox` but setting it back would cause the combos to appear solid dark if `drawComboBox` is ever changed to use JUCE defaults.
- **`irSynthComponent.setLookAndFeel(&pingLook)` is called explicitly after `setLookAndFeel(&pingLook)`** — belt-and-suspenders propagation that guarantees all child combos inside `IRSynthComponent` use `PingLookAndFeel::drawComboBox`, regardless of construction ordering. The destructor calls `irSynthComponent.setLookAndFeel(nullptr)` before `setLookAndFeel(nullptr)`. Do not remove either call — without the explicit propagation, IRSynthComponent's combos can silently fall back to JUCE's default opaque combo rendering.

---

## Factory content system

### File system layout

```
/Library/Application Support/Ping/   ← installed by .pkg (system-wide, read-only)
    Factory IRs/
        Halls/
            <name>.wav
            <name>.wav.ping    ← IR Synth sidecar (optional)
        Large Spaces/
            ...
        Rooms/
        Scoring Stages/
        Tight Spaces/
    Factory Presets/
        Halls/
            <name>.xml
        Large Spaces/
        Rooms/
        Scoring Stages/
        Tight Spaces/

~/Library/Audio/Impulse Responses/Ping/               ← user IRs (flat, no subcategories)
~/Library/Audio/Presets/Ping/  ← user presets
    <name>.xml                       ← presets saved with no folder
    Vocals/                          ← user-created subfolders (optional)
        <name>.xml
```

Subfolder names (one level deep) become the section headings shown in the UI. Both factory and user locations support this structure. Factory content is world-readable but not user-writable.

### Repo structure for factory content

```
Installer/
    factory_irs/       ← populate with audio files + sidecars before building installer
        Halls/
        Large Spaces/
        Rooms/
        Scoring Stages/
        Tight Spaces/
    factory_presets/   ← populate with preset XML files
        Halls/
        Large Spaces/
        Rooms/
        Scoring Stages/
        Tight Spaces/
```

**To add more factory content in a future release:** drop new files (or create a new subfolder for a new category) into the appropriate `factory_irs/` or `factory_presets/` directory, bump the version in `CMakeLists.txt` and `Installer/build_installer.sh`, then rebuild the installer. No code changes are needed.

`.gitattributes` marks `*.wav` and `*.aiff` as binary so git doesn't attempt text diffs on audio files.

### Factory preset binary format

JUCE preset files use a binary format: `[magic 4B][size 4B][XML bytes][null 1B]`. The `size` field must equal the number of XML bytes **not including the null terminator**. `getXmlFromBinary` silently returns nullptr if `size > fileSize - 8`, making the preset completely unparseable.

When saving presets from within the plugin, JUCE's `copyXmlToBinary` sets the size field correctly. If preset files are ever created or edited externally (e.g. via Python scripts), always set `size_field = len(xml_bytes_without_null)`, i.e. `file_size - 9` (subtract 8-byte header and 1-byte null).

**Quick sanity check:**
```python
import struct
data = open('preset.xml', 'rb').read()
size_field = struct.unpack('<I', data[4:8])[0]
assert size_field == len(data) - 9, f"Bad size_field: {size_field} vs {len(data)-9}"
```

If a preset fails to load silently (no parameters change, no error), check the size field first — it's the most common cause.

---

## IRManager — IREntry struct and dual-location scanning

### `IREntry` struct (`IRManager.h`)

The private member changed from `juce::Array<juce::File> irFiles` to `juce::Array<IREntry> irEntries`:

```cpp
struct IREntry {
    juce::File   file;
    juce::String category;    // subfolder name, e.g. "Halls"; empty for root-level or user IRs
    bool         isFactory = false;
};
```

### New API

```cpp
static juce::File getSystemFactoryIRFolder();
// → /Library/Application Support/Ping/Factory IRs/

const juce::Array<IREntry>& getEntries() const;   // full structured list
```

All legacy methods (`getDisplayNames`, `getIRFileAt`, `getIRFiles`, `getNumIRs`) are kept intact — they iterate `irEntries` extracting `.file`. `getDisplayNames4Channel()` and `getIRFiles4Channel()` used by `IRSynthComponent` are unaffected.

### Scan order

`scanFolder()` does two passes. Factory entries come first, user entries second:

1. **Factory folder** — root files (no category), then immediate subdirectories sorted alphabetically (each becomes a category name). All entries have `isFactory = true`.
2. **User folder** — flat (no subcategories). All entries have `isFactory = false`.

The IR Synth panel (`IRSynthComponent`) uses `getDisplayNames4Channel()` which filters `irEntries` by channel count — it therefore automatically includes both factory and user 4-channel IRs.

---

## PluginProcessor — IR state persistence by file path

`selectedIRIndex` (int) has been replaced with `selectedIRFile` (juce::File) throughout:

```cpp
// PluginProcessor.h
juce::File selectedIRFile;   // empty = synth IR or nothing loaded

juce::File getSelectedIRFile() const  { return selectedIRFile; }
void       setSelectedIRFile (const juce::File& f) { selectedIRFile = f; }
```

`getStateInformation` saves the full path string:
```cpp
if (selectedIRFile != juce::File())
    xml->setAttribute ("irFilePath", selectedIRFile.getFullPathName());
```

`setStateInformation` reads `irFilePath`, with a filename-stem fallback if the saved path no longer exists (e.g. factory IR folder relocated between builds):
```cpp
juce::String savedPath = xml->getStringAttribute ("irFilePath", "");
if (savedPath.isNotEmpty())
{
    juce::File savedFile (savedPath);
    if (savedFile.existsAsFile())
    {
        selectedIRFile = savedFile;
    }
    else
    {
        // Path no longer valid — search all known IR locations by filename stem.
        juce::String stem = savedFile.getFileNameWithoutExtension();
        for (const auto& entry : irManager.getEntries())
        {
            if (entry.file.getFileNameWithoutExtension() == stem)
            {
                selectedIRFile = entry.file;
                break;
            }
        }
    }
}
```

`loadIRFromBuffer` sets `selectedIRFile = juce::File()` (empty) when `fromSynth = true`, same role as the old `selectedIRIndex = -1`.

---

## PluginEditor — sectioned IR combo

### `refreshIRList()` structure

```
[ID 1]  Synthesized IR
─── Factory ───              addSectionHeading("Factory")
  ─ Halls ─                  addSectionHeading("  Halls")
  [ID 2]  Lyndhurst Hall
  [ID 3]  ...
  ─ Large Spaces ─           addSectionHeading("  Large Spaces")
  [ID 4]  Epic Room
─── Your IRs ───             addSectionHeading("Your IRs")  (only if user has files)
  [ID 5]  My Custom IR
```

Section headings consume no IDs. The index into `getEntries()` is always `selectedId - 2`. Factory entries come first in `irEntries`, so the factory section is built by iterating until `e.isFactory` is false; the user section iterates entries skipping `isFactory == true`.

Selection is restored by file path: `pingProcessor.getSelectedIRFile()` is compared against `entries[i].file`. Falls back to entry index 0 (first available) if the file is not found.

### `refreshIRList()` — display-only, never triggers an IR reload

`refreshIRList()` populates the combo items and calls `updateIRComboSelection()` to sync the selection display. It does **not** call `loadSelectedIR()`. This is critical: `refreshIRList()` is called from the editor constructor, and opening/closing/switching the plugin UI must not trigger `loadImpulseResponse` on the convolvers. Doing so arms the `irLoadFadeBlocksRemaining` crossfade counter, which fades the wet signal to silence — killing any active reverb tail every time the user opens the UI or switches tracks in the DAW. IR loading is the processor's responsibility (via `setStateInformation`, `prepareToPlay`'s `callAsync`, or explicit user actions like combo selection).

`irCombo.clear()` **must** be called as `irCombo.clear(juce::dontSendNotification)`. Without this, clearing the combo fires `comboBoxChanged`, which calls `loadSelectedIR()`, which wipes `selectedIRFile` via `setSelectedIRFile(juce::File())` — destroying the restored file reference before `updateIRComboSelection()` can use it. Same issue applies to any other combo that has a listener wired up.

### `loadSelectedIR()`

Uses `getEntries()` directly and **always keeps `selectedIRFile` in sync**:
```cpp
int idx = irCombo.getSelectedId() - 2;
if (idx < 0) {
    pingProcessor.setSelectedIRFile (juce::File());   // Synth IR
    pingProcessor.reloadSynthIR();
    return;
}
const auto& entries = pingProcessor.getIRManager().getEntries();
if (! juce::isPositiveAndBelow (idx, entries.size())) return;
auto file = entries[idx].file;
pingProcessor.setSelectedIRFile (file);
if (file.existsAsFile()) pingProcessor.loadIRFromFile (file);
```

`setSelectedIRFile` is called in both branches so `selectedIRFile` always matches the actual loaded IR, making `updateIRComboSelection()` and `getStateInformation` reliable.

### `updateIRComboSelection()`

Syncs the IR combo display (and IRSynth panel display name) to the current `selectedIRFile` / `isIRFromSynth()` state **without** triggering any audio load. Called from `loadPreset()` after `setStateInformation`, and from anywhere the combo needs a display-only refresh. Always uses `dontSendNotification`.

### IR name display when opening the IR Synth panel

`irSynthButton.onClick` restores the currently-loaded file IR name into the IR Synth panel's combo before hiding the main panel:
```cpp
auto selectedFile = pingProcessor.getSelectedIRFile();
if (selectedFile != juce::File())
    irSynthComponent.setSelectedIRDisplayName (selectedFile.getFileNameWithoutExtension());
```
Without this, returning from the IR Synth panel always reverted to the first entry in the IRSynth IR list (the last synth preset name).

### `setSelectedIRDisplayName` — uses 0-based `getItemText` index

`IRSynthComponent::setSelectedIRDisplayName` searches `irCombo` for a matching name and selects the corresponding item. `ComboBox::getItemText(int index)` takes a **0-based** position index (not an item ID). The loop must use `getItemText(i)`, not `getItemText(i + 1)`:

```cpp
void IRSynthComponent::setSelectedIRDisplayName (const juce::String& name)
{
    int id = -1;
    for (int i = 0; i < irCombo.getNumItems(); ++i)
        if (irCombo.getItemText (i) == name)          // 0-based index — do NOT use i+1
            { id = i + 1; break; }
    if (id >= 1)
        irCombo.setSelectedId (id, juce::dontSendNotification);
    else
        irCombo.setText (name, juce::dontSendNotification);
}
```

Using `i + 1` skips position 0 entirely and causes every match to resolve to the item one position above the intended one — `setSelectedId(k)` selects position `k-1`. This was the root cause of two symptoms: (1) selecting from the IR Synth dropdown loaded the item above the selected one; (2) after loading a preset in the main menu, switching to the IR Synth panel displayed the IR above the correct one.

### `comboBoxChanged()` / `loadPreset()` / `finishSaveSynthIR()` / `setOnLoadIR` callback

All these call sites use `setSelectedIRFile(file)` directly, with file looked up from `getEntries()[idx].file`. `getPresetFile()` in `loadPreset()` searches entries by file path to restore the combo ID after `setStateInformation`.

---

## PresetManager — PresetEntry struct and dual-location scanning

### `PresetEntry` struct (`PresetManager.h`)

Mirrors `IREntry`:

```cpp
struct PresetEntry {
    juce::File   file;
    juce::String category;    // subfolder name, e.g. "Halls"; empty = root level
    bool         isFactory = false;
};
```

### New API

```cpp
static juce::File getSystemFactoryPresetFolder();
// → /Library/Application Support/Ping/Factory Presets/

static juce::Array<PresetEntry> getEntries();
```

### Scan order

Same two-pass pattern as `IRManager`:

1. **Factory folder** — root `.xml` files (no category), then immediate subdirs sorted alphabetically.
2. **User folder** (`~/Library/Audio/Presets/Ping/`) — root `.xml` files, then immediate subdirs sorted alphabetically.

### `getPresetFile(name)` updated

Searches `getEntries()` for a matching filename stem before falling back to a new file in the user root:
```cpp
for (const auto& e : getEntries())
    if (e.file.getFileNameWithoutExtension() == name)
        return e.file;
return getPresetDirectory().getChildFile (name + ".xml");
```

This means factory presets can be loaded by name. The fallback is only used when saving a brand-new preset not yet on disk.

---

## PluginEditor — sectioned preset combo and folder save UI

### `refreshPresetList()` structure

```
─── Factory ───           addSectionHeading("Factory")
  ─ Halls ─               addSectionHeading("  Halls")
  [ID n]  Big Hall
  ─ Large Spaces ─
  [ID n]  Epic Cello
─── Your Presets ───      addSectionHeading("Your Presets")  (only if user has any)
  ─ Vocals ─              addSectionHeading("  Vocals")      (if user has subfolders)
  [ID n]  My Preset       ← root-level user preset
```

IDs are assigned `i + 2` across all entries from `getEntries()` in order. Section headings consume no IDs.

`savePreset()` always saves to the user preset root directory (`~/Library/Audio/Presets/Ping/`). After saving, `refreshPresetList()` updates the combo so the new preset appears immediately.

The overwrite prompt fires only when the exact target file already exists and the typed name matches the currently selected item.

---

## Key design decisions — factory content

- **`/Library/Application Support/` for factory content, not `~/`** — the `.pkg` installer runs as root so can write there; users cannot write there, making factory content permanently read-only. No per-user copying at launch. All users on a multi-user Mac share the same factory content automatically.
- **`selectedIRFile` (juce::File) replaces `selectedIRIndex` (int)** — integer indices are fragile: they shift whenever the IR list changes (e.g. new factory IRs added in an update). Full file paths survive list changes.
- **`setStateInformation` resolves stale `irFilePath` values by filename-stem fallback** — if the saved path doesn't exist on disk (e.g. factory IR folder relocated between builds), `setStateInformation` searches `irManager.getEntries()` for a file whose `getFileNameWithoutExtension()` matches the saved path's stem. This makes presets self-healing across factory path changes without any preset migration. The match is stem-only (no extension) so `.wav`/`.aiff` format changes also survive. If no match is found `selectedIRFile` stays empty and no IR loads (same silent-skip behaviour as before).
- **`loadSelectedIR()` is the single entry point for all IR reloads** — `parameterChanged` (Stretch, Decay), the Reverse button, the Trim handle, `comboBoxChanged`, `loadPreset`, `finishSaveSynthIR`, and the `setOnLoadIR` callback all route through `loadSelectedIR()` (or `refreshIRList()` which calls it). Never call `loadIRFromFile()` or `reloadSynthIR()` directly from parameter listeners. `loadSelectedIR()` always calls `setSelectedIRFile()` in both branches (synth and file) so `selectedIRFile` stays in sync with the actual loaded IR.
- **`irCombo.clear()` must always use `juce::dontSendNotification`** — `ComboBox::clear()` fires `comboBoxChanged` if the combo previously had a selection, which calls `loadSelectedIR()`, which calls `setSelectedIRFile(juce::File())`, wiping the restored file path before `updateIRComboSelection()` has a chance to use it. Always call `irCombo.clear(juce::dontSendNotification)` in `refreshIRList()`.
- **`updateIRComboSelection()` is display-only** — it syncs the combo and IRSynth panel display name to `selectedIRFile`/`isIRFromSynth()` using `dontSendNotification`. Call it after `setStateInformation` (from `loadPreset()`). It never triggers an audio load.
- **`IRSynthComponent::setSelectedIRDisplayName` uses 0-based `getItemText` index** — `ComboBox::getItemText(int index)` takes a 0-based position, not an item ID. The search loop must use `getItemText(i)` — using `getItemText(i + 1)` skips position 0 and causes every match to resolve to the item one position above the intended one, selecting `id = i` via `setSelectedId(i)` which picks position `i-1`. This bug caused the IR Synth dropdown to load the item above the selected one, and caused the IR Synth panel to display the wrong item after loading a preset from the main menu.
- **IR combo ID mapping: `selectedId - 2 = index into getEntries()`** — `addSectionHeading()` in JUCE's `ComboBox` does not consume IDs. IDs are therefore a flat 1-based offset: ID 1 = Synth, ID 2 = `entries[0]`, ID 3 = `entries[1]`, etc. This mapping must be maintained exactly — do not use a separate ID counter or the entries array will be misaligned.
- **Factory entries always come first in `irEntries` / `getEntries()`** — `scanFolder()` does the factory pass before the user pass. `refreshIRList()` and `refreshPresetList()` rely on this ordering: they iterate forward and break/continue on `isFactory` to separate the two sections. Do not interleave factory and user entries.
- **`getPresetFile(name)` searches factory entries too but `savePreset()` bypasses it** — `getPresetFile()` is used by `loadPreset()` (reading) and finds both factory and user presets by name. `savePreset()` constructs the target path directly from `getPresetDirectory()`, so it always writes to the user location regardless of what `getPresetFile()` would return.
- **`.ping` sidecar files are copied alongside `.wav` IRs by `cp -R`** — `build_installer.sh` uses `cp -R "$SCRIPT_DIR/factory_irs/"* "$FACTORY_DEST/Factory IRs/"`, which copies the full subfolder tree including any `.ping` sidecar files. When `IRManager` loads a factory IR selected from the combo, `PluginEditor` checks for a `.ping` sidecar via `file.getSiblingFile(stem + ".ping")` and loads the `IRSynthParams` from it if present. This restores the IR Synth panel state to match how the IR was generated.
- **`Tools/generate_factory_irs.cpp` regenerates all factory content** — builds a standalone binary (no JUCE dependency) that calls `IRSynthEngine` directly to synthesise the 27 real-world venue IRs and writes WAV + `.ping` sidecar + JUCE binary preset for each. Compile and run:
  ```bash
  g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
      Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
      -o build/generate_factory_irs -lm
  ./build/generate_factory_irs Installer/factory_irs Installer/factory_presets
  python3 Tools/trim_factory_irs.py Installer/factory_irs
  ```
  After any `IRSynthEngine` change that affects acoustic output (room geometry, FDN, height formula, etc.), regenerate all factory IRs before cutting a release so they stay in sync with the engine. The tool outputs to `Installer/factory_irs/` and `Installer/factory_presets/` — both are committed to the repo and are the source of truth for the installer payload. Current inventory: 27 venues across Halls (11), Large Spaces (8), Rooms (9), Scoring Stages (7), Tight Spaces (8). Venue parameters are documented inline in the tool.

  **Factory IRs must use WAVE_FORMAT_EXTENSIBLE** — `IRSynthEngine::makeWav` now writes `tag=0xFFFE` with a 40-byte `fmt` chunk. If you ever see factory IRs appearing in the IR combo but silently failing to load (waveform blank, no audio change), check the WAV `fmt` tag first: `python3 -c "import struct; d=open('file.wav','rb').read(80); [print(f'tag=0x{struct.unpack(\"<H\",d[p+8:p+10])[0]:04x}') for p in range(12,60) if d[p:p+4]==b'fmt ']"` — it must be `0xfffe`, not `0x0001`.
- **No `.DS_Store` files in the installer payload** — macOS creates `.DS_Store` files in any Finder-browsed folder. These must be removed from `Installer/factory_irs/` and `Installer/factory_presets/` before building the installer, or they will be installed to `/Library/Application Support/`. Remove with `find Installer -name '.DS_Store' -delete` before running `cmake --build build --target installer`.
- **`Tools/trim_factory_irs.py` trims trailing silence from IR .wav files** — `build_installer.sh` runs this automatically on the staging copy of factory IRs during packaging. Run it manually on user-saved IRs and installed factory IRs to fix files saved before silence trimming was introduced:
  ```bash
  # Trim repo factory IRs + ~/Library/Audio/Impulse Responses/Ping/ (user-saved IRs):
  python3 Tools/trim_factory_irs.py
  # Trim the currently-installed factory IRs on this machine:
  python3 Tools/trim_factory_irs.py "/Library/Application Support/Ping/Factory IRs"
  ```
  The script handles WAVE_FORMAT_EXTENSIBLE (4-channel 24-bit) files, applies −80 dB trim with 200 ms safety tail (minimum 300 ms), and writes in-place atomically. User-saved IRs created before v2.3.3 may be 8×RT60 (up to 60 s) — these are the main target. Future saves via `saveCurrentIRToFile` write `currentIRBuffer` which is already trimmed at load time.

  **Critical — file permissions must be preserved on write:** `tempfile.mkstemp` creates temp files with mode `0600` (owner-only). When the script rewrites a file owned by root (as factory IRs are after `.pkg` install), the output ends up `0600` — unreadable by the plugin running as the user. JUCE's `createReaderFor` silently returns nullptr for unreadable files, so the IR silently fails to load with no error or waveform update. Fix is in `_write_wav`: `os.stat(path).st_mode` is captured before writing, then `os.chmod(tmp, orig_mode)` is called on the temp file before `os.replace`. **If IRs stop loading after running the trim script on installed files, check permissions first:** `ls -la "/Library/Application Support/Ping/Factory IRs/Large Spaces/"` — any file showing `-rw-------` instead of `-rw-r--r--` needs `sudo chmod 644 <file>`.

  **Chunk order is also preserved on write:** `_write_wav` reconstructs the RIFF body by iterating the original chunks in order and replacing only the `data` chunk content in place. Earlier versions always output `fmt → extra_chunks → data` regardless of the original order (e.g. `JUNK → fmt → data`). JUCE handles both orders, but preserving the original is more correct.
