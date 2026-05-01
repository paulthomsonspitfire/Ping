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
    ~IRSynthComponent() override;   // out-of-line so unique_ptr<MirrorAxisButton> can see the full type

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Called when synthesis completes successfully – loads IR into processor, caller stays on page. */
    void setOnComplete (OnCompleteFn fn) { onComplete = std::move (fn); }

    /** Discard the result of any in-flight Calculate IR job when it
        eventually completes, instead of installing it in the convolvers.
        Call this before loading a new preset or a new IR from disk so a
        pending background synth can't overwrite the freshly-loaded IR.
        Safe to call whether or not a job is actually running — a no-op
        if nothing is pending. */
    void invalidatePendingSynth() noexcept { pendingSynthInvalidated.store (true); }

    /** Enable/disable every user-interactive control in the IR Synth panel
        except the Calculate IR and Bake Balance buttons (which have their
        own `synthRunning` lifecycle) and the progress bar/label. Used to
        lock the UI for the ~1 s (typical) to ~10 s (worst case, big
        polygon room) while a Calculate IR job is running on the synth
        pool, so the user can't (a) navigate away via Main Menu and get
        confused, (b) change parameters that the in-flight synth is not
        using, or (c) Save the still-old IR from the bottom bar. */
    void setInteractionLocked (bool locked);

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

    /** Supplier polled by the GUI timer to populate the per-path filename labels
        beneath the Mic Paths strip. Called once per tick with the four MicPath
        values (0 = Main, 1 = Direct, 2 = Outrig, 3 = Ambient). Returns the
        display string for that path (e.g. "<empty>", "<unsaved>", "Venue_outrig").
        The editor binds this to PingProcessor::getPathIRDisplayName. */
    void setPathDisplayNameSupplier (std::function<juce::String (int)> fn) { pathDisplayNameSupplier = std::move (fn); }

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
                         // Horizontal Mic Paths strip: 3 columns of path-local controls
                         // (MAIN | OUTRIG | AMBIENT) sitting under the floor plan,
                         // above the LOADED row + bottom bar. DIRECT no longer has
                         // its own column — it's a checkbox in MAIN, next to Decca
                         // Tree (v2.13 UI compaction). Each column owns its own
                         // section header.
                         mainHeaderBounds,
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

    // ── Shape proportion sliders (v2.8.0 polygon geometry) ──────────────────
    // Visible only when the selected shape uses the corresponding parameter:
    //   Cathedral       → navePctSlider + trptPctSlider
    //   Fan / Shoebox   → taperSlider
    //   Octagonal       → cornerCutSlider (0..1 chamfer depth)
    //   Circular Hall   → cornerCutSlider (0 = rectangle, 1 = ellipse)
    // The readout labels are right-justified unlabelled slots (no editable
    // number) — unlike Width/Depth/Height these are dimensionless fractions.
    juce::Slider navePctSlider, trptPctSlider, taperSlider, cornerCutSlider;
    juce::Label  navePctLabel, trptPctLabel, taperLabel, cornerCutLabel;
    juce::Label  navePctReadout, trptPctReadout, taperReadout, cornerCutReadout;

    // Helper called from comboBoxChanged + setParams to toggle slider
    // visibility based on the current shape combo selection.
    void updateShapeProportionVisibility();

    // Option-mirror axis selector (two small icon buttons sitting under the
    // Room Geometry section). Vertical = mirror across x = 0.5 (default,
    // L/R pairs); Horizontal = mirror across y = 0.5 (front/back pairs).
    // The selected axis drives FloorPlanComponent::mirrorAxis directly and
    // is round-tripped via IRSynthParams::mirror_axis (UI-only, not used
    // by the synthesis engine).
    class MirrorAxisButton; // defined in .cpp — small custom-paint icon button
    std::unique_ptr<MirrorAxisButton> mirrorVerticalButton;
    std::unique_ptr<MirrorAxisButton> mirrorHorizontalButton;
    juce::Label mirrorAxisLabel;

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
    juce::ComboBox micPatternCombo;
    juce::ToggleButton erOnlyButton { "ER only" };
    juce::TextButton bakeBalanceButton { "Bake ER/Tail Bal" };
    juce::Label micPatternLabel;

    // Source radiation preset (instrument directivity model). Populated
    // from the SourceRadiation registry (built-in Phase 1 + JSON Phase 2).
    // Drives IRSynthParams::source_radiation. Default = Cardioid (legacy)
    // which is bit-identical to pre-v2.11 behaviour.
    juce::ComboBox sourceRadiationCombo;
    juce::Label    sourceRadiationLabel;

    // Source elevation tilt (v2.12). Per-source so L and R can be tipped
    // independently; auto-linked when monoSourceButton is on (R slider
    // mirrors L). Picking a Radiation preset auto-populates both sliders
    // with the preset's defaultTiltDeg. The sliders are greyed-out when
    // the selected radiation kind is Cardioid (legacy), since the legacy
    // path intentionally ignores tilt to preserve v2.11 bit-identity.
    juce::Slider sourceTiltLSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider sourceTiltRSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label  sourceTiltLLabel;
    juce::Label  sourceTiltRLabel;

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

    // monoSourceButton drives IRSynthParams::mono_source. When ON the engine
    // renders a single speaker (positioned at the L speaker puck) for both
    // L- and R-source slots in the 4-IR layout, eliminating inter-speaker
    // comb filtering when speakers are placed close together. The R speaker
    // puck is hidden on the floor plan while this is on. Off by default —
    // preserves bit-identity with pre-feature builds.
    juce::ToggleButton monoSourceButton { "Mono Src" };

    // Mic paths (DIRECT / OUTRIG / AMBIENT). MAIN is always on for synthesis
    // (there is always a MAIN pair) — its mixer strip handles on/off.
    //
    // DIRECT has no position/pattern/height controls — it reuses the MAIN
    // pair. OUTRIG and AMBIENT each expose a pattern combo and height
    // slider; their L/R x/y positions come from the FloorPlanComponent.
    // DIRECT enable lives in the MAIN column next to Decca Tree (v2.13 UI
    // compaction), so its label is "Direct" rather than the bare "Enabled"
    // used by OUTRIG/AMBIENT (which still sit under their own column headers).
    juce::ToggleButton directEnableButton  { "Direct" };
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

    // Information label kept (zero-sized) in the MAIN column of the strip.
    // DIRECT no longer has its own column or info label (v2.13 UI compaction).
    juce::Label mainPathInfoLabel;

    // Filename display labels — one per mic path, drawn in the row above the
    // bottom bar. Populated by polling pathDisplayNameSupplier from
    // timerCallback(); show "<empty>", "<unsaved>", or the loaded IR's stem
    // (suffix preserved for aux paths so the user can verify the actual file
    // backing each slot). Each label is tinted with its mic-path mixer accent
    // colour (see MicMixerComponent kAccent*) so the row reads as one piece
    // with the mixer on the main page.
    juce::Label pathNameLabels[4];
    juce::Label loadedRowLabel;  // "LOADED:" caption on the left of the row
    std::function<juce::String (int)> pathDisplayNameSupplier;

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
    // When set true, the async completion of an in-flight Calculate IR job
    // will skip calling onComplete() and leave whatever convolvers / params
    // the plugin currently has in place. Used to defuse the race where the
    // user selects a new preset between clicking Calculate and the background
    // synthIR finishing — without this guard the stale result fires *after*
    // setStateInformation has installed the new preset's IR and silently
    // overwrites it with the pre-switch preset's synthesised buffer.
    std::atomic<bool> pendingSynthInvalidated { false };
    std::unique_ptr<IRSynthResult> pendingResult;
    IRSynthParams lastRenderParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRSynthComponent)
};
