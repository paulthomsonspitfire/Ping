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
                         private juce::Button::Listener
{
public:
    /** Called when synthesis completes. Run on message thread. */
    using OnCompleteFn = std::function<void (const IRSynthResult&)>;

    explicit IRSynthComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Set callback when user clicks Accept (IR ready to load). */
    void setOnComplete (OnCompleteFn fn) { onComplete = std::move (fn); }

    /** Set callback when user clicks Cancel (optional, e.g. to hide dialog). */
    void setOnCancel (std::function<void()> fn) { onCancelFn = std::move (fn); }

    /** Current params (read from UI). */
    IRSynthParams getParams() const;

    /** Update UI from params (e.g. when loading a preset). */
    void setParams (const IRSynthParams& p);

private:
    void timerCallback() override;
    void buttonClicked (juce::Button* b) override;
    void startSynthesis();
    void onAccept();
    void onCancel();
    void updateRT60Display();
    void updateProgress (double fraction, const juce::String& message);
    void layoutCharacterTab (juce::Rectangle<int> bounds);
    void layoutPlacementTab (juce::Rectangle<int> bounds);

    OnCompleteFn onComplete;
    std::function<void()> onCancelFn;

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
    juce::TextButton previewButton { "Preview" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::TextButton acceptButton { "Accept" };
    juce::TextButton cancelButton { "Cancel" };

    juce::ThreadPool synthPool { 1 };
    std::atomic<bool> synthRunning { false };
    std::unique_ptr<IRSynthResult> pendingResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSynthComponent)
};
