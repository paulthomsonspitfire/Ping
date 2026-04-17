#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
 * MicMixerComponent — four vertical mixer strips side-by-side, one per mic path
 * (MAIN, DIRECT, OUTRIG, AMBIENT). Each strip contains:
 *   - Power toggle  (enables/disables the path's contribution)
 *   - Pan knob      (constant-power pan, -1..+1)
 *   - Vertical gain fader + L/R peak meters
 *   - Gain readout  (dB)
 *   - Mute / Solo / HP buttons
 *
 * All controls bind to APVTS parameters added in C7. Peak meters are driven
 * from the processor's per-path atomic peaks at ~30 Hz via an internal timer.
 *
 * Layout is fixed for 300×153 (the former OutputLevelMeter footprint) but
 * scales proportionally if resized.
 */
class MicMixerComponent : public juce::Component,
                          private juce::Timer
{
public:
    MicMixerComponent (PingProcessor& processor);
    ~MicMixerComponent() override;

    void paint    (juce::Graphics&) override;
    void resized()                   override;

private:
    void timerCallback() override;

    // ── Per-strip data ──────────────────────────────────────────────────────
    struct StripIDs
    {
        const char* label;                      // "MAIN", "DIRECT", …
        const char* onID;
        const char* gainID;
        const char* panID;
        const char* muteID;
        const char* soloID;
        const char* hpID;
        PingProcessor::MicPath path;
        juce::Colour accent;                    // strip header accent
    };

    struct Strip
    {
        // Child UI
        juce::Label         nameLabel;
        juce::ToggleButton  powerBtn;
        juce::Slider        panKnob;
        juce::Slider        gainFader;
        juce::Label         gainReadout;
        juce::TextButton    muteBtn      { "M" };
        juce::TextButton    soloBtn      { "S" };
        juce::TextButton    hpBtn        { "HP" };

        // APVTS attachments (owning)
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hpAttach;

        // Live peak values used by paint() — ticked by timerCallback.
        // Stored in linear (not dB) to match processor getPathPeak().
        float peakL = 0.f, peakR = 0.f;
        // Smoothed display values (with slow decay) for paint
        float displayL = 0.f, displayR = 0.f;

        // Per-strip bounds (computed in resized)
        juce::Rectangle<int> stripBounds;
        juce::Rectangle<int> faderBounds;
        juce::Rectangle<int> meterBounds;
    };

    void initStrip (int idx, const StripIDs& ids);
    void layoutStrip (Strip& s, juce::Rectangle<int> area);
    void paintStripChrome (juce::Graphics& g, const Strip& s, const StripIDs& ids);
    void paintMeters (juce::Graphics& g, const Strip& s);

    PingProcessor& processor;
    std::array<Strip, 4>    strips;
    std::array<StripIDs, 4> stripIDs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicMixerComponent)
};
