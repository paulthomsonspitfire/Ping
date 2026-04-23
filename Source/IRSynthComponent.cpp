#include "IRSynthComponent.h"
#include "PingBinaryData.h"

// Small custom-paint button used for the Option-mirror axis selector.
// Renders a rounded rectangle with a dashed line through the centre — vertical
// or horizontal depending on the constructor flag. Highlights in the plugin
// accent colour when toggled on. Lives at the file scope so layoutControls()
// can layout it directly via the unique_ptr members declared in the header.
class IRSynthComponent::MirrorAxisButton : public juce::Button
{
public:
    explicit MirrorAxisButton (bool isHorizontal_)
        : juce::Button (isHorizontal_ ? "MirrorH" : "MirrorV"),
          isHorizontal (isHorizontal_)
    {
        setClickingTogglesState (true);
        setRadioGroupId (8211);   // arbitrary unique id — pairs the two buttons
        setTooltip (isHorizontal
                        ? "Option-drag mirrors mics across the horizontal centre line (front / back)"
                        : "Option-drag mirrors mics across the vertical centre line (left / right)");
    }

    void paintButton (juce::Graphics& g, bool /*isOver*/, bool /*isDown*/) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = getToggleState();

        const juce::Colour accent { 0xff8cd6ef };
        const juce::Colour dim    { 0xff707070 };
        const juce::Colour fg     = on ? accent : dim;

        // Soft-rounded box outline
        g.setColour (on ? accent.withAlpha (0.16f) : juce::Colour (0x18ffffff));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (fg.withAlpha (on ? 0.95f : 0.55f));
        g.drawRoundedRectangle (r, 3.0f, 1.2f);

        // Dashed centre line, oriented per-axis. Slightly inset from the box.
        g.setColour (fg);
        const float dashes[] = { 2.5f, 2.0f };
        if (isHorizontal)
        {
            const float yMid = r.getCentreY();
            const float x0 = r.getX() + 3.0f;
            const float x1 = r.getRight() - 3.0f;
            g.drawDashedLine (juce::Line<float> (x0, yMid, x1, yMid), dashes, 2, 1.4f);
        }
        else
        {
            const float xMid = r.getCentreX();
            const float y0 = r.getY() + 3.0f;
            const float y1 = r.getBottom() - 3.0f;
            g.drawDashedLine (juce::Line<float> (xMid, y0, xMid, y1), dashes, 2, 1.4f);
        }
    }

private:
    const bool isHorizontal;
};

namespace
{
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour accent      { 0xff8cd6ef };
    const juce::Colour textCol     { 0xffe8e8e8 };
    const juce::Colour textDim     { 0xff909090 };

    // v2.8.0: "L-shaped" removed, "Cylindrical" renamed to "Circular Hall".
    // setParams()/setComboTo() migrates legacy values before matching, so old
    // presets still surface the right UI selection.
    const char* const shapeOptions[] = {
        "Rectangular", "Fan / Shoebox", "Octagonal", "Circular Hall", "Cathedral"
    };
    constexpr int kNumShapeOptions = 5;
    const char* const materialOptions[] = {
        "Concrete / bare brick", "Painted plaster", "Hardwood floor", "Carpet (thin)",
        "Carpet (thick)", "Glass (large pane)", "Heavy curtains", "Acoustic ceiling tile",
        "Plywood panel", "Upholstered seats", "Bare wooden seats", "Water / pool surface",
        "Rough stone / rock", "Exposed brick (rough)"
    };
    const char* const vaultOptions[] = {
        "None (flat)", "Shallow barrel vault", "Deep pointed vault  (gothic)",
        "Groin / cross vault  (Lyndhurst Hall)", "Fan vault  (King's College)",
        "Coffered dome  (circular hall)"
    };
    const char* const micOptions[] = { "omni", "omni (MK2H)", "subcardioid", "wide cardioid (MK21)", "cardioid (LDC)", "cardioid (SDC)", "figure8" };

    void addOptions (juce::ComboBox& combo, const char* const* opts, int n)
    {
        combo.clear();
        for (int i = 0; i < n; ++i)
            combo.addItem (opts[i], i + 1);
    }

    juce::String collapseSpaces (const juce::String& s)
    {
        juce::String r;
        bool lastWasSpace = false;
        for (int i = 0; i < s.length(); ++i)
        {
            auto c = s[i];
            if (c == ' ') { if (! lastWasSpace) r += c; lastWasSpace = true; }
            else           { r += c; lastWasSpace = false; }
        }
        return r;
    }

    void setComboTo (juce::ComboBox& combo, const juce::String& value, const char* const* opts, int n)
    {
        auto normValue = collapseSpaces (value);
        for (int i = 0; i < n; ++i)
            if (normValue == collapseSpaces (opts[i]))
            {
                combo.setSelectedId (i + 1, juce::dontSendNotification);
                return;
            }
        combo.setSelectedId (1, juce::dontSendNotification);
    }

    juce::String comboSelection (const juce::ComboBox& combo, const char* const* opts, int n)
    {
        int id = combo.getSelectedId();
        if (id >= 1 && id <= n)
            return opts[id - 1];
        return opts[0];
    }
}

// Out-of-line destructor so unique_ptr<MirrorAxisButton> sees the complete
// type defined above in this translation unit.
IRSynthComponent::~IRSynthComponent() = default;

IRSynthComponent::IRSynthComponent()
{
    setOpaque (true);

    // Load background texture (same brushed-steel image as the main plugin UI)
    bgTexture = juce::ImageCache::getFromMemory (BinaryData::texture_bg_jpg,
                                                  BinaryData::texture_bg_jpgSize);

    // Room geometry (previously "Placement" tab)
    addOptions (shapeCombo, shapeOptions, kNumShapeOptions);
    shapeCombo.setSelectedId (1, juce::dontSendNotification);

    widthSlider.setRange (0.5, 50.0, 0.5);
    depthSlider.setRange (0.5, 50.0, 0.5);
    heightSlider.setRange (1.0, 30.0, 0.5);
    widthSlider.setValue (28.0);
    depthSlider.setValue (16.0);
    heightSlider.setValue (12.0);

    for (auto* s : { &widthSlider, &depthSlider, &heightSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }

    auto setupDimRow = [&] (juce::Label& nameLabel, juce::Label& valueLabel, juce::Slider& s, const char* title)
    {
        nameLabel.setText (title, juce::dontSendNotification);
        nameLabel.setEditable (false);
        valueLabel.setText (juce::String (s.getValue(), 1), juce::dontSendNotification);
        valueLabel.setEditable (true);
        valueLabel.setJustificationType (juce::Justification::centredLeft);
        valueLabel.onTextChange = [&valueLabel, &s]
        {
            double v = valueLabel.getText().getDoubleValue();
            if (v > 0)
            {
                s.setValue (juce::jlimit (s.getMinimum(), s.getMaximum(), v), juce::dontSendNotification);
            }
        };
    };
    setupDimRow (widthLabel, widthValueLabel, widthSlider, "Width");
    setupDimRow (depthLabel, depthValueLabel, depthSlider, "Depth");
    setupDimRow (heightLabel, heightValueLabel, heightSlider, "Height");

    // Shape proportion sliders (v2.8.0). Ranges match the documented clamps in
    // makeWalls2D / IRSynthParams. Defaults match IRSynthParams struct so a
    // fresh instance shows the same value the engine will use.
    navePctSlider  .setRange (0.15, 0.50, 0.01); navePctSlider  .setValue (0.30);
    trptPctSlider  .setRange (0.20, 0.45, 0.01); trptPctSlider  .setValue (0.35);
    taperSlider    .setRange (0.00, 0.70, 0.01); taperSlider    .setValue (0.30);
    cornerCutSlider.setRange (0.00, 1.00, 0.01); cornerCutSlider.setValue (0.414);
    for (auto* s : { &navePctSlider, &trptPctSlider, &taperSlider, &cornerCutSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }
    navePctLabel  .setText ("Nave",      juce::dontSendNotification);
    trptPctLabel  .setText ("Transept",  juce::dontSendNotification);
    taperLabel    .setText ("Taper",     juce::dontSendNotification);
    cornerCutLabel.setText ("Cnr Cut",   juce::dontSendNotification);

    // Surfaces
    addOptions (floorCombo, materialOptions, 14);
    addOptions (ceilingCombo, materialOptions, 14);
    addOptions (wallCombo, materialOptions, 14);
    floorCombo.setSelectedId (3, juce::dontSendNotification);   // Hardwood floor
    ceilingCombo.setSelectedId (2, juce::dontSendNotification); // Painted plaster
    wallCombo.setSelectedId (1, juce::dontSendNotification);   // Concrete / bare brick

    floorLabel.setText ("Floor", juce::dontSendNotification);
    ceilingLabel.setText ("Ceiling", juce::dontSendNotification);
    wallLabel.setText ("Walls", juce::dontSendNotification);
    windowsSlider.setRange (0.0, 1.0, 0.01);
    windowsSlider.setValue (0.27);
    windowsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    windowsSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    windowsSlider.setColour (juce::Slider::thumbColourId, accent);
    windowsSlider.setColour (juce::Slider::trackColourId, panelBorder);
    windowsLabel.setText ("Windows", juce::dontSendNotification);

    // Contents
    audienceSlider.setRange (0.0, 1.0, 0.01);
    diffusionSlider.setRange (0.0, 1.0, 0.01);
    audienceSlider.setValue (0.45);
    diffusionSlider.setValue (0.40);
    for (auto* s : { &audienceSlider, &diffusionSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }
    audienceLabel.setText ("Audience", juce::dontSendNotification);
    diffusionLabel.setText ("Diffusion", juce::dontSendNotification);

    // Interior
    addOptions (vaultCombo, vaultOptions, 6);
    vaultCombo.setSelectedId (4, juce::dontSendNotification);  // Groin / cross vault (Lyndhurst Hall)
    organSlider.setRange (0.0, 1.0, 0.01);
    balconiesSlider.setRange (0.0, 1.0, 0.01);
    organSlider.setValue (0.59);
    balconiesSlider.setValue (0.54);
    for (auto* s : { &organSlider, &balconiesSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }
    vaultLabel.setText ("Vault", juce::dontSendNotification);
    organLabel.setText ("Organ", juce::dontSendNotification);
    balconiesLabel.setText ("Balconies", juce::dontSendNotification);

    // Options
    addOptions (micPatternCombo, micOptions, 7);
    micPatternCombo.setSelectedId (5, juce::dontSendNotification); // cardioid (LDC) — 5th item after adding omni (MK2H) + wide cardioid (MK21)
    erOnlyButton.setToggleState (false, juce::dontSendNotification);
    bakeBalanceButton.addListener (this);
    bakeBalanceButton.setColour (juce::TextButton::buttonColourId, panelBg);
    bakeBalanceButton.setColour (juce::TextButton::textColourOffId, textDim);
    micPatternLabel.setText ("Pattern", juce::dontSendNotification);

    // Experimental early-reflection A/B toggles.
    directMaxOrderCombo.addItem ("0 (direct only)",     1);
    directMaxOrderCombo.addItem ("1 (+ first-order)",   2);
    directMaxOrderCombo.addItem ("2 (+ second-order)",  3);
    directMaxOrderCombo.setSelectedId (2, juce::dontSendNotification); // default 1
    directMaxOrderLabel.setText ("DIRECT reach", juce::dontSendNotification);
    directMaxOrderLabel.setColour (juce::Label::textColourId, textDim);
    for (auto* b : { &lambertScatterButton, &spkDirFullButton })
    {
        b->setColour (juce::ToggleButton::textColourId,         textDim);
        b->setColour (juce::ToggleButton::tickColourId,         accent);
        b->setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff404040));
    }
    lambertScatterButton.setToggleState (true,  juce::dontSendNotification);  // current default
    spkDirFullButton    .setToggleState (false, juce::dontSendNotification);  // current default

    // ── Mic path toggles + OUTRIG / AMBIENT pattern + height controls ────────
    // Defaults mirror IRSynthParams: all paths off until explicitly enabled;
    // OUTRIG height 3 m (same as MAIN mics), pattern cardioid (LDC);
    // AMBIENT height 6 m, pattern omni.
    for (auto* b : { &directEnableButton, &outrigEnableButton, &ambientEnableButton, &deccaEnableButton })
    {
        b->setToggleState (false, juce::dontSendNotification);
        b->setColour (juce::ToggleButton::textColourId, textDim);
        b->setColour (juce::ToggleButton::tickColourId, accent);
        b->setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff404040));
    }

    addOptions (outrigPatternCombo,  micOptions, 7);
    addOptions (ambientPatternCombo, micOptions, 7);
    outrigPatternCombo.setSelectedId  (5, juce::dontSendNotification); // cardioid (LDC)
    ambientPatternCombo.setSelectedId (1, juce::dontSendNotification); // omni

    outrigHeightSlider.setRange  (0.5, 30.0, 0.1);
    ambientHeightSlider.setRange (0.5, 30.0, 0.1);
    outrigHeightSlider.setValue  (3.0, juce::dontSendNotification);
    ambientHeightSlider.setValue (6.0, juce::dontSendNotification);

    // Centre fill: clamp range to engine's documented upper bound (0.707, the
    // previous fixed value). Default 0.5 (−6 dB) — see IRSynthParams.
    deccaCentreGainSlider.setRange (0.0, 0.707, 0.001);
    deccaCentreGainSlider.setValue (0.5, juce::dontSendNotification);

    // Decca toe-out: expose as degrees on the slider (0..90°). Stored internally
    // in radians in IRSynthParams::decca_toe_out. Default 90° (fully side-firing).
    deccaToeOutSlider.setRange (0.0, 90.0, 1.0);
    deccaToeOutSlider.setValue (90.0, juce::dontSendNotification);

    // Mic-tilt sliders (one per column). Range −90..+90° (1° step) with a
    // default of −30° matching IRSynthParams::micl_tilt / micr_tilt /
    // outrig_*tilt / ambient_*tilt / decca_tilt. Stored internally in
    // radians; the slider exposes degrees for user-facing readability. The
    // MAIN slider drives both mic L/R tilts (rigid pair) AND decca_tilt
    // (rigid 3-mic array) so a single control covers both modes — see the
    // 3D mic-tilt section in CLAUDE.md.
    for (auto* s : { &mainTiltSlider, &outrigTiltSlider, &ambientTiltSlider })
    {
        s->setRange (-90.0, 90.0, 1.0);
        s->setValue (-30.0, juce::dontSendNotification);
    }

    for (auto* s : { &outrigHeightSlider, &ambientHeightSlider, &deccaCentreGainSlider,
                     &deccaToeOutSlider, &mainTiltSlider, &outrigTiltSlider, &ambientTiltSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }

    outrigPatternLabel.setText  ("Pattern",  juce::dontSendNotification);
    ambientPatternLabel.setText ("Pattern",  juce::dontSendNotification);
    outrigHeightLabel.setText   ("Height",   juce::dontSendNotification);
    ambientHeightLabel.setText  ("Height",   juce::dontSendNotification);
    outrigHeightReadout.setText  (juce::String (outrigHeightSlider.getValue(), 1) + " m",
                                  juce::dontSendNotification);
    ambientHeightReadout.setText (juce::String (ambientHeightSlider.getValue(), 1) + " m",
                                  juce::dontSendNotification);

    deccaCentreGainLabel.setText  ("C mic", juce::dontSendNotification);
    // Readout in dB to match mixing intuition (0.5 → −6 dB, 0.707 → −3 dB).
    // Compact format (no space, no decimal) to fit the 48 px readout column.
    {
        const double v = deccaCentreGainSlider.getValue();
        const auto txt = (v <= 1e-6) ? juce::String ("off")
                                     : juce::String ((int) std::round (20.0 * std::log10 (v))) + "dB";
        deccaCentreGainReadout.setText (txt, juce::dontSendNotification);
    }

    deccaToeOutLabel.setText ("Splay", juce::dontSendNotification);
    {
        const int deg = (int) std::round (deccaToeOutSlider.getValue());
        const auto txt = (deg == 0) ? juce::String ("off")
                                    : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (deg) + juce::String::fromUTF8 ("\xc2\xb0");
        deccaToeOutReadout.setText (txt, juce::dontSendNotification);
    }

    // Tilt labels + readouts. Common signed-degree formatter ("-30°", "+12°", "0°").
    auto formatTilt = [] (double v) -> juce::String
    {
        const int deg = (int) std::round (v);
        return juce::String (deg) + juce::String::fromUTF8 ("\xc2\xb0");
    };
    mainTiltLabel.setText    ("Tilt", juce::dontSendNotification);
    outrigTiltLabel.setText  ("Tilt", juce::dontSendNotification);
    ambientTiltLabel.setText ("Tilt", juce::dontSendNotification);
    mainTiltReadout.setText    (formatTilt (mainTiltSlider.getValue()),    juce::dontSendNotification);
    outrigTiltReadout.setText  (formatTilt (outrigTiltSlider.getValue()),  juce::dontSendNotification);
    ambientTiltReadout.setText (formatTilt (ambientTiltSlider.getValue()), juce::dontSendNotification);

    // Add all controls directly (no tabs / viewports)
    addAndMakeVisible (floorPlanComponent);
    floorPlanComponent.setParamsGetter ([this] { return getParams(); });
    floorPlanComponent.setOnPlacementChanged ([this] { if (onParamModifiedFn) onParamModifiedFn(); });

    auto notifyParamChanged = [this] { if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn(); };
    for (auto* s : { &widthSlider, &depthSlider, &heightSlider, &windowsSlider,
                     &audienceSlider, &diffusionSlider, &organSlider, &balconiesSlider,
                     &outrigHeightSlider, &ambientHeightSlider, &deccaCentreGainSlider,
                     &deccaToeOutSlider })
        s->onValueChange = notifyParamChanged;

    // Width/Depth/Height also need to repaint the floor plan because the
    // shape outline is parametric in W/D from v2.8.0.
    auto wdhRepaint = [this]
    {
        floorPlanComponent.repaint();
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    widthSlider .onValueChange = wdhRepaint;
    depthSlider .onValueChange = wdhRepaint;
    heightSlider.onValueChange = wdhRepaint;

    // Shape proportion sliders — each updates its own readout, repaints the
    // floor plan (so the shape outline reflects the new proportion), then
    // notifies the editor so the IR-synth-dirty flag flips.
    auto wirePropSlider = [this] (juce::Slider& s, juce::Label& readout, int decimals)
    {
        s.onValueChange = [this, &s, &readout, decimals]
        {
            readout.setText (juce::String (s.getValue(), decimals), juce::dontSendNotification);
            floorPlanComponent.repaint();
            if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
        };
    };
    wirePropSlider (navePctSlider,   navePctReadout,   2);
    wirePropSlider (trptPctSlider,   trptPctReadout,   2);
    wirePropSlider (taperSlider,     taperReadout,     2);
    wirePropSlider (cornerCutSlider, cornerCutReadout, 2);

    // Tilt slider callbacks: update readout text, write the tilt value (in
    // radians) into FloorPlanComponent's TransducerState so it travels with
    // the rest of the mic-layout state, then notify. MAIN drives both
    // tilt[2/3] (regular L/R pair) and deccaTilt — one slider serves both
    // modes. OUTRIG drives tilt[4/5]; AMBIENT drives tilt[6/7].
    mainTiltSlider.onValueChange = [this]
    {
        const double deg = mainTiltSlider.getValue();
        const double rad = deg * M_PI / 180.0;
        mainTiltReadout.setText (juce::String ((int) std::round (deg)) + juce::String::fromUTF8 ("\xc2\xb0"),
                                 juce::dontSendNotification);
        auto t = floorPlanComponent.getTransducerState();
        t.tilt[2] = rad;
        t.tilt[3] = rad;
        t.deccaTilt = rad;
        floorPlanComponent.setTransducerState (t);
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    outrigTiltSlider.onValueChange = [this]
    {
        const double deg = outrigTiltSlider.getValue();
        const double rad = deg * M_PI / 180.0;
        outrigTiltReadout.setText (juce::String ((int) std::round (deg)) + juce::String::fromUTF8 ("\xc2\xb0"),
                                   juce::dontSendNotification);
        auto t = floorPlanComponent.getTransducerState();
        t.tilt[4] = rad;
        t.tilt[5] = rad;
        floorPlanComponent.setTransducerState (t);
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    ambientTiltSlider.onValueChange = [this]
    {
        const double deg = ambientTiltSlider.getValue();
        const double rad = deg * M_PI / 180.0;
        ambientTiltReadout.setText (juce::String ((int) std::round (deg)) + juce::String::fromUTF8 ("\xc2\xb0"),
                                    juce::dontSendNotification);
        auto t = floorPlanComponent.getTransducerState();
        t.tilt[6] = rad;
        t.tilt[7] = rad;
        floorPlanComponent.setTransducerState (t);
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };

    // Update the dB readout live while the centre-fill slider is dragged.
    deccaCentreGainSlider.onValueChange = [this]
    {
        const double v = deccaCentreGainSlider.getValue();
        const auto txt = (v <= 1e-6) ? juce::String ("off")
                                     : juce::String ((int) std::round (20.0 * std::log10 (v))) + "dB";
        deccaCentreGainReadout.setText (txt, juce::dontSendNotification);
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };

    // Update the degrees readout live while the toe-out slider is dragged.
    deccaToeOutSlider.onValueChange = [this]
    {
        const int deg = (int) std::round (deccaToeOutSlider.getValue());
        const auto txt = (deg == 0) ? juce::String ("off")
                                    : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (deg) + juce::String::fromUTF8 ("\xc2\xb0");
        deccaToeOutReadout.setText (txt, juce::dontSendNotification);
        floorPlanComponent.repaint();   // visual toe-out ticks update live
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    erOnlyButton.onClick         = notifyParamChanged;
    lambertScatterButton.onClick = notifyParamChanged;
    spkDirFullButton.onClick     = notifyParamChanged;
    for (auto* cb : { &shapeCombo, &floorCombo, &ceilingCombo, &wallCombo,
                      &vaultCombo, &micPatternCombo,
                      &outrigPatternCombo, &ambientPatternCombo, &directMaxOrderCombo })
        cb->addListener (this);

    // Mic-path toggles: flip FloorPlan visibility for OUTRIG/AMBIENT so the
    // extra mic pairs only draw when enabled. Always notify the param-changed
    // callback (suppression flag respected). DIRECT has no floor-plan pair.
    outrigEnableButton.onClick = [this]
    {
        floorPlanComponent.outrigVisible = outrigEnableButton.getToggleState();
        floorPlanComponent.repaint();
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    ambientEnableButton.onClick = [this]
    {
        floorPlanComponent.ambientVisible = ambientEnableButton.getToggleState();
        floorPlanComponent.repaint();
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    directEnableButton.onClick = notifyParamChanged;

    // Decca Tree mode: flip FloorPlanComponent.deccaVisible so the MAIN mic
    // pucks are replaced by the single Decca tree puck. Always notifies the
    // param-changed callback so the UI state goes dirty and a Calculate IR
    // prompt is shown.
    deccaEnableButton.onClick = [this]
    {
        floorPlanComponent.deccaVisible = deccaEnableButton.getToggleState();
        floorPlanComponent.repaint();
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };

    addAndMakeVisible (shapeCombo);
    addAndMakeVisible (widthSlider);
    addAndMakeVisible (depthSlider);
    addAndMakeVisible (heightSlider);
    addAndMakeVisible (widthLabel);
    addAndMakeVisible (depthLabel);
    addAndMakeVisible (heightLabel);
    addAndMakeVisible (widthValueLabel);
    addAndMakeVisible (depthValueLabel);
    addAndMakeVisible (heightValueLabel);

    // Shape proportion sliders + readouts. Visibility is set by
    // updateShapeProportionVisibility() — calling addAndMakeVisible just makes
    // them children of this component; they will be hidden again immediately
    // for any shape that does not use them.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &navePctSlider, &navePctLabel, &navePctReadout,
             &trptPctSlider, &trptPctLabel, &trptPctReadout,
             &taperSlider,   &taperLabel,   &taperReadout,
             &cornerCutSlider, &cornerCutLabel, &cornerCutReadout })
        addAndMakeVisible (*c);

    // Style the readouts to match the existing windowsReadout / audienceReadout
    // pattern (right-justified dim text).
    for (auto* l : { &navePctReadout, &trptPctReadout, &taperReadout, &cornerCutReadout })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setColour (juce::Label::textColourId, textDim);
    }
    navePctReadout  .setText (juce::String (navePctSlider  .getValue(), 2), juce::dontSendNotification);
    trptPctReadout  .setText (juce::String (trptPctSlider  .getValue(), 2), juce::dontSendNotification);
    taperReadout    .setText (juce::String (taperSlider    .getValue(), 2), juce::dontSendNotification);
    cornerCutReadout.setText (juce::String (cornerCutSlider.getValue(), 2), juce::dontSendNotification);

    // Initial visibility — defaults to "Rectangular" so all four are hidden.
    updateShapeProportionVisibility();

    // Option-mirror axis selector. Two small radio buttons (shared radioGroupId
    // inside the class) — tapping one sets FloorPlanComponent::mirrorAxis and
    // fires the param-changed notification so the choice round-trips into the
    // .ping sidecar via IRSynthParams::mirror_axis.
    mirrorVerticalButton   = std::make_unique<MirrorAxisButton> (false);
    mirrorHorizontalButton = std::make_unique<MirrorAxisButton> (true);
    mirrorVerticalButton->setToggleState   (true,  juce::dontSendNotification);
    mirrorHorizontalButton->setToggleState (false, juce::dontSendNotification);
    mirrorVerticalButton->onClick = [this]
    {
        floorPlanComponent.mirrorAxis = FloorPlanComponent::MirrorAxis::Vertical;
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    mirrorHorizontalButton->onClick = [this]
    {
        floorPlanComponent.mirrorAxis = FloorPlanComponent::MirrorAxis::Horizontal;
        if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn();
    };
    mirrorAxisLabel.setText ("Option-mirror", juce::dontSendNotification);
    mirrorAxisLabel.setColour (juce::Label::textColourId, textDim);
    mirrorAxisLabel.setJustificationType (juce::Justification::centredLeft);
    mirrorAxisLabel.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (*mirrorVerticalButton);
    addAndMakeVisible (*mirrorHorizontalButton);
    addAndMakeVisible (mirrorAxisLabel);

    addAndMakeVisible (floorCombo);
    addAndMakeVisible (ceilingCombo);
    addAndMakeVisible (wallCombo);
    addAndMakeVisible (floorLabel);
    addAndMakeVisible (ceilingLabel);
    addAndMakeVisible (wallLabel);
    addAndMakeVisible (windowsSlider);
    addAndMakeVisible (windowsLabel);
    addAndMakeVisible (windowsReadout);
    addAndMakeVisible (audienceSlider);
    addAndMakeVisible (diffusionSlider);
    addAndMakeVisible (audienceLabel);
    addAndMakeVisible (diffusionLabel);
    addAndMakeVisible (audienceReadout);
    addAndMakeVisible (diffusionReadout);
    addAndMakeVisible (vaultCombo);
    addAndMakeVisible (organSlider);
    addAndMakeVisible (balconiesSlider);
    addAndMakeVisible (vaultLabel);
    addAndMakeVisible (organLabel);
    addAndMakeVisible (balconiesLabel);
    addAndMakeVisible (organReadout);
    addAndMakeVisible (balconiesReadout);
    addAndMakeVisible (micPatternCombo);
    addAndMakeVisible (erOnlyButton);
    addAndMakeVisible (bakeBalanceButton);
    addAndMakeVisible (micPatternLabel);
    addAndMakeVisible (directMaxOrderCombo);
    addAndMakeVisible (directMaxOrderLabel);
    addAndMakeVisible (lambertScatterButton);
    addAndMakeVisible (spkDirFullButton);

    addAndMakeVisible (directEnableButton);
    addAndMakeVisible (outrigEnableButton);
    addAndMakeVisible (ambientEnableButton);
    addAndMakeVisible (deccaEnableButton);
    addAndMakeVisible (deccaCentreGainSlider);
    addAndMakeVisible (deccaCentreGainLabel);
    addAndMakeVisible (deccaCentreGainReadout);
    addAndMakeVisible (deccaToeOutSlider);
    addAndMakeVisible (deccaToeOutLabel);
    addAndMakeVisible (deccaToeOutReadout);
    addAndMakeVisible (outrigPatternCombo);
    addAndMakeVisible (ambientPatternCombo);
    addAndMakeVisible (outrigPatternLabel);
    addAndMakeVisible (ambientPatternLabel);
    addAndMakeVisible (outrigHeightSlider);
    addAndMakeVisible (ambientHeightSlider);
    addAndMakeVisible (outrigHeightLabel);
    addAndMakeVisible (ambientHeightLabel);
    addAndMakeVisible (outrigHeightReadout);
    addAndMakeVisible (ambientHeightReadout);
    addAndMakeVisible (mainTiltSlider);
    addAndMakeVisible (mainTiltLabel);
    addAndMakeVisible (mainTiltReadout);
    addAndMakeVisible (outrigTiltSlider);
    addAndMakeVisible (outrigTiltLabel);
    addAndMakeVisible (outrigTiltReadout);
    addAndMakeVisible (ambientTiltSlider);
    addAndMakeVisible (ambientTiltLabel);
    addAndMakeVisible (ambientTiltReadout);

    // Info labels used in the MAIN / DIRECT columns of the Mic Paths strip.
    // Styled to match section headers: small, dim text.
    addAndMakeVisible (mainPathInfoLabel);
    mainPathInfoLabel.setText ("", juce::dontSendNotification);
    mainPathInfoLabel.setJustificationType (juce::Justification::topLeft);
    mainPathInfoLabel.setColour (juce::Label::textColourId, textDim);
    mainPathInfoLabel.setFont (juce::FontOptions (10.0f));

    addAndMakeVisible (directPathInfoLabel);
    directPathInfoLabel.setText ("Reach set in Options\nShares MAIN pair position",
                                 juce::dontSendNotification);
    directPathInfoLabel.setJustificationType (juce::Justification::topLeft);
    directPathInfoLabel.setColour (juce::Label::textColourId, textDim);
    directPathInfoLabel.setFont (juce::FontOptions (10.0f));

    // Per-path filename labels (one per MAIN/DIRECT/OUTRIG/AMBIENT column).
    // Drawn directly under the Mic Paths strip so the user can see at a
    // glance which file is loaded into each slot. Each label is tinted with
    // the mic path's mixer accent colour (matches MicMixerComponent
    // kAccent* — main page) so the row visually pairs with the mixer
    // strips. Updated each tick by timerCallback() via pathDisplayNameSupplier.
    //
    // Mixer accents (kept in sync with MicMixerComponent.cpp):
    //   MAIN    = 0xff8cd6ef icy blue (plugin-wide accent)
    //   DIRECT  = 0xffe87a2d warm orange
    //   OUTRIG  = 0xffc987e8 soft violet
    //   AMBIENT = 0xff7bd67b fresh green
    static const juce::Colour pathAccents[4] {
        juce::Colour (0xff8cd6ef),
        juce::Colour (0xffe87a2d),
        juce::Colour (0xffc987e8),
        juce::Colour (0xff7bd67b)
    };
    for (int i = 0; i < 4; ++i)
    {
        auto& l = pathNameLabels[i];
        addAndMakeVisible (l);
        l.setText ("<empty>", juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, pathAccents[i]);
        l.setFont (juce::FontOptions (11.0f));
        l.setInterceptsMouseClicks (false, false);
    }

    // "LOADED:" caption on the far-left of the path-name row — same font as
    // the per-path labels, dim grey to match the section headers above so it
    // reads as a row label rather than competing with the per-path filenames.
    addAndMakeVisible (loadedRowLabel);
    loadedRowLabel.setText ("LOADED:", juce::dontSendNotification);
    loadedRowLabel.setJustificationType (juce::Justification::centredLeft);
    loadedRowLabel.setColour (juce::Label::textColourId, textDim);
    loadedRowLabel.setFont (juce::FontOptions (11.0f));
    loadedRowLabel.setInterceptsMouseClicks (false, false);

    // Bottom bar: RT60 | IR combo + Save | Preview | Progress | Done
    const char* const rt60Freqs[] = { "125", "250", "500", "1k", "2k", "4k" };
    addAndMakeVisible (rt60Label);
    rt60Label.setText ("RT60", juce::dontSendNotification);
    rt60Label.setColour (juce::Label::textColourId, textDim);
    rt60Label.setFont (juce::FontOptions (9.0f));
    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible (rt60FreqLabels[i]);
        rt60FreqLabels[i].setText (rt60Freqs[i], juce::dontSendNotification);
        rt60FreqLabels[i].setColour (juce::Label::textColourId, textDim);
        rt60FreqLabels[i].setFont (juce::FontOptions (9.0f));
        addAndMakeVisible (rt60Values[i]);
        rt60Values[i].setText ("—", juce::dontSendNotification);
        rt60Values[i].setColour (juce::Label::textColourId, accent);
        rt60Values[i].setFont (juce::FontOptions (9.0f));
    }

    addAndMakeVisible (irCombo);
    irCombo.addListener (this);
    irCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0x1effffff));
    irCombo.setColour (juce::ComboBox::textColourId, textDim);
    irCombo.setColour (juce::ComboBox::arrowColourId, accent);
    irCombo.setEditableText (true);

    // Apply the same glassy transparent fill to all other combos on this page so they
    // visually match the main-page preset combo (0x30ffffff = 19 % opaque white).
    for (auto* cb : { &shapeCombo, &floorCombo, &ceilingCombo, &wallCombo,
                      &vaultCombo, &micPatternCombo,
                      &outrigPatternCombo, &ambientPatternCombo })
    {
        cb->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0x1effffff));
        cb->setColour (juce::ComboBox::textColourId, textDim);
        cb->setColour (juce::ComboBox::arrowColourId, accent);
    }

    addAndMakeVisible (irPresetLabel);
    irPresetLabel.setText ("IR preset", juce::dontSendNotification);
    irPresetLabel.setJustificationType (juce::Justification::centredRight);
    irPresetLabel.setColour (juce::Label::textColourId, textDim);
    irPresetLabel.setFont (juce::FontOptions (10.0f));

    addAndMakeVisible (saveIRButton);
    saveIRButton.addListener (this);
    saveIRButton.setColour (juce::TextButton::buttonColourId, panelBg);
    saveIRButton.setColour (juce::TextButton::textColourOffId, textDim);

    addAndMakeVisible (exportIRButton);
    exportIRButton.setColour (juce::TextButton::buttonColourId, panelBg);
    exportIRButton.setColour (juce::TextButton::textColourOffId, textDim);
    exportIRButton.onClick = [this] { if (onExportIRFn) onExportIRFn(); };

    addAndMakeVisible (importIRButton);
    importIRButton.setColour (juce::TextButton::buttonColourId, panelBg);
    importIRButton.setColour (juce::TextButton::textColourOffId, textDim);
    importIRButton.onClick = [this]
    {
        if (onImportIRFn)
            onImportIRFn();
    };

    addAndMakeVisible (previewButton);
    previewButton.addListener (this);
    previewButton.setColour (juce::TextButton::buttonColourId, accent);
    previewButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

    addAndMakeVisible (progressBar);
    addAndMakeVisible (progressLabel);
    progressBar.setPercentageDisplay (true);
    progressValue = 0.0;
    progressLabel.setText ("", juce::dontSendNotification);
    progressLabel.setColour (juce::Label::textColourId, textDim);

    addAndMakeVisible (doneButton);
    doneButton.addListener (this);
    doneButton.setColour (juce::TextButton::buttonColourId, panelBg);
    doneButton.setColour (juce::TextButton::textColourOffId, textDim);

    for (auto* l : { &widthLabel, &depthLabel, &heightLabel, &widthValueLabel, &depthValueLabel, &heightValueLabel,
                     &floorLabel, &ceilingLabel, &wallLabel, &windowsLabel,
                     &audienceLabel, &diffusionLabel, &vaultLabel, &organLabel, &balconiesLabel,
                     &micPatternLabel,
                     &outrigPatternLabel, &ambientPatternLabel,
                     &outrigHeightLabel,  &ambientHeightLabel,
                     &outrigHeightReadout, &ambientHeightReadout,
                     &deccaCentreGainLabel, &deccaCentreGainReadout,
                     &deccaToeOutLabel, &deccaToeOutReadout,
                     // v2.8.0 shape-proportion labels (Nave / Transept / Taper / Cnr Cut / Roundness)
                     &navePctLabel, &trptPctLabel, &taperLabel, &cornerCutLabel,
                     // v2.7.6 per-mic Tilt labels (MAIN / OUTRIG / AMBIENT)
                     &mainTiltLabel, &outrigTiltLabel, &ambientTiltLabel })
        l->setColour (juce::Label::textColourId, textDim);

    startTimerHz (4);
}

void IRSynthComponent::paint (juce::Graphics& g)
{
    // ── Background: right 60% of the brushed-steel texture (brighter region),
    //    scaled to fill the full component area. ───────────────────────────────
    if (bgTexture.isValid())
    {
        const int srcX = (int) (bgTexture.getWidth() * 0.40f);   // start 40% in
        const int srcW = bgTexture.getWidth() - srcX;            // remaining 60%
        g.drawImage (bgTexture,
                     0, 0, getWidth(), getHeight(),               // dest: full component
                     srcX, 0, srcW, bgTexture.getHeight());       // source: right 60%
    }
    else
        g.fillAll (juce::Colour (0xff141414));

    // Dark overlay so controls remain readable (slightly lighter than main UI
    // body so the texture gives a subtle warm metallic hint)
    g.setColour (juce::Colour (0xd4141414));
    g.fillRect (getLocalBounds());

    // ── Section headers (small-caps text + underline separator) ─────────────
    // Matches the main plugin's drawGroupHeader style: textDim colour, 10pt,
    // left-aligned text, 1px horizontal rule at the bottom of the header rect.
    g.setFont (juce::FontOptions (10.0f));

    auto drawSectionHeader = [&] (const juce::Rectangle<int>& bounds, const juce::String& text)
    {
        if (bounds.getWidth() <= 0) return;
        // Dim text label
        g.setColour (textDim);
        g.drawText (text.toUpperCase(),
                    bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight() - 3,
                    juce::Justification::centredLeft, false);
        // Separator line
        g.setColour (juce::Colour (0xff2e2e2e));
        g.drawLine ((float) bounds.getX(),
                    (float) bounds.getBottom() - 1.0f,
                    (float) bounds.getRight(),
                    (float) bounds.getBottom() - 1.0f,
                    1.0f);
    };

    drawSectionHeader (surfacesHeaderBounds, "Surfaces");
    drawSectionHeader (contentsHeaderBounds, "Contents");
    drawSectionHeader (interiorHeaderBounds, "Interior");
    drawSectionHeader (optionsHeaderBounds,  "Options");
    drawSectionHeader (roomHeaderBounds,     "Room Geometry");

    // Mic Paths strip: thin top rule + per-column section headers.
    if (micPathsStripBounds.getWidth() > 0)
    {
        g.setColour (juce::Colour (0xff2e2e2e));
        g.drawLine ((float) micPathsStripBounds.getX(),
                    (float) micPathsStripBounds.getY() + 0.5f,
                    (float) micPathsStripBounds.getRight(),
                    (float) micPathsStripBounds.getY() + 0.5f,
                    1.0f);
    }
    drawSectionHeader (mainHeaderBounds,    "Main");
    drawSectionHeader (directHeaderBounds,  "Direct");
    drawSectionHeader (outrigHeaderBounds,  "Outrigger");
    drawSectionHeader (ambientHeaderBounds, "Ambient");
}

void IRSynthComponent::resized()
{
    auto b = getLocalBounds().reduced (12);
    const int barH = 52;
    auto contentArea = b.removeFromTop (b.getHeight() - barH);
    auto barArea = b;

    // ── Horizontal Mic Paths strip ──────────────────────────────────────────
    // Carved off the bottom of contentArea (above barArea). Spans the full
    // content width so MAIN / DIRECT / OUTRIG / AMBIENT read left-to-right
    // and pair naturally with the MicMixerComponent column order on the
    // main page. A thin gap separates it from the FloorPlan / left column
    // above, and the bottom bar below.
    //
    // stripH sized to the tallest column (MAIN = header + Decca toggle +
    // Pattern + Centre fill + Toe-out rows) with a small margin so the
    // bottom-most Toe-out control sits just above the RT60 display in the
    // bar below (user request, v2.7.5).
    // stripH bumped to 154 to fit one extra Tilt row in MAIN (Decca + Pattern
    // + Centre fill + Toe-out + Tilt). OUTRIG/AMBIENT also fit a Tilt row
    // below their Height row; DIRECT remains short (just the enable toggle).
    const int stripH   = 154;
    auto micStripArea = contentArea.removeFromBottom (stripH);

    // ── Per-path filename row ───────────────────────────────────────────────
    // Sits *immediately under* the Mic Paths strip with no separating gap, so
    // it visually attaches to the column headers above (MAIN / DIRECT /
    // OUTRIG / AMBIENT) rather than to the bottom bar. We carve the row
    // partly out of the strip-to-bar gap that used to be there (stripGap = 6)
    // and partly out of the bottom bar's vertical slack (the existing bar
    // contents are ~30 px in a 52 px area — there's ~22 px of unused space
    // we can reclaim). Net effect: floor plan / left column / strip are all
    // unchanged; the bar's controls shift down by ~14 px to fill its own
    // bottom slack.
    const int pathNameRowH = 16;
    const int pathBarGap   = 4;   // breathing room between labels and bar
    auto pathNameRow = barArea.removeFromTop (pathNameRowH);
    barArea.removeFromTop (pathBarGap);

    // ── Left / right column split ───────────────────────────────────────────
    // Left column: all acoustic-character + room-geometry controls (~35% width).
    // Right column: FloorPlanComponent, always visible (remaining ~65% width).
    const int leftW = juce::jmax (280, (int) (0.35f * contentArea.getWidth()));
    auto leftCol  = contentArea.removeFromLeft (leftW);
    auto rightCol = contentArea;   // remaining width

    layoutControls (leftCol);
    floorPlanComponent.setBounds (rightCol.reduced (8));
    layoutMicPathsStrip (micStripArea);

    // Lay out the four per-path filename labels directly under the Mic Paths
    // strip's four columns so each label visually anchors to its section
    // header above. We re-derive the column geometry from micStripArea
    // exactly the way layoutMicPathsStrip does (4 equal columns, 10 px
    // gap, 6 px inset) so the labels track the columns even if the column
    // formula changes later — a single source of geometry is preferable to
    // exposing the column rectangles as members.
    //
    // The "LOADED:" caption sits at the far-left edge of the row, anchored
    // to the same X as the bar's left edge (= mic strip left edge) so it
    // aligns with the RT60 label below.
    {
        const int colGap   = 10;
        const int inset    = 6;
        const int stripX   = micStripArea.getX();
        const int stripW   = micStripArea.getWidth();
        const int colW     = (stripW - colGap * 3) / 4;

        const int loadedW = 56;  // wide enough for "LOADED:" at 11 pt + small margin
        loadedRowLabel.setBounds (stripX, pathNameRow.getY(),
                                  loadedW, pathNameRow.getHeight());

        for (int i = 0; i < 4; ++i)
        {
            const int colX = stripX + (colW + colGap) * i;
            pathNameLabels[i].setBounds (colX + inset, pathNameRow.getY(),
                                         colW - inset * 2, pathNameRow.getHeight());
        }
    }

    // ── Bottom bar ──────────────────────────────────────────────────────────
    const int barY = barArea.getY();
    const int barW = barArea.getWidth();
    const int barCentreX = barArea.getX() + barW / 2;

    // RT60 section on left: label + 6 bands (freq label above value)
    int x = barArea.getX();
    rt60Label.setBounds (x, barY + 2, 32, 12);
    x += 36;
    const int bandW = 36;
    const int freqH = 11;
    const int valH = 12;
    for (int i = 0; i < 6; ++i)
    {
        rt60FreqLabels[i].setBounds (x, barY + 2, bandW, freqH);
        rt60Values[i].setBounds (x, barY + 2 + freqH, bandW, valH);
        x += bandW;
    }
    const int rt60EndX = x;

    // Save / Export / Import buttons at centre
    const int saveIRW = 50;
    const int eiW     = 52;
    saveIRButton.setBounds (barCentreX - saveIRW / 2, barY + 6, saveIRW, 24);
    exportIRButton.setBounds (saveIRButton.getRight() + 4, barY + 6, eiW, 24);
    importIRButton.setBounds (exportIRButton.getRight() + 4, barY + 6, eiW, 24);
    int leftX = importIRButton.getRight() + 12;

    // IR combo: placed just to the left of Save button, with gap
    const int irComboW = juce::jmin (140, (int) (0.18f * barW));
    irCombo.setBounds (barCentreX - saveIRW / 2 - 6 - irComboW, barY + 6, irComboW, 24);

    // "IR preset" label to the left of the combo
    const int irPresetLabelW = 56;
    irPresetLabel.setBounds (irCombo.getX() - irPresetLabelW - 4, barY + 6, irPresetLabelW, 24);

    // Calculate IR button just to the right of Export/Import
    const int previewW = 100;
    previewButton.setBounds (leftX, barY + 6, previewW, 24);
    leftX += previewW + 16;

    // Main Menu button at far right
    const int doneW = 84;
    int rightX = barArea.getRight();
    doneButton.setBounds (rightX - doneW, barY + 6, doneW, 24);
    rightX -= doneW + 12;

    // Progress bar right-justified (before Main Menu), shifted 15px left for visual balance
    const int progW = std::min (200, rightX - leftX - 12);
    progressBar.setBounds (rightX - progW - 15, barY + 8, progW, 16);
    progressLabel.setBounds (rightX - progW - 15, barY + 26, progW, 14);
}

void IRSynthComponent::layoutControls (juce::Rectangle<int> b)
{
    const int rowH     = 22;
    const int labelW   = 78;
    const int readoutW = 40;
    const int gap      = 6;
    const int secGap   = 10;   // vertical gap between sections
    const int headerH  = 16;   // section header height (text + underline)
    const int inset    = 10;   // left / right inset within the left column

    const int x0    = b.getX() + inset;
    const int ctrlW = b.getWidth() - inset * 2;
    const int comboW  = juce::jmax (80, ctrlW - labelW - gap);
    const int sliderW = juce::jmax (60, comboW - readoutW - gap);

    // Helper: label + combo on one row
    auto rowCombo = [&] (int y, juce::Label& lbl, juce::ComboBox& ctrl)
    {
        lbl.setBounds  (x0,                y, labelW, rowH);
        ctrl.setBounds (x0 + labelW + gap, y, comboW, rowH);
    };

    // Helper: label + slider + readout on one row
    auto rowSlider = [&] (int y, juce::Label& lbl, juce::Slider& ctrl, juce::Label& readout)
    {
        lbl.setBounds     (x0,                               y, labelW,  rowH);
        ctrl.setBounds    (x0 + labelW + gap,                y, sliderW, rowH);
        readout.setBounds (x0 + labelW + gap + sliderW + gap, y, readoutW, rowH);
    };

    int y = b.getY() + 6;

    // ── SURFACES ────────────────────────────────────────────────────────────
    surfacesHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;
    rowCombo  (y, floorLabel,   floorCombo);                         y += rowH;
    rowCombo  (y, ceilingLabel, ceilingCombo);                       y += rowH;
    rowCombo  (y, wallLabel,    wallCombo);                          y += rowH;
    rowSlider (y, windowsLabel, windowsSlider, windowsReadout);      y += rowH + secGap;

    // ── CONTENTS ────────────────────────────────────────────────────────────
    contentsHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;
    rowSlider (y, audienceLabel,  audienceSlider,  audienceReadout);  y += rowH;
    rowSlider (y, diffusionLabel, diffusionSlider, diffusionReadout); y += rowH + secGap;

    // ── INTERIOR ────────────────────────────────────────────────────────────
    interiorHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;
    rowCombo  (y, vaultLabel,     vaultCombo);                        y += rowH;
    rowSlider (y, organLabel,     organSlider,     organReadout);     y += rowH;
    rowSlider (y, balconiesLabel, balconiesSlider, balconiesReadout); y += rowH + secGap;

    // ── OPTIONS ─────────────────────────────────────────────────────────────
    // MAIN mic pattern has moved into the MAIN column of the Mic Paths strip
    // (alongside Outrig/Ambient patterns), so Options here only holds the
    // ER-only toggle and the bake-balance action.
    //
    // Layout:
    //   Row 1: [ER only]  [Bake ER/Tail Bal]                       (one row)
    //   Row 2: DIRECT reach label + half-width combo
    //   Row 3: [Lambert Scatter]      [Full Spkr Dir]              (one row)
    //
    // v2.8.x: the Sample Rate picker was removed. It was a holdover from the
    // pre-plugin web-app era where the user downloaded a WAV of the IR at a
    // chosen rate. Inside the plugin the IR is synthesised at 48 kHz and
    // juce::dsp::Convolution::loadImpulseResponse automatically resamples it
    // to whatever rate prepareToPlay received from the host, so the picker
    // had no effect on playback — only on the native rate of exported WAVs.
    // Presets still round-trip the "sr" XML attribute (default 48000) so old
    // content loads unchanged; IRSynthParams::sample_rate is now effectively
    // pinned to 48000 by the UI.
    optionsHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;

    // Row 1: ER only + Bake button side-by-side. The Bake button's text
    // ("Bake ER/Tail Bal") is longer than the ER-only label, so give it ~60%
    // of the width and the toggle ~40%, minus one small inter-item gap.
    {
        const int rowGap   = 6;
        const int erW      = juce::jmax (60,  (ctrlW - rowGap) * 2 / 5);
        const int bakeW    = juce::jmax (110, ctrlW - rowGap - erW);
        erOnlyButton.setBounds     (x0,                  y, erW,   rowH);
        bakeBalanceButton.setBounds(x0 + erW + rowGap,   y, bakeW, rowH + 2);
        y += rowH + secGap;
    }

    // Row 3: DIRECT reach label + half-width combo.
    {
        const int lblW    = 88;
        const int comboHalfW = juce::jmax (100, (ctrlW - lblW - gap) / 2);
        directMaxOrderLabel.setBounds (x0,                y, lblW, rowH);
        directMaxOrderCombo.setBounds (x0 + lblW + gap,   y, comboHalfW, rowH);
        y += rowH + 2;
    }

    // Row 4: Lambert Scatter + Full Spkr Dir side-by-side, each half the
    // control width minus a small gap.
    {
        const int halfW = (ctrlW - gap) / 2;
        lambertScatterButton.setBounds (x0,                  y, halfW, rowH);
        spkDirFullButton    .setBounds (x0 + halfW + gap,    y, halfW, rowH);
        y += rowH + secGap;
    }

    // ── ROOM GEOMETRY ────────────────────────────────────────────────────────
    roomHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;

    // Shape combo spans the full control width
    shapeCombo.setBounds (x0, y, ctrlW, rowH);                       y += rowH + 2;

    // Reserve the rightmost 15% of the geometry column for the Option-mirror
    // V/H buttons, so the symbols sit beside the geometry rows rather than
    // taking their own row beneath them. Dimension and proportion rows compute
    // their slider+readout layout against geomRowW (the narrowed width), not
    // ctrlW. The reserve is independent of the row count, so all geometry rows
    // shrink uniformly.
    const int mirrorReserveW = juce::jmax (42, juce::roundToInt (ctrlW * 0.15f));
    const int geomRowW       = juce::jmax (120, ctrlW - mirrorReserveW);
    int       lastGeomRowY   = y;     // tracks the most recent geometry row baseline

    // Dimension rows: name label + slider + editable value label on a single
    // row, matching the vertical rhythm of audience/diffusion in Contents.
    auto dimRow = [&] (juce::Label& nameLbl, juce::Label& valueLbl, juce::Slider& slider)
    {
        const int nameW     = 52;
        const int valueBoxW = 48;
        const int sW        = juce::jmax (60, geomRowW - nameW - valueBoxW - gap * 2);
        nameLbl .setBounds (x0,                              y, nameW,     rowH);
        slider  .setBounds (x0 + nameW + gap,                y, sW,        rowH);
        valueLbl.setBounds (x0 + nameW + gap + sW + gap,     y, valueBoxW, rowH);
        lastGeomRowY = y;
        y += rowH;
    };
    dimRow (widthLabel,  widthValueLabel,  widthSlider);
    dimRow (depthLabel,  depthValueLabel,  depthSlider);
    dimRow (heightLabel, heightValueLabel, heightSlider);

    // Shape proportion rows (v2.8.0). Same layout as dimRow but the readout is
    // right-justified, dim and non-editable (dimensionless fraction, not an
    // editable number). Rows are skipped when the control is hidden so they
    // don't leave a gap for shapes that don't consume them.
    auto propRow = [&] (juce::Slider& slider, juce::Label& nameLbl, juce::Label& readoutLbl)
    {
        if (! slider.isVisible()) return;
        const int nameW     = 52;
        const int valueBoxW = 48;
        const int sW        = juce::jmax (60, geomRowW - nameW - valueBoxW - gap * 2);
        nameLbl   .setBounds (x0,                              y, nameW,     rowH);
        slider    .setBounds (x0 + nameW + gap,                y, sW,        rowH);
        readoutLbl.setBounds (x0 + nameW + gap + sW + gap,     y, valueBoxW, rowH);
        lastGeomRowY = y;
        y += rowH;
    };

    // Cathedral has two proportion rows (Nave + Transept) which used to stack
    // and crash into the bottom Mic Paths strip. Lay them side by side so they
    // occupy a single row and free vertical space below.
    if (navePctSlider.isVisible() && trptPctSlider.isVisible())
    {
        const int halfW   = (geomRowW - gap) / 2;
        const int nameW   = 38;            // tighter than dimRow — half the width
        const int valueW  = 36;
        const int sW      = juce::jmax (40, halfW - nameW - valueW - gap * 2);
        navePctLabel  .setBounds (x0,                              y, nameW,  rowH);
        navePctSlider .setBounds (x0 + nameW + gap,                y, sW,     rowH);
        navePctReadout.setBounds (x0 + nameW + gap + sW + gap,     y, valueW, rowH);
        const int x1 = x0 + halfW + gap;
        trptPctLabel  .setBounds (x1,                              y, nameW,  rowH);
        trptPctSlider .setBounds (x1 + nameW + gap,                y, sW,     rowH);
        trptPctReadout.setBounds (x1 + nameW + gap + sW + gap,     y, valueW, rowH);
        lastGeomRowY = y;
        y += rowH;
    }
    else
    {
        propRow (navePctSlider, navePctLabel, navePctReadout);
        propRow (trptPctSlider, trptPctLabel, trptPctReadout);
    }
    propRow (taperSlider,     taperLabel,     taperReadout);
    propRow (cornerCutSlider, cornerCutLabel, cornerCutReadout);

    // Option-mirror axis selector — two small icon buttons (V then H), packed
    // tightly together at the right edge in the reserved 15% strip, no label
    // (the dashed-line glyphs are self-explanatory). They sit on the same Y
    // as the *last* geometry / proportion row, in the space we reserved on
    // the right side of that row.
    {
        mirrorAxisLabel.setVisible (false);   // glyphs are clear enough on their own
        const int btnSize   = 18;
        const int btnGap    = 3;              // tight pairing
        const int btnY      = lastGeomRowY + (rowH - btnSize) / 2;
        const int rightEdge = x0 + ctrlW;
        const int hBtnX     = rightEdge - btnSize;
        const int vBtnX     = hBtnX - btnGap - btnSize;
        if (mirrorVerticalButton)
            mirrorVerticalButton->setBounds   (vBtnX, btnY, btnSize, btnSize);
        if (mirrorHorizontalButton)
            mirrorHorizontalButton->setBounds (hBtnX, btnY, btnSize, btnSize);
    }

    // Mic paths have moved out of the left column into a horizontal strip
    // along the bottom of the content area — see layoutMicPathsStrip().
}

void IRSynthComponent::layoutMicPathsStrip (juce::Rectangle<int> b)
{
    // Four equal-width columns in left-to-right order: MAIN, DIRECT, OUTRIG, AMBIENT.
    // Each column contains a section header, optional toggle, and optional control rows.
    micPathsStripBounds = b;

    const int rowH     = 22;
    const int headerH  = 16;
    const int labelW   = 52;   // narrower than left column (less horizontal room)
    const int readoutW = 38;
    const int gap      = 6;
    const int colGap   = 10;
    const int inset    = 6;

    const int stripX   = b.getX();
    const int stripY   = b.getY() + 8;   // leave 8 px for separator line in paint()
    const int stripW   = b.getWidth();

    const int colW = (stripW - colGap * 3) / 4;

    // Per-column helpers — capture colX so each column lays out its own controls.
    auto layoutMainCol = [&] (int colX)
    {
        const int x0    = colX + inset;
        const int ctrlW = colW - inset * 2;
        int y = stripY;

        mainHeaderBounds = { x0, y, ctrlW, headerH };
        y += headerH + 4;

        // MAIN always on — the enable-toggle row the other columns use here
        // is instead occupied by the Decca Tree mode switch (replaces the
        // MAIN L/R mics with a single Decca tree puck). Keeps the Pattern
        // combo row aligned horizontally with OUTRIG/AMBIENT.
        deccaEnableButton.setBounds (x0, y, ctrlW, rowH);
        y += rowH + 2;

        micPatternLabel.setBounds (x0, y, labelW, rowH);
        micPatternCombo.setBounds (x0 + labelW + gap, y,
                                   ctrlW - labelW - gap, rowH);
        y += rowH + 2;

        // Decca Centre-fill + Toe-out rows share a tighter label / wider readout
        // column layout so the numeric values ("-6dB", "±90°") fit without
        // truncation. The default readoutW (38 px) is too narrow for the ± / °
        // glyphs and the dB suffix; bumping it to 50 px and reducing labelW to
        // 44 px keeps the slider roughly the same width.
        const int dLabelW   = 44;
        const int dReadoutW = 50;
        const int dSlW      = ctrlW - dLabelW - gap - dReadoutW - gap;

        // Decca Centre-fill row — label + slider + dB readout.
        {
            deccaCentreGainLabel.setBounds   (x0, y, dLabelW, rowH);
            deccaCentreGainSlider.setBounds  (x0 + dLabelW + gap, y, dSlW, rowH);
            deccaCentreGainReadout.setBounds (x0 + dLabelW + gap + dSlW + gap, y, dReadoutW, rowH);
            y += rowH + 2;
        }

        // Decca Toe-out row — label + slider + degrees readout.
        {
            deccaToeOutLabel.setBounds   (x0, y, dLabelW, rowH);
            deccaToeOutSlider.setBounds  (x0 + dLabelW + gap, y, dSlW, rowH);
            deccaToeOutReadout.setBounds (x0 + dLabelW + gap + dSlW + gap, y, dReadoutW, rowH);
            y += rowH + 2;
        }

        // Tilt row — drives both micl_tilt/micr_tilt and decca_tilt; sits
        // directly above the bottom RT60 bar.
        {
            mainTiltLabel.setBounds   (x0, y, dLabelW, rowH);
            mainTiltSlider.setBounds  (x0 + dLabelW + gap, y, dSlW, rowH);
            mainTiltReadout.setBounds (x0 + dLabelW + gap + dSlW + gap, y, dReadoutW, rowH);
            y += rowH + 2;
        }

        // mainPathInfoLabel kept visible but zero-sized — the empty string
        // didn't render anything anyway.
        mainPathInfoLabel.setBounds (x0, y, 0, 0);
    };

    auto layoutDirectCol = [&] (int colX)
    {
        const int x0    = colX + inset;
        const int ctrlW = colW - inset * 2;
        int y = stripY;

        directHeaderBounds = { x0, y, ctrlW, headerH };
        y += headerH + 4;

        directEnableButton.setBounds (x0, y, ctrlW, rowH);
        y += rowH + 2;

        directPathInfoLabel.setBounds (x0, y, ctrlW, rowH * 2);
    };

    auto layoutOutrigCol = [&] (int colX)
    {
        const int x0    = colX + inset;
        const int ctrlW = colW - inset * 2;
        int y = stripY;

        outrigHeaderBounds = { x0, y, ctrlW, headerH };
        y += headerH + 4;

        outrigEnableButton.setBounds (x0, y, ctrlW, rowH);
        y += rowH + 2;

        // Pattern row — label + combo
        outrigPatternLabel.setBounds (x0, y, labelW, rowH);
        outrigPatternCombo.setBounds (x0 + labelW + gap, y,
                                      ctrlW - labelW - gap, rowH);
        y += rowH + 2;

        // Height row — label + slider + readout
        const int slW = ctrlW - labelW - gap - readoutW - gap;
        outrigHeightLabel.setBounds  (x0, y, labelW, rowH);
        outrigHeightSlider.setBounds (x0 + labelW + gap, y, slW, rowH);
        outrigHeightReadout.setBounds(x0 + labelW + gap + slW + gap, y, readoutW, rowH);
        y += rowH + 2;

        // Tilt row — drives outrig_ltilt and outrig_rtilt as a pair.
        outrigTiltLabel.setBounds   (x0, y, labelW, rowH);
        outrigTiltSlider.setBounds  (x0 + labelW + gap, y, slW, rowH);
        outrigTiltReadout.setBounds (x0 + labelW + gap + slW + gap, y, readoutW, rowH);
    };

    auto layoutAmbientCol = [&] (int colX)
    {
        const int x0    = colX + inset;
        const int ctrlW = colW - inset * 2;
        int y = stripY;

        ambientHeaderBounds = { x0, y, ctrlW, headerH };
        y += headerH + 4;

        ambientEnableButton.setBounds (x0, y, ctrlW, rowH);
        y += rowH + 2;

        ambientPatternLabel.setBounds (x0, y, labelW, rowH);
        ambientPatternCombo.setBounds (x0 + labelW + gap, y,
                                       ctrlW - labelW - gap, rowH);
        y += rowH + 2;

        const int slW = ctrlW - labelW - gap - readoutW - gap;
        ambientHeightLabel.setBounds  (x0, y, labelW, rowH);
        ambientHeightSlider.setBounds (x0 + labelW + gap, y, slW, rowH);
        ambientHeightReadout.setBounds(x0 + labelW + gap + slW + gap, y, readoutW, rowH);
        y += rowH + 2;

        // Tilt row — drives ambient_ltilt and ambient_rtilt as a pair.
        ambientTiltLabel.setBounds   (x0, y, labelW, rowH);
        ambientTiltSlider.setBounds  (x0 + labelW + gap, y, slW, rowH);
        ambientTiltReadout.setBounds (x0 + labelW + gap + slW + gap, y, readoutW, rowH);
    };

    const int col0X = stripX;
    const int col1X = stripX + (colW + colGap) * 1;
    const int col2X = stripX + (colW + colGap) * 2;
    const int col3X = stripX + (colW + colGap) * 3;

    layoutMainCol    (col0X);
    layoutDirectCol  (col1X);
    layoutOutrigCol  (col2X);
    layoutAmbientCol (col3X);
}

IRSynthParams IRSynthComponent::getParams() const
{
    IRSynthParams p;
    p.shape = comboSelection (shapeCombo, shapeOptions, kNumShapeOptions).toStdString();
    p.width = widthSlider.getValue();
    p.depth = depthSlider.getValue();
    p.height = heightSlider.getValue();
    p.shapeNavePct   = navePctSlider  .getValue();
    p.shapeTrptPct   = trptPctSlider  .getValue();
    p.shapeTaper     = taperSlider    .getValue();
    p.shapeCornerCut = cornerCutSlider.getValue();
    p.floor_material = comboSelection (floorCombo, materialOptions, 14).toStdString();
    p.ceiling_material = comboSelection (ceilingCombo, materialOptions, 14).toStdString();
    p.wall_material = comboSelection (wallCombo, materialOptions, 14).toStdString();
    p.window_fraction = windowsSlider.getValue();
    p.audience = audienceSlider.getValue();
    p.diffusion = diffusionSlider.getValue();
    p.vault_type = comboSelection (vaultCombo, vaultOptions, 6).toStdString();
    p.organ_case = organSlider.getValue();
    p.balconies = balconiesSlider.getValue();
    auto t = floorPlanComponent.getTransducerState();
    p.source_lx = t.cx[0];   p.source_ly = t.cy[0];   p.spkl_angle = t.angle[0];
    p.source_rx = t.cx[1];   p.source_ry = t.cy[1];   p.spkr_angle = t.angle[1];
    p.receiver_lx = t.cx[2]; p.receiver_ly = t.cy[2]; p.micl_angle = t.angle[2];
    p.receiver_rx = t.cx[3]; p.receiver_ry = t.cy[3]; p.micr_angle = t.angle[3];
    p.micl_tilt = t.tilt[2];
    p.micr_tilt = t.tilt[3];
    p.mic_pattern = comboSelection (micPatternCombo, micOptions, 7).toStdString();

    // Decca Tree capture mode (see IRSynthEngine.h). The toggle is a UI-level
    // switch; the tree centre position and angle are stored on the floor
    // plan's TransducerState so they travel with mic-layout preset data.
    p.main_decca_enabled = deccaEnableButton.getToggleState();
    p.decca_cx    = t.deccaCx;
    p.decca_cy    = t.deccaCy;
    p.decca_angle = t.deccaAngle;
    p.decca_tilt  = t.deccaTilt;
    p.decca_centre_gain = deccaCentreGainSlider.getValue();
    p.decca_toe_out     = deccaToeOutSlider.getValue() * M_PI / 180.0;
    p.er_only = erOnlyButton.getToggleState();
    // sample_rate is pinned to 48 kHz — the native rate of all factory IRs.
    // The picker that used to drive this was removed in v2.8.x (see layout
    // comment in resized() for full rationale); JUCE's Convolution resamples
    // the IR to the host rate regardless. Any old preset that saved
    // sr="44100" will synthesise at 48 kHz on the next Calculate IR, but the
    // previously-rendered raw synth buffer stored in the preset still plays
    // back at its original rate until re-rendered.
    p.sample_rate = 48000;

    // Mic paths (DIRECT / OUTRIG / AMBIENT).
    p.direct_enabled  = directEnableButton.getToggleState();
    p.outrig_enabled  = outrigEnableButton.getToggleState();
    p.ambient_enabled = ambientEnableButton.getToggleState();

    p.outrig_lx      = t.cx[4];    p.outrig_ly      = t.cy[4];
    p.outrig_rx      = t.cx[5];    p.outrig_ry      = t.cy[5];
    p.outrig_langle  = t.angle[4]; p.outrig_rangle  = t.angle[5];
    p.outrig_ltilt   = t.tilt[4];  p.outrig_rtilt   = t.tilt[5];
    p.outrig_height  = outrigHeightSlider.getValue();
    p.outrig_pattern = comboSelection (outrigPatternCombo, micOptions, 7).toStdString();

    p.ambient_lx     = t.cx[6];    p.ambient_ly     = t.cy[6];
    p.ambient_rx     = t.cx[7];    p.ambient_ry     = t.cy[7];
    p.ambient_langle = t.angle[6]; p.ambient_rangle = t.angle[7];
    p.ambient_ltilt  = t.tilt[6];  p.ambient_rtilt  = t.tilt[7];
    p.ambient_height = ambientHeightSlider.getValue();
    p.ambient_pattern = comboSelection (ambientPatternCombo, micOptions, 7).toStdString();

    // Experimental early-reflection A/B toggles (see IRSynthEngine.h).
    p.direct_max_order         = juce::jlimit (0, 2, directMaxOrderCombo.getSelectedId() - 1);
    p.lambert_scatter_enabled  = lambertScatterButton.getToggleState();
    p.spk_directivity_full     = spkDirFullButton    .getToggleState();

    // Floor-plan Option-mirror axis (UI-only; not consumed by the synthesis
    // engine, persisted so the preference round-trips with the rest of the
    // floor-plan UI state).
    p.mirror_axis = (floorPlanComponent.mirrorAxis == FloorPlanComponent::MirrorAxis::Horizontal) ? 1 : 0;

    return p;
}

void IRSynthComponent::setParams (const IRSynthParams& p)
{
    suppressingParamNotifications = true;

    // v2.8.0 shape migration for legacy APVTS state that embedded the old
    // strings. irSynthParamsFromXml handles the XML path; this covers the
    // case where setParams is called with a pre-migrated p struct that still
    // carries an old string (e.g. in-process preset restore).
    juce::String shapeStr = juce::String (p.shape);
    if (shapeStr == "L-shaped")    shapeStr = "Rectangular";
    if (shapeStr == "Cylindrical") shapeStr = "Circular Hall";
    setComboTo (shapeCombo, shapeStr, shapeOptions, kNumShapeOptions);
    widthSlider.setValue (p.width, juce::dontSendNotification);
    depthSlider.setValue (p.depth, juce::dontSendNotification);
    heightSlider.setValue (p.height, juce::dontSendNotification);
    widthValueLabel.setText (juce::String (p.width, 1), juce::dontSendNotification);
    depthValueLabel.setText (juce::String (p.depth, 1), juce::dontSendNotification);
    heightValueLabel.setText (juce::String (p.height, 1), juce::dontSendNotification);

    navePctSlider  .setValue (p.shapeNavePct,   juce::dontSendNotification);
    trptPctSlider  .setValue (p.shapeTrptPct,   juce::dontSendNotification);
    taperSlider    .setValue (p.shapeTaper,     juce::dontSendNotification);
    cornerCutSlider.setValue (p.shapeCornerCut, juce::dontSendNotification);
    navePctReadout  .setText (juce::String (p.shapeNavePct,   2), juce::dontSendNotification);
    trptPctReadout  .setText (juce::String (p.shapeTrptPct,   2), juce::dontSendNotification);
    taperReadout    .setText (juce::String (p.shapeTaper,     2), juce::dontSendNotification);
    cornerCutReadout.setText (juce::String (p.shapeCornerCut, 2), juce::dontSendNotification);
    updateShapeProportionVisibility();
    setComboTo (floorCombo, p.floor_material, materialOptions, 14);
    setComboTo (ceilingCombo, p.ceiling_material, materialOptions, 14);
    setComboTo (wallCombo, p.wall_material, materialOptions, 14);
    windowsSlider.setValue (p.window_fraction, juce::dontSendNotification);
    windowsReadout.setText (juce::String (juce::roundToInt (p.window_fraction * 100)) + "%", juce::dontSendNotification);
    audienceSlider.setValue (p.audience, juce::dontSendNotification);
    diffusionSlider.setValue (p.diffusion, juce::dontSendNotification);
    setComboTo (vaultCombo, p.vault_type, vaultOptions, 6);
    organSlider.setValue (p.organ_case, juce::dontSendNotification);
    balconiesSlider.setValue (p.balconies, juce::dontSendNotification);
    TransducerState t;
    t.cx[0] = p.source_lx;   t.cy[0] = p.source_ly;   t.angle[0] = p.spkl_angle;
    t.cx[1] = p.source_rx;   t.cy[1] = p.source_ry;   t.angle[1] = p.spkr_angle;
    t.cx[2] = p.receiver_lx; t.cy[2] = p.receiver_ly; t.angle[2] = p.micl_angle;
    t.cx[3] = p.receiver_rx; t.cy[3] = p.receiver_ry; t.angle[3] = p.micr_angle;
    t.tilt[2] = p.micl_tilt; t.tilt[3] = p.micr_tilt;
    t.cx[4] = p.outrig_lx;   t.cy[4] = p.outrig_ly;   t.angle[4] = p.outrig_langle;
    t.cx[5] = p.outrig_rx;   t.cy[5] = p.outrig_ry;   t.angle[5] = p.outrig_rangle;
    t.tilt[4] = p.outrig_ltilt; t.tilt[5] = p.outrig_rtilt;
    t.cx[6] = p.ambient_lx;  t.cy[6] = p.ambient_ly;  t.angle[6] = p.ambient_langle;
    t.cx[7] = p.ambient_rx;  t.cy[7] = p.ambient_ry;  t.angle[7] = p.ambient_rangle;
    t.tilt[6] = p.ambient_ltilt; t.tilt[7] = p.ambient_rtilt;
    t.deccaCx    = p.decca_cx;
    t.deccaCy    = p.decca_cy;
    t.deccaAngle = p.decca_angle;
    t.deccaTilt  = p.decca_tilt;
    floorPlanComponent.setTransducerState (t);
    floorPlanComponent.outrigVisible  = p.outrig_enabled;
    floorPlanComponent.ambientVisible = p.ambient_enabled;
    floorPlanComponent.deccaVisible   = p.main_decca_enabled;
    floorPlanComponent.repaint();
    // Migrate legacy "cardioid" (written by older plugin versions) to the new "cardioid (LDC)" display key
    auto migratePattern = [] (juce::String s)
    {
        if (s.trim().equalsIgnoreCase ("cardioid"))
            return juce::String ("cardioid (LDC)");
        return s;
    };
    setComboTo (micPatternCombo,     migratePattern (p.mic_pattern),     micOptions, 7);
    setComboTo (outrigPatternCombo,  migratePattern (p.outrig_pattern),  micOptions, 7);
    setComboTo (ambientPatternCombo, migratePattern (p.ambient_pattern), micOptions, 7);
    erOnlyButton.setToggleState (p.er_only, juce::dontSendNotification);
    // p.sample_rate is no longer surfaced in the UI (v2.8.x removal of the
    // Sample Rate picker). Value is preserved on the params struct for XML
    // round-trip but has no UI control to populate.

    directEnableButton.setToggleState  (p.direct_enabled,  juce::dontSendNotification);
    outrigEnableButton.setToggleState  (p.outrig_enabled,  juce::dontSendNotification);
    ambientEnableButton.setToggleState (p.ambient_enabled, juce::dontSendNotification);
    deccaEnableButton.setToggleState   (p.main_decca_enabled, juce::dontSendNotification);
    deccaCentreGainSlider.setValue     (p.decca_centre_gain, juce::dontSendNotification);
    {
        const double v = p.decca_centre_gain;
        const auto txt = (v <= 1e-6) ? juce::String ("off")
                                     : juce::String ((int) std::round (20.0 * std::log10 (v))) + "dB";
        deccaCentreGainReadout.setText (txt, juce::dontSendNotification);
    }
    {
        const double deg = juce::jlimit (0.0, 90.0, p.decca_toe_out * 180.0 / M_PI);
        deccaToeOutSlider.setValue (deg, juce::dontSendNotification);
        const int degI = (int) std::round (deg);
        const auto txt = (degI == 0) ? juce::String ("off")
                                     : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (degI) + juce::String::fromUTF8 ("\xc2\xb0");
        deccaToeOutReadout.setText (txt, juce::dontSendNotification);
    }
    outrigHeightSlider.setValue  (p.outrig_height,  juce::dontSendNotification);
    ambientHeightSlider.setValue (p.ambient_height, juce::dontSendNotification);

    // Tilt sliders — derived in degrees from the radian *_tilt fields.
    // MAIN slider follows micl_tilt (paired with micr_tilt and decca_tilt by
    // the slider's own onValueChange); OUTRIG/AMBIENT follow each pair's
    // L tilt. Read-back uses jlimit so out-of-range legacy values clamp
    // cleanly into the slider's −90..+90° range.
    {
        auto setTilt = [] (juce::Slider& s, juce::Label& r, double rad)
        {
            const double deg = juce::jlimit (-90.0, 90.0, rad * 180.0 / M_PI);
            s.setValue (deg, juce::dontSendNotification);
            r.setText (juce::String ((int) std::round (deg)) + juce::String::fromUTF8 ("\xc2\xb0"),
                       juce::dontSendNotification);
        };
        setTilt (mainTiltSlider,    mainTiltReadout,    p.micl_tilt);
        setTilt (outrigTiltSlider,  outrigTiltReadout,  p.outrig_ltilt);
        setTilt (ambientTiltSlider, ambientTiltReadout, p.ambient_ltilt);
    }

    // Experimental early-reflection A/B toggles.
    directMaxOrderCombo.setSelectedId (juce::jlimit (0, 2, p.direct_max_order) + 1,
                                       juce::dontSendNotification);
    lambertScatterButton.setToggleState (p.lambert_scatter_enabled,
                                         juce::dontSendNotification);
    spkDirFullButton    .setToggleState (p.spk_directivity_full,
                                         juce::dontSendNotification);
    outrigHeightReadout.setText  (juce::String (p.outrig_height,  1) + " m", juce::dontSendNotification);
    ambientHeightReadout.setText (juce::String (p.ambient_height, 1) + " m", juce::dontSendNotification);

    // Option-mirror axis: restore the preference onto both the floor plan
    // (used immediately by the next drag) and the two toggle buttons (so the
    // UI reflects the stored value). Guarded like all other setParams() writes
    // by the suppressingParamNotifications flag that wraps this method.
    {
        const bool horiz = (p.mirror_axis == 1);
        floorPlanComponent.mirrorAxis = horiz ? FloorPlanComponent::MirrorAxis::Horizontal
                                              : FloorPlanComponent::MirrorAxis::Vertical;
        if (mirrorVerticalButton)
            mirrorVerticalButton->setToggleState   (! horiz, juce::dontSendNotification);
        if (mirrorHorizontalButton)
            mirrorHorizontalButton->setToggleState (  horiz, juce::dontSendNotification);
    }

    suppressingParamNotifications = false;
}

void IRSynthComponent::timerCallback()
{
    if (! widthValueLabel.hasKeyboardFocus (true))
        widthValueLabel.setText (juce::String (widthSlider.getValue(), 1), juce::dontSendNotification);
    if (! depthValueLabel.hasKeyboardFocus (true))
        depthValueLabel.setText (juce::String (depthSlider.getValue(), 1), juce::dontSendNotification);
    if (! heightValueLabel.hasKeyboardFocus (true))
        heightValueLabel.setText (juce::String (heightSlider.getValue(), 1), juce::dontSendNotification);
    floorPlanComponent.repaint();
    windowsReadout.setText (juce::String (juce::roundToInt (windowsSlider.getValue() * 100)) + "%", juce::dontSendNotification);
    audienceReadout.setText (juce::String (audienceSlider.getValue(), 2), juce::dontSendNotification);
    diffusionReadout.setText (juce::String (diffusionSlider.getValue(), 2), juce::dontSendNotification);
    organReadout.setText (juce::String (organSlider.getValue(), 2), juce::dontSendNotification);
    balconiesReadout.setText (juce::String (balconiesSlider.getValue(), 2), juce::dontSendNotification);
    outrigHeightReadout.setText  (juce::String (outrigHeightSlider.getValue(),  1) + " m", juce::dontSendNotification);
    ambientHeightReadout.setText (juce::String (ambientHeightSlider.getValue(), 1) + " m", juce::dontSendNotification);
    updateRT60Display();

    if (! irCombo.isPopupActive())
    {
        juce::String txt = irCombo.getText();
        if (dirty)
        {
            if (! txt.endsWith (" *"))
                irCombo.setText (txt + " *", juce::dontSendNotification);
        }
        else if (txt.endsWith (" *"))
        {
            irCombo.setText (txt.dropLastCharacters (2), juce::dontSendNotification);
        }
    }

    // Refresh the per-path filename labels above the bottom bar from the
    // processor's display-name supplier. Cheap string compare-and-set so
    // setText with juce::dontSendNotification only triggers a repaint when
    // the text actually changes.
    if (pathDisplayNameSupplier)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto s = pathDisplayNameSupplier (i);
            if (pathNameLabels[i].getText() != s)
                pathNameLabels[i].setText (s, juce::dontSendNotification);
        }
    }
}

void IRSynthComponent::buttonClicked (juce::Button* b)
{
    if (b == &previewButton && ! synthRunning.load())
        startSynthesis();
    else if (b == &bakeBalanceButton && ! synthRunning.load())
        startSynthesis (true);
    else if (b == &doneButton)
        onDone();
    else if (b == &saveIRButton && onSaveIRFn)
    {
        juce::String current = irCombo.getText().trim();
        if (current.endsWith ("*"))
            current = current.dropLastCharacters (1).trim();
        if (current.isEmpty())
            current = "My IR";

        auto* aw = new juce::AlertWindow ("Save IR As", "Enter a name for the IR:",
                                          juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor ("irName", current, "Name:");
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw] (int result)
            {
                if (result == 1)
                {
                    juce::String saveName = aw->getTextEditorContents ("irName").trim();
                    if (saveName.isNotEmpty() && onSaveIRFn)
                    {
                        onSaveIRFn (saveName);
                        dirty = false;
                    }
                }
                delete aw;
            }), true);
    }
}

void IRSynthComponent::comboBoxChanged (juce::ComboBox* combo)
{
    if (combo == &irCombo && onLoadIRFn)
    {
        int id = irCombo.getSelectedId();
        if (id >= 1)
            onLoadIRFn (id - 1);
    }
    else if (combo != &irCombo && ! suppressingParamNotifications && onParamModifiedFn)
    {
        if (combo == &shapeCombo)
        {
            updateShapeProportionVisibility();
            floorPlanComponent.repaint();
            resized();
        }
        onParamModifiedFn();
    }
}

// Show only the shape-proportion sliders the current shape actually consumes.
// Called from comboBoxChanged + setParams.
void IRSynthComponent::updateShapeProportionVisibility()
{
    const juce::String shape = shapeCombo.getText();
    const bool cathedral    = (shape == "Cathedral");
    const bool fanShoebox   = (shape == "Fan / Shoebox");
    const bool octagonal    = (shape == "Octagonal");
    const bool circularHall = (shape == "Circular Hall");

    auto setRowVisible = [] (juce::Slider& s, juce::Label& lbl, juce::Label& readout, bool v)
    {
        s.setVisible (v);
        lbl.setVisible (v);
        readout.setVisible (v);
    };
    setRowVisible (navePctSlider,   navePctLabel,   navePctReadout,   cathedral);
    setRowVisible (trptPctSlider,   trptPctLabel,   trptPctReadout,   cathedral);
    setRowVisible (taperSlider,     taperLabel,     taperReadout,     fanShoebox);
    // Corner Cut covers both octagonal chamfer and circular ellipse-roundness.
    setRowVisible (cornerCutSlider, cornerCutLabel, cornerCutReadout, octagonal || circularHall);

    // Retitle the cornerCut row to match its semantic role for the current shape.
    if (circularHall)      cornerCutLabel.setText ("Roundness", juce::dontSendNotification);
    else /* octagonal */   cornerCutLabel.setText ("Cnr Cut",   juce::dontSendNotification);
}

void IRSynthComponent::setIRList (const juce::Array<IRManager::IREntry>& entries)
{
    // Preserve the currently displayed text across list rebuilds. ComboBox::clear()
    // with dontSendNotification clears items but not the text field; the caller
    // decides what (if anything) to display afterwards via setSelectedIRDisplayName.
    const juce::String previousText = irCombo.getText();
    irCombo.clear (juce::dontSendNotification);

    bool factoryHeaderAdded = false;
    juce::String lastCategory;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (! e.isFactory) break;

        if (! factoryHeaderAdded)
        {
            irCombo.addSectionHeading ("Factory");
            factoryHeaderAdded = true;
        }
        if (e.category != lastCategory)
        {
            if (e.category.isNotEmpty())
                irCombo.addSectionHeading ("  " + e.category);
            lastCategory = e.category;
        }
        irCombo.addItem (e.file.getFileNameWithoutExtension(), i + 1);
    }

    bool userHeaderAdded = false;
    lastCategory = {};
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (e.isFactory) continue;

        if (! userHeaderAdded)
        {
            irCombo.addSectionHeading ("Your IRs");
            userHeaderAdded = true;
        }
        if (e.category != lastCategory)
        {
            if (e.category.isNotEmpty())
                irCombo.addSectionHeading ("  " + e.category);
            lastCategory = e.category;
        }
        irCombo.addItem (e.file.getFileNameWithoutExtension(), i + 1);
    }

    irCombo.setEditableText (true);
    irCombo.setText (previousText, juce::dontSendNotification);
}

void IRSynthComponent::setSelectedIRDisplayName (const juce::String& name)
{
    int id = -1;
    for (int i = 0; i < irCombo.getNumItems(); ++i)
        if (irCombo.getItemText (i) == name)
            { id = i + 1; break; }
    if (id >= 1)
        irCombo.setSelectedId (id, juce::dontSendNotification);
    else
        irCombo.setText (name, juce::dontSendNotification);
}

void IRSynthComponent::startSynthesis (bool bakeCurrentBalance)
{
    IRSynthParams p = getParams();
    if (bakeCurrentBalance)
    {
        p.bake_er_tail_balance = true;
        if (bakeLevelsGetter)
        {
            auto [erDb, tailDb] = bakeLevelsGetter();
            p.baked_er_gain = juce::Decibels::decibelsToGain (erDb);
            p.baked_tail_gain = juce::Decibels::decibelsToGain (tailDb);
        }
    }
    else
    {
        p.bake_er_tail_balance = false;
        p.baked_er_gain = 1.0;
        p.baked_tail_gain = 1.0;
    }

    lastRenderParams = p;
    synthRunning = true;
    previewButton.setEnabled (false);
    bakeBalanceButton.setEnabled (false);
    pendingResult.reset();
    progressValue = 0.0;
    progressLabel.setText ("Starting\xe2\x80\xa6", juce::dontSendNotification);

    synthPool.addJob ([this, p]
    {
        IRSynthProgressFn progressCb = [this] (double frac, const std::string& msg)
        {
            juce::MessageManager::callAsync ([this, frac, msg]
            {
                updateProgress (frac, juce::String (msg));
            });
        };

        IRSynthResult result = IRSynthEngine::synthIR (p, progressCb);

        juce::MessageManager::callAsync ([this, result]
        {
            synthRunning = false;
            previewButton.setEnabled (true);
            bakeBalanceButton.setEnabled (true);
            progressValue = 1.0;
            progressLabel.setText ("Done.", juce::dontSendNotification);

            if (result.success && onComplete)
            {
                onComplete (result);
            }
        });
    });
}

void IRSynthComponent::onDone()
{
    if (onDoneFn)
        onDoneFn();
}

void IRSynthComponent::updateRT60Display()
{
    auto rt = IRSynthEngine::calcRT60 (getParams());
    for (int i = 0; i < 6 && i < (int) rt.size(); ++i)
        rt60Values[i].setText (juce::String (rt[(size_t) i], 2) + "s", juce::dontSendNotification);
}

void IRSynthComponent::updateProgress (double fraction, const juce::String& message)
{
    progressValue = fraction;
    progressLabel.setText (message, juce::dontSendNotification);
}
