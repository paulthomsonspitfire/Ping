#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include "IRSynthEngine.h"
#include "FloorPlanComponent.h"

/**
 * UI component for the IR Synthesiser. Provides controls for room geometry,
 * materials, placement, and synthesis options. Runs synthesis on a background
 * thread and reports progress.
 */
class IRSynthComponent : public juce::Component,
                         private juce::Timer,
                         private juce::Button::Listener,
                         private juce::ComboBox::Listener
{
public:
    /** Called when synthesis completes. Run on message thread. */
    using OnCompleteFn = std::function<void (const IRSynthResult&)>;

    explicit IRSynthComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Called when synthesis completes successfully – loads IR into processor, caller stays on page. */
    void setOnComplete (OnCompleteFn fn) { onComplete = std::move (fn); }

    /** Called when user clicks Done/Back – return to main UI. */
    void setOnDone (std::function<void()> fn) { onDoneFn = std::move (fn); }

    /** Called when user saves a synthesized IR. Passes name; caller saves from processor. */
    void setOnSaveIR (std::function<void (const juce::String&)> fn) { onSaveIRFn = std::move (fn); }

    /** Called when user selects an IR to load. Passes 0-based index into the list. */
    void setOnLoadIR (std::function<void (int)> fn) { onLoadIRFn = std::move (fn); }

    /** Refresh the IR combo from the given names (e.g. from IRManager). */
    void setIRList (const juce::StringArray& names);

    /** Set the IR combo to show the given display name (e.g. when loading from main UI). */
    void setSelectedIRDisplayName (const juce::String& name);

    /** Current params (read from UI). */
    IRSynthParams getParams() const;

    /** Update UI from params (e.g. when loading a preset). */
    void setParams (const IRSynthParams& p);

private:
    void timerCallback() override;
    void buttonClicked (juce::Button* b) override;
    void comboBoxChanged (juce::ComboBox* combo) override;
    void startSynthesis();
    void onDone();
    void updateRT60Display();
    void updateProgress (double fraction, const juce::String& message);
    void layoutCharacterTab (juce::Rectangle<int> bounds);
    void layoutPlacementTab (juce::Rectangle<int> bounds);

    OnCompleteFn onComplete;
    std::function<void()> onDoneFn;
    std::function<void (const juce::String&)> onSaveIRFn;
    std::function<void (int)> onLoadIRFn;

    juce::TabbedComponent tabbedComponent { juce::TabbedButtonBar::TabsAtTop };
    juce::Component characterTab;
    juce::Component placementTab;
    juce::Viewport characterViewport;
    juce::Viewport placementViewport;
    juce::Component characterContent;
    juce::Component placementContent;
    FloorPlanComponent floorPlanComponent;

    // Room (Placement tab)
    juce::ComboBox shapeCombo;
    juce::Slider widthSlider, depthSlider, heightSlider;
    juce::Label widthLabel, depthLabel, heightLabel;
    juce::Label widthValueLabel, depthValueLabel, heightValueLabel;  // Editable number only

    // Surfaces (Character tab)
    juce::ComboBox floorCombo, ceilingCombo, wallCombo;
    juce::Label floorLabel, ceilingLabel, wallLabel;

    // Contents (Character tab)
    juce::Slider audienceSlider, diffusionSlider;
    juce::Label audienceLabel, diffusionLabel;
    juce::Label audienceReadout, diffusionReadout;

    // Interior (Character tab)
    juce::ComboBox vaultCombo;
    juce::Slider organSlider, balconiesSlider;
    juce::Label vaultLabel, organLabel, balconiesLabel;
    juce::Label organReadout, balconiesReadout;

    // Options (Character tab)
    juce::ComboBox micPatternCombo, sampleRateCombo;
    juce::ToggleButton erOnlyButton { "Early reflections only" };
    juce::Label micPatternLabel, sampleRateLabel;

    // Bottom bar
    juce::Label rt60Label;
    juce::Label rt60FreqLabels[6];   // 125, 250, 500, 1k, 2k, 4k
    juce::Label rt60Values[6];
    juce::ComboBox irCombo;
    juce::TextButton saveIRButton { "Save" };
    juce::TextButton previewButton { "Preview" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::TextButton doneButton { "Done" };

    juce::ThreadPool synthPool { 1 };
    std::atomic<bool> synthRunning { false };
    std::unique_ptr<IRSynthResult> pendingResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSynthComponent)
};
