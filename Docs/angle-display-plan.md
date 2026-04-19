# Plan: Transducer Angle Readouts in Floor Plan Legend

## Overview

Add a live numeric angle readout (degrees) for all four transducers (Spk L, Spk R, Mic L, Mic R) directly alongside their existing legend labels in the top-left of the `FloorPlanComponent`. The readout updates in real time as the user drags the direction ring.

**Scope:** `Source/FloorPlanComponent.cpp` only — no header changes, no new parameters, no state additions.

---

## Angle Convention

### Internal storage
Angles are stored in `transducers.angle[4]` as radians using the standard `atan2(dy, dx)` convention with screen coordinates (Y-axis pointing **down**). This means:

| Internal radians | Screen direction |
|---|---|
| `0` | East (right) |
| `+π/2` | South (down — towards audience) |
| `−π/2` | North (up) |
| `±π` | West (left) |
| Positive = clockwise visually (screen Y-down makes CW positive) |

### Desired display convention
`0° = North (up), positive = clockwise, South = ±180°`

### Conversion formula
```cpp
double displayDeg = (transducers.angle[i] + kPi / 2.0) * 180.0 / kPi;

// Normalise to (−180, +180]
while (displayDeg >  180.0) displayDeg -= 360.0;
while (displayDeg <= -180.0) displayDeg += 360.0;
```

### Verification against defaults
| Transducer | Stored (rad) | Displayed |
|---|---|---|
| Spk L | `+π/2` | `+180°` — facing south (towards audience) ✓ |
| Spk R | `+π/2` | `+180°` ✓ |
| Mic L | `−3π/4` | `−45°` — NW (up-left) ✓ |
| Mic R | `−π/4` | `+45°` — NE (up-right) ✓ |

---

## What Changes

### 1. Widen the legend text area

The current legend draws each row as:

```
[icon 10px] [gap 6px] [label 52px wide]
```

Starting at `legX = 8.0f`, `legY = 8.0f`, `legRow = 14.0f`.

The label column needs to shrink slightly to make room for the degrees readout. Proposed new layout:

```
[icon 10px] [gap 6px] [label 30px] [gap 4px] [degrees 36px]
```

| Column | X position | Width |
|---|---|---|
| Icon centre | `legX + legIconSz * 0.5f` | 10px |
| Label | `legX + legIconSz + 6` | 30px (was 52) |
| Degrees | `legX + legIconSz + 6 + 30 + 4` = `legX + 50` | 36px |

"Spk L" / "Mic R" etc. comfortably fit in 30px at 9pt. The degrees readout needs to accommodate `"−180°"` (5 chars) — 36px at 9pt is sufficient.

### 2. Compute and draw the degrees readout

Inside the legend loop (currently lines 356–365 of `FloorPlanComponent.cpp`), after drawing the label text, add:

```cpp
// Angle readout
double rawRad = transducers.angle[i];
double displayDeg = (rawRad + kPi / 2.0) * 180.0 / kPi;
while (displayDeg >  180.0) displayDeg -= 360.0;
while (displayDeg <= -180.0) displayDeg += 360.0;
int displayDegInt = (int) std::round (displayDeg);

juce::String degStr = juce::String (displayDegInt) + juce::String::fromUTF8 (u8"\u00b0");

g.setColour (cols[i].withAlpha (0.9f));   // use transducer accent colour, slightly dimmed
g.drawText (degStr,
            (int) (legX + legIconSz + 50),
            (int) (legY + i * legRow),
            36,
            (int) legRow,
            juce::Justification::centredLeft,
            false);
```

The degrees text uses each transducer's own colour (slightly dimmed) rather than the plain white `legText`, making it easy to scan which readout belongs to which transducer at a glance.

### 3. Restore the label column width

Change the existing label `drawText` call's `width` argument from `52` to `30`:

```cpp
// Before:
g.drawText (legLabels[i], ..., 52, (int) legRow, ...);

// After:
g.drawText (legLabels[i], ..., 30, (int) legRow, ...);
```

---

## Full Diff Summary (paint() legend block only)

```cpp
// ── BEFORE ────────────────────────────────────────────────────
for (int i = 0; i < 4; ++i)
{
    float iconCx = legX + legIconSz * 0.5f;
    float yy = legY + (i + 0.5f) * legRow;
    const juce::Path& legIcon = (i < 2) ? spkPath : micPath;
    drawTransducerIcon (g, legIcon, iconCx, yy, 0.0f, legIconSz * 0.5f, cols[i]);
    g.setColour (legText);
    g.drawText (legLabels[i], (int)(legX + legIconSz + 6), (int)(legY + i * legRow),
                52, (int)legRow, juce::Justification::centredLeft, false);
}

// ── AFTER ─────────────────────────────────────────────────────
for (int i = 0; i < 4; ++i)
{
    float iconCx = legX + legIconSz * 0.5f;
    float yy = legY + (i + 0.5f) * legRow;
    const juce::Path& legIcon = (i < 2) ? spkPath : micPath;
    drawTransducerIcon (g, legIcon, iconCx, yy, 0.0f, legIconSz * 0.5f, cols[i]);

    // Label
    g.setColour (legText);
    g.drawText (legLabels[i], (int)(legX + legIconSz + 6), (int)(legY + i * legRow),
                30, (int)legRow, juce::Justification::centredLeft, false);

    // Angle readout (0° = north, +CW, −CCW, ±180° = south)
    double displayDeg = (transducers.angle[i] + kPi / 2.0) * 180.0 / kPi;
    while (displayDeg >  180.0) displayDeg -= 360.0;
    while (displayDeg <= -180.0) displayDeg += 360.0;
    juce::String degStr = juce::String ((int) std::round (displayDeg))
                          + juce::String::fromUTF8 (u8"\u00b0");
    g.setColour (cols[i].withAlpha (0.85f));
    g.drawText (degStr, (int)(legX + legIconSz + 50), (int)(legY + i * legRow),
                36, (int)legRow, juce::Justification::centredLeft, false);
}
```

---

## Why repaint() is already handled

`mouseDrag()` already calls `repaint()` on every drag event (line 204), so the angle readout will update live with zero additional plumbing.

---

## Files Changed

| File | Change |
|---|---|
| `Source/FloorPlanComponent.cpp` | Legend loop only — ~8 lines added, 1 line changed |
| `Source/FloorPlanComponent.h` | **None** |
| All other files | **None** |

---

## Edge Cases

| Situation | Behaviour |
|---|---|
| Angle exactly north (−π/2 rad) | Displays `0°` |
| Angle exactly south (+π/2 rad) | Displays `+180°` |
| Angle near ±180° boundary | Normalisation loop handles gracefully; `-180°` is mapped to `+180°` |
| Angle after many drag cycles (value drift beyond 2π) | Normalisation loop corrects regardless of accumulated windings |
| Very small component (W/H < 20) | Early return before legend block — no change needed |

---

## Complexity Assessment

**Very low.** This is a pure display addition inside a single `paint()` function. There is no new state, no new parameters, no callbacks, no header changes, and no risk of affecting audio processing or IR loading. The only consideration is the narrow legend column width — if any future label name is added that is longer than "Mic R", the 30px column may need revisiting.

Estimated implementation time: **~15 minutes**.
