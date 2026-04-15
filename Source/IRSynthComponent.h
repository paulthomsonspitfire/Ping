#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <utility>
#include "IRSynthEngine.h"
#include "FloorPlanComponent.h"

/**
 * UI component for the IR Synthesiser. Provides controls for room geometry,
 * materials, placement, and synthesis options. Runs synthesis on a background
 * thread and reports progress.
 *
 * Single-page layout (no tabs): left column holds all acoustic-character and
 * room-geometry controls; right column shows the FloorPlanComponent at all times.
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

    /** Returns the current main-page ER/Tail levels in dB for baked renders. */
    void setBakeLevelsGetter (std::function<std::pair<float, float>()> fn) { bakeLevelsGetter = std::move (fn); }

    /** Called whenever any IR synth parameter is modified by the user. */
    void setOnParamModified (std::function<void()> fn) { onParamModifiedFn = std::move (fn); }

    /** Refresh the IR combo from the given names (e.g. from IRManager). */
    void setIRList (const juce::StringArray& names);

    /** Set the IR combo to show the given display name (e.g. when loading from main UI). */
    void setSelectedIRDisplayName (const juce::String& name);

    /** Current selected IR item id in the IR combo (1-based, 0 if none). */
    int getSelectedIRId() const { return irCombo.getSelectedId(); }

    /** Current selected IR item text in the IR combo (empty if no selected item). */
    juce::String getSelectedIRName() const
    {
        const int id = irCombo.getSelectedId();
        return id >= 1 ? irCombo.getItemText (id) : juce::String();
    }

    /** IR synth dirty state (parameters changed since last load/save/calculate). */
    bool isDirty() const { return dirty; }
    void setDirty (bool d) { dirty = d; }

    /** Current params (read from UI). */
    IRSynthParams getParams() const;
    const IRSynthParams& getLastRenderParams() const { return lastRenderParams; }

    /** Update UI from params (e.g. when loading a preset). */
    void setParams (const IRSynthParams& p);

private:
    void timerCallback() override;
    void buttonClicked (juce::Button* b) override;
    void comboBoxChanged (juce::ComboBox* combo) override;
    void startSynthesis (bool bakeCurrentBalance = false);
    void onDone();
    void updateRT60Display();
    void updateProgress (double fraction, const juce::String& message);

    /** Lay out all controls within the left column. Stores section header bounds for paint(). */
    void layoutControls (juce::Rectangle<int> leftBounds);

    OnCompleteFn onComplete;
    std::function<void()> onDoneFn;
    std::function<void (const juce::String&)> onSaveIRFn;
    std::function<void (int)> onLoadIRFn;
    std::function<std::pair<float, float>()> bakeLevelsGetter;
    std::function<void()> onParamModifiedFn;

    // Background texture (same brushed-steel image as the main plugin UI)
    juce::Image bgTexture;

    // Section header bounds – set in layoutControls(), read back in paint()
    juce::Rectangle<int> surfacesHeaderBounds, contentsHeaderBounds,
                         interiorHeaderBounds,  optionsHeaderBounds,
                         roomHeaderBounds;

    // Right-column floor-plan visualiser (always visible, not tab-gated)
    FloorPlanComponent floorPlanComponent;

    // Room geometry (previously "Placement" tab)
    juce::ComboBox shapeCombo;
    juce::Slider widthSlider, depthSlider, heightSlider;
    juce::Label widthLabel, depthLabel, heightLabel;
    juce::Label widthValueLabel, depthValueLabel, heightValueLabel;  // Editable number only

    // Surfaces (previously "Character" tab)
    juce::ComboBox floorCombo, ceilingCombo, wallCombo;
    juce::Label floorLabel, ceilingLabel, wallLabel;
    juce::Slider windowsSlider;
    juce::Label windowsLabel, windowsReadout;

    // Contents
    juce::Slider audienceSlider, diffusionSlider;
    juce::Label audienceLabel, diffusionLabel;
    juce::Label audienceReadout, diffusionReadout;

    // Interior
    juce::ComboBox vaultCombo;
    juce::Slider organSlider, balconiesSlider;
    juce::Label vaultLabel, organLabel, balconiesLabel;
    juce::Label organReadout, balconiesReadout;

    // Options
    juce::ComboBox micPatternCombo, sampleRateCombo;
    juce::ToggleButton erOnlyButton { "Early reflections only" };
    juce::TextButton bakeBalanceButton { "Bake ER/Tail Balance" };
    juce::Label micPatternLabel, sampleRateLabel;

    // Bottom bar
    juce::Label rt60Label;
    juce::Label rt60FreqLabels[6];   // 125, 250, 500, 1k, 2k, 4k
    juce::Label rt60Values[6];
    juce::ComboBox irCombo;
    juce::Label irPresetLabel;
    juce::TextButton saveIRButton { "Save" };
    juce::TextButton previewButton { "Calculate IR" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::TextButton doneButton { "Main Menu" };

    bool dirty = false;
    juce::String cleanIRName;

    juce::ThreadPool synthPool { 1 };
    std::atomic<bool> synthRunning { false };
    std::unique_ptr<IRSynthResult> pendingResult;
    IRSynthParams lastRenderParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSynthComponent)
};
