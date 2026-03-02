# P!NG UI Control Specification

For use when designing in Figma. Layout and hierarchy are fixed; visual styling (colors, bevels, shadows) can be polished.

---

## Control Types

### 1. Rotary Knob (Small) — 48×48 px
- Depicted in PingLookAndFeel: thin circular track, filled arc, centre dot
- Track: 1.2 px stroke, grey (#3a3a3a)
- Fill arc: accent (#e8a84a) or grey (#909090) for secondary controls
- Thumb: white dot, 6 px diameter, on track radius
- States: default, hover (optional), dragging

### 2. Rotary Knob (Big / Dry-Wet) — 80×80 px
- Same as small but with:
- Inner dark circle (#1a1a1a) with dotted texture
- 24 dots around centre (optional design refinement)
- Outer ring, thin track, filled arc, centre dot

### 3. Horizontal Slider — height 18 px
- Track: grey (#3a3a3a), rounded ends
- Fill: accent (#e8a84a) for value portion
- Thumb: small square or pill
- Used for ER Level, Tail Level; also in IR Synth (Audience, Diffusion, etc.)

### 4. Text Buttons
- Save preset, IR Synth, Reverse
- Background: #1e1e1e or #1a1a1a
- Text: #909090 (dim) or white when active
- Reverse engaged: #b83030 fill, darker border
- Corner radius: 6 px
- Height: ~24–38 px

### 5. ComboBox (Dropdown)
- Background: #2a2a2a
- Text: #909090
- Arrow: #e8a84a

### 6. EQ Graph
- Background: #1e1e1e
- Grid: #3a3a3a
- Response curve: #e8a84a with band markers (purple, green, orange)
- Band buttons: 1, 2, 3 — toggle state uses accent

### 7. Waveform
- Panel bg: #1e1e1e, border #2a2a2a
- Fill: rgba(232, 168, 74, 0.16)
- Line: #e8e8e8

---

## Reference Image Notes

The attached reference shows:
- Dark grey background with orange accent
- Knobs with dotted orange ring (range indicator)
- Horizontal slider with explicit % readout
- Toggle switches (square)
- 3D / embossed feel
- Orange border glow on active elements

These can inform refinements while keeping the existing P!NG layout and control positions.
