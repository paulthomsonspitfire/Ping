#include "IRSynthComponent.h"
#include "PingBinaryData.h"

namespace
{
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour accent      { 0xff8cd6ef };
    const juce::Colour textCol     { 0xffe8e8e8 };
    const juce::Colour textDim     { 0xff909090 };

    const char* const shapeOptions[] = {
        "Rectangular", "L-shaped", "Fan / Shoebox", "Cylindrical", "Cathedral", "Octagonal"
    };
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
    const char* const micOptions[] = { "omni", "subcardioid", "cardioid", "figure8" };

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

IRSynthComponent::IRSynthComponent()
{
    setOpaque (true);

    // Load background texture (same brushed-steel image as the main plugin UI)
    bgTexture = juce::ImageCache::getFromMemory (BinaryData::texture_bg_jpg,
                                                  BinaryData::texture_bg_jpgSize);

    // Room geometry (previously "Placement" tab)
    addOptions (shapeCombo, shapeOptions, 6);
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
    addOptions (micPatternCombo, micOptions, 4);
    micPatternCombo.setSelectedId (3, juce::dontSendNotification); // cardioid
    sampleRateCombo.addItem ("44100", 1);
    sampleRateCombo.addItem ("48000", 2);
    sampleRateCombo.setSelectedId (2, juce::dontSendNotification);
    erOnlyButton.setToggleState (false, juce::dontSendNotification);
    bakeBalanceButton.addListener (this);
    bakeBalanceButton.setColour (juce::TextButton::buttonColourId, panelBg);
    bakeBalanceButton.setColour (juce::TextButton::textColourOffId, textDim);
    micPatternLabel.setText ("Mic Pattern", juce::dontSendNotification);
    sampleRateLabel.setText ("Sample Rate", juce::dontSendNotification);

    // Add all controls directly (no tabs / viewports)
    addAndMakeVisible (floorPlanComponent);
    floorPlanComponent.setParamsGetter ([this] { return getParams(); });
    floorPlanComponent.setOnPlacementChanged ([this] { if (onParamModifiedFn) onParamModifiedFn(); });

    auto notifyParamChanged = [this] { if (! suppressingParamNotifications && onParamModifiedFn) onParamModifiedFn(); };
    for (auto* s : { &widthSlider, &depthSlider, &heightSlider, &windowsSlider,
                     &audienceSlider, &diffusionSlider, &organSlider, &balconiesSlider })
        s->onValueChange = notifyParamChanged;
    erOnlyButton.onClick = notifyParamChanged;
    for (auto* cb : { &shapeCombo, &floorCombo, &ceilingCombo, &wallCombo,
                      &vaultCombo, &micPatternCombo, &sampleRateCombo })
        cb->addListener (this);

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
    addAndMakeVisible (sampleRateCombo);
    addAndMakeVisible (erOnlyButton);
    addAndMakeVisible (bakeBalanceButton);
    addAndMakeVisible (micPatternLabel);
    addAndMakeVisible (sampleRateLabel);

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
                      &vaultCombo, &micPatternCombo, &sampleRateCombo })
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
                     &micPatternLabel, &sampleRateLabel })
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
}

void IRSynthComponent::resized()
{
    auto b = getLocalBounds().reduced (12);
    const int barH = 52;
    auto contentArea = b.removeFromTop (b.getHeight() - barH);
    auto barArea = b;

    // ── Left / right column split ───────────────────────────────────────────
    // Left column: all acoustic-character + room-geometry controls (~35% width).
    // Right column: FloorPlanComponent, always visible (remaining ~65% width).
    const int leftW = juce::jmax (280, (int) (0.35f * contentArea.getWidth()));
    auto leftCol  = contentArea.removeFromLeft (leftW);
    auto rightCol = contentArea;   // remaining width

    layoutControls (leftCol);
    floorPlanComponent.setBounds (rightCol.reduced (8));

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

    // Progress bar right-justified (before Main Menu), shifted 30px left for visual balance
    const int progW = std::min (200, rightX - leftX - 12);
    progressBar.setBounds (rightX - progW - 30, barY + 8, progW, 16);
    progressLabel.setBounds (rightX - progW - 30, barY + 26, progW, 14);
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
    // Mic Pattern and Sample Rate each get a label row + control row (tighter
    // than a full combo-row, matching the old Character-tab right-column style).
    optionsHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;

    const int optComboW = juce::jmin (140, comboW);

    micPatternLabel.setBounds (x0, y, ctrlW, rowH - 4);              y += rowH - 2;
    micPatternCombo.setBounds (x0, y, optComboW, rowH);              y += rowH + secGap / 2;

    sampleRateLabel.setBounds (x0, y, ctrlW, rowH - 4);              y += rowH - 2;
    sampleRateCombo.setBounds (x0, y, juce::jmin (100, optComboW), rowH); y += rowH + secGap / 2;

    erOnlyButton.setBounds     (x0, y, ctrlW, rowH);                  y += rowH + 4;
    bakeBalanceButton.setBounds(x0, y, juce::jmax (160, ctrlW - 20), rowH + 2); y += rowH + secGap;

    // ── ROOM GEOMETRY ────────────────────────────────────────────────────────
    roomHeaderBounds = { x0, y, ctrlW, headerH };
    y += headerH + 2;

    // Shape combo spans the full control width
    shapeCombo.setBounds (x0, y, ctrlW, rowH);                       y += rowH + gap;

    // Dimension rows: name label + editable value label, then slider below
    auto dimRow = [&] (juce::Label& nameLbl, juce::Label& valueLbl, juce::Slider& slider)
    {
        const int valueBoxW = 48;
        nameLbl.setBounds  (x0,        y, 52,         rowH);
        valueLbl.setBounds (x0 + 54,   y, valueBoxW,  rowH);
        y += rowH + 2;
        slider.setBounds   (x0,        y, ctrlW,      rowH);
        y += rowH + gap;
    };
    dimRow (widthLabel,  widthValueLabel,  widthSlider);
    dimRow (depthLabel,  depthValueLabel,  depthSlider);
    dimRow (heightLabel, heightValueLabel, heightSlider);
}

IRSynthParams IRSynthComponent::getParams() const
{
    IRSynthParams p;
    p.shape = comboSelection (shapeCombo, shapeOptions, 6).toStdString();
    p.width = widthSlider.getValue();
    p.depth = depthSlider.getValue();
    p.height = heightSlider.getValue();
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
    p.mic_pattern = comboSelection (micPatternCombo, micOptions, 4).toStdString();
    p.er_only = erOnlyButton.getToggleState();
    p.sample_rate = sampleRateCombo.getSelectedId() == 1 ? 44100 : 48000;
    return p;
}

void IRSynthComponent::setParams (const IRSynthParams& p)
{
    suppressingParamNotifications = true;

    setComboTo (shapeCombo, p.shape, shapeOptions, 6);
    widthSlider.setValue (p.width, juce::dontSendNotification);
    depthSlider.setValue (p.depth, juce::dontSendNotification);
    heightSlider.setValue (p.height, juce::dontSendNotification);
    widthValueLabel.setText (juce::String (p.width, 1), juce::dontSendNotification);
    depthValueLabel.setText (juce::String (p.depth, 1), juce::dontSendNotification);
    heightValueLabel.setText (juce::String (p.height, 1), juce::dontSendNotification);
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
    floorPlanComponent.setTransducerState (t);
    floorPlanComponent.repaint();
    setComboTo (micPatternCombo, p.mic_pattern, micOptions, 4);
    erOnlyButton.setToggleState (p.er_only, juce::dontSendNotification);
    sampleRateCombo.setSelectedId (p.sample_rate == 44100 ? 1 : 2, juce::dontSendNotification);

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
        juce::String name = irCombo.getText().trim();
        if (name.endsWith ("*"))
            name = name.dropLastCharacters (1).trim();
        if (name.isEmpty())
            return;

        if (dirty)
        {
            auto* aw = new juce::AlertWindow ("Save IR As", "Enter a name for the IR:",
                                              juce::MessageBoxIconType::NoIcon, this);
            aw->addTextEditor ("irName", name, "Name:");
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
        else
        {
            onSaveIRFn (name);
        }
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
        onParamModifiedFn();
    }
}

void IRSynthComponent::setIRList (const juce::Array<IRManager::IREntry>& entries)
{
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
    irCombo.setText ("", juce::dontSendNotification);
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
