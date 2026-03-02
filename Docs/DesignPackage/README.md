# P!NG Design Package for Figma

This package helps you polish the P!NG UI in Figma and bring the designs back to implementation. The **layout and structure remain fixed**; this focuses on **control visuals** (knobs, sliders, buttons).

---

## What’s Included

| File | Purpose |
|------|---------|
| `design-tokens.json` | Colors (hex), sizes (px), radii. Use for Figma variables or manual reference. |
| `control-spec.md` | Detailed spec for each control type (dimensions, states, colors). |
| `assets/*.svg` | SVG versions of knob (small/big), slider, button. Import as components. |

---

## Workflow: Cursor → Figma → Cursor

### 1. Import into Figma

1. **Create a new Figma file** (or page) for P!NG controls.
2. **Add design tokens**  
   - Copy color hex values from `design-tokens.json` into Figma color styles.  
   - Define variables for sizes (48, 80, etc.).
3. **Import SVGs**  
   - Drag `assets/knob-small.svg`, `knob-big.svg`, `slider-horizontal.svg`, `button-default.svg` into Figma.  
   - Convert to components for reuse.
4. **Use control-spec.md**  
   - Recreate or refine components to match the spec.  
   - Add states (default, hover, active) as variants.

### 2. Design in Figma

- Work on **visual style only** (bevels, shadows, glow, dotted rings).  
- Keep **dimensions** from the spec so layout stays the same.  
- Use your reference image for accents, 3D feel, orange highlights.

### 3. Bring Back to Cursor

- **There is no Figma → JUCE export.** Implementation stays in code.  
- After design, you can:
  - **Option A**: Describe changes and have the AI update `PingLookAndFeel.cpp` and related styling.
  - **Option B**: Export PNGs/SVGs from Figma and use them as references to update `drawRotarySlider`, `drawButtonBackground`, etc.
  - **Option C**: Share Figma links or exports so the implementation can match the new look.

---

## Implementation Notes

Control appearance is implemented in:

- **`Source/PingLookAndFeel.cpp`** — rotary knobs, Reverse button
- **`Source/PluginEditor.cpp`** — colors for sliders, combos, labels
- **`Source/IRSynthComponent.cpp`** — IR Synth tab styling
- **`Source/EQGraphComponent.cpp`** — EQ graph

JUCE’s `Slider`, `Button`, `ComboBox` use `findColour()` for theming. Changing colors in code is quick; changing shapes (e.g. custom knob geometry) needs edits to `drawRotarySlider` and similar methods.

---

## Tips

- Use **components** in Figma for knobs/sliders so you can try multiple variants.
- Keep a **redline** frame with original sizes to avoid layout drift.
- Export **@2x PNG** or **SVG** for any new textures or graphics to embed in the plugin.
