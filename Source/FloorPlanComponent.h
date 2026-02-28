#pragma once

#include <JuceHeader.h>
#include <functional>
#include "IRSynthEngine.h"

struct TransducerState
{
    double cx[4]  = { 0.35, 0.65, 0.40, 0.60 };
    double cy[4]  = { 0.30, 0.30, 0.70, 0.70 };
    double angle[4] = { 1.5 * 3.14159, 1.5 * 3.14159, 0.5 * 3.14159, 0.5 * 3.14159 };
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
