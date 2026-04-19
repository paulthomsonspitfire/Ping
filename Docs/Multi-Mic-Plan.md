# P!NG — Multi-Mic IR Synthesis: Architecture Plan

**Feature:** Four independent mic paths (Direct, Main, Outriggers, Ambients) with a summing mixer replacing the bottom-left level meter.

**Note on naming:** Strip names are DIRECT / MAIN / OUTRIG / AMBIENT. The main pair is called "MAIN" not "TREE" to avoid implying a Decca Tree configuration. When a dedicated Decca Tree synthesis mode is added in a future release, the main strip will become "DECCA" or gain a mode selector. This label is in the UI only — parameter IDs remain `main*` throughout.

---

## Overview

The current IR synth produces a single true-stereo (4-channel) IR from one mic pair (the main stereo pair). This plan extends that to four independent mic paths synthesised in parallel, each producing its own 4-channel IR, all feeding a small mixer before summing into the existing post-convolution chain. Anything downstream of the convolver — EQ, LFO mod, Width, Tail Chorus, Cloud, Shimmer, Bloom Volume — is unchanged and operates on the summed signal.

### The four paths

| Strip | UI Label | Description | IR type | Can switch off? |
|---|---|---|---|---|
| 1 | DIRECT | Order-0 only: direct wavefront from speaker to **main mic positions**. No wall bounces. Frequency-dependent mic polar colouration applied. Like Altiverb's "direct" path. Additive on top of the MAIN full IR. | Extremely short (~100 samples at the arrival spike) | Yes |
| 2 | MAIN | The primary stereo mic pair. Full ER + Tail synthesis. | Existing 4-channel, full length | Yes |
| 3 | OUTRIG | Wider stereo outrigger pair, same height as main mics. Full ER + Tail. | 4-channel, full length | Yes |
| 4 | AMBIENT | Higher, further-back ambient pair. Full ER + Tail. | 4-channel, full length | Yes |

All four paths can be independently switched on or off. The DIRECT path uses the same mic positions as MAIN (the same physical mics receive both the direct wavefront and the reverberant field — that's physically correct). Per-path DIRECT paths for Outrig/Ambient are a future enhancement; for now DIRECT is tied to the main mic pair only (their direct arrival is already embedded naturally in the Outrig/Ambient full IRs).

### Single combined fader per path

Each strip has one gain fader covering the full path output. There are no separate ER/Tail faders per strip. The MAIN path's existing `erLevel` and `tailLevel` parameters continue to work as they do now and feed into the MAIN path before that strip's fader is applied.

### High-pass filter per strip

Each strip has a fixed 110 Hz high-pass toggle (2nd-order Butterworth). Off by default on DIRECT and MAIN; typically engaged on OUTRIG and AMBIENT. The 110 Hz crossover is the standard starting point for orchestral ambient mics.

---

## Signal Flow (updated)

```
Input (stereo)
  │
  ├──────────────────────────────► dryBuffer (for dry/wet blend at end)
  │
  ▼
[Predelay → Input Gain → Saturator]
  │
  ▼
[Plate / Bloom / Cloud IR Feed / Shimmer]  ← pre-conv effects (unchanged)
  │
  ▼  (single signal bus, same as now)
  ┌──────────────────────────────────────────────────────┐
  │  PARALLEL CONVOLUTION (up to 4 active paths)         │
  │                                                      │
  │  DIRECT path (if on):                                │
  │    tsDirectConvLL/RL/LR/RR  ──► gain + pan ──┐       │
  │                                               │       │
  │  TREE path (always on):                       │       │
  │    tsErConvLL/RL/LR/RR  ─ ER ─► erLevel ─┐   │       │
  │    tsTailConvLL/RL/LR/RR─ Tail►tailLevel─┘   │       │
  │    └──────────────────────────► gain + pan ──┤       │
  │                                               │       │
  │  OUTRIG path (if on):                         │       │
  │    tsOutrigErLL/RL/LR/RR  ─ ER ──────────┐   │       │
  │    tsOutrigTailLL/RL/LR/RR─ Tail ─────────┘   │       │
  │    └──────────────────────────► gain + pan ──┤       │
  │                                               │       │
  │  AMBIENT path (if on):                        │       │
  │    tsAmbErLL/RL/LR/RR  ─ ER ──────────────┐   │       │
  │    tsAmbTailLL/RL/LR/RR─ Tail ─────────────┘   │       │
  │    └──────────────────────────► gain + pan ──┤       │
  │                                               ▼       │
  │  Solo logic applied ──────────────────► WET SUM       │
  └──────────────────────────────────────────────────────┘
  │
  ▼
[Crossfeed (ER/Tail) → EQ → Decorrelation → LFO Mod → Width → Tail Chorus]
  │
  ▼
[Cloud post-conv → Output Gain → Spectrum push → Dry/Wet blend]
  │
  ▼
[Bloom Volume injection]
  │
  ▼
Output (stereo) + per-path peak meters
```

**Key point:** Everything from Crossfeed onward is completely unchanged — it operates on the summed wet bus. The four paths are summed *before* all existing post-conv processing.

---

## Convolver Count

| Path | State | Convolvers |
|---|---|---|
| DIRECT | On | 4 (LL/RL/LR/RR, no ER/Tail split — IR too short) |
| TREE | Always on | 8 (4 ER + 4 Tail) — unchanged |
| OUTRIG | On | 8 (4 ER + 4 Tail) |
| AMBIENT | On | 8 (4 ER + 4 Tail) |
| **Total (all active)** | | **28 convolvers** |
| **Total (Tree only)** | | **8 convolvers** — same as today |

Disabled paths contribute exactly 0 convolvers and 0 DSP cost.

The DIRECT path IR is typically ≤100 samples long (one spike per speaker-mic pair at the direct-arrival time). At 48kHz this is a ~2ms IR. JUCE's NUPC convolution is extremely efficient for short IRs — the first FFT partition covers the entire IR. Cost is negligible relative to the long reverb IRs.

---

## IR File Storage

Four 4-channel WAVE_FORMAT_EXTENSIBLE WAV files per synthesised IR, using a consistent stem + suffix convention:

| File | Contents |
|---|---|
| `<name>.wav` | TREE / main mics — unchanged, same format as now |
| `<name>_direct.wav` | DIRECT path (very short, ~2ms) |
| `<name>_outrig.wav` | OUTRIGGER mics |
| `<name>_ambient.wav` | AMBIENT mics |

One `.ping` JSON sidecar (`<name>.wav.ping`) covers all paths, extended with outrigger and ambient position params (see IRSynthParams additions below).

**Backward compatibility:** Old presets and factory IRs have only `<name>.wav`. When loaded, the `_direct`, `_outrig`, `_ambient` files are simply absent. The processor treats absent files as "path not generated/disabled" — those three strips are set to off and show an "ungenerated" state. Users can synthesise the full set by hitting Calculate IR.

---

## Part 1: IRSynthEngine Changes

### 1.1 IRSynthParams additions

```cpp
struct IRSynthParams
{
    // ... all existing fields unchanged ...

    // ── Outrigger mics ────────────────────────────────────────────────────
    bool outrig_enabled = false;
    // Normalised 0..1 positions in room footprint (wider than main mics)
    double outrig_lx = 0.15; double outrig_ly = 0.80;
    double outrig_rx = 0.85; double outrig_ry = 0.80;
    double outrig_langle = -2.35619449019;  // up-left  (-3π/4)
    double outrig_rangle = -0.785398163397; // up-right (-π/4)
    double outrig_height = 3.0;             // metres above floor (same as tree default)
    std::string outrig_pattern = "cardioid (LDC)";

    // ── Ambient mics ──────────────────────────────────────────────────────
    bool ambient_enabled = false;
    double ambient_lx = 0.20; double ambient_ly = 0.95; // further back
    double ambient_rx = 0.80; double ambient_ry = 0.95;
    double ambient_langle = -2.35619449019;
    double ambient_rangle = -0.785398163397;
    double ambient_height = 6.0;            // metres — higher up
    std::string ambient_pattern = "omni";   // ambients are typically omni

    // ── Direct path ───────────────────────────────────────────────────────
    bool direct_enabled = false;
    // No position fields — direct uses same mic positions as the main tree mics.
    // Pattern is also inherited from the tree (mic_pattern field).
};
```

### 1.2 IRSynthResult additions

```cpp
struct MicIRChannels
{
    std::vector<double> LL, RL, LR, RR;
    int irLen = 0;
};

struct IRSynthResult
{
    // Main/Tree — existing fields preserved for back-compat
    std::vector<double> iLL, iRL, iLR, iRR;  // ← kept as-is

    // Additional paths (empty vectors = not synthesised)
    MicIRChannels direct;
    MicIRChannels outrig;
    MicIRChannels ambient;

    std::vector<double> rt60;
    int   irLen      = 0;
    int   sampleRate = 0;
    bool  success    = false;
    std::string errorMessage;
};
```

The `iLL/iRL/iLR/iRR` fields continue to hold the main/tree IR, so all existing code that reads from `IRSynthResult` stays unchanged.

### 1.3 Parallel synthesis in `synthIR()`

Current: one sequential synthesis pass for the tree mics.

New: up to 4 parallel `std::thread` passes. Each thread synthesises one mic path using the full existing pipeline (calcRT60 → calcRefs × 4 → renderCh × 4 → renderFDNTail × 2 → blend → makeWav). The only difference between paths is the `rz` height and the `receiver_lx/ly/rx/ry/langle/rangle` geometry values passed to `calcRefs`.

The DIRECT path is special: `synthIRDirect()` calls `calcRefs` with `maxOrder = 0` (direct wavefront only, no reflections) and `renderCh` with the resulting single-spike Ref list. No FDN needed. The resulting IR is ≤100 samples — the entire true-stereo set is synthesised in milliseconds.

**Thread design:**

```cpp
// Inside synthIR():
auto mainFuture   = std::async(std::launch::async, [&]{ return synthMainMics(p, progressMain); });
auto directFuture = p.direct_enabled
    ? std::async(std::launch::async, [&]{ return synthDirectPath(p, progressDirect); })
    : std::future<MicIRChannels>{};
auto outrigFuture = p.outrig_enabled
    ? std::async(std::launch::async, [&]{ return synthMicPair(p, outrigGeom, progressOutrig); })
    : std::future<MicIRChannels>{};
auto ambientFuture = p.ambient_enabled
    ? std::async(std::launch::async, [&]{ return synthMicPair(p, ambientGeom, progressAmbient); })
    : std::future<MicIRChannels>{};

result.iLL = mainFuture.get().LL;  // etc.
if (directFuture.valid())  result.direct  = directFuture.get();
if (outrigFuture.valid())  result.outrig  = outrigFuture.get();
if (ambientFuture.valid()) result.ambient = ambientFuture.get();
```

Progress reporting: each thread reports independently into its own fraction of the 0..1 progress bar. Tree mics get ~60% of the bar (longest). Outrig/ambient share the remaining 40%. Direct completes near-instantly and is not shown in the progress bar.

### 1.4 `makeWav` called once per synthesised path

`IRSynthEngine::makeWav` already produces WAVE_FORMAT_EXTENSIBLE 4-channel WAVs. It will be called separately for each enabled path, with the respective channel data and the appropriate filename suffix.

---

## Part 2: PluginProcessor Changes

### 2.1 New APVTS parameters

```
// Mixer controls for each of the 4 paths
// (mainMute / mainSolo added to the existing tree path — no "mainOn" since tree is always active)

"mainGain"      -48 to +6 dB,  default 0     SmoothedValue
"mainPan"       -1 to +1,      default 0     SmoothedValue
"mainMute"      bool,          default false
"mainSolo"      bool,          default false

"directOn"      bool,          default false
"directGain"    -48 to +6 dB,  default 0     SmoothedValue
"directPan"     -1 to +1,      default 0     SmoothedValue
"directMute"    bool,          default false
"directSolo"    bool,          default false

"outrigOn"      bool,          default false
"outrigGain"    -48 to +6 dB,  default 0     SmoothedValue
"outrigPan"     -1 to +1,      default 0     SmoothedValue
"outrigMute"    bool,          default false
"outrigSolo"    bool,          default false

"ambientOn"     bool,          default false
"ambientGain"   -48 to +6 dB,  default 0     SmoothedValue
"ambientPan"    -1 to +1,      default 0     SmoothedValue
"ambientMute"   bool,          default false
"ambientSolo"   bool,          default false
```

All four `*Gain` parameters use SmoothedValue (20 ms ramp) to prevent zipper noise on fader moves. Pan uses constant-power panning: `L *= cos(pan * π/2)`, `R *= sin((1+pan) * π/4)`.

### 2.2 New convolvers

```cpp
// DIRECT path — 4 convolvers, no ER/Tail split
juce::dsp::Convolution tsDirectConvLL, tsDirectConvRL, tsDirectConvLR, tsDirectConvRR;

// OUTRIG path — 8 convolvers (ER + Tail × true stereo)
juce::dsp::Convolution tsOutrigErConvLL, tsOutrigErConvRL, tsOutrigErConvLR, tsOutrigErConvRR;
juce::dsp::Convolution tsOutrigTailConvLL, tsOutrigTailConvRL, tsOutrigTailConvLR, tsOutrigTailConvRR;

// AMBIENT path — 8 convolvers (ER + Tail × true stereo)
juce::dsp::Convolution tsAmbErConvLL, tsAmbErConvRL, tsAmbErConvLR, tsAmbErConvRR;
juce::dsp::Convolution tsAmbTailConvLL, tsAmbTailConvRL, tsAmbTailConvLR, tsAmbTailConvRR;
```

The existing `tsErConv*` and `tsTailConv*` (tree mics) are completely unchanged.

`prepareToPlay()` and `releaseResources()` are extended to prepare/reset the new convolvers.

### 2.3 IR loading

`loadIRFromBuffer()` gains a `MicPath` enum parameter (or is called via new per-path variants):

```cpp
enum class MicPath { Tree, Direct, Outrig, Ambient };
void loadIRFromBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate,
                       bool fromSynth = false, MicPath path = MicPath::Tree,
                       bool deferConvolverLoad = false);
```

The existing implementation runs for `MicPath::Tree` unchanged. For other paths, the same ER/Tail split logic runs against the appropriate convolver set.

For DIRECT: the IR is so short there's no meaningful 80ms split point. The entire IR loads into the four `tsDirectConv*` convolvers directly (no ER/Tail separation). ER/Tail level controls from the TREE path do not apply to DIRECT.

**File loading convenience:**

```cpp
void loadMicPathFromFile (const juce::File& baseIRFile, MicPath path);
// Derives _direct.wav / _outrig.wav / _ambient.wav from baseIRFile stem.
// Returns silently if the file doesn't exist (path stays unloaded/disabled).
```

`loadSelectedIR()` calls `loadMicPathFromFile` for each enabled path after loading the main IR.

### 2.4 processBlock changes

After the existing convolution block (the TREE mix to `lEr + rEr` and `lTail + rTail`), add parallel processing for each active extra path, then apply the mixer before summing to the existing wet bus:

```cpp
// ── TREE path result (existing — unchanged) ──────────────────────────────
// lEr, rEr, lTail, rTail already computed as now.
// Apply existing erLevel / tailLevel.
// Then apply TREE mixer fader + pan + mute/solo → treeL, treeR

// ── DIRECT path ───────────────────────────────────────────────────────────
if (directOn && !directMute_effective)
{
    // Run 4 short convolvers (same pattern as tsEr block but into directBuf)
    // Apply directGain + constantPower pan → directL, directR
}

// ── OUTRIG path ───────────────────────────────────────────────────────────
if (outrigOn && !outrigMute_effective)
{
    // ER + Tail from outrig convolvers, apply outrigErLevel and outrigTailLevel
    // Apply outrigGain + pan → outrigL, outrigR
}

// ── AMBIENT path ─────────────────────────────────────────────────────────
// Same pattern for ambient convolvers → ambientL, ambientR

// ── Solo logic ────────────────────────────────────────────────────────────
// If ANY solo button is active, zero out non-soloed paths before summing.

// ── Sum to wet bus ────────────────────────────────────────────────────────
// buffer = treeL/treeR + directL/directR + outrigL/outrigR + ambientL/ambientR
// Then continue with EQ, decorr, LFO, Width, Tail Chorus etc. — UNCHANGED.
```

**Important:** The existing crossfeed (ER/Tail) currently applies to the TREE path before summing with ER/Tail level. This continues to apply only to the TREE path (it models interaural crosstalk in the main mic pair). The outrig/ambient paths do not get crossfeed — their stereo image comes purely from their placement geometry.

### 2.5 Per-path peak metering

Four pairs of `std::atomic<float>` peak values (L+R per path), updated per block in processBlock, read by the GUI timer for the mixer meters. This replaces the single existing `peakL` / `peakR` pair (the existing ones are kept for the waveform display and continue to reflect the summed wet output).

```cpp
struct PathPeaks { std::atomic<float> L{0.f}, R{0.f}; };
PathPeaks peaksTree, peaksDirect, peaksOutrig, peaksAmbient;
```

### 2.6 State persistence

`getStateInformation` saves:
- All new APVTS params (automatic via `apvts.copyState()`)
- `irFilePath` — unchanged (the main TREE IR path; other paths derived from it)
- The four `rawSynthBuffer` variants: `rawSynthBuffer` (tree, existing), plus `rawSynthDirectBuffer`, `rawSynthOutrigBuffer`, `rawSynthAmbientBuffer`
- New IR synth params (outrig/ambient positions) go into the `irSynthParams` XML child as new attributes

`setStateInformation` reads back the new params. Missing attributes get defaults (new params default to disabled, so old presets simply have all extras off).

---

## Part 3: IRSynthParams Extended Storage (.ping sidecar)

The `.ping` sidecar is JSON. New fields added:

```json
{
  "outrig_enabled": false,
  "outrig_lx": 0.15, "outrig_ly": 0.80,
  "outrig_rx": 0.85, "outrig_ry": 0.80,
  "outrig_langle": -2.356, "outrig_rangle": -0.785,
  "outrig_height": 3.0,
  "outrig_pattern": "cardioid (LDC)",

  "ambient_enabled": false,
  "ambient_lx": 0.20, "ambient_ly": 0.95,
  "ambient_rx": 0.80, "ambient_ry": 0.95,
  "ambient_langle": -2.356, "ambient_rangle": -0.785,
  "ambient_height": 6.0,
  "ambient_pattern": "omni",

  "direct_enabled": false
}
```

Missing fields on load → defaults. Existing sidecars continue to load without errors.

---

## Part 4: FloorPlan UI Changes

The FloorPlan currently shows 2 speakers (white/grey) + 2 main mics (existing colour). It gains 4 more draggable mic points: outrigger L + R, and ambient L + R. Height is set by dedicated sliders in the IR Synth panel (the FloorPlan is 2D; height is a separate parameter).

### Colour coding

| Path | Point colour |
|---|---|
| Speakers | White/light grey (unchanged) |
| TREE mics | Existing accent colour (icy blue `0xff8cd6ef`) |
| OUTRIG mics | Soft purple `0xffb09aff` |
| AMBIENT mics | Amber `0xffcfa95e` |
| DIRECT | No extra point — shares TREE mic positions |

### Visibility

When a path is disabled (`outrig_enabled = false`), its mic points are shown as dimmed/hollow circles rather than filled — they indicate where the mics *would* be if enabled, without cluttering an already-active FloorPlan. Dragging a disabled mic pair's point auto-enables that path.

### FloorPlanComponent changes

`TransducerState` is extended with a `PathID` enum (`Tree, Outrig, Ambient`). `FloorPlanComponent` stores two additional `TransducerState` pairs for outrig and ambient. `mouseDown/mouseDrag/mouseUp` routing uses the PathID to call `onOutrigPlacementChanged` / `onAmbientPlacementChanged` callbacks (same pattern as the existing `onPlacementChanged`).

---

## Part 5: IRSynthComponent UI Changes

### New controls in the left column

Below the existing mic placement controls (after `micPatternCombo`):

**Outrigger section** (collapsible, shown when `outrigOn` is enabled):
- Section header "Outrigger mics" with on/off toggle (power button, same style as Plate/Bloom)
- `outrig_pattern` combo (same choices as main mic)
- `outrig_height` slider (0.5 – 8.0 m, default 3.0)
- (outrig X/Y positions are drag-only on FloorPlan — no numeric sliders needed)

**Ambient section** (collapsible, shown when `ambientOn` is enabled):
- Section header "Ambient mics" with on/off toggle
- `ambient_pattern` combo
- `ambient_height` slider (1.0 – 12.0 m, default 6.0)

**Direct section**:
- A single toggle "Direct path" with no additional params (it inherits tree mic positions and pattern)

These toggles set `IRSynthParams::outrig_enabled` etc. and dirty the IR (same `onParamModifiedFn` pattern). They do not trigger an IR reload by themselves — only Calculate IR produces new audio.

---

## Part 6: PluginEditor Mixer UI

The bottom-left area currently holds `OutputLevelMeter` (300×153px at `(rowStartX, h−176)`). This is replaced by a 4-strip mixer component (`MicMixerComponent`).

### MicMixerComponent layout

Total dimensions: ~310×160px (slightly wider than the old meter to fit 4 strips + labels).

Each strip is ~70px wide:

```
┌──────────────────────────────────────────────┐
│  DIRECT   │  TREE   │  OUTRIG   │  AMBIENT   │
│           │         │           │            │
│  [0-]     │  [0-]   │  [0-]     │  [0-]      │  ← fader readout
│           │         │           │            │
│  ║│║      │  ║│║   │  ║│║      │  ║│║       │  ← vertical fader + meter
│  ║│║      │  ║│║   │  ║│║      │  ║│║       │
│  ║│║      │  ║│║   │  ║│║      │  ║│║       │
│           │         │           │            │
│  DIRECT   │  TREE   │  OUTRIG   │  AMBIENT   │  ← label
│           │         │           │            │
│  [⏻]      │  [⏻]   │  [⏻]      │  [⏻]       │  ← power toggle (TREE always lit)
│  [M] [S]  │  [M][S] │  [M] [S]  │  [M] [S]   │  ← mute + solo
│  [pan ──] │  [pan─] │  [pan ──] │  [pan ──]  │  ← pan knob (small rotary)
└──────────────────────────────────────────────┘
```

### Strip anatomy

Each strip, top to bottom:
1. **Fader readout** — small label showing current gain in dB (e.g. "0.0", "-6.2")
2. **Fader + meter** — the combined fader/meter column:
   - Coloured level bar (fills from bottom, path colour from the FloorPlan colour table)
   - Thin white fader thumb overlaid on the bar
   - Dragging the thumb sets the APVTS gain param
   - Disabled paths shown grey/dim
3. **Label** — DIRECT / TREE / OUTRIG / AMBIENT, 10pt, same small-caps style as group headers
4. **Power toggle** — circular power-button icon (same `PingLookAndFeel::drawToggleButton` as Plate/Bloom/Cloud/Shimmer). TREE's toggle is always lit and non-interactive (it cannot be switched off)
5. **M / S buttons** — small pill-style toggle buttons; M = mute, S = solo. Same style as used elsewhere. When any S is active, all non-soloed strips show M-state visually
6. **Pan knob** — small rotary (same size as power-button, `rowKnobSize * 0.7f`), centred below M/S. Centre detent. Displays "C", "L50", "R100" etc. in a tooltip/readout

### Colour scheme (matches FloorPlan + reference image)

| Strip | Meter colour |
|---|---|
| DIRECT | Pale blue `0xff7ab8d4` |
| TREE | Icy accent `0xff8cd6ef` (existing accent) |
| OUTRIG | Soft purple `0xffb09aff` |
| AMBIENT | Amber `0xffcfa95e` |

Disabled/muted strips: bar colour at 40% opacity, label greyed.

### Layout position

`MicMixerComponent` is positioned identically to the old `outputLevelMeter`:
```cpp
const int mixerW = 310;
const int mixerH = 160;
const int mixerX = rowStartX;
const int mixerY = h - 38 - mixerH + 15;   // bottom at h − 23, same as old meter
micMixer.setBounds(mixerX, mixerY, mixerW, mixerH);
```

`outputLevelMeter` is removed from `PluginEditor` entirely. The summed wet bus peaks (for the waveform `WaveformComponent`) continue to be computed in processBlock as they are now.

---

## Part 7: Factory IR Regeneration

`Tools/generate_factory_irs.cpp` must be updated to:
1. Define outrigger and ambient positions for each of the 27 venues (typical orchestral convention: outriggers at ~10–15% further out than tree mics, ambients at 95% depth, 6–8m high)
2. Emit 4 WAV files per venue instead of 1
3. Pass `outrig_enabled = true` and `ambient_enabled = true` to `synthIR()` by default for factory content

Factory IR count will grow from 27 to 27×4 = 108 WAV files. Installer payload increases by ~3× (outrig/ambient are full-length IRs; direct is tiny).

The `trim_factory_irs.py` script handles any WAV matching `*.wav` in the factory directories — no changes needed, it will automatically trim `_direct.wav`, `_outrig.wav`, `_ambient.wav` files alongside the main ones.

---

## Part 8: Key Risks and Mitigations

### R1: Memory footprint with 28 convolvers
**Risk:** 3 full-length (10–15s) true-stereo IR sets × 4 convolvers each = ~24 NUPC convolvers holding large FFT partition tables. Potential memory pressure on machines with limited RAM.
**Mitigation:** Outrig and ambient paths are off by default. Only enabled paths allocate convolver memory. We can also add a "quality" option later (shared ER/Tail for outrig/ambient to halve convolver count). Direct path IR is ~100 samples — negligible memory.

### R2: irLoadFadeBlocksRemaining coverage
**Risk:** Currently 64 blocks covers IR swap for 8 convolvers. With up to 28 convolvers (if all loaded simultaneously), JUCE background threads may not all complete within 64 blocks at small buffer sizes.
**Mitigation:** Port `irLoadFadeBlocksRemaining` to sample-based (as documented in CLAUDE.md — currently block-based in code). Set to 96000 samples (2 seconds) when any of the extra paths is loaded. This gives JUCE's threads ample time regardless of buffer size.

### R3: setStateInformation with multiple rawSynthBuffers
**Risk:** `getStateInformation` currently embeds `rawSynthBuffer` as base64 (~10–15 MB). With 3 more buffers it could reach 40–60 MB, exceeding DAW state size limits.
**Mitigation:** Only embed `rawSynthBuffer` (tree mics) in state — same as now. The other three paths' WAV files are always on disk (saved alongside the main IR) and are re-loaded from file on state restore. If the outrig/ambient/direct WAV files are absent at restore time, those paths default to off.

### R4: Synthesis thread safety
**Risk:** `calcRefs` and `renderCh` use local-only state except for the `IRSynthParams` input (read-only). The FDN functions (`renderFDNTail`) also use only local state. Parallel thread calls are safe.
**Risk edge case:** Progress callback is called from all 4 threads simultaneously — the `IRSynthProgressFn` lambda must be thread-safe.
**Mitigation:** Use per-thread progress values guarded by atomic floats; aggregate in the reporting callback.

### R5: Convolver prepare/reset ordering in prepareToPlay
**Risk:** prepareToPlay currently has the `audioEnginePrepared` / `isRestoringState` guard system to prevent data races. Adding 20 more convolvers to the reset sequence increases the window for racing.
**Mitigation:** All new convolvers are added to the same sequential reset block in prepareToPlay, before `audioEnginePrepared` is set to true. The existing guard mechanism already covers all convolvers — we just extend the list.

### R6: FloorPlan complexity with 10 drag points
**Risk:** 2 speakers + 6 mic points (tree L/R, outrig L/R, ambient L/R) — the FloorPlan becomes crowded.
**Mitigation:** Show only enabled paths' mic points as active (filled). Disabled paths show ghost/hollow points. Label each pair (T, O, A) in small text next to the points.

---

## Implementation Phases

| Phase | Scope | Dependencies |
|---|---|---|
| 1 | `IRSynthParams` / `IRSynthResult` extensions + parallel `synthIR()` | None — pure engine |
| 2 | New convolvers + `loadIRFromBuffer(MicPath)` in PluginProcessor | Phase 1 |
| 3 | New APVTS params + processBlock mixer logic | Phase 2 |
| 4 | FloorPlan drag points for outrig + ambient | None — pure UI |
| 5 | IRSynthComponent controls (outrig/ambient toggles, height sliders) | Phase 1, 4 |
| 6 | `MicMixerComponent` (fader + meter + M/S/pan strips) | Phase 3 |
| 7 | State persistence (getState/setState for new buffers and params) | Phase 3 |
| 8 | Factory IR regeneration (`generate_factory_irs.cpp` update) | Phase 1 |
| 9 | Test suite extensions (new engine tests for multi-mic output) | All |

Phases 1 and 4 can run in parallel. Phases 2/3/5/6/7 are sequential. Phase 8 is gated only on Phase 1.

---

## What Doesn't Change

The following are completely unchanged by this feature:

- All pre-convolution effects (Plate, Bloom, Cloud IR Feed, Shimmer) — same signal feeds all active paths
- All post-convolution processing (ER/Tail crossfeed on TREE path, EQ, Width, LFO mod, Tail Chorus, Cloud post-conv, Bloom Volume, Dry/Wet blend, Output Gain)
- The existing TREE mic path and its 8 convolvers — not touched
- The `WaveformComponent` — continues to display the TREE IR
- `IRManager`, `PresetManager` — no changes
- All existing APVTS parameters — no changes
- The Licence system, `LicenceVerifier` — no changes
- Test suite IR_01–IR_13 and DSP_01–DSP_14 — these test the engine and DSP building blocks which are not modified

---

## Resolved Design Decisions

1. **Single combined fader per strip** — No separate ER/Tail faders on OUTRIG or AMBIENT. One gain fader per path. The MAIN path's existing `erLevel`/`tailLevel` parameters continue to work internally and feed into MAIN's strip fader.

2. **All four paths can be switched off independently** — including MAIN. No always-on paths. If MAIN is off and DIRECT is on, the user hears only the direct arrival with no reverb (a useful creative option).

3. **Default outrigger/ambient positions** — Outrig: 15%/85% width, same depth and height as main mics. Ambient: 20%/80% width, 95% depth, 6 m height. These apply when opening old presets and hitting Calculate IR.

4. **High-pass per strip** — Fixed 110 Hz 2nd-order Butterworth toggle per strip. Off by default on DIRECT and MAIN; default on for OUTRIG and AMBIENT. No full per-path EQ.

5. **DIRECT tied to MAIN mic positions only** — The Outrig and Ambient full IRs already contain their own direct arrivals naturally. Adding separate per-path DIRECT paths is a future enhancement (and would require stripping the order-0 Ref from the full ER synthesis pass to avoid double-counting).

6. **Strip naming: DIRECT / MAIN / OUTRIG / AMBIENT** — Not "TREE". Future Decca Tree mode will update this label or add a mode selector.

7. **Decca Tree implementation: after this feature** — Multi-mic framework is the correct foundation for Decca Tree. Implement this first, Decca Tree second.
