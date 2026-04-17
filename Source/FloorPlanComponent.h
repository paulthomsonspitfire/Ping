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

    struct HitResult { int index; bool rotate; };
    HitResult transducerHitTest (float mx, float my);
    bool transducerVisible (int index) const;
    juce::Point<float> transducerCanvasPos (int index) const;
    void canvasToNorm (float cx, float cy, double& nx, double& ny) const;
    bool isInsideRoom (double nx, double ny) const;

    static std::vector<std::pair<double, double>> roomPoly (const std::string& shape);
};
