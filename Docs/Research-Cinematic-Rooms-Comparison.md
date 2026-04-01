# Research: Cinematic Rooms (Liquid Sonics) vs Ping — Engine Comparison & Improvement Ideas

Summary of public information on **Cinematic Rooms** / **Cinematic Rooms Professional** (Liquid Sonics) and how it compares to **Ping**’s reverb pipeline. Focus: algorithmic/architectural differences that could inform improvements to Ping.

**Sources:** LiquidSonics blog posts (“Introducing Cinematic Rooms”, “Do You Really Need Another Reverb In 2020?”), product pages, and Ping’s codebase (IRSynthEngine, PluginProcessor, CLAUDE.md).

---

## 1. Cinematic Rooms — What’s Publicly Described

- **Algorithmic reverb** (no Fusion-IR / no large IR library); “novel algorithm” for high density and spectral purity without modulated convolution. Infinite tail; small install footprint.
- **Early reflections** and **reverb tail** are separate sections with **independent controls** (including level, diffusion, and “size” / spacing).
- **Crossfeed control:** Level, **pre-delay** (~10 ms), and **attenuation** (~6 dB) on cross-channel paths (L→R, R→L) are **user-controllable**, and can be set **separately for reflections vs tail**. So: tight ER (low crossfeed) + wide tail (high crossfeed), or both tight for pan-tracking.
- **Reflections:** Described as “achromatic” and “strictly decorrelated” so they can be placed **very close or on top of each other** without comb filtering; enables very small, tight spaces. Diffusion (specular vs diffuse) and **spacing** (how close/far reflections sit) are **independent**.
- **Constant density:** “Multichannel constant density” — density is maintained regardless of channel count; tails stay “lush” off-axis and fold down well because channels are “fully decorrelated”.
- **Modulation:** User can control or remove modulation (unlike e.g. Seventh Heaven).
- **Filtering:** Separate rolloff curves for reflections vs late reverb; “echo rolloff” on delay/slap; curves tuned for musical response.
- **Room character:** “Hundreds of real spaces” analysed and profiled; this data is “fed into the algorithm at key points” to shape acoustic profile (still algorithmic, not convolution).
- **Surround:** Designed for surround (up to 7.1.6); stereo benefits from the same engine (transparent, high density, decorrelated).

---

## 2. Ping — Current Engine (Relevant Parts)

- **IR-based:** Convolution with either **synthesised** (IR Synth) or **loaded** IRs. True-stereo = 4 channels (LL, RL, LR, RR); ER and tail are split at 80 ms (file load) / 85 ms (synth), each with its own convolvers.
- **IR Synth:** Image-source method (ISM) for early reflections (per-band, 8 bands, absorption, directivity); deferred allpass diffusion (dry 0–65 ms, blend 65–85 ms); 16-line FDN for tail (Hadamard, prime delays, LFO-modulated, per-line 1-pole LP from RT60); FDN seeded from summed ER (eL/eR) with **three-stage allpass pre-diffusion**; ER/tail crossfade at 85 ms with level-matched gain.
- **Stereo decorrelation:** Post-convolution, a **2-stage allpass on the R channel only** (7.13 ms, 14.27 ms, g=0.5) after EQ and before width to reduce stereo collapse and widen the tail.
- **User controls:** Dry/wet, predelay, width (0–2), erLevel/tailLevel, 3-band EQ, LFO depth/rate, tail modulation (chorus), stretch, decay (damping), reverse/trim. **No** separate crossfeed, ER “size”, or ER vs tail filter curves.

---

## 3. Significant Differences & Possible Improvements for Ping

### 3.1 Crossfeed control (early vs tail)

**Difference:** Cinematic Rooms exposes **crossfeed** with **pre-delay** and **attenuation** on L↔R paths, **separately for reflections and tail**. That allows e.g. tight early image (low crossfeed) and wide tail (high crossfeed), or full pan-tracking (both low).

**Ping today:** True-stereo is fixed: L→LL+LR, R→RL+RR; summing to Lout = LL+RL, Rout = LR+RR is built into the IR and the convolver layout. There is no user control over “how much” or “when” the opposite channel appears.

**Improvement ideas:**

- **Plugin-side crossfeed (post-convolution):** After convolution, add a small **crossfeed block**: take a delayed (e.g. 5–15 ms), attenuated (e.g. −3 to −9 dB), optionally filtered copy of L→R and R→L and mix into the wet buffer. Control could be one “crossfeed” amount and one “crossfeed pre-delay”, or separate **ER crossfeed** vs **tail crossfeed** by applying different crossfeed to ER and tail portions (would require either separate ER/tail buffers or a simple time gate). That would not change the IR Synth itself but would give Cinematic-style “reverb follows pan” vs “reverb spills wide” in the plugin.
- **IR Synth:** More advanced would be to synthesise or load **two** true-stereo “flavours” (tight vs wide crossfeed) and crossfade between them with a crossfeed parameter; much heavier and not necessary for a first step.

### 3.2 Achromatic / decorrelated early reflections (very small rooms)

**Difference:** Cinematic Rooms claims reflections are so decorrelated that they can be **placed very close or on top of each other** without comb filtering, enabling **very small, tight spaces** that still sound clean.

**Ping today:** Early reflections are ISM spikes plus per-band rendering; **deferred allpass** (dry 0–65 ms, then blend) keeps early transients sharp but means very-early reflections (< 65 ms) are **not** diffused. So when many reflections land in a narrow time window (e.g. small room), comb-like interaction is still possible. Frequency-dependent scatter (Feature C) and Lambert scatter (Feature A) add some spread but are conservative.

**Improvement ideas:**

- **Optional “achromatic” ER mode:** Allow diffusion (or a dedicated “ER decorrelation” stage) to start **earlier** (e.g. from 20–30 ms) with a **gentler** curve so that early energy is decorrelated sooner while still preserving some sharpness. Tune so that when “ER size” is small (reflections close together), comb artefacts are reduced.
- **Independent “ER spacing” vs “ER diffusion”:** Today Diffusion affects the deferred allpass and FDN tail diffusion. Adding a separate **ER spacing** (or “density”) control could scale or jitter the **arrival times** of early reflections (e.g. in `calcRefs` / `renderCh`) so that in small rooms you can spread reflections slightly in time without making them more diffuse. That would mimic “reflections close vs far” without changing diffusion.
- **Stronger decorrelation for coincident/small rooms:** When speaker separation is very small or room is very small, consider an extra **short** decorrelation (e.g. very short allpass or minimal spread) applied only to the first 20–40 ms of the ER buffer so that early comb regions are softened.

### 3.3 Constant density (channel count / fold-down)

**Difference:** Cinematic Rooms is designed so that **density is constant** regardless of channel count and that channels are **fully decorrelated**, which helps off-axis listening and fold-down.

**Ping today:** True-stereo uses two FDN seeds (L and R) with different phase (seed 100 vs 101) and the R-channel post-EQ allpass; density comes from the 16-line FDN and ISM. There is no explicit “constant density” or “per-channel density” design.

**Improvement ideas:**

- **Verify fold-down:** Listen to Ping in stereo with L+R mono sum and with various width/crossfeed (once added); ensure no obvious collapse or phasing. The R-only allpass is already chosen to preserve mono sum; any new crossfeed should be checked for mono compatibility.
- **Tail decorrelation:** If more channels are ever added (e.g. surround), consider separate decorrelation per output channel (e.g. different allpass times per channel) so that each channel gets dense, decorrelated tail without sharing one stereo pair’s character.

### 3.4 Separate ER vs tail filtering

**Difference:** Cinematic Rooms has **different rolloff curves** for reflections vs late reverb, and “echo rolloff” on delays, all tuned for musical response.

**Ping today:** One **3-band parametric EQ** on the whole wet path; **erLevel** and **tailLevel** only balance levels. No separate EQ or rolloff for “ER only” vs “tail only”.

**Improvement ideas:**

- **ER vs tail EQ:** Add optional **separate** EQ or high/low cut for “early” vs “tail” (e.g. ER high shelf down for darker early, tail low shelf down for less rumble). Implementation could be: duplicate the current 3-band EQ into two blocks and apply one to the ER portion and one to the tail portion using the same 80/85 ms split used for level metering, or use a simple time-based crossfade between two EQ curves.
- **Echo/slap rolloff:** If a “slap” or delay line is ever added in the plugin, a dedicated rolloff (e.g. lowpass) on that path would mirror the “echo rolloff” idea.

### 3.5 Modulation control

**Difference:** Cinematic Rooms gives **user control over modulation** (amount, possibly on/off); Seventh Heaven does not.

**Ping today:** **LFO depth** and **tail modulation** (chorus) are already user-controllable (including “off” at 0). So Ping is already in line with “user can reduce or remove modulation” and no change is strictly required.

### 3.6 Room profile / character data

**Difference:** Cinematic Rooms uses **analysed room data** (hundreds of spaces) “fed into the algorithm at key points” to shape the acoustic profile while staying algorithmic.

**Ping today:** Fully **physics-based** (geometry, materials, RT60, air absorption, directivity). No sampled room profiles.

**Improvement ideas (longer term):**

- **Optional “character” layer:** Could add a small set of **room-profile curves** (e.g. “smooth”, “lively”, “dark”) that scale or bias RT60 per band, or add a subtle spectral tilt to the ER or tail, without changing the core ISM/FDN. That would be a lightweight way to add “flavour” without moving to convolution of real rooms.
- **Preset-specific tweaks:** If presets are added, they could store not only geometry/materials but also small “character” offsets (e.g. ER brightness, tail curve) that map to such a layer.

### 3.7 Architecture: algorithmic vs IR

**Difference:** Cinematic Rooms is **fully algorithmic** (no IR convolution for the main reverb); density and tail are generated in real time. Ping is **IR-based** (convolution with synthesised or loaded IRs).

**Ping:** The choice of IR-based design is fundamental (flexibility to load any IR, use synth for physically based rooms). Improving **crossfeed**, **ER decorrelation/spacing**, and **ER vs tail filtering** in the **plugin** and in the **IR Synth** keeps that architecture while bringing behaviour closer to what Cinematic Rooms describes (control over imaging, small-room clarity, and spectral shape).

---

## 4. Summary Table

| Area                 | Cinematic Rooms (described)     | Ping (current)                          | Possible improvement for Ping                          |
|----------------------|----------------------------------|-----------------------------------------|--------------------------------------------------------|
| Crossfeed            | Separate ER/tail; pre-delay, level | Fixed true-stereo sum                   | Post-conv crossfeed (amount + pre-delay); ER vs tail   |
| ER “achromaticity”   | Reflections very close without comb | Deferred diffusion from 65 ms           | Earlier/gentler ER decorrelation; ER spacing control   |
| Constant density     | Per-channel, decorrelated        | FDN + 2 seeds + R allpass               | Verify mono/fold-down; per-channel if going surround   |
| ER vs tail filtering | Separate rolloffs                | Single 3-band EQ                        | Separate ER/tail EQ or rolloffs                        |
| Modulation           | User control                     | User control (LFO, tail chorus)         | Already aligned                                       |
| Room character       | Profile data in algorithm        | Physics only                            | Optional “character” curves / preset offsets           |
| Core engine          | Algorithmic                      | IR (convolution)                        | Keep; improve control and ER/tail behaviour above      |

---

## 5. Suggested Priorities

1. **High value, plugin-only:** **Crossfeed control** (amount + pre-delay, optionally separate for ER vs tail) — improves imaging and pan-tracking without touching IR Synth.
2. **High value, IR Synth:** **ER decorrelation / spacing** — earlier or gentler diffusion for small rooms, and/or a dedicated “ER spacing” parameter to reduce comb when reflections are very close.
3. **Medium value:** **Separate ER vs tail EQ** (or at least ER vs tail rolloffs) — more flexibility for tone without changing the reverb algorithm.
4. **Lower / later:** Room “character” profiles, constant-density checks for future surround, and any “echo rolloff” if a delay/slap is added.

This document is a research snapshot from public sources and code inspection; no reverse engineering of Cinematic Rooms was performed.
