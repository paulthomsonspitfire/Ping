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
    void saveSynthIR (const juce::String& name);
    void finishSaveSynthIR (const juce::String& name);
    void setMainPanelControlsVisible (bool visible);

    PingProcessor& pingProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::ComboBox irCombo;
    juce::ComboBox presetCombo;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton reverseButton { "REVERSE" };
    juce::TextButton irSynthButton { "IR SYNTH" };

    juce::Slider dryWetSlider;
    juce::Slider predelaySlider, decaySlider, modDepthSlider;
    juce::Slider stretchSlider, widthSlider, modRateSlider;
    juce::Slider tailModSlider, delayDepthSlider, tailRateSlider;
    juce::Slider irInputGainSlider, irInputDriveSlider, outputGainSlider;
    juce::Slider erLevelSlider, tailLevelSlider;

    // Crossfeed controls (row 2)
    juce::Slider erCrossfeedDelaySlider, erCrossfeedAttSlider;
    juce::Slider tailCrossfeedDelaySlider, tailCrossfeedAttSlider;
    juce::ToggleButton erCrossfeedOnButton, tailCrossfeedOnButton;

    // Plate controls (row 3)
    juce::Slider plateDiffusionSlider, plateColourSlider, plateSizeSlider, plateIRFeedSlider;
    juce::ToggleButton plateOnButton;

    // Bloom controls (row 4)
    juce::Slider bloomSizeSlider, bloomFeedbackSlider, bloomTimeSlider, bloomIRFeedSlider, bloomVolumeSlider;
    juce::ToggleButton bloomOnButton;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dryWetAttach, predelayAttach, decayAttach;
    std::unique_ptr<SliderAttachment> stretchAttach, widthAttach, modDepthAttach, modRateAttach;
    std::unique_ptr<SliderAttachment> tailModAttach, delayDepthAttach, tailRateAttach;
    std::unique_ptr<SliderAttachment> irInputGainAttach, irInputDriveAttach, outputGainAttach;
    std::unique_ptr<SliderAttachment> erLevelAttach, tailLevelAttach;
    std::unique_ptr<SliderAttachment> erCrossfeedDelayAttach, erCrossfeedAttAttach;
    std::unique_ptr<SliderAttachment> tailCrossfeedDelayAttach, tailCrossfeedAttAttach;
    std::unique_ptr<ButtonAttachment> erCrossfeedOnAttach, tailCrossfeedOnAttach;
    std::unique_ptr<SliderAttachment> plateDiffusionAttach, plateColourAttach, plateSizeAttach, plateIRFeedAttach;
    std::unique_ptr<ButtonAttachment> plateOnAttach;
    std::unique_ptr<SliderAttachment> bloomSizeAttach, bloomFeedbackAttach, bloomTimeAttach, bloomIRFeedAttach, bloomVolumeAttach;
    std::unique_ptr<ButtonAttachment> bloomOnAttach;

    juce::Label dryWetLabel, predelayLabel, decayLabel, modDepthLabel;
    juce::Label stretchLabel, widthLabel, modRateLabel;
    juce::Label tailModLabel, delayDepthLabel, tailRateLabel;
    juce::Label irInputGainLabel, irInputDriveLabel, outputGainLabel;
    juce::Label erLevelLabel, tailLevelLabel;
    juce::Label erCrossfeedDelayLabel, erCrossfeedAttLabel;
    juce::Label tailCrossfeedDelayLabel, tailCrossfeedAttLabel;
    juce::Label plateDiffusionLabel, plateColourLabel, plateSizeLabel, plateIRFeedLabel;
    juce::Label bloomSizeLabel, bloomFeedbackLabel, bloomTimeLabel, bloomIRFeedLabel, bloomVolumeLabel;

    juce::Label dryWetReadout, predelayReadout, decayReadout, modDepthReadout;
    juce::Label stretchReadout, widthReadout, modRateReadout;
    juce::Label tailModReadout, delayDepthReadout, tailRateReadout;
    juce::Label irInputGainReadout, irInputDriveReadout, outputGainReadout;
    juce::Label erLevelReadout, tailLevelReadout;
    juce::Label erCrossfeedDelayReadout, erCrossfeedAttReadout;
    juce::Label tailCrossfeedDelayReadout, tailCrossfeedAttReadout;
    juce::Label plateDiffusionReadout, plateColourReadout, plateSizeReadout, plateIRFeedReadout;
    juce::Label bloomSizeReadout, bloomFeedbackReadout, bloomTimeReadout, bloomIRFeedReadout, bloomVolumeReadout;

    PingLookAndFeel pingLook;
    EQGraphComponent eqGraph;
    LicenceScreen licenceScreen;

    void updateAllReadouts();

    WaveformComponent waveformComponent;
    IRSynthComponent irSynthComponent;
    OutputLevelMeter outputLevelMeter;
    juce::Label licenceLabel;
    juce::Label versionLabel;
    juce::Rectangle<int> spitfireBounds;
    juce::Rectangle<int> pingBounds;
    juce::Rectangle<int> irInputGroupBounds;    // area above the "IR Input" knob pair (text + line)
    juce::Rectangle<int> irControlsGroupBounds; // area above the "IR Controls" knob triple (text + line)
    juce::Rectangle<int> erCrossfadeGroupBounds;   // area above the ER Crossfade pair
    juce::Rectangle<int> tailCrossfadeGroupBounds; // area above the Tail Crossfade pair
    juce::Rectangle<int> plateGroupBounds;          // area above the Plate group
    juce::Rectangle<int> bloomGroupBounds;          // area above the Bloom group

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingEditor)
};
