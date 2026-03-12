# P!NG — CLAUDE.md

Developer context for AI-assisted work on this codebase.

---

## What this project is

**P!NG** (`PRODUCT_NAME "P!NG"`) is a stereo reverb plugin for macOS (AU + VST3) built with JUCE. It convolves audio with impulse responses (IRs) and also includes a from-scratch IR synthesiser that simulates room acoustics using the image-source method + a 16-line FDN.

**Current version:** 1.2.0 (see `CMakeLists.txt`)
**Minimum macOS:** 13.0 Ventura
**Formats:** AU (primary, for Logic Pro) + VST3

---

## Build

Prerequisites: Xcode, CMake, libsodium (`brew install libsodium`).
JUCE is fetched automatically by CMake on first configure.

```bash
cd "/Users/paulthomson/Cursor wip/Ping"
mkdir -p build && cd build
cmake -G Xcode ..
cmake --build . --config Release
```

Built artefacts land in `build/Ping_artefacts/Release/`.

**Install to system library (intentional — plugins install system-wide for all users):**
```bash
cp -R build/Ping_artefacts/Release/Audio\ Unit/Ping.component /Library/Audio/Plug-Ins/Components/
cp -R build/Ping_artefacts/Release/VST3/Ping.vst3 /Library/Audio/Plug-Ins/VST3/
```

> Note: CMake sets `JUCE_AU_COPY_DIR` and `JUCE_VST3_COPY_DIR` to `/Library/...` (system-wide), not `~/Library/...`. This is deliberate — the .pkg installer requires admin and deploys for all users on the machine. Don't change this to `~/Library`.

**Build installer .pkg:**
```bash
cmake --build build --target installer
```
(Requires the `Installer/build_installer.sh` script and that `Ping_AU` + `Ping_VST3` are already built.)

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
  EQGraphComponent.h/.cpp  — Draws the EQ frequency response curve
  WaveformComponent.h/.cpp — IR waveform thumbnail display
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
| `reversetrim` | Reverse Trim | 0–0.95 | 0 |
| `b0freq/b0gain/b0q` | Band 0 EQ | 20–20k Hz / ±12 dB / 0.3–10 | 400 Hz, 0 dB, 0.707 |
| `b1freq/b1gain/b1q` | Band 1 EQ | — | 1000 Hz, 0 dB, 0.707 |
| `b2freq/b2gain/b2q` | Band 2 EQ | — | 4000 Hz, 0 dB, 0.707 |

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
Convolution  ── see "Convolution modes" below
  │
  ▼
3-band Parametric EQ (JUCE makePeakFilter, peak IIR)
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
Output Gain (dB, smoothed)
  │
  ▼
[Spectrum analysis push — lock-free FIFO for GUI EQ display]
  │
  ▼
Dry/Wet blend: out = wet × √(mix) + dry × √(1−mix)   [constant-power crossfade]
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

Floor, ceiling, room shape, room dimensions, speaker/mic placement, and all other parameters are unchanged from their previous defaults.

Pipeline:
1. **calcRT60** — Eyring formula at 6 octave bands (125–4k Hz) + air absorption correction.
2. **calcRefs** × 4 — Image-source method for each of the 4 cross-channel paths (seeds 42–45). Accepts `minJitterMs` and `highOrderJitterMs` parameters for order-dependent time jitter (see close-speaker handling below). Speaker directivity fades to omnidirectional with bounce order: order 0–1 use full cardioid, order 2 blends 50/50 cardioid+omni, order 3+ are fully omnidirectional. Mic polar pattern applied per-reflection. Reflection order `mo` is sized by the **original RT60-based formula**: `mo = min(60, floor(RT60_MF × 343 / minDim / 2))`. No distance gate applied in full-reverb mode — sources decay naturally through cumulative surface absorption. **ER-only mode** (`eo = true`): sources with `t >= ec` (arrival time ≥ 85 ms after all jitter) are skipped via `continue`; this is the primary gate that keeps late periodic reflections out of ER-only output. **Feature A — Lambert diffuse scatter** (enabled via `#if 1` in `calcRefs`): for each specular Ref of order 1–3 when `ts > 0.05`, `N_SCATTER = 2` secondary Refs are spawned with random azimuth and 0–4 ms extra delay; amplitude = specular × `(ts * 0.08) / N_SCATTER * 0.6^(order-1)` (~3% at order 1 at default ts). Fills in comb-like gaps between specular spikes. Scatter arrivals are also gated (`scatterT >= ec` in eo mode) since the +0–4 ms offset can push a near-boundary parent over the ec edge.
3. **renderCh** × 4 — Accumulate into per-band buffers, bandpass-filter each band, sum. **Temporal smoothing** was tried (5 ms moving average from 60 ms onward) but disabled: replacing each sample with the window mean attenuated content after 60 ms by ~200× and caused “extremely quiet reverb”; the block is commented out in `renderCh`. Then apply deferred allpass diffusion (pure-dry 0–65 ms, crossfade 65–85 ms, fully diffused 85 ms+). Accepts `reflectionSpreadMs` (always 0 — disabled) and **Feature C — frequency-dependent scatter**: when `freqScatterMs > 0` (= `ts × 0.5`), bands 1–5 get a deterministic hash-based time offset (0 at 125 Hz, max ±freqScatterMs at 4 kHz). Simulates surface micro-structure without changing the Ref struct.
4. **renderFDNTail** × 2 (L seed 100, R seed 101) — 16-line FDN with Hadamard mixing, prime delay times derived from mean free path, LFO-modulated (±1.2 ms per line), 1-pole LP per line for frequency-dependent decay from RT60. FDN seeding starts at `t_first` (min direct-path arrival sample), so the full 85 ms warmup is spent on real signal. FDN output starts at `ecFdn = t_first + ec`. Seeding continues through `fdnMaxRefCut = (ecFdn + fdnMaxMs) × 1.1` before free-running. Before seeding, `eL` and `eR` are run through a three-stage allpass pre-diffusion cascade (short → long → short) to spread the seed spike over 50+ ms and prevent any single FDN delay line from producing a coherent repeating echo.
5. **Blend** — Two separate paths depending on `eo`:
   - **Full-reverb** (`eo = false`): FDN fade-in `tf` (linear 0→1) and ER fade-out `ef` (cosine 1→`erFloor`) run simultaneously over the same 20 ms window `[ecFdn−xfade, ecFdn]`. After `ecFdn`, `ef` holds at `erFloor = 0.05` — the already-diffuse late ISM continues at 5% amplitude, decaying naturally at the room RT60 without any extra envelope. A dynamic per-channel gain (`fdnGainL`/`fdnGainR`) is computed just before the blend loop: ER reference is the windowed-RMS of `eLL+eRL` / `eLR+eRR` over `[ecFdn−xfade, ecFdn]` (the last 20 ms before the crossfade, reliably in the allpass-diffused zone); FDN reference is the windowed-RMS of `tL`/`tR` over `[ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade]` (after the longest delay line has completed its first full recirculation cycle and the FDN is near quasi-steady-state), multiplied by the `0.5` blend factor. Gain is clamped to `[1.0, kMaxGain=16]` — the FDN is only ever boosted, never attenuated. Formula: `out = ER * bakedErGain * ef + FDN * 0.5 * bakedTailGain * fdnGain * tf`.
   - **ER-only** (`eo = true`): No FDN. A 10 ms cosine taper (`erFade`) runs over `[ec−10ms, ec]`, fading to zero at `ec` to avoid a hard silence edge. Formula: `out = ER * bakedErGain * erFade`.
6. **Feature B — Modal bank** — After the blend, all 4 channels are processed by `applyModalBank`: a parallel bank of high-Q IIR resonators (via `bpFQ`) tuned to axial modes f_n = c·n/(2L) for n=1..4 (10–250 Hz). Q from 125 Hz RT60; gain 0.18, normalised by mode count. Adds standing-wave ringing the ISM under-represents. For the default large room (e.g. 28×16×12 m) all axial modes fall below ~57 Hz (below the 125 Hz synthesis band), so the feature is effectively a no-op there; it becomes audible in smaller rooms or if the mode range were extended.
7. **Post-process** — LP@18kHz, HP@20Hz, 500ms cosine end fade. **No peak normalisation** — amplitude scales naturally with the 1/r law (proximity effect preserved).

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
- **`/Library` install path** — Intentional. The .pkg installer deploys system-wide. Don't change copy dirs to `~/Library`.
- **Speaker directivity in image-source** — `spkG()` is applied using the real source→receiver angle, not the image-source position. This is deliberate: image-source positions aren't physical emitters and using them would give wrong distance-dependent directivity results.
- **Deferred allpass diffusion** — The allpass starts at 65 ms (not sample 0) to prevent early-reflection spikes from creating audible 17ms-interval echoes in the tail.
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
- **ER/FDN crossfade uses cosine×linear envelopes with an erFloor residual** — The 20 ms window `[ecFdn−xfade, ecFdn]` has a linear `tf` FDN fade-in and a cosine `ef` ER fade-out (from 1.0 to `erFloor = 0.05`, not to 0). After `ecFdn`, `ef` holds at `erFloor = 0.05`; the fully-diffuse late-ISM contributes a natural 5% undertone that decays at the room RT60. A dynamic `fdnGainL/R` calibrates the FDN level. **Critical window choices:** ER reference = `[ecFdn-xfade, ecFdn]` (last 20 ms before crossfade, in the allpass diffusion zone — do NOT shift earlier into the sparse ISM zone or RMS collapses to near-zero). FDN measurement = `[ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade]` after the FDN settles (do NOT measure at onset `[ecFdn, ecFdn+xfade]` — the burst from 99 ms of accumulated warmup energy exceeds the ER snapshot and produces `fdnGain < 1`, silencing the tail). The `0.5` blend factor must be included in the fdnRms denominator. `fdnGain` is clamped to `[1.0, 16.0]` — only ever boost, never attenuate. The ER-only path uses a separate 10 ms cosine taper `erFade` at `ec`; the `erFloor`/`fdnGain` block is inside `if (!eo)` and does not affect ER-only mode.
- **Feature A (Lambert scatter) is conservative** — `scatterWeight = (ts * 0.08) / N_SCATTER * 0.6^(order-1)`; at default ts≈0.74, order 1 scatter is ~3% of specular, decaying 0.6× per order. Keeps scattered energy from dominating. Toggle with `#if 1` / `#if 0` in `calcRefs`.
- **Temporal smoothing is disabled** — A 5 ms moving average from 60 ms onward was tried to soften periodic peaks; it replaced each sample with the window mean and cut level after 60 ms by ~200× (“extremely quiet reverb”). The block is commented out in `renderCh`. Any future smoothing should preserve peak level (e.g. very short window or envelope-based).
- **Feature C (frequency-dependent scatter) uses a deterministic hash** — `renderCh` has no RNG. Hash of `(r.t + 1) * 2654435769 XOR b * 1234567891` gives per-band, per-reflection offsets; `freqScatterMs = ts * 0.5` (≈0.37 ms at default), so ±~18 samples at 4 kHz.
- **Feature B (modal bank) gain is fixed** — `modalGain = 0.18`; normalisation by mode count keeps level consistent. For large default rooms, axial modes sit below the 125 Hz band so the effect is inaudible there.
