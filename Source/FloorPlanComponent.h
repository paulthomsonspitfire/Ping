#pragma once

#include <JuceHeader.h>
#include <functional>
#include "IRSynthEngine.h"

struct TransducerState
{
    // Indices:
    //   0/1 = speakers (Spk L / Spk R)
    //   2/3 = MAIN mics (Mic L / Mic R)
    //   4/5 = OUTRIG mics (Outrig L / Outrig R)
    //   6/7 = AMBIENT mics (Amb L / Amb R)
    // Defaults mirror IRSynthParams and must stay in sync with the three
    // places listed in CLAUDE.md's "Speaker/mic placement defaults live in
    // three places" note.
    double cx[8] = { 0.25, 0.75,  0.35, 0.65,  0.15, 0.85,  0.20, 0.80 };
    double cy[8] = { 0.50, 0.50,  0.80, 0.80,  0.80, 0.80,  0.95, 0.95 };
    double angle[8] = {
        1.57079632679,   1.57079632679,        // speakers facing down
       -2.35619449019,  -0.785398163397,       // MAIN mics: up-left, up-right
       -2.35619449019,  -0.785398163397,       // OUTRIG mics: same facing
       -2.35619449019,  -0.785398163397        // AMBIENT mics: same facing
    };
    // 3D mic-tilt elevation (radians). 0 = mic faces horizontally; negative
    // tilts the mic downward (toward the floor / source plane). Indices 0/1
    // (speakers) are unused — kept in the array for indexing symmetry with
    // cx/cy/angle. Default −π/6 (≈ −30°) for all 6 mic slots matches the
    // *_tilt defaults in IRSynthParams.
    double tilt[8] = {
        0.0, 0.0,                              // speakers (unused)
       -0.5235987755982988, -0.5235987755982988, // MAIN  L/R: −30°
       -0.5235987755982988, -0.5235987755982988, // OUTRIG L/R: −30°
       -0.5235987755982988, -0.5235987755982988  // AMBIENT L/R: −30°
    };

    // Decca Tree capture mode (see Source/IRSynthEngine.h). These fields hold
    // the tree-centre position and face angle used when FloorPlanComponent's
    // deccaVisible flag is true. The L/C/R mic positions are derived from
    // these by the engine using fixed classical spacing, so they are not
    // stored separately.
    double deccaCx    = 0.5;
    double deccaCy    = 0.65;
    double deccaAngle = -1.5707963267948966; // -π/2: forward = low-y (source stage)
    // Decca tree elevation tilt (rigid array — single value applied to L/C/R).
    // Default −π/6 (≈ −30°) matches IRSynthParams::decca_tilt.
    double deccaTilt  = -0.5235987755982988;
};

/**
 * Interactive floor plan: drag dots to move, drag ring to rotate direction.
 * Draws room with aspect ratio, direction beams, transducers with rings.
 *
 * Supports up to 8 transducers (speakers + MAIN mics + OUTRIG mics +
 * AMBIENT mics). The OUTRIG and AMBIENT mic pairs are only drawn and
 * hit-tested when the corresponding `outrigVisible` / `ambientVisible`
 * flag is true — IRSynthComponent toggles these when the user enables
 * the corresponding mic path.
 */
class FloorPlanComponent : public juce::Component
{
public:
    FloorPlanComponent();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    void setParamsGetter (std::function<IRSynthParams()> fn) { getParams = std::move (fn); }

    // Global catch-all callback (fires for any placement change). Kept as the
    // primary wiring point — IRSynthComponent sets this to trigger onParamModifiedFn.
    void setOnPlacementChanged (std::function<void()> fn) { onPlacementChanged = std::move (fn); }

    // Per-group callbacks (fired in addition to onPlacementChanged).
    // Lets callers react to which mic pair changed if they need to.
    void setOnMainPlacementChanged   (std::function<void()> fn) { onMainPlacementChanged   = std::move (fn); }
    void setOnOutrigPlacementChanged (std::function<void()> fn) { onOutrigPlacementChanged = std::move (fn); }
    void setOnAmbientPlacementChanged(std::function<void()> fn) { onAmbientPlacementChanged= std::move (fn); }

    TransducerState getTransducerState() const { return transducers; }
    void setTransducerState (const TransducerState& t) { transducers = t; }

    // Visibility flags for the optional mic groups. When false, the pair is
    // not drawn and not hit-testable. IRSynthComponent flips these when the
    // user toggles outrig_enabled / ambient_enabled.
    bool outrigVisible  = false;
    bool ambientVisible = false;

    // Decca Tree capture mode. When true, the two MAIN mic pucks (indices 2/3)
    // are hidden and replaced by a single draggable + rotatable tree puck
    // drawn at (deccaCx, deccaCy). IRSynthComponent flips this when the user
    // toggles main_decca_enabled.
    bool deccaVisible   = false;

    // Option-mirror axis preference. Vertical mirrors a partner puck across
    // x = 0.5 (the historical default — useful for L/R pairs); Horizontal
    // mirrors across y = 0.5 (useful for matching front/back placements).
    // Selected from two icon buttons in IRSynthComponent and persisted in
    // the .ping sidecar via IRSynthParams::mirror_axis.
    enum class MirrorAxis { Vertical = 0, Horizontal = 1 };
    MirrorAxis mirrorAxis = MirrorAxis::Vertical;

private:
    std::function<IRSynthParams()> getParams;
    std::function<void()> onPlacementChanged;
    std::function<void()> onMainPlacementChanged;
    std::function<void()> onOutrigPlacementChanged;
    std::function<void()> onAmbientPlacementChanged;
    TransducerState transducers;
    int dragIndex = -1;           // 0..7
    bool dragRotate = false;
    double dragStartAngle = 0;
    // Option-mirror: when true for the current drag, the partner puck
    // (index ^ 1) is snapped to the horizontal mirror position/angle.
    // Latched at mouseDown from e.mods.isAltDown(), cleared on mouseUp.
    bool mirrorDrag = false;
    // Latched at mouseDown alongside mirrorDrag — captures which axis was
    // active so the cursor / paint guides / drag math all stay consistent
    // even if the user changes the axis mid-drag.
    MirrorAxis mirrorDragAxis = MirrorAxis::Vertical;
    // Pre-built custom cursors shown during an Option-mirror drag. Two
    // glyphs (vertical-axis arrows ←|→ and horizontal-axis arrows ↑—↓)
    // are constructed once in the ctor; see makeMirrorCursor().
    juce::MouseCursor mirrorCursorVertical;
    juce::MouseCursor mirrorCursorHorizontal;
    static juce::MouseCursor makeMirrorCursor (bool horizontal);

    struct HitResult { int index; bool rotate; };
    HitResult transducerHitTest (float mx, float my);
    bool transducerVisible (int index) const;
    juce::Point<float> transducerCanvasPos (int index) const;
    void canvasToNorm (float cx, float cy, double& nx, double& ny) const;
    bool isInsideRoom (double nx, double ny) const;

    static std::vector<std::pair<double, double>> roomPoly (const std::string& shape);
};
