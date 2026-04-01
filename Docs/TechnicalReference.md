# P!NG — Technical Reference

A comprehensive description of the P!NG impulse response reverb plugin: architecture, signal routing, calculations, and acoustic theory.

---

## 1. Overview

P!NG is an AU/VST3 stereo reverb plugin for macOS that:

1. **Loads impulse responses** from `~/Documents/P!NG/IRs` (WAV, AIFF)
2. **Convolves** the input with the IR to produce reverb
3. **Optionally synthesises** IRs from room geometry via the IR Synth engine
4. **Processes** the wet signal with EQ, modulation, and width control

The plugin supports both **stereo** (2-channel) and **true-stereo** (4-channel: L→L, R→L, L→R, R→R) IRs. True-stereo IRs preserve full spatial information for the early reflections.

---

## 2. Plugin Architecture

### 2.1 Main Components

| Component | Role |
|-----------|------|
| `PingProcessor` | Audio processing, parameter state, IR loading |
| `PingEditor` | UI, waveform display, IR list, IR Synth panel |
| `IRManager` | Scans `~/Documents/P!NG/IRs`, provides file list |
| `IRSynthEngine` | Synthesises IRs from room parameters (image-source + FDN) |
| `LicenceVerifier` | Ed25519 serial verification (libsodium) |

### 2.2 Parameters (APVTS)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Dry / Wet | 0–1 | 0.3 | Mix: √(1−mix) dry, √(mix) wet |
| Predelay (ms) | 0–500 | 0 | Delay before wet path |
| Damping | 0–1 | 1 | Decay envelope on IR (1 = flat, 0 = heavily damped) |
| Stretch | 0.5–2 | 1 | Time-scale IR (50%–200%) |
| Width | 0–2 | 1 | Stereo width (M/S: S × width) |
| LFO Depth | 0–1 | 0 | Wet gain modulation depth |
| LFO Rate | 0.01–2 | 0.5 | Modulation period (UI: left=slow, right=fast) |
| Tail Modulation | 0–1 | 0 | Chorus on wet signal |
| Delay Depth (ms) | 0.5–8 | 2 | Chorus LFO depth |
| Tail Rate (Hz) | 0.05–3 | 0.5 | Chorus LFO rate |
| IR Input Gain (dB) | -24–12 | 0 | Gain before convolution |
| IR Input Drive | 0–1 | 0 | Harmonic saturator mix |
| Early Reflections | -48–6 dB | 0 | ER convolution level |
| Tail | -48–6 dB | 0 | Tail convolution level |
| Reverse Trim | 0–0.95 | 0 | Trim start of reversed IR |
| Band 0/1/2 Freq, Gain, Q | — | 400/1k/4k Hz, 0 dB, 0.707 | 3-band parametric EQ |

---

## 3. Signal Flow (processBlock)

```
Input (stereo)
    │
    ├─────────────────────────────────────► dryBuffer (copy)
    │
    ▼
Predelay (0–500 ms)
    │
    ▼
Input Gain (dB)
    │
    ▼
Harmonic Saturator (cubic soft clip, drive mix)
    │
    ▼
┌───────────────────────────────────────────────────────────┐
│  Convolution (wet path)                                    │
│                                                             │
│  • True stereo (4ch IR):                                    │
│      L_in → [LL, RL] → L_out   R_in → [LR, RR] → R_out     │
│      ER: 4 mono convolvers (LL, RL, LR, RR)                │
│      Tail: stereo convolver (L+R combined from 4ch tail)   │
│                                                             │
│  • Stereo (2ch IR):                                         │
│      erConvolver (stereo) + tailConvolver (stereo)          │
│      Both process same input; output = ER×erLevel + Tail×tailLevel │
└───────────────────────────────────────────────────────────┘
    │
    ▼
3-band Parametric EQ (peak filters)
    │
    ▼
LFO Modulation (optional: wet gain × (1 + depth×sin(ωt)))
    │
    ▼
Width (M/S: S × width)
    │
    ▼
Tail Modulation / Chorus (optional: variable delay, LFO-modulated)
    │
    ▼
Dry/Wet Mix: out = wet×√mix + dry×√(1−mix)
    │
    ▼
Output (stereo)
```

---

## 4. IR Loading and Pre-Processing

When an IR is loaded (from file or synth), the following steps run in order.

### 4.1 Reverse (optional)

If **Reverse** is enabled:

1. Sample order is reversed per channel.
2. **Reverse Trim** (0–0.95): skip the first `trimFrac × length` samples of the reversed IR to remove initial silence or long tail. Minimum output length: 64 samples.

### 4.2 Stretch

Time-scale the IR to `stretchFactor × originalLength` (0.5–2.0). Linear interpolation between samples:

```
dst[i] = src[i0]*(1−f) + src[i1]*f
  where srcIdx = i × origLen / newLen, i0 = floor(srcIdx), f = frac(srcIdx)
```

### 4.3 Decay Envelope

Exponential fade-out to simulate damping:

```
env(t) = exp(−decayParam × 6 × t)   where t = i/N, decayParam = 1 − UI_decay
```

- UI Decay = 1 (right): decayParam = 0 → flat
- UI Decay = 0 (left): decayParam = 1 → heavily damped

### 4.4 Early / Tail Split

The IR is split at **80 ms** (crossover):

- **Early (ER):** 0 → 80 ms + 10 ms fade-out
- **Tail:** 80 ms → end, with 10 ms fade-in

Constants: `crossoverSamples = 0.080 × sr`, `fadeLength = 0.010 × sr`.

---

## 5. Convolution Modes

### 5.1 Stereo (2-channel IR)

- **erConvolver** and **tailConvolver** each get a stereo IR (L/R).
- Same stereo input is convolved with both.
- Output: `ER × erLevel + Tail × tailLevel` (per channel).

### 5.2 True Stereo (4-channel IR)

Channels: **iLL, iRL, iLR, iRR** (L→L, R→L, L→R, R→R).

- **Early reflections:** 4 mono convolvers. L_in convolved with LL and RL, summed → L_out. R_in convolved with LR and RR, summed → R_out.
- **Tail:** For IR Synth, the tail is diffuse (iLL≈iRL, iLR≈iRR). A combined stereo tail (L = LL+RL, R = LR+RR) is built and convolved with the stereo input. The result is scaled 0.5× and added to the ER output.
- **ER normalisation:** 4 ER channels are group-normalised by `1/max(peak, L1)` so both transients and sustained levels stay bounded.

---

## 6. EQ

Three cascaded **peak filters** (JUCE `makePeakFilter`):

- Band 0: default 400 Hz
- Band 1: default 1000 Hz  
- Band 2: default 4000 Hz

Each: frequency, gain (dB), Q. Applied in series to the wet signal.

---

## 7. Width

Mid/side processing on the wet signal:

```
M = (L + R) / 2
S = (L - R) / 2
S' = S × width
L' = M + S'
R' = M - S'
```

- Width = 0: mono (S = 0)
- Width = 1: original stereo
- Width = 2: widened

---

## 8. Dry/Wet Mix

Equal-power crossfade:

```
dryGain = √(1 − mix)
wetGain = √(mix)
output = dry × dryGain + wet × wetGain
```

---

## 9. IR Synth Engine — Theory

The IR Synth generates room impulse responses from geometric and material parameters. It combines:

1. **Image-source method** for early reflections
2. **FDN (Feedback Delay Network)** for the late reverberant tail
3. **Per-band processing** (6 bands: 125, 250, 500, 1k, 2k, 4k Hz)
4. **Allpass diffusion** (deferred: first 30 ms dry)

### 9.1 RT60 (Eyring)

Reverberation time at 6 bands from Eyring’s formula:

```
RT60 = 0.161 × V / (−S × ln(1 − α))
```

- V = volume
- S = total surface area (floor, ceiling, walls; vaults modify ceiling area)
- α = mean absorption = Σ(A_i × α_i) / S

Air absorption is added:

```
RT60_final = 1 / (1/RT60 + AIR[band] × c / 60)
```

with c = 343 m/s.

### 9.2 Surface Areas

- Floor: W × D
- Ceiling: W × D × (1 + (h_mult − 1) × 1.6) for vaults
- Walls: 2×(D×H + W×H)
- Optional: audience, balconies, organ case

### 9.3 Image-Source Method

For each integer triple (nx, ny, nz), an image source is placed at:

```
ix = nx×W + (nx odd ? W−sx : sx)
iy = ny×D + (ny odd ? D−sy : sy)
iz = nz×H + (nz odd ? H−sz : sz)
```

- Distance to receiver: `dist = √((ix−rx)² + (iy−ry)² + (iz−rz)²)`
- Arrival time: `t = dist / 343` (samples at sr)
- Amplitude per band: geometric spreading × reflection coefficients × air absorption × mic/speaker directivity

Reflection coefficients: `r = √(1 − α)` for each material. Applied per bounce (floor, ceiling, walls). Organ front and balconies add extra absorption.

### 9.4 Per-Band Rendering (renderCh)

1. Accumulate reflection amplitudes in 6 band buffers at sample `t`.
2. Apply **bandpass** at each centre frequency (Q from bandwidth).
3. Sum bands → broadband `raw`.
4. **Deferred allpass diffusion** (see below).

### 9.5 Deferred Allpass Diffusion

The allpass diffuser is **stateful** (feedback). To keep early reflections sharp:

1. Run the diffuser on a **copy** of the full buffer from sample 0 → `wet`.
2. Blend:
   - **0–30 ms:** pure dry (no diffusion)
   - **30–40 ms:** linear crossfade dry → wet
   - **40 ms+:** fully wet

Allpass stages (4): g = 0.5 + diffusion×0.25; delays ≈ 17.1, 6.3, 2.3, 0.8 ms.

### 9.6 FDN Tail

16-line FDN with:

- **Delays:** Prime lengths from mean-free-path (20–150 ms range), LFO-modulated (±1.2 ms).
- **Feedback matrix:** Hadamard 16×16 (orthogonal).
- **Per-line filters:** 1-pole lowpass to match RT60 slope (LF vs HF).
- **Injection:** Early IR (0–85 ms) faded in over 40 ms, fed into FDN. Output from 85 ms onward.

Left and right tails use different seeds for decorrelation.

### 9.7 Output Format

IR Synth produces 4 channels (iLL, iRL, iLR, iRR) at 48 kHz. ER and tail are crossfaded at 85 ms (ec = 0.085×sr). Final IR is highpassed at 20 Hz and lowpassed at 18 kHz.

---

## 10. Harmonic Saturator

Cubic soft clip before convolution:

```
x_scaled = x × (1 + 3×drive)
x_clipped = clamp(x_scaled, −√3, √3)
saturated = x_clipped − x_clipped³/3
out = dry×(1−mix) + saturated×mix
out /= (1 + drive×0.5)   // compensation
```

Inflection at ±√3 preserves derivative continuity.

---

## 11. Licence Verification

- **Algorithm:** Ed25519 (libsodium)
- **Flow:** User enters name + serial. Serial is Base32-encoded signed message: `normalisedName|tier|expiry`.
- **Verification:** `crypto_sign_open` with embedded public key. Name is normalised (lowercase, collapsed spaces) and must match payload.
- **Storage:** `~/Library/Audio/Ping/P!NG/licence.xml` or `/Library/Application Support/Audio/Ping/P!NG/`

---

## 12. File Locations

| Purpose | Path |
|---------|------|
| IR folder | `~/Documents/P!NG/IRs` |
| Licence (user) | `~/Library/Audio/Ping/P!NG/licence.xml` |
| Licence (system) | `/Library/Application Support/Audio/Ping/P!NG/licence.xml` |
| IR Synth params sidecar | `*.wav.ping` (same name as IR, in IR folder) |

---

## 13. Version

P!NG v1.1.0. Minimum macOS: 13.0 (Ventura).
