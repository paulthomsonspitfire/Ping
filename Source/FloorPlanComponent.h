#pragma once

#include <JuceHeader.h>
#include <functional>
#include "IRSynthEngine.h"

struct TransducerState
{
    // Speakers: centre of room (y=0.5), 25% and 75% across, facing down
    // Mics: 1/5 up from bottom (y=0.8), 25% and 75% across; L faces up-left (-135°), R faces up-right (-45°)
    double cx[4]    = { 0.25, 0.75, 0.25, 0.75 };
    double cy[4]    = { 0.50, 0.50, 0.80, 0.80 };
    double angle[4] = { 1.57079632679, 1.57079632679, -2.35619449019, -0.785398163397 };  // π/2 down, -3π/4 up-left, -π/4 up-right
};

/**
 * Interactive floor plan: drag dots to move, drag ring to rotate direction.
 * Draws room with aspect ratio, direction beams, transducers with rings.
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
    TransducerState getTransducerState() const { return transducers; }
    void setTransducerState (const TransducerState& t) { transducers = t; }

private:
    std::function<IRSynthParams()> getParams;
    TransducerState transducers;
    int dragIndex = -1;           // 0..3
    bool dragRotate = false;
    double dragStartAngle = 0;

    struct HitResult { int index; bool rotate; };
    HitResult transducerHitTest (float mx, float my);
    juce::Point<float> transducerCanvasPos (int index) const;
    void canvasToNorm (float cx, float cy, double& nx, double& ny) const;
    bool isInsideRoom (double nx, double ny) const;

    static std::vector<std::pair<double, double>> roomPoly (const std::string& shape);
};
