#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "IRManager.h"
#include "PingLookAndFeel.h"
#include "EQGraphComponent.h"
#include "PresetManager.h"
#include "WaveformComponent.h"
#include "LicenceScreen.h"
#include "OutputLevelMeter.h"
#include "IRSynthComponent.h"

class PingEditor : public juce::AudioProcessorEditor,
                   private juce::ComboBox::Listener,
                   private juce::Timer,
                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PingEditor (PingProcessor&);
    ~PingEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void comboBoxChanged (juce::ComboBox* combo) override;
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void refreshIRList();
    void loadSelectedIR();
    void updateWaveform();
    void refreshPresetList();
    void loadPreset (const juce::String& name);
    void savePreset (const juce::String& name);
    void setMainPanelControlsVisible (bool visible);

    PingProcessor& pingProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::ComboBox irCombo;
    juce::ComboBox presetCombo;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton reverseButton { "REVERSE" };
    juce::TextButton irSynthButton { "IR Synth" };

    juce::Slider dryWetSlider;
    juce::Slider predelaySlider, decaySlider, modDepthSlider;
    juce::Slider stretchSlider, widthSlider, modRateSlider;
    juce::Slider tailModSlider, delayDepthSlider, tailRateSlider;
    juce::Slider irInputGainSlider, irInputDriveSlider;
    juce::Slider erLevelSlider, tailLevelSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> dryWetAttach, predelayAttach, decayAttach;
    std::unique_ptr<SliderAttachment> stretchAttach, widthAttach, modDepthAttach, modRateAttach;
    std::unique_ptr<SliderAttachment> tailModAttach, delayDepthAttach, tailRateAttach;
    std::unique_ptr<SliderAttachment> irInputGainAttach, irInputDriveAttach;
    std::unique_ptr<SliderAttachment> erLevelAttach, tailLevelAttach;

    juce::Label dryWetLabel, predelayLabel, decayLabel, modDepthLabel;
    juce::Label stretchLabel, widthLabel, modRateLabel;
    juce::Label tailModLabel, delayDepthLabel, tailRateLabel;
    juce::Label irInputGainLabel, irInputDriveLabel;
    juce::Label erLevelLabel, tailLevelLabel;

    juce::Label dryWetReadout, predelayReadout, decayReadout, modDepthReadout;
    juce::Label stretchReadout, widthReadout, modRateReadout;
    juce::Label tailModReadout, delayDepthReadout, tailRateReadout;
    juce::Label irInputGainReadout, irInputDriveReadout;
    juce::Label erLevelReadout, tailLevelReadout;

    PingLookAndFeel pingLook;
    EQGraphComponent eqGraph;
    LicenceScreen licenceScreen;

    void updateAllReadouts();

    WaveformComponent waveformComponent;
    IRSynthComponent irSynthComponent;
    OutputLevelMeter outputLevelMeter;
    juce::Label licenceLabel;
    juce::Rectangle<int> spitfireBounds;
    juce::Rectangle<int> pingBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingEditor)
};
