# P!NG — 3D Microphone Polar Patterns & Tilt Implementation Brief

**Target version:** 2.8.0  
**Codebase:** JUCE AU+VST3 plugin, macOS. See `CLAUDE.md` for full project context.  
**Motivation:** Make the image-source ray tracing self-consistent by applying mic directivity in the same 3D space as the geometry that already exists.

---

## What This Feature Is

The IR synthesis engine already computes full 3D geometry — every image source has an (x, y, z) position, distances are Euclidean in 3D, and floor/ceiling bounce counts drive correct per-band absorption. However, microphone directivity is currently applied using only the **horizontal azimuth** of each image source, discarding the vertical elevation component entirely.

This creates a genuine physical inconsistency: a floor reflection arriving steeply from below at a 3 m mic gets exactly the same directional weighting as a horizontally-arriving reflection at the same azimuth. For a cardioid facing horizontally, those two arrivals should be treated very differently.

This feature corrects that inconsistency and adds a **mic tilt parameter** (elevation angle of the mic's facing direction) as its natural consequence:

- `micG()` is extended to accept full 3D source direction (azimuth + elevation) and a 3D mic orientation (face azimuth + face elevation/tilt)
- The formula uses the **spherical law of cosines** to compute the correct 3D angle between source direction and mic axis
- A `*_tilt` parameter is added per mic pair to `IRSynthParams`, defaulting to **−30° (−π/6 radians)** — tilted downward, representing the standard real-world Decca tree / outrigger hanging position
- Even at 0° tilt, the output changes slightly because floor and ceiling reflections are now correctly attenuated by the vertical component of the polar pattern — this is more physically accurate, not a regression

**The sound will change from current defaults.** This is expected and correct. Factory IRs must be regenerated after implementation. IR_11 and IR_14 golden values must be recaptured.

---

## Mathematical Foundation

### Current 2D model

`micG()` currently computes:

```cpp
double az = std::atan2(iy - ry, ix - rx);  // horizontal azimuth only
gain = max(0, o + d * cos(az - faceAngle));
```

The term `cos(az − faceAngle)` is the cosine of the horizontal angle between the source and the mic's facing direction. Elevation is completely absent — `iz`, `rz`, and any vertical component of arrival direction are ignored by the directivity calculation even though they are fully used for distance and absorption.

### 3D model — spherical law of cosines

The correct 3D generalisation uses the dot product of two unit vectors: the mic's **facing direction** and the **direction to the image source**. Both are expressed as (azimuth, elevation) pairs.

**Mic facing vector** (unit):
```
f = ( cos(faceElevation)·cos(faceAzimuth),
      cos(faceElevation)·sin(faceAzimuth),
      sin(faceElevation) )
```

**Direction from mic to image source** (unit):
```
s = ( cos(el)·cos(az),
      cos(el)·sin(az),
      sin(el) )

where:
  az = atan2(iy − ry, ix − rx)           — horizontal azimuth (existing)
  el = atan2(iz − rz, sqrt((ix−rx)²+(iy−ry)²))  — vertical elevation (NEW)
```

**Dot product f · s** (the cosine of the 3D angle between mic axis and source direction):
```
cos(θ₃ᴅ) = f·s
          = cos(faceElevation)·cos(faceAzimuth)·cos(el)·cos(az)
          + cos(faceElevation)·sin(faceAzimuth)·cos(el)·sin(az)
          + sin(faceElevation)·sin(el)
```

Factoring using the cosine angle-sum identity `cos(A)cos(B) + sin(A)sin(B) = cos(A−B)`:

```
cos(θ₃ᴅ) = cos(faceElevation)·cos(el)·cos(az − faceAzimuth)
          + sin(faceElevation)·sin(el)
```

This is the **spherical law of cosines for sides**, applied to the triangle on the unit sphere defined by the two direction vectors. It is the exact, closed-form formula with no approximation.

**The new polar pattern gain:**
```cpp
const double cosTheta = std::sin(el) * std::sin(faceElevation)
                      + std::cos(el) * std::cos(faceElevation) * std::cos(az - faceAzimuth);
return std::max(0.0, o + d * cosTheta);
```

### Verification of special cases

| Scenario | Expected | Formula result |
|---|---|---|
| Source exactly on-axis (az=faceAz, el=faceEl) | gain = 1 (for o+d=1) | cos(θ)=sin²(el)+cos²(el)=1 ✓ |
| Source directly behind (az=faceAz+π, el=faceEl) | gain = o−d (cardioid→0) | cos(θ)=−1 ✓ |
| Source 90° off-axis any direction | gain = o | cos(θ)=0 ✓ |
| faceElevation=0, el=0 (both horizontal) | reduces to current formula | cos(θ)=cos(az−faceAz) ✓ |
| Source directly above/below (el=±90°), faceElevation=0 | cos(θ)=0 for horizontal facing | sin(±90°)·0 + cos(±90°)·...=0 ✓ |

The last case confirms that a horizontally-facing cardioid or figure-8 is correctly insensitive to sounds arriving from directly above or below — a physically accurate result that the 2D model could not represent.

### Behaviour at faceElevation = 0° (horizontal, no tilt)

At the default horizontal orientation, the formula simplifies to:
```
cos(θ₃ᴅ) = cos(el) · cos(az − faceAzimuth)
```

This differs from the current formula by the factor `cos(el)`. The effect:
- Horizontally-arriving reflections (el ≈ 0): `cos(0)=1` — identical to current
- Floor/ceiling bounces (|el| large): attenuated by `cos(el)` — more physically correct
- At the transition height (3m mic, 1m source, 5m horizontal distance): el ≈ −22°, `cos(−22°) ≈ 0.93` — subtle −0.6 dB reduction

Even without any user-set tilt, enabling this corrects a systematic over-weighting of steeply-arriving floor and ceiling reflections. The change is subtle for the more omnidirectional patterns and pronounced for the highly directional ones.

---

## Pattern-by-Pattern Analysis

### Elevation convention
- **el = 0**: horizontal (source at same height as mic)
- **el < 0**: source below the mic (typical for floor reflections, instruments)
- **el > 0**: source above the mic (ceiling reflections)
- **faceElevation < 0**: mic tilted downward (default −30° = −π/6 for real-world hanging mics)
- **faceElevation > 0**: mic tilted upward

### 1. `omni` — {o=1.00, d=0.00} all bands

The directional term `d·cosTheta` is always zero. The formula returns `1.0` regardless of source direction or mic tilt.

**3D impact: none.** No code change is needed for this pattern's output. The tilt parameter exists in the data but has zero effect.

### 2. `omni (MK2H)` — {o≥1.00, d=0.00} all bands

Same as omni: `d=0` everywhere, so the 3D angle has no effect on directional weighting. The on-axis HF boost (`o=1.35` at 8 kHz, `o=1.55` at 16 kHz) applies equally in all directions.

**3D impact: none.** Tilt parameter present but acoustically inert.

### 3. `subcardioid` — o: 0.85→0.50, d: 0.15→0.50 (LF→HF)

Gentle directivity. The factor `cos(el)` at `faceElevation=0` creates a mild additional attenuation for steeply-arriving reflections. With −30° tilt, floor reflections near the mic's forward axis are brought more on-axis, which slightly increases their contribution — correct behaviour for a downward-tilted mic aiming toward the ensemble.

**3D impact: subtle.** The pattern is broad enough that the elevation correction is a minor refinement. Correct and noticeable but not dramatic.

### 4. `wide cardioid (MK21)` — o: 0.77→0.62, d: 0.23→0.38

More noticeable than subcardioid. At HF (d=0.38), a floor reflection arriving from 60° below horizontal at a horizontally-facing mic currently gets no elevation penalty. In 3D: `cos(−60°)=0.5`, so it is attenuated by roughly −6 dB via the elevation term. With −30° tilt, the mic's sensitivity axis drops toward the typical source elevation, bringing the direct path and first-order floor reflections closer to on-axis — perceptibly warmer, more present early reflections.

**3D impact: moderate.** The MK21's excellent off-axis consistency in the horizontal plane is now extended to the vertical plane, matching its real-world behaviour.

### 5. `cardioid (LDC)` — o: 0.78→0.06, d: 0.22→0.94 (LF→HF)

Most significantly affected at high frequencies where `d` approaches 0.94. A floor reflection arriving at el=−45° from a horizontally-facing LDC:
- Current: `gain = 0.06 + 0.94·cos(Δaz)` — no elevation penalty at all
- 3D at faceEl=0: `gain = 0.06 + 0.94·cos(−45°)·cos(Δaz) = 0.06 + 0.66·cos(Δaz)`
- 3D at faceEl=−30°: direct path from 1m instrument to 3m mic arrives at el≈−22°; the facing axis is now tilted −30°, so the instrument sits near the sensitivity axis — correctly boosted relative to the overhead ceiling reflections which are now further off-axis

The large-diaphragm cardioid's well-known proximity and "presence" character in real recordings is partly explained by the fact that real LDC mics at height do tilt down. The 3D model captures this correctly for the first time.

**3D impact: large, especially at HF.** This is the pattern most engineers use for MAIN mics. The improvement in early-reflection accuracy is perceptible.

### 6. `cardioid (SDC)` — o: 0.65→0.18, d: 0.35→0.82

Similar to LDC but with more consistent directivity across frequency. The `d` term reaches 0.82 at 16 kHz. Effects and reasoning are the same as LDC, scaled proportionally.

**3D impact: large at HF, more frequency-consistent than LDC.**

### 7. `figure8` — o≈0, d≈1 from 250 Hz upward

The figure-8 is the most mathematically elegant case in 3D. Its polar pattern is a perfect dipole: maximum sensitivity along ±facing axis, a complete null on the great circle perpendicular to the axis.

In 2D, this is approximated as a null at az ± 90° only in the horizontal plane. In 3D:

- A horizontal figure-8 (faceElevation=0) has its null as the **entire vertical plane** perpendicular to the facing axis. This means sounds arriving from directly above or below (el=±90°) are correctly at null — something the 2D model cannot represent.
- With −30° tilt, the null plane rotates accordingly. The instrument (below the mic) now sits partially on the sensitive side of the axis, while the ceiling (above) shifts further toward the null.

For the rare use case of a figure-8 as a main room mic, the 3D model makes the Blumlein stereo imaging geometry physically consistent with the 3D room.

**3D impact: substantial, especially for vertical rejection.** The 2D model was particularly wrong for figure-8 because it only had nulls at two azimuths; the 3D model gives the correct null plane.

### 8. `M50-like` — o: 1.00→0.70, d: 0.00→0.30 (LF→HF)

Used exclusively by the Decca Tree capture mode. Behaves as omni at LF (d=0) and narrows toward a wide cardioid at HF (d=0.30). The 3D correction applies only at HF where the directional term is non-zero, and the effect is mild given d stays ≤0.30.

**3D impact: mild at HF only.** Appropriate for a spherical pressure transducer whose real-world directivity at HF is due to sphere diffraction — the 3D spherical law of cosines is actually the more physically correct model for sphere-mounted mics than the 2D approximation.

---

## Coordinate System and Conventions

### Room coordinate system (unchanged)
- **x**: room width axis (0 = left wall, `p.width` = right wall)
- **y**: room depth axis (0 = front/source end, `p.depth` = back wall); increases "into the screen" on the floor plan (screen-space y)
- **z**: height axis (0 = floor, `He` = effective ceiling); up = positive

### Azimuth convention (unchanged)
`atan2(dy, dx)` in screen-space room coords: `0 = right, π/2 = "down" on floor plan, −π/2 = "up" on floor plan`.

### Elevation convention (NEW)
`el = atan2(iz − rz, horizontal_dist)` where `horizontal_dist = sqrt((ix−rx)² + (iy−ry)²)`:
- `el = 0`: source at same height as mic (horizontal arrival)
- `el > 0`: source above the mic (positive elevation — ceiling reflections)
- `el < 0`: source below the mic (negative elevation — floor reflections, typical instruments)

### Tilt convention (NEW)
`faceElevation` = the vertical angle of the mic's sensitivity axis from horizontal:
- `faceElevation = 0`: mic faces horizontally (current implicit assumption)
- `faceElevation < 0`: mic tilted downward (standard hanging mic position)
- `faceElevation > 0`: mic tilted upward
- Range: −π/2 to +π/2 (−90° to +90°)
- **Default: −π/6 (−30°)** for all MAIN, OUTRIG, and AMBIENT mic pairs
- Displayed in UI as degrees (integer or 1 decimal place), negative = down

### Why −30° default
A mic mounted on a bar or stand at 3 m height above a stage at 1 m is typically tilted 20–35° downward to aim at the ensemble rather than the ceiling. 30° is the standard Decca tree hanging angle and a reasonable middle value for a range of rooms.

---

## Complete Change Specification

### File 1: `Source/IRSynthEngine.h` — IRSynthParams additions

Add tilt parameters to `IRSynthParams` for MAIN, OUTRIG, and AMBIENT mic pairs. Place each group's tilt fields immediately after the corresponding angle fields.

**After `micl_angle` / `micr_angle`:**
```cpp
double micl_tilt = -0.5235987755982988;  // -π/6 = -30° downward
double micr_tilt = -0.5235987755982988;
```

**After `outrig_langle` / `outrig_rangle`:**
```cpp
double outrig_ltilt = -0.5235987755982988;  // -π/6 = -30° downward
double outrig_rtilt = -0.5235987755982988;
```

**After `ambient_langle` / `ambient_rangle`:**
```cpp
double ambient_ltilt = -0.5235987755982988;  // -π/6 = -30° downward
double ambient_rtilt = -0.5235987755982988;
```

**After `decca_angle` / `decca_toe_out` (Decca Tree):**
```cpp
double decca_tilt = -0.5235987755982988;  // -π/6 = -30°; applies to all three Decca mics equally
```

**Total new fields: 7 doubles in IRSynthParams.**

No changes to `IRSynthResult`, no changes to `MicIRChannels`.

---

### File 2: `Source/IRSynthEngine.cpp` — `micG()` function

**Current signature and body (lines 163–175):**
```cpp
double IRSynthEngine::micG (int band, double az, const std::string& pat, double faceAngle)
{
    const auto& mic = getMIC();
    auto it = mic.find(pat);
    if (it == mic.end()) return 1.0;
    const double o = it->second[band].first;
    const double d = it->second[band].second;
    return std::max(0.0, o + d * std::cos(az - faceAngle));
}
```

**New signature and body:**
```cpp
double IRSynthEngine::micG (int band, double az, double el,
                             const std::string& pat,
                             double faceAzimuth, double faceElevation)
{
    const auto& mic = getMIC();
    auto it = mic.find(pat);
    if (it == mic.end()) return 1.0;
    const double o = it->second[band].first;
    const double d = it->second[band].second;
    // 3D angle between source direction and mic facing axis via spherical law of cosines:
    //   cos(θ) = sin(el)·sin(faceElevation) + cos(el)·cos(faceElevation)·cos(az − faceAzimuth)
    // At faceElevation=0 this reduces to cos(el)·cos(az−faceAzimuth) — the 2D formula
    // multiplied by cos(el), which correctly attenuates steeply-arriving reflections.
    const double cosTheta = std::sin(el)  * std::sin(faceElevation)
                          + std::cos(el)  * std::cos(faceElevation)
                                          * std::cos(az - faceAzimuth);
    return std::max(0.0, o + d * cosTheta);
}
```

Also update the forward declaration in `IRSynthEngine.h`:
```cpp
// Old:
static double micG (int band, double az, const std::string& pat, double faceAngle);
// New:
static double micG (int band, double az, double el,
                    const std::string& pat,
                    double faceAzimuth, double faceElevation);
```

---

### File 3: `Source/IRSynthEngine.cpp` — `calcRefs()` function

#### 3a. Signature — add `micFaceTilt` parameter

**Current signature (line 238, abbreviated):**
```cpp
std::vector<IRSynthEngine::Ref> IRSynthEngine::calcRefs (
    double rx, double ry, double rz,
    double sx, double sy, double sz,
    ...
    const std::string& micPat,
    double spkFaceAngle, double micFaceAngle,
    double maxRefDist,
    double minJitterMs,
    double highOrderJitterMs)
```

**New signature — add `micFaceTilt` after `micFaceAngle`:**
```cpp
std::vector<IRSynthEngine::Ref> IRSynthEngine::calcRefs (
    double rx, double ry, double rz,
    double sx, double sy, double sz,
    ...
    const std::string& micPat,
    double spkFaceAngle, double micFaceAngle, double micFaceTilt,
    double maxRefDist,
    double minJitterMs,
    double highOrderJitterMs)
```

#### 3b. Inside `calcRefs()` — compute elevation and update `micG()` call

**Locate the existing azimuth line (currently around line 287):**
```cpp
double az = std::atan2(iy - ry, ix - rx);
```

**Add elevation computation immediately after it:**
```cpp
double az = std::atan2(iy - ry, ix - rx);
// Elevation of image source as seen from receiver — NEW for 3D polar patterns.
// Positive = source above mic (ceiling bounces), negative = source below (floor bounces, instruments).
const double hDist = std::sqrt((ix - rx) * (ix - rx) + (iy - ry) * (iy - ry));
const double el    = std::atan2(iz - rz, std::max(hDist, 1e-9));
```

The `std::max(hDist, 1e-9)` guard prevents division-by-zero when the image source is directly above or below the receiver (hDist ≈ 0), which can occur for high-order floor/ceiling bounces near the centre of the room.

**Update the `micG()` call inside the per-band loop (currently `amps[b]` computation):**

Find the existing call:
```cpp
amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * micG(b, az, micPat, micFaceAngle) * sg * polarity;
```

Replace with:
```cpp
amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * micG(b, az, el, micPat, micFaceAngle, micFaceTilt) * sg * polarity;
```

That is the only call to `micG()` in `calcRefs()`. The Lambert scatter secondary refs (Feature A) inherit the `amps[b]` values that already have `micG` applied, so they require no change.

#### 3c. Update `calcRefs()` forward declaration in `IRSynthEngine.h`

Add `double micFaceTilt` to the declaration immediately after `double micFaceAngle`.

---

### File 4: `Source/IRSynthEngine.cpp` — `synthMainPath()`

All four `calcRefs()` calls must pass the appropriate tilt. 

**Locate the face angle assignments (around line 1081):**
```cpp
double faceL = p.micl_angle;
double faceR = p.micr_angle;
```

**Add tilt assignments immediately below:**
```cpp
double tiltL = p.micl_tilt;
double tiltR = p.micr_tilt;
```

**For Decca Tree mode** — find the block that overrides `faceL`/`faceR`/`faceC` when `p.main_decca_enabled` is true. Add a corresponding tilt override in the same block:
```cpp
double tiltC = p.decca_tilt;  // centre mic
tiltL        = p.decca_tilt;  // L mic shares same tilt in rigid tree
tiltR        = p.decca_tilt;  // R mic
```

**Update the four `calcRefs()` calls for the standard (non-Decca) path:**

```cpp
// Old (abbreviated):
std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, ..., mainMicPattern, p.spkl_angle, faceL, maxRefDist, ...);
std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, ..., mainMicPattern, p.spkr_angle, faceL, maxRefDist, ...);
std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, ..., mainMicPattern, p.spkl_angle, faceR, maxRefDist, ...);
std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, ..., mainMicPattern, p.spkr_angle, faceR, maxRefDist, ...);

// New — insert tiltL/tiltR after each faceL/faceR:
std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, ..., mainMicPattern, p.spkl_angle, faceL, tiltL, maxRefDist, ...);
std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, ..., mainMicPattern, p.spkr_angle, faceL, tiltL, maxRefDist, ...);
std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, ..., mainMicPattern, p.spkl_angle, faceR, tiltR, maxRefDist, ...);
std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, ..., mainMicPattern, p.spkr_angle, faceR, tiltR, maxRefDist, ...);
```

**For Decca Tree mode** — the centre-mic `calcRefs()` calls (seeds 46/47) also need `tiltC` added in the same position.

---

### File 5: `Source/IRSynthEngine.cpp` — `synthExtraPath()`

`synthExtraPath()` is used for OUTRIG and AMBIENT paths. It takes explicit `rx1, ry1, rx2, ry2` positions, a `rheight`, and a `pattern` string. Add tilt parameters.

**Current `synthExtraPath()` signature (abbreviated):**
```cpp
MicIRChannels IRSynthEngine::synthExtraPath (
    const IRSynthParams& p,
    double sx1, double sy1, double sx2, double sy2,
    double rx1, double ry1, double rx2, double ry2,
    double rheight, double sheight,
    const std::string& pattern,
    double angL, double angR,
    int seedBase,
    IRSynthProgressFn onProgress)
```

**Add `tiltL` and `tiltR` after `angL` and `angR`:**
```cpp
MicIRChannels IRSynthEngine::synthExtraPath (
    const IRSynthParams& p,
    double sx1, double sy1, double sx2, double sy2,
    double rx1, double ry1, double rx2, double ry2,
    double rheight, double sheight,
    const std::string& pattern,
    double angL, double angR,
    double tiltL, double tiltR,
    int seedBase,
    IRSynthProgressFn onProgress)
```

Inside `synthExtraPath()`, pass `tiltL`/`tiltR` to its four `calcRefs()` calls in the same way as `synthMainPath()`.

**Update the four call sites in `synthIR()` (the dispatcher):**
- OUTRIG: pass `p.outrig_ltilt` and `p.outrig_rtilt`
- AMBIENT: pass `p.ambient_ltilt` and `p.ambient_rtilt`

Also update the forward declaration for `synthExtraPath` in `IRSynthEngine.h`.

---

### File 6: `Source/IRSynthEngine.cpp` — `synthDirectPath()`

`synthDirectPath()` shares MAIN's mic pattern and angles (by design decision). It must also share the tilt.

**Locate where `calcRefs()` is called inside `synthDirectPath()`.** The tilt values come from `p.micl_tilt` and `p.micr_tilt` (same as MAIN).

If Decca tree is enabled (`p.main_decca_enabled`), use `p.decca_tilt` for all three mic positions, same as `synthMainPath()`.

No signature change is needed for `synthDirectPath()` itself since it takes the full `IRSynthParams&` and can read the new fields directly.

---

### File 7: `Source/FloorPlanComponent.h` — `TransducerState`

Add a `tilt` array to track per-transducer elevation angle alongside the existing `angle` (azimuth) array.

**Current struct (abbreviated):**
```cpp
struct TransducerState
{
    double cx[8] = { ... };
    double cy[8] = { ... };
    double angle[8] = { ... };
    // Decca fields ...
};
```

**Add after the `angle` array:**
```cpp
// Elevation tilt per transducer (radians). 0 = horizontal facing. Negative = tilted down.
// Speakers (0/1) are rarely tilted — default 0. Mic pairs (2–7) default to −π/6 (−30°).
double tilt[8] = {
    0.0, 0.0,                              // speakers: horizontal
   -0.5235987755982988, -0.5235987755982988,  // MAIN mics: −30°
   -0.5235987755982988, -0.5235987755982988,  // OUTRIG mics: −30°
   -0.5235987755982988, -0.5235987755982988   // AMBIENT mics: −30°
};

// Decca Tree tilt (single value for all three mics, rigid array):
double deccaTilt = -0.5235987755982988;  // −30°
```

The `tilt` values flow into `IRSynthParams` through `IRSynthComponent::buildParamsFromState()` (see File 8).

---

### File 8: `Source/IRSynthComponent.cpp` — `buildParamsFromState()`

`buildParamsFromState()` converts `FloorPlanComponent`'s `TransducerState` into `IRSynthParams`. Add tilt field mappings.

**After the angle assignments**, add:
```cpp
// MAIN mic tilts
params.micl_tilt = state.tilt[2];
params.micr_tilt = state.tilt[3];

// OUTRIG mic tilts
params.outrig_ltilt = state.tilt[4];
params.outrig_rtilt = state.tilt[5];

// AMBIENT mic tilts
params.ambient_ltilt = state.tilt[6];
params.ambient_rtilt = state.tilt[7];

// Decca tilt
params.decca_tilt = state.deccaTilt;
```

---

### File 9: `Source/IRSynthComponent.cpp` — UI for tilt controls

The mic-paths strip at the bottom of the IRSynth panel currently has four columns (MAIN, DIRECT, OUTRIG, AMBIENT), each with a Pattern combo. Add a tilt angle control per column.

**Layout approach:**
- Add a small labelled rotary knob or a numeric text label + increment/decrement buttons below the Pattern combo in each mic column (MAIN, OUTRIG, AMBIENT; DIRECT has no separate mic — it shares MAIN and therefore shares MAIN's tilt, so no tilt control is shown for DIRECT).
- Alternatively, and more compactly: a **small `juce::Slider` in `LinearVertical` or `IncDecButtons` style**, 40 px wide, 32 px tall, positioned below the Pattern combo in each column.
- Label: "TILT" above the control.
- Readout: integer degrees (e.g. "−30°") displayed below the control.

**Control mapping:**
- MAIN column tilt slider → `state.tilt[2]` and `state.tilt[3]` (L and R share the same tilt value from the slider; they always move together)
- OUTRIG column tilt slider → `state.tilt[4]` and `state.tilt[5]`
- AMBIENT column tilt slider → `state.tilt[6]` and `state.tilt[7]`
- Decca column (when Decca mode is enabled) → `state.deccaTilt`

**Slider range:** −90° to +90° (−π/2 to π/2 internally), step 1°. Default: −30°.

**Value display:** Show as integer degrees with sign and degree symbol: "−30°", "+10°", "0°".

**`onValueChange` callback:** Set both L and R tilt for the pair simultaneously, then call `onParamModifiedFn()` to mark the IR synth dirty (same pattern as all other parameter controls in `IRSynthComponent`).

**`setParams()` / `getParams()` updates:** Add tilt readback in `setParams()` (to restore UI when loading a preset) and tilt writeback in `buildParamsFromState()`. The suppression guard (`suppressingParamNotifications`) applies the same way.

---

### File 10: `PluginProcessor.cpp` — sidecar and state persistence

#### 10a. Sidecar XML (`.ping` files)

In `writeIRSynthParamsSidecar()` (or wherever `.ping` sidecar attributes are written), add the new tilt fields:

```cpp
// Existing angle attributes:
el.setAttribute("miclAngle",  p.micl_angle);
el.setAttribute("micrAngle",  p.micr_angle);
// New tilt attributes — add immediately after:
el.setAttribute("miclTilt",   p.micl_tilt);
el.setAttribute("micrTilt",   p.micr_tilt);
el.setAttribute("outrigLTilt", p.outrig_ltilt);
el.setAttribute("outrigRTilt", p.outrig_rtilt);
el.setAttribute("ambientLTilt", p.ambient_ltilt);
el.setAttribute("ambientRTilt", p.ambient_rtilt);
el.setAttribute("deccaTilt",   p.decca_tilt);
```

In `loadIRSynthParamsFromSidecar()`, read them with the new defaults as fallback:
```cpp
p.micl_tilt      = el.getDoubleAttribute("miclTilt",   -0.5235987755982988);
p.micr_tilt      = el.getDoubleAttribute("micrTilt",   -0.5235987755982988);
p.outrig_ltilt   = el.getDoubleAttribute("outrigLTilt",-0.5235987755982988);
p.outrig_rtilt   = el.getDoubleAttribute("outrigRTilt",-0.5235987755982988);
p.ambient_ltilt  = el.getDoubleAttribute("ambientLTilt",-0.5235987755982988);
p.ambient_rtilt  = el.getDoubleAttribute("ambientRTilt",-0.5235987755982988);
p.decca_tilt     = el.getDoubleAttribute("deccaTilt",  -0.5235987755982988);
```

**Backward compatibility:** Old `.ping` sidecars without tilt attributes will fall back to the −30° default. This is intentional — all existing factory IRs will sound slightly different after regeneration anyway (expected), and any user-saved sidecars from before this change will load with physically-realistic −30° tilt rather than the incorrect 0° assumption.

#### 10b. Plugin state XML (preset state in `getStateInformation` / `setStateInformation`)

The `IRSynthParams` tilt fields are persisted via the `<irSynthParams>` child element in the APVTS state XML. Follow the exact same pattern as the existing angle/position attributes in that block. Add all 7 new attributes with the same `getAttribute` / `setAttribute` calls and `−π/6` defaults for `setStateInformation` fallback.

---

## Tests

### Tests that will break (expected, must be recaptured)

**IR_11** (golden regression lock, `Tests/PingEngineTests.cpp`)  
The 3D polar pattern change affects the amplitude of every image source that has a non-zero elevation angle, which is essentially all of them except the direct path (which arrives nearly horizontally for typical source/mic positions). The 30 golden sample values **will change** even though the onset index (482) should stay the same (geometry unchanged). After implementation, run:
```bash
./PingTests "[capture]" -s
```
Paste the new `golden_iLL[30]` values and commit with a note: "Recapture after 3D polar pattern (3D mic tilt v2.8.0)".

**IR_14** (bit-identity hash, `Tests/PingMultiMicTests.cpp`)  
Bit-identity will break because `micG()` now returns different values. After implementation, run:
```bash
./PingTests "[capture14]" -s
```
Paste the new FNV-1a digests and commit.

### New DSP test: `DSP_21` — 3D polar pattern correctness

Add to `Tests/PingDSPTests.cpp`. This test verifies the `micG()` formula directly, without requiring a full IR synthesis run.

```cpp
TEST_CASE("DSP_21: 3D polar pattern micG correctness", "[dsp]")
{
    // Test the spherical law of cosines formula directly.
    // micG(band, az, el, pat, faceAzimuth, faceElevation)

    SECTION("On-axis in 3D returns 1.0 for cardioid (o+d=1)")
    {
        // Source directly along the mic's facing axis (θ=0 → cosTheta=1)
        double gain = IRSynthEngine::micG(4, 0.5, -0.3, "cardioid (LDC)", 0.5, -0.3);
        REQUIRE(gain == Approx(1.0).epsilon(1e-9));
    }

    SECTION("Directly behind returns 0.0 for cardioid at HF")
    {
        // faceAz=0, faceEl=0; source at az=π (behind), el=0 → cosTheta=-1
        // cardioid LDC at band 7 (16 kHz): o=0.06, d=0.94 → gain=0.06-0.94=-0.88 → clamped to 0
        double gain = IRSynthEngine::micG(7, M_PI, 0.0, "cardioid (LDC)", 0.0, 0.0);
        REQUIRE(gain == Approx(0.0).epsilon(1e-9));
    }

    SECTION("Omni pattern is unaffected by elevation")
    {
        double g1 = IRSynthEngine::micG(4, 0.0,  0.0, "omni", 0.0, 0.0);
        double g2 = IRSynthEngine::micG(4, 1.5, -1.0, "omni", 0.5, -0.5);
        double g3 = IRSynthEngine::micG(4, -2.0, 0.8, "omni", -1.0, 0.3);
        REQUIRE(g1 == Approx(1.0).epsilon(1e-9));
        REQUIRE(g2 == Approx(1.0).epsilon(1e-9));
        REQUIRE(g3 == Approx(1.0).epsilon(1e-9));
    }

    SECTION("Horizontal facing (faceEl=0) reduces to cos(el)*cos(az-faceAz)")
    {
        // At faceElevation=0 the formula must equal cos(el)*cos(az-faceAz)
        // Use cardioid LDC band 4 (2 kHz): o=0.40, d=0.60
        const double az = 0.7, el = -0.4, faceAz = 0.2;
        double expected = 0.40 + 0.60 * std::cos(el) * std::cos(az - faceAz);
        double actual   = IRSynthEngine::micG(4, az, el, "cardioid (LDC)", faceAz, 0.0);
        REQUIRE(actual == Approx(expected).epsilon(1e-9));
    }

    SECTION("Source directly above/below is null for horizontal figure-8")
    {
        // el = ±π/2 → cos(el) = 0 → cosTheta = 0 regardless of azimuth
        // figure-8 at mid-HF: o=0, d=1 → gain = max(0, 0+1*0) = 0
        double g1 = IRSynthEngine::micG(4,  0.0,  M_PI/2, "figure8", 0.0, 0.0);
        double g2 = IRSynthEngine::micG(4,  1.5, -M_PI/2, "figure8", 1.5, 0.0);
        REQUIRE(g1 == Approx(0.0).epsilon(1e-9));
        REQUIRE(g2 == Approx(0.0).epsilon(1e-9));
    }

    SECTION("Tilted mic — source on new axis gives gain 1.0")
    {
        // Mic tilted −30° (faceElevation = -π/6), facing azimuth π/4
        // Source arriving from az=π/4, el=−π/6 (exactly on-axis)
        double faceAz = M_PI / 4.0, faceEl = -M_PI / 6.0;
        double gain = IRSynthEngine::micG(4, faceAz, faceEl, "cardioid (SDC)", faceAz, faceEl);
        REQUIRE(gain == Approx(1.0).epsilon(1e-9));
    }

    SECTION("−30° tilt boosts floor reflections vs. horizontal facing (LDC, HF)")
    {
        // Source arriving at el=−22° (typical instrument at 1m below a 3m mic, 5m away)
        // az matches faceAz so horizontal azimuth component is on-axis.
        // cardioid LDC band 7 (16 kHz): o=0.06, d=0.94
        const double el = -0.384;  // −22°
        const double az = 0.0, faceAz = 0.0;
        double g_horizontal = IRSynthEngine::micG(7, az, el, "cardioid (LDC)", faceAz, 0.0);
        double g_tilted     = IRSynthEngine::micG(7, az, el, "cardioid (LDC)", faceAz, el); // tilt to match source
        // Tilted mic facing exactly toward source gives gain=1.0
        REQUIRE(g_tilted == Approx(1.0).epsilon(1e-6));
        // Horizontal facing gives cos(el)*1 < 1
        REQUIRE(g_horizontal < g_tilted);
    }
}
```

To make `micG()` accessible from tests without JUCE dependency, ensure its declaration remains `static` (or a free function) in `IRSynthEngine.h` and that `PING_TESTING_BUILD` compiles it correctly. The function is already `static` in the existing codebase.

---

## `spkG()` — Not changed in this implementation

Speaker directivity (`spkG`) has the same 2D limitation but is left unchanged in this implementation for scope reasons. Real orchestral speakers/instruments are at roughly the same height as each other and any elevation angle between speaker and mic is small compared to the mic's vertical polar attenuation. Speaker tilt is noted as a future enhancement.

If it is added later, the formula is identical — `spkG` becomes:
```cpp
double spkG (double faceAzimuth, double faceElevation, double azToReceiver, double elToReceiver)
{
    const double cosTheta = std::sin(elToReceiver) * std::sin(faceElevation)
                          + std::cos(elToReceiver) * std::cos(faceElevation)
                                                   * std::cos(azToReceiver - faceAzimuth);
    return std::max(0.0, 0.5 + 0.5 * cosTheta);
}
```
Speaker tilt defaults: 0° (horizontal) for both speakers. No `spkl_tilt` / `spkr_tilt` parameters added in this feature.

---

## Factory IR Regeneration

After this implementation is complete and tested, **all factory IRs must be regenerated** because the acoustic output of `synthMainPath()` changes for every room.

```bash
g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
    Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
    -o build/generate_factory_irs -lm
./build/generate_factory_irs Installer/factory_irs Installer/factory_presets
python3 Tools/trim_factory_irs.py Installer/factory_irs
```

The regenerated IRs will reflect 3D polar patterns with −30° tilt. The acoustic character of all 27 factory venues will shift slightly — more accurate floor reflection weighting, stronger early-reflection presence (especially for LDC/SDC patterns with the mic now better aimed at the ensemble level).

Verify a sample of the new files:
```bash
# Check WAV format tag is still 0xFFFE (WAVE_FORMAT_EXTENSIBLE)
python3 -c "import struct; d=open('Installer/factory_irs/Halls/Lyndhurst Hall.wav','rb').read(80); [print(f'tag=0x{struct.unpack(\"<H\",d[p+8:p+10])[0]:04x}') for p in range(12,60) if d[p:p+4]==b'fmt ']"
# Should print: tag=0xfffe
```

---

## Summary of All Changed Files

| File | Nature of change |
|---|---|
| `Source/IRSynthEngine.h` | 7 new doubles in `IRSynthParams`; updated `micG()` and `calcRefs()` declarations |
| `Source/IRSynthEngine.cpp` | `micG()` — new signature + 3D formula; `calcRefs()` — new `el` computation + updated `micG()` call + new `micFaceTilt` param; `synthMainPath()` — tiltL/tiltR locals + pass to calcRefs; `synthExtraPath()` — tiltL/tiltR params + pass to calcRefs; `synthDirectPath()` — pass tilt from params |
| `Source/FloorPlanComponent.h` | `tilt[8]` array + `deccaTilt` in `TransducerState` |
| `Source/IRSynthComponent.cpp` | `buildParamsFromState()` + `setParams()` tilt fields; new tilt UI controls per mic-pair column |
| `Source/IRSynthComponent.h` | New tilt slider/control member declarations |
| `PluginProcessor.cpp` | Sidecar read/write for 7 new tilt attributes; `<irSynthParams>` XML state read/write |
| `Tests/PingEngineTests.cpp` | IR_11 golden values recaptured |
| `Tests/PingMultiMicTests.cpp` | IR_14 hash recaptured |
| `Tests/PingDSPTests.cpp` | DSP_21 new test added |
| `Installer/factory_irs/**` | All 27 WAVs + sidecars regenerated |
| `Installer/factory_presets/**` | All factory presets regenerated (reference fresh IRs) |
| `CLAUDE.md` | Document new tilt parameters, 3D micG formula, DSP_21, updated defaults |

---

## CLAUDE.md Additions Required

After implementation, add the following entries to `CLAUDE.md` Key Design Decisions:

- **`micG()` uses 3D spherical law of cosines** — formula is `cos(θ) = sin(el)·sin(faceEl) + cos(el)·cos(faceEl)·cos(az−faceAz)`. At `faceElevation=0` this reduces to `cos(el)·cos(az−faceAz)`, which correctly attenuates steeply-arriving reflections by the vertical component of the polar pattern. Do not revert to the 2D formula `cos(az−faceAngle)` — even at zero tilt, the 3D formula is more physically correct.

- **Default mic tilt is −30° (−π/6 radians) for all mic pairs** — matches the standard hanging angle for Decca tree / outrigger arrays at 3 m height. Tilting downward means `faceElevation < 0`. Speakers default to 0° (horizontal). The tilt is stored per-pair in `IRSynthParams` and per-transducer in `TransducerState.tilt[]`. L and R mics in each pair always share the same tilt value in the UI.

- **Tilt and azimuth are independent angles** — `angle[i]` in `TransducerState` is the horizontal facing direction (unchanged convention). `tilt[i]` is the elevation tilt from horizontal. They map to `faceAzimuth` and `faceElevation` in `micG()`. The FloorPlan UI shows azimuth only (top-down view); tilt is controlled by the separate slider in the mic-paths strip.

- **`calcRefs()` computes elevation `el` per image source** — `el = atan2(iz − rz, max(hDist, 1e-9))` where `hDist = sqrt((ix−rx)²+(iy−ry)²)`. The `1e-9` guard prevents division-by-zero for image sources directly overhead or below. `el` is passed to every `micG()` call alongside the existing `az`.

- **IR_11 and IR_14 recaptured at v2.8.0 after 3D polar pattern implementation** — any future change to `micG()` or the image-source geometry will again require recapture.
