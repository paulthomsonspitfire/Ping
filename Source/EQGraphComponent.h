#pragma once

#include <JuceHeader.h>

class PingProcessor;

/**  Five-band parametric EQ with draggable graph nodes and per-band rotary knobs
 *   arranged in a zig-zag (SSL-style) layout below the graph.
 *
 *   Band order (display / drag / column):
 *     0  Low Shelf  – params b3freq / b3gain / b3q
 *     1  Peak 1     – params b0freq / b0gain / b0q
 *     2  Peak 2     – params b1freq / b1gain / b1q
 *     3  Peak 3     – params b2freq / b2gain / b2q
 *     4  High Shelf – params b4freq / b4gain / b4q
 */
class EQGraphComponent : public juce::Component,
                         private juce::Timer
{
public:
    explicit EQGraphComponent (juce::AudioProcessorValueTreeState& apvts,
                               PingProcessor* processor = nullptr);

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

private:
    // ── timer ──────────────────────────────────────────────────────────
    void timerCallback() override;

    // ── graph helpers ──────────────────────────────────────────────────
    juce::Rectangle<float> getGraphArea() const;
    int   hitTestBand    (juce::Point<float> pos) const;
    void  freqGainToXY   (float freqHz, float gainDb, float& x, float& y) const;
    void  xyToFreqGain   (float x, float y, float& freqHz, float& gainDb) const;
    float getResponseAt  (float freqHz) const;

    // ── knob helpers ───────────────────────────────────────────────────
    void setupKnob (juce::Slider& s, int band, int param);
    void syncKnobsFromParams();

    // ── state ──────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState& apvts;
    PingProcessor* processor = nullptr;

    // ── spectrum analyser ──────────────────────────────────────────────
    static constexpr int spectrumFftOrder  = 11;
    static constexpr int spectrumFftSize   = 1 << spectrumFftOrder;
    static constexpr int spectrumScopeSize = 256;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> spectrumWindow;
    std::vector<float> spectrumFftData;
    std::vector<float> spectrumScopeData;
    void drawNextFrameOfSpectrum();

    // ── 5-band EQ ──────────────────────────────────────────────────────
    static constexpr int numBands = 5;
    static const char* bandIds[5][3];  // [band][0=freq, 1=gain, 2=q]
    static const bool  bandIsShelf[5]; // true for bands 0 and 4

    // Per-band controls: [band][0=freq, 1=gain, 2=q]
    juce::Slider knobs[numBands][3];
    juce::Label  knobLabels[numBands][3];   // parameter name labels ("FREQ", "GAIN", etc.)
    juce::Label  knobReadouts[numBands][3]; // live value readouts (accent orange)
    juce::Label  bandNameLabel[numBands];

    void updateReadouts();

    int draggingBand = -1;
    int selectedBand = -1;

    // Cached graph rect (set in resized, used for paint + hit-test)
    juce::Rectangle<float> graphBounds;

    // ── constants ──────────────────────────────────────────────────────
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minGain = -12.0f;
    static constexpr float maxGain =  12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQGraphComponent)
};
