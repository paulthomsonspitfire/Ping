# P!NG Chrome UI — Implementation Plan

Four independent changes, each self-contained. They can be done in any order,
but the suggested sequence is: Accent → Texture → Panels → Meter (each
builds naturally on what's already visible).

---

## 1 — Accent Colour Change

**Files:** `PluginEditor.cpp` (anonymous namespace), `PingLookAndFeel.cpp`

### PluginEditor.cpp — anonymous namespace colour constants (lines 13–21)

Replace the four orange-derived constants:

| Old constant | Old hex | New hex | Note |
|---|---|---|---|
| `accent` | `0xffe8a84a` | `0xff8cd6ef` | Icy blue-white |
| `accentDim` | `0xffc48938` | `0xff5ab0cc` | Dimmer variant |
| `waveFill` | `0x28e8a84a` | `0x288cd6ef` | Same alpha, new colour |
| `waveLine` | `0xffe8e8e8` | `0xffd8e8f4` | Slight cool tint on waveform outline |

The `bgDark`, `panelBg`, `panelBorder`, `textCol`, `textDim` constants remain
unchanged — only the orange-derived ones move.

### PingLookAndFeel.cpp

Three things to change:

**1. `accentOrange` constant (top of file)**
```
// was:
static const juce::Colour accentOrange { 0xffe8a84a };

// becomes:
static const juce::Colour accentIce    { 0xff8cd6ef };   // icy blue-white
static const juce::Colour accentLed    { 0xffc4ecf8 };   // near-white for pill LED centre
```

**2. All uses of `accentOrange` in `drawToggleButton`**
- Pill LED glow radial gradient centre: change from `accentOrange.brighter(0.25f)` → `accentLed`
- Pill LED glow outer stop: `accentOrange.withAlpha(0.0f)` → `accentIce.withAlpha(0.0f)`
- Pill border ON colour: `accentOrange` → `accentIce`
- Pill border hovered-ON colour: `accentOrange.brighter(0.15f)` → `accentIce.brighter(0.1f)`

**3. `drawButtonBackground` — IRSynth button border**
- Hover border: `accentOrange` → `accentIce`

**Downstream — knob dot-ring fill colour**
The active dot colour on every knob comes from `slider.findColour(rotarySliderFillColourId)`.
Search `PluginEditor.cpp` for all `.setColour(juce::Slider::rotarySliderFillColourId, ...)`
calls and replace the colour argument with the new `accent` constant. (There will be one call
per slider style group in the constructor.)

---

## 2 — Brushed Metallic Background Texture

**Files:** `PluginEditor.h` (member variable), `PluginEditor.cpp` (constructor + `paint()`)

### What it is

A 512 × 512 `juce::Image` generated once at construction time, drawn tiled
at low opacity over `bgDark` in every `paint()` call. The texture has:

- **Horizontal grain** — each pixel row is generated with an independent random
  seed, so grain streaks run left-to-right. Adjacent pixels in the same row are
  correlated (~70% carry-over from the previous pixel), giving smooth horizontal
  scratches rather than pure noise.
- **Slow vertical luminance sweep** — a sum of two sine waves (`0.04f` and `0.11f`
  radians/px) creates gently varying bright and dark horizontal bands, the way a
  curved metal surface catches light differently at different heights.
- **Fixed seed** — `juce::Random rng(0x8CD6EF42)`. Texture is identical on every
  plugin instance; no per-launch variation.

### Member variable (PluginEditor.h)

```cpp
juce::Image brushedMetalTexture;
```

### Constructor (PluginEditor.cpp) — add after `setOpaque(true)`

```
Generate a 512×512 ARGB image.
For each row y:
  rowBrightness = sin(y * 0.040f) * 0.018f + sin(y * 0.110f) * 0.009f
  grainCarry = rng.nextFloat()
  For each column x:
    grain = grainCarry * 0.72f + rng.nextFloat() * 0.28f
    grainCarry = grain
    brightness = rowBrightness + (grain - 0.5f) * 0.08f   // ±4% variation
    alpha = 48   // out of 255; adjust up for more obvious texture
    pixel = Colour(grey, grey, grey, alpha)
      where grey = jlimit(0, 255, int((0.5f + brightness) * 255))
```

`alpha = 48` (~19%) gives a clearly visible but not overpowering brushed look.
Increase to `60` if it needs to be more aggressive.

### paint() — first two lines, replace the current fill

```
Current:
  g.fillAll(bgDark);
  g.setGradientFill(ColourGradient::vertical(...));
  g.fillRect(getLocalBounds());

New:
  // 1. Solid base
  g.fillAll(bgDark);

  // 2. Subtle vertical gradient — keep exactly as-is (same code, just below fillAll)
  g.setGradientFill(ColourGradient::vertical(bgDark.brighter(0.03f), 0,
                                              bgDark.darker(0.04f), getHeight()));
  g.fillRect(getLocalBounds());

  // 3. Brushed metal overlay — tiled, alpha baked into the texture pixels
  g.setTiledImageFill(brushedMetalTexture, 0, 0, 1.0f);
  g.fillRect(getLocalBounds());
```

The tiled fill call comes third so the grain sits on top of the gradient,
not underneath it. The alpha already baked into each texture pixel means no
`setOpacity()` call is needed.

---

## 3 — Bevelled Panels

**Files:** `PluginEditor.h` (member variables), `PluginEditor.cpp` (`resized()` + `paint()`)

### New colour constants (anonymous namespace, PluginEditor.cpp)

Add alongside the existing constants:

```cpp
// Panel chrome surface
const juce::Colour panelFaceTop  { 0xff181d28 };  // top of raised panel face
const juce::Colour panelFaceBot  { 0xff0e1018 };  // bottom of raised panel face
const juce::Colour bevHi         { 0xff3e4a62 };  // top/left catch light (bright chrome edge)
const juce::Colour bevLo         { 0xff03040a };  // bottom/right shadow (near-black)
const juce::Colour grooveDark    { 0xff03040a };  // top line of inner divider groove
const juce::Colour grooveLight   { 0xff28304a };  // bottom line of inner divider groove
```

`panelBg` and `panelBorder` (the waveform panel colours) stay as-is; these new
constants are only used for the row-group panels.

### New member variables (PluginEditor.h)

One `juce::Rectangle<int>` per panel drawn in the background. Seven in total:

```cpp
// Individual row panels
juce::Rectangle<int> panelRow1;     // IR Input + IR Controls
juce::Rectangle<int> panelRow2;     // ER Crossfade + Tail Crossfade
juce::Rectangle<int> panelRow3R;    // Tail AM mod + Tail Frq mod (right side)

// Grouped panels (two rows sharing one surface)
juce::Rectangle<int> panelPlateBloom;     // Plate pre-diffuser + Bloom hybrid
juce::Rectangle<int> panelCloudShim;      // Clouds post convolution + Shimmer

// Groove Y positions inside the grouped panels
int groovePlateBloom = 0;   // Y of inner divider between Plate and Bloom
int grooveCloudShim  = 0;   // Y of inner divider between Cloud and Shimmer
```

### resized() additions

After all the existing row Y and bounds calculations are complete, add a section
at the end of `resized()` that computes panel bounds from the already-known anchors.

The general formula per panel is:
```
panel.x      = rowStartX - hPad
panel.y      = groupHeaderTop - vPad
panel.width  = (right edge of rightmost knob in row) - panel.x + hPad
panel.height = (bottom of value readout in last row) - panel.y + vPad
```

Specifically:

**`panelRow1`** — wraps Row 1 (all 5 knobs: GAIN, DRIVE, PREDELAY, DAMPING, STRETCH)
- Top: `irInputGroupBounds.getY() - 4`
- Bottom: `rowY + rowKnobSize + labelH + readoutH + 4`
  (where `labelH` and `readoutH` are the heights of the text below each knob)
- Left: `rowStartX - 6`
- Right: left + width of 5 knobs at `rowStep` spacing + `6`

**`panelRow2`** — wraps Row 2 (DELAY, ATT, DELAY, ATT + two pills)
- Same approach, from `erCrossfadeGroupBounds.getY() - 4` to row2 knob bottoms + 4

**`panelPlateBloom`** — spans Rows 3 and 4 together
- Top: `plateGroupBounds.getY() - 4`
- Bottom: `row4AbsY + row4TotalH_ + 4`
- Left/right: same as Row 1 (same column)
- `groovePlateBloom` = `bloomGroupBounds.getY() - 3`  (the line sits just above the Bloom header)

**`panelCloudShim`** — spans Rows R1 and R2 together
- Top: `cloudGroupBounds.getY() - 4`
- Bottom: `row2AbsY + row2TotalH_ + 4`  (Row R2 = row2AbsY equivalent on right side)
- Left: right edge of window minus (5 knobs × rowStep) minus `6`
- Right: `b.getRight() + 6`
- `grooveCloudShim` = `shimGroupBounds.getY() - 3`

**`panelRow3R`** — wraps Row R3 (Tail AM + Tail Frq mod, 5 knobs)
- Top: `tailAMModGroupBounds.getY() - 4`
- Bottom: `row3KnobY + rowKnobSize + labelH + readoutH + 4`
- Left/right: mirror of Row 1 on the right side

### New helper lambda in paint()

Add a `drawBevelPanel` lambda immediately after the brushed texture fill,
before any group header drawing:

```
drawBevelPanel = [&](Rectangle<int> r) {
  if (r.isEmpty()) return;
  auto rf = r.toFloat();
  const float corner = 4.5f;

  // Face fill — vertical gradient, slightly lighter top than bottom
  g.setGradientFill(ColourGradient(panelFaceTop, rf.getX(), rf.getY(),
                                   panelFaceBot, rf.getX(), rf.getBottom(), false));
  g.fillRoundedRectangle(rf, corner);

  // Top bevel — bright chrome catch light (1 px)
  g.setColour(bevHi);
  g.drawLine(rf.getX() + corner, rf.getY() + 0.5f,
             rf.getRight() - corner, rf.getY() + 0.5f, 1.0f);

  // Left bevel — half-brightness catch light
  g.setColour(bevHi.withAlpha(0.45f));
  g.drawLine(rf.getX() + 0.5f, rf.getY() + corner,
             rf.getX() + 0.5f, rf.getBottom() - corner, 1.0f);

  // Bottom shadow (1 px)
  g.setColour(bevLo);
  g.drawLine(rf.getX() + corner, rf.getBottom() - 0.5f,
             rf.getRight() - corner, rf.getBottom() - 0.5f, 1.0f);

  // Right shadow
  g.setColour(bevLo.withAlpha(0.6f));
  g.drawLine(rf.getRight() - 0.5f, rf.getY() + corner,
             rf.getRight() - 0.5f, rf.getBottom() - corner, 1.0f);

  // Outer drop shadow — draw a very slightly larger near-black rect behind it
  // (achieved cheaply with a second fillRoundedRectangle at low alpha
  //  on the shadow side only, offset 2px down/right)
  g.setColour(juce::Colour(0x30000000));
  g.drawRoundedRectangle(rf.translated(1.0f, 1.5f).expanded(0.5f), corner, 1.5f);
};
```

Then add a `drawGroove` lambda for the inner divider:

```
drawGroove = [&](int y, int x, int width) {
  // Two 1px horizontal lines: dark on top (shadow), lighter below (highlight)
  // Creates the look of a machined groove pressed into the panel face
  g.setColour(grooveDark);
  g.drawHorizontalLine(y,     (float)x + 8.f, (float)(x + width) - 8.f);
  g.setColour(grooveLight);
  g.drawHorizontalLine(y + 1, (float)x + 8.f, (float)(x + width) - 8.f);
};
```

### paint() — call order

Insert immediately after the texture fill, before any group header drawing:

```cpp
drawBevelPanel(panelRow1);
drawBevelPanel(panelRow2);
drawBevelPanel(panelPlateBloom);
drawGroove(groovePlateBloom, panelPlateBloom.getX(), panelPlateBloom.getWidth());
drawBevelPanel(panelCloudShim);
drawGroove(grooveCloudShim, panelCloudShim.getX(), panelCloudShim.getWidth());
drawBevelPanel(panelRow3R);
```

The existing `drawGroupHeader` calls that follow immediately after continue
unchanged — the panel surfaces simply become the background they paint on top of.

### Waveform panel

The waveform panel already uses `panelBg`/`panelBorder` — update those two
constants to match the new panel colours so it blends consistently:

```cpp
const juce::Colour panelBg     { 0xff141820 };  // was 0xff1e1e1e — cooler, darker
const juce::Colour panelBorder { 0xff1c2438 };  // was 0xff2a2a2a — blue-grey border
```

---

## 4 — Meter: Multi-Segment Gradient + Glow

**File:** `OutputLevelMeter.h` — `paint()` method only

### New palette constants (inside `paint()`, replace existing ones)

```cpp
// Replace the old fillNormal / fillHot / fillClip block with:
const juce::Colour railColour  { 0xff0d1018 };   // cooler dark rail
const juce::Colour segTeal     { 0xff1a8878 };   // -60 dB start
const juce::Colour segCyan     { 0xff2ab898 };   // dominant colour up to -12 dB
const juce::Colour segYellow   { 0xff98c030 };   // -12 dB threshold
const juce::Colour segAmber    { 0xffe89020 };   // -6 dB threshold
const juce::Colour segRed      { 0xffdc2626 };   // -3 dB and above
const juce::Colour scaleColour { 0xff384055 };   // cooler grey for tick labels
const juce::Colour gridColour  { 0xff101828 };   // subtle grid lines

// Key threshold normalised positions (derived from dbToNorm() inline here)
// Range is kMinDb=-60 to kMaxDb=6, span=66
const float thr12 = (66.f - 12.f) / 66.f;   // ≈ 0.727  (-12 dB)
const float thr6  = (66.f -  6.f) / 66.f;   // ≈ 0.818  (- 6 dB)
const float thr3  = (66.f -  3.f) / 66.f;   // ≈ 0.864  (- 3 dB)
```

### Drawing approach — full-width gradient, clipped to fill level

The key visual idea: the full colour gradient always exists across the entire
bar width; the current signal level acts as a clip mask. At low levels you see
only the teal section; as the signal rises the yellow, amber and red sections
are progressively revealed. This is how hardware VU/PPM meters look.

**Replace the current signal fill block** (currently lines ~102–113) with:

```
Step 1 — Build a horizontal ColourGradient spanning the full bar width:
  pos 0.000 → segTeal
  pos thr12 → segCyan (smooth teal-to-cyan up to -12 dB)
  pos thr12 → segYellow (hard colour break at -12 dB marker)
  pos thr6  → segAmber  (hard break at -6 dB)
  pos thr3  → segRed    (hard break at -3 dB)
  pos 1.000 → segRed.brighter(0.2f)

  To get hard breaks, add two stops at the same position:
    addColour(thr12 - 0.001f, segCyan)
    addColour(thr12,          segYellow)
    addColour(thr6  - 0.001f, segYellow.interpolatedWith(segAmber, 0.5f))
    addColour(thr6,           segAmber)
    addColour(thr3  - 0.001f, segAmber.interpolatedWith(segRed, 0.5f))
    addColour(thr3,           segRed)

Step 2 — GLOW pass (draw first, underneath the main fill):
  Save clip region.
  Reduce clip to: (barX, barY-1, fillWidth, barH+2)   ← 1px vertical expansion
  Set gradient fill (same gradient but all stops at alpha ≈ 55, i.e. 21%)
    — achieved by setting withAlpha(0.22f) on each stop colour
  Fill rect (barX, barY-1, barW, barH+2)  ← fill full width, clip does the work
  Restore clip region.

Step 3 — MAIN fill pass:
  Save clip region.
  Reduce clip to: (barX, barY, fillWidth, barH)
  Set gradient fill (full-opacity gradient from Step 1)
  Fill rect (barX, barY, barW, barH)
  Restore clip region.

Step 4 — LED EDGE highlight (topmost 1px of bar, brighter):
  if fillWidth > 2.f:
    Set gradient fill (same gradient, withAlpha(0.7f) on all stops, brightened)
    Draw a 1px horizontal line along the top edge of the fill:
      drawLine(barX, barY + 0.5f, barX + fillWidth, barY + 0.5f, 1.0f)
    This gives a thin bright "lit edge" like a real LED strip.
```

### Threshold tick marks on the scale

In the scale drawing section (bottom of `paint()`), add three additional ticks
at -12, -6, and -3 dB that are slightly taller and brighter than the existing
grey ticks, to reinforce the colour-zone boundaries:

```
Extra ticks at: scaleDbs = { ..., -12.f, -6.f, -3.f } (add to existing array)
scaleLabels = { ..., "-12", "-6", "-3" }

Draw these three with segYellow / segAmber / segRed respectively instead of
scaleColour, and at tick height 5px instead of 3px.
```

### Grid lines inside bars

Add -3 dB to the existing `markerDbs[]` array in the grid-line drawing loop,
and colour it `segRed.withAlpha(0.25f)` instead of `gridColour`, so the clip
threshold is visually marked on every bar.

---

## Summary of files changed

| File | What changes |
|---|---|
| `PluginEditor.cpp` | Colour constants (7 values); `paint()` texture fill + panel drawing; `resized()` panel bounds computation |
| `PluginEditor.h` | 7 new `Rectangle<int>` panel bounds + 2 `int` groove Y members |
| `PingLookAndFeel.cpp` | `accentOrange` → `accentIce` + `accentLed`; all downstream uses in pill switch and button drawing |
| `OutputLevelMeter.h` | `paint()` colour palette; signal fill drawing (gradient + glow); scale ticks |

No CMakeLists changes. No new source files. No new BinaryData assets.
Everything is drawn programmatically.
