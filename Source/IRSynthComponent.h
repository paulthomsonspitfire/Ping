#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <utility>
#include "IRSynthEngine.h"
#include "FloorPlanComponent.h"
#include "IRManager.h"

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

    /** Called when user clicks Export IR. Caller handles the file chooser. */
    void setOnExportIR (std::function<void()> fn) { onExportIRFn = std::move (fn); }

    /** Called when user clicks Import IR. Caller handles the file chooser. */
    void setOnImportIR (std::function<void()> fn) { onImportIRFn = std::move (fn); }

    /** Called when user selects an IR to load. Passes 0-based index into the list. */
    void setOnLoadIR (std::function<void (int)> fn) { onLoadIRFn = std::move (fn); }

    /** Returns the current main-page ER/Tail levels in dB for baked renders. */
    void setBakeLevelsGetter (std::function<std::pair<float, float>()> fn) { bakeLevelsGetter = std::move (fn); }

    /** Called whenever any IR synth parameter is modified by the user. */
    void setOnParamModified (std::function<void()> fn) { onParamModifiedFn = std::move (fn); }

    /** Refresh the IR combo from structured 4-channel entries (preserves category/factory sections). */
    void setIRList (const juce::Array<IRManager::IREntry>& entries);

    /** Set the IR combo to show the given display name (e.g. when loading from main UI). */
    void setSelectedIRDisplayName (const juce::String& name);

    /** Current selected IR item id in the IR combo (1-based, 0 if none). */
    int getSelectedIRId() const { return irCombo.getSelectedId(); }

    /** Current selected IR item text in the IR combo (empty if no selected item). */
    juce::String getSelectedIRName() const
    {
        const int id = irCombo.getSelectedId();
        return id >= 1 ? irCombo.getItemText (id - 1) : juce::String();
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

    /** Lay out the horizontal Mic Paths strip (MAIN / DIRECT / OUTRIG / AMBIENT columns)
        along the bottom of the content area. Stores per-column header bounds for paint(). */
    void layoutMicPathsStrip (juce::Rectangle<int> stripBounds);

    OnCompleteFn onComplete;
    std::function<void()> onDoneFn;
    std::function<void (const juce::String&)> onSaveIRFn;
    std::function<void()> onExportIRFn;
    std::function<void()> onImportIRFn;
    std::function<void (int)> onLoadIRFn;
    std::function<std::pair<float, float>()> bakeLevelsGetter;
    std::function<void()> onParamModifiedFn;

    // Background texture (same brushed-steel image as the main plugin UI)
    juce::Image bgTexture;

    // Section header bounds – set in layoutControls()/layoutMicPathsStrip(), read back in paint()
    juce::Rectangle<int> surfacesHeaderBounds, contentsHeaderBounds,
                         interiorHeaderBounds,  optionsHeaderBounds,
                         roomHeaderBounds,
                         // Horizontal Mic Paths strip: 4 columns of path-local controls
                         // sitting between the floor-plan / left column above and the
                         // bottom bar below. Each column owns its own section header.
                         mainHeaderBounds,
                         directHeaderBounds,
                         outrigHeaderBounds,
                         ambientHeaderBounds,
                         micPathsStripBounds;   // whole-strip rect (for top separator)

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
    juce::ToggleButton erOnlyButton { "ER only" };
    juce::TextButton bakeBalanceButton { "Bake ER/Tail Bal" };
    juce::Label micPatternLabel, sampleRateLabel;

    // ── Experimental early-reflection A/B toggles ────────────────────────────
    // directMaxOrderCombo drives IRSynthParams::direct_max_order (0 / 1 / 2).
    // lambertScatterButton drives IRSynthParams::lambert_scatter_enabled.
    // spkDirFullButton     drives IRSynthParams::spk_directivity_full.
    // All three only affect newly calculated IRs (like every other Options
    // control).
    juce::ComboBox     directMaxOrderCombo;
    juce::Label        directMaxOrderLabel;
    juce::ToggleButton lambertScatterButton { "Lambert Scatter" };
    juce::ToggleButton spkDirFullButton     { "Full Spkr Dir" };

    // Mic paths (DIRECT / OUTRIG / AMBIENT). MAIN is always on for synthesis
    // (there is always a MAIN pair) — its mixer strip handles on/off.
    //
    // DIRECT has no position/pattern/height controls — it reuses the MAIN
    // pair. OUTRIG and AMBIENT each expose a pattern combo and height
    // slider; their L/R x/y positions come from the FloorPlanComponent.
    juce::ToggleButton directEnableButton  { "Enabled" };
    juce::ToggleButton outrigEnableButton  { "Enabled" };
    juce::ToggleButton ambientEnableButton { "Enabled" };

    // Decca Tree capture mode (MAIN + DIRECT only). When enabled, the two
    // MAIN mic pucks on the floor plan are hidden and replaced by a single
    // draggable + rotatable Decca tree puck. See Source/IRSynthEngine.h and
    // CLAUDE.md's "Decca Tree capture mode" section.
    juce::ToggleButton deccaEnableButton { "Decca Tree" };

    // Decca centre-mic fill level (p.decca_centre_gain). Controls how much of
    // the centre mic is summed into L/R outputs. 0 = off (tree behaves as a
    // bare outer pair); 0.5 (default, −6 dB) = classical tracking balance;
    // 0.707 (−3 dB) = max, previous fixed value.
    juce::Slider deccaCentreGainSlider;
    juce::Label  deccaCentreGainLabel;
    juce::Label  deccaCentreGainReadout;

    // Decca outer-mic toe-out (±0..90° each side from the forward axis). 0 =
    // all three mics point forward (collapses the tree); ±45° = matches the
    // classic main pair; ±90° (default) = outer mics fully side-firing for
    // maximum stereo separation. See IRSynthParams::decca_toe_out.
    juce::Slider deccaToeOutSlider;
    juce::Label  deccaToeOutLabel;
    juce::Label  deccaToeOutReadout;

    juce::ComboBox outrigPatternCombo, ambientPatternCombo;
    juce::Label    outrigPatternLabel, ambientPatternLabel;
    juce::Slider   outrigHeightSlider, ambientHeightSlider;
    juce::Label    outrigHeightLabel, ambientHeightLabel;
    juce::Label    outrigHeightReadout, ambientHeightReadout;

    // 3D mic-tilt sliders. One slider per mic-paths column controls the
    // elevation of that path's mics (range −90..+90°, 1° step). MAIN's tilt
    // slider drives both micl_tilt and micr_tilt as a pair (they are always
    // moved together — the L/R rig is a rigid array on the floor plan, so
    // an independent L vs R tilt has no acoustic meaning we can model
    // here). When Decca mode is active the same MAIN slider also drives
    // decca_tilt (rigid 3-mic array). DIRECT shares MAIN's tilt and so has
    // no slider of its own. OUTRIG and AMBIENT each get one slider that
    // drives both L and R tilts of that pair. See IRSynthParams::*_tilt
    // and TransducerState::tilt[] / deccaTilt for the underlying fields.
    juce::Slider mainTiltSlider, outrigTiltSlider, ambientTiltSlider;
    juce::Label  mainTiltLabel,  outrigTiltLabel,  ambientTiltLabel;
    juce::Label  mainTiltReadout, outrigTiltReadout, ambientTiltReadout;

    // Information labels shown in the MAIN and DIRECT columns of the strip.
    // MAIN has no per-column controls (it is always synthesised and configured
    // elsewhere); DIRECT has only an enable toggle (it inherits MAIN pair).
    juce::Label mainPathInfoLabel, directPathInfoLabel;

    // Bottom bar
    juce::Label rt60Label;
    juce::Label rt60FreqLabels[6];   // 125, 250, 500, 1k, 2k, 4k
    juce::Label rt60Values[6];
    juce::ComboBox irCombo;
    juce::Label irPresetLabel;
    juce::TextButton saveIRButton { "Save" };
    juce::TextButton exportIRButton { "Export" };
    juce::TextButton importIRButton { "Import" };
    juce::TextButton previewButton { "Calculate IR" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::TextButton doneButton { "Main Menu" };

    bool dirty = false;
    bool suppressingParamNotifications = false;
    juce::String cleanIRName;

    juce::ThreadPool synthPool { 1 };
    std::atomic<bool> synthRunning { false };
    std::unique_ptr<IRSynthResult> pendingResult;
    IRSynthParams lastRenderParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSynthComponent)
};
