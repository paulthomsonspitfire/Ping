#include "IRSynthComponent.h"

namespace
{
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour accent      { 0xffe8a84a };
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

    void setComboTo (juce::ComboBox& combo, const juce::String& value, const char* const* opts, int n)
    {
        for (int i = 0; i < n; ++i)
            if (value == opts[i])
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

    // Room (Placement tab)
    addOptions (shapeCombo, shapeOptions, 6);
    shapeCombo.setSelectedId (1, juce::dontSendNotification);

    widthSlider.setRange (0.5, 50.0, 0.5);
    depthSlider.setRange (0.5, 50.0, 0.5);
    heightSlider.setRange (1.0, 30.0, 0.5);
    widthSlider.setValue (8.0);
    depthSlider.setValue (12.0);
    heightSlider.setValue (4.0);

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

    // DEBUG sliders: position 0..1, angle -π..π (display as degrees)
    for (auto* s : { &spkLXSlider, &spkLYSlider, &spkRXSlider, &spkRYSlider,
                     &micLXSlider, &micLYSlider, &micRXSlider, &micRYSlider })
    {
        s->setRange (0.0, 1.0, 0.01);
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }
    const double kPi = 3.14159265358979323846;
    for (auto* s : { &spkLAngSlider, &spkRAngSlider, &micLAngSlider, &micRAngSlider })
    {
        s->setRange (-kPi, kPi, 0.01);
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
        s->setNumDecimalPlacesToDisplay (1);
    }
    spkLXSlider.setValue (0.35); spkLYSlider.setValue (0.30); spkLAngSlider.setValue (1.5 * kPi);
    spkRXSlider.setValue (0.65); spkRYSlider.setValue (0.30); spkRAngSlider.setValue (1.5 * kPi);
    micLXSlider.setValue (0.40); micLYSlider.setValue (0.70); micLAngSlider.setValue (0.5 * kPi);
    micRXSlider.setValue (0.60); micRYSlider.setValue (0.70); micRAngSlider.setValue (0.5 * kPi);

    auto syncFloorPlanFromSliders = [this]
    {
        TransducerState t;
        t.cx[0] = spkLXSlider.getValue();  t.cy[0] = spkLYSlider.getValue();  t.angle[0] = spkLAngSlider.getValue();
        t.cx[1] = spkRXSlider.getValue();  t.cy[1] = spkRYSlider.getValue();  t.angle[1] = spkRAngSlider.getValue();
        t.cx[2] = micLXSlider.getValue();  t.cy[2] = micLYSlider.getValue();  t.angle[2] = micLAngSlider.getValue();
        t.cx[3] = micRXSlider.getValue();  t.cy[3] = micRYSlider.getValue();  t.angle[3] = micRAngSlider.getValue();
        floorPlanComponent.setTransducerState (t);
    };
    for (auto* s : { &spkLXSlider, &spkLYSlider, &spkLAngSlider, &spkRXSlider, &spkRYSlider, &spkRAngSlider,
                     &micLXSlider, &micLYSlider, &micLAngSlider, &micRXSlider, &micRYSlider, &micRAngSlider })
        s->onValueChange = syncFloorPlanFromSliders;
    syncFloorPlanFromSliders();

    // Surfaces (Character tab)
    addOptions (floorCombo, materialOptions, 14);
    addOptions (ceilingCombo, materialOptions, 14);
    addOptions (wallCombo, materialOptions, 14);
    floorCombo.setSelectedId (3, juce::dontSendNotification);   // Hardwood floor
    ceilingCombo.setSelectedId (8, juce::dontSendNotification); // Acoustic ceiling tile
    wallCombo.setSelectedId (2, juce::dontSendNotification);   // Painted plaster

    floorLabel.setText ("Floor", juce::dontSendNotification);
    ceilingLabel.setText ("Ceiling", juce::dontSendNotification);
    wallLabel.setText ("Walls", juce::dontSendNotification);
    // Contents
    audienceSlider.setRange (0.0, 1.0, 0.01);
    diffusionSlider.setRange (0.0, 1.0, 0.01);
    audienceSlider.setValue (0.0);
    diffusionSlider.setValue (0.4);
    for (auto* s : { &audienceSlider, &diffusionSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::thumbColourId, accent);
        s->setColour (juce::Slider::trackColourId, panelBorder);
    }
    addAndMakeVisible (audienceLabel);
    addAndMakeVisible (diffusionLabel);
    audienceLabel.setText ("Audience", juce::dontSendNotification);
    diffusionLabel.setText ("Diffusion", juce::dontSendNotification);

    // Interior
    addOptions (vaultCombo, vaultOptions, 6);
    vaultCombo.setSelectedId (1, juce::dontSendNotification);
    organSlider.setRange (0.0, 1.0, 0.01);
    balconiesSlider.setRange (0.0, 1.0, 0.01);
    organSlider.setValue (0.0);
    balconiesSlider.setValue (0.0);
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
    micPatternLabel.setText ("Mic Pattern", juce::dontSendNotification);
    sampleRateLabel.setText ("Sample Rate", juce::dontSendNotification);

    // Viewports for scrollable tab content (HTML design: sections with fixed row heights)
    characterTab.addAndMakeVisible (characterViewport);
    characterViewport.setViewedComponent (&characterContent, false);
    characterViewport.setScrollBarsShown (true, false);
    characterViewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, panelBorder);

    placementTab.addAndMakeVisible (placementViewport);
    placementViewport.setViewedComponent (&placementContent, false);
    placementViewport.setScrollBarsShown (true, false);
    placementViewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, panelBorder);

    // Add controls to content (Character tab — matches HTML .sec sections)
    characterContent.addAndMakeVisible (floorCombo);
    characterContent.addAndMakeVisible (ceilingCombo);
    characterContent.addAndMakeVisible (wallCombo);
    characterContent.addAndMakeVisible (floorLabel);
    characterContent.addAndMakeVisible (ceilingLabel);
    characterContent.addAndMakeVisible (wallLabel);
    characterContent.addAndMakeVisible (audienceSlider);
    characterContent.addAndMakeVisible (diffusionSlider);
    characterContent.addAndMakeVisible (audienceLabel);
    characterContent.addAndMakeVisible (diffusionLabel);
    characterContent.addAndMakeVisible (audienceReadout);
    characterContent.addAndMakeVisible (diffusionReadout);
    characterContent.addAndMakeVisible (vaultCombo);
    characterContent.addAndMakeVisible (organSlider);
    characterContent.addAndMakeVisible (balconiesSlider);
    characterContent.addAndMakeVisible (vaultLabel);
    characterContent.addAndMakeVisible (organLabel);
    characterContent.addAndMakeVisible (balconiesLabel);
    characterContent.addAndMakeVisible (organReadout);
    characterContent.addAndMakeVisible (balconiesReadout);
    characterContent.addAndMakeVisible (micPatternCombo);
    characterContent.addAndMakeVisible (sampleRateCombo);
    characterContent.addAndMakeVisible (erOnlyButton);
    characterContent.addAndMakeVisible (micPatternLabel);
    characterContent.addAndMakeVisible (sampleRateLabel);

    placementContent.addAndMakeVisible (shapeCombo);
    placementContent.addAndMakeVisible (widthSlider);
    placementContent.addAndMakeVisible (depthSlider);
    placementContent.addAndMakeVisible (heightSlider);
    placementContent.addAndMakeVisible (widthLabel);
    placementContent.addAndMakeVisible (depthLabel);
    placementContent.addAndMakeVisible (heightLabel);
    placementContent.addAndMakeVisible (widthValueLabel);
    placementContent.addAndMakeVisible (depthValueLabel);
    placementContent.addAndMakeVisible (heightValueLabel);
    for (auto* s : { &spkLXSlider, &spkLYSlider, &spkLAngSlider, &spkRXSlider, &spkRYSlider, &spkRAngSlider,
                     &micLXSlider, &micLYSlider, &micLAngSlider, &micRXSlider, &micRYSlider, &micRAngSlider })
        placementContent.addAndMakeVisible (*s);
    placementContent.addAndMakeVisible (floorPlanComponent);
    floorPlanComponent.setParamsGetter ([this] { return getParams(); });

    tabbedComponent.addTab ("Character", juce::Colour (0xff1e1e1e), &characterTab, false);
    tabbedComponent.addTab ("Placement", juce::Colour (0xff1e1e1e), &placementTab, false);
    addAndMakeVisible (tabbedComponent);

    // Bottom bar: RT60 | Preview (centre) | Progress (right) | Accept Cancel
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

    addAndMakeVisible (acceptButton);
    addAndMakeVisible (cancelButton);
    acceptButton.addListener (this);
    cancelButton.addListener (this);
    acceptButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4caf50));
    acceptButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    cancelButton.setColour (juce::TextButton::buttonColourId, panelBg);
    cancelButton.setColour (juce::TextButton::textColourOffId, textDim);
    acceptButton.setVisible (false);
    cancelButton.setVisible (false);

    for (auto* l : { &widthLabel, &depthLabel, &heightLabel, &widthValueLabel, &depthValueLabel, &heightValueLabel,
                     &floorLabel, &ceilingLabel, &wallLabel,
                     &audienceLabel, &diffusionLabel, &vaultLabel, &organLabel, &balconiesLabel,
                     &micPatternLabel, &sampleRateLabel })
        l->setColour (juce::Label::textColourId, textDim);

    startTimerHz (4);
}

void IRSynthComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));
}

void IRSynthComponent::resized()
{
    auto b = getLocalBounds().reduced (12);
    const int barH = 52;
    auto tabArea = b.removeFromTop (b.getHeight() - barH);
    auto barArea = b;

    tabbedComponent.setBounds (tabArea);

    // Tab content area (TabbedComponent gives each tab the area below the tab bar)
    const int tabBarH = 26;
    const int contentW = juce::jmax (280, tabArea.getWidth());
    const int contentH = juce::jmax (200, tabArea.getHeight() - tabBarH);
    // Viewports fill their parent (characterTab/placementTab), which is the content area
    characterViewport.setBounds (0, 0, contentW, contentH);
    placementViewport.setBounds (0, 0, contentW, contentH);

    // Content panels: placement needs extra height for DEBUG sliders
    const int characterContentH = contentH;
    const int placementContentH = contentH + 100;
    characterContent.setSize (contentW, characterContentH);
    placementContent.setSize (contentW, placementContentH);

    layoutCharacterTab (juce::Rectangle<int> (0, 0, contentW, characterContentH));
    layoutPlacementTab (juce::Rectangle<int> (0, 0, contentW, placementContentH));

    // Bottom bar: [RT60 + freq labels + values] | [Preview centre] | [Progress right] | [Accept] [Cancel]
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

    // Accept/Cancel at far right
    const int acceptCancelW = 64 + 6 + 64;
    int rightX = barArea.getRight();
    cancelButton.setBounds (rightX - 64, barY + 6, 64, 24);
    acceptButton.setBounds (rightX - 64 - 6 - 64, barY + 6, 64, 24);
    rightX -= acceptCancelW + 12;

    // Progress bar right-justified (before Accept/Cancel)
    const int progW = std::min (200, rightX - rt60EndX - 100);
    progressBar.setBounds (rightX - progW, barY + 8, progW, 16);
    progressLabel.setBounds (rightX - progW, barY + 26, progW, 14);
    rightX -= progW + 16;

    // Preview button centred in the bar
    const int previewW = 80;
    previewButton.setBounds (barCentreX - previewW / 2, barY + 6, previewW, 24);
}

void IRSynthComponent::layoutCharacterTab (juce::Rectangle<int> b)
{
    const int rowH = 22;
    const int labelW = 84;
    const int readoutW = 40;
    const int gap = 8;
    const int secPad = 14;

    // Combos and sliders: 1/3 of available control width
    const int availForCombo = b.getWidth() - secPad * 2 - labelW - gap;
    const int availForSlider = availForCombo - readoutW - gap;
    const int comboW = juce::jmax (70, availForCombo / 3);
    const int sliderW = juce::jmax (50, availForSlider / 3);

    auto rowCombo = [&] (int y, juce::Component& label, juce::Component& ctrl)
    {
        label.setBounds (secPad, y, labelW, rowH);
        ctrl.setBounds (secPad + labelW + gap, y, comboW, rowH);
    };
    auto rowSlider = [&] (int y, juce::Component& label, juce::Slider& ctrl, juce::Label& readout)
    {
        label.setBounds (secPad, y, labelW, rowH);
        ctrl.setBounds (secPad + labelW + gap, y, sliderW, rowH);
        readout.setBounds (secPad + labelW + gap + sliderW + gap, y, readoutW, rowH);
    };

    int y = secPad;

    // Surfaces — narrow combos
    rowCombo (y, floorLabel, floorCombo);    y += rowH;
    rowCombo (y, ceilingLabel, ceilingCombo); y += rowH;
    rowCombo (y, wallLabel, wallCombo);      y += rowH + secPad;

    // Contents — narrow sliders
    rowSlider (y, audienceLabel, audienceSlider, audienceReadout);  y += rowH;
    rowSlider (y, diffusionLabel, diffusionSlider, diffusionReadout); y += rowH + secPad;

    // Interior
    rowCombo (y, vaultLabel, vaultCombo);    y += rowH;
    rowSlider (y, organLabel, organSlider, organReadout);   y += rowH;
    rowSlider (y, balconiesLabel, balconiesSlider, balconiesReadout); y += rowH + secPad;

    // Right column: Mic Pattern, Sample Rate, Early reflections (stacked) — offset by 1/8 width to avoid slider readouts
    const int rightColX = secPad + labelW + gap + sliderW + gap + readoutW + (b.getWidth() / 8);
    const int rightColW = juce::jmin (140, b.getWidth() - rightColX - secPad);
    int ry = secPad;

    micPatternLabel.setBounds (rightColX, ry, rightColW, rowH);
    ry += rowH + 2;
    micPatternCombo.setBounds (rightColX, ry, rightColW, rowH);
    ry += rowH + secPad;

    sampleRateLabel.setBounds (rightColX, ry, rightColW, rowH);
    ry += rowH + 2;
    sampleRateCombo.setBounds (rightColX, ry, juce::jmin (100, rightColW), rowH);
    ry += rowH + secPad;

    erOnlyButton.setBounds (rightColX, ry, rightColW + 60, rowH);
}

void IRSynthComponent::layoutPlacementTab (juce::Rectangle<int> b)
{
    const int rowH = 22;
    const int gap = 8;
    const int secPad = 14;

    const int leftColW = juce::jmax (180, (int) (0.28f * b.getWidth()));
    const int leftColX = secPad;
    const int floorPlanX = leftColW + gap;
    const int floorPlanW = b.getWidth() - floorPlanX - secPad;
    const int floorPlanH = b.getHeight() - secPad * 2;

    int y = secPad;

    shapeCombo.setBounds (leftColX, y, leftColW - secPad, rowH);
    y += rowH + gap;

    const int valueBoxW = 48;
    auto dimRow = [&] (juce::Label& nameLabel, juce::Label& valueLabel, juce::Slider& slider)
    {
        nameLabel.setBounds (leftColX, y, 52, rowH);
        valueLabel.setBounds (leftColX + 54, y, valueBoxW, rowH);
        y += rowH + 2;
        slider.setBounds (leftColX, y, leftColW - secPad, rowH);
        y += rowH + gap;
    };
    dimRow (widthLabel, widthValueLabel, widthSlider);
    dimRow (depthLabel, depthValueLabel, depthSlider);
    dimRow (heightLabel, heightValueLabel, heightSlider);

    const int dbgSliderH = 18;
    int yy = y;
    int col1 = leftColX, col2 = leftColX + 58, col3 = leftColX + 116;
    spkLXSlider.setBounds (col1, yy, 52, dbgSliderH);
    spkLYSlider.setBounds (col2, yy, 52, dbgSliderH);
    spkLAngSlider.setBounds (col3, yy, leftColW - secPad - col3, dbgSliderH);
    yy += dbgSliderH + 2;
    spkRXSlider.setBounds (col1, yy, 52, dbgSliderH);
    spkRYSlider.setBounds (col2, yy, 52, dbgSliderH);
    spkRAngSlider.setBounds (col3, yy, leftColW - secPad - col3, dbgSliderH);
    yy += dbgSliderH + 2;
    micLXSlider.setBounds (col1, yy, 52, dbgSliderH);
    micLYSlider.setBounds (col2, yy, 52, dbgSliderH);
    micLAngSlider.setBounds (col3, yy, leftColW - secPad - col3, dbgSliderH);
    yy += dbgSliderH + 2;
    micRXSlider.setBounds (col1, yy, 52, dbgSliderH);
    micRYSlider.setBounds (col2, yy, 52, dbgSliderH);
    micRAngSlider.setBounds (col3, yy, leftColW - secPad - col3, dbgSliderH);
    yy += dbgSliderH + 6;

    floorPlanComponent.setBounds (floorPlanX, secPad, floorPlanW, juce::jmax (120, floorPlanH));
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
    p.audience = audienceSlider.getValue();
    p.diffusion = diffusionSlider.getValue();
    p.vault_type = comboSelection (vaultCombo, vaultOptions, 6).toStdString();
    p.organ_case = organSlider.getValue();
    p.balconies = balconiesSlider.getValue();
    // Use DEBUG sliders directly (they drive floor plan; no angle conversion for now)
    p.source_lx = spkLXSlider.getValue();   p.source_ly = spkLYSlider.getValue();   p.spkl_angle = spkLAngSlider.getValue();
    p.source_rx = spkRXSlider.getValue();   p.source_ry = spkRYSlider.getValue();   p.spkr_angle = spkRAngSlider.getValue();
    p.receiver_lx = micLXSlider.getValue(); p.receiver_ly = micLYSlider.getValue(); p.micl_angle = micLAngSlider.getValue();
    p.receiver_rx = micRXSlider.getValue(); p.receiver_ry = micRYSlider.getValue(); p.micr_angle = micRAngSlider.getValue();
    p.mic_pattern = comboSelection (micPatternCombo, micOptions, 4).toStdString();
    p.er_only = erOnlyButton.getToggleState();
    p.sample_rate = sampleRateCombo.getSelectedId() == 1 ? 44100 : 48000;
    return p;
}

void IRSynthComponent::setParams (const IRSynthParams& p)
{
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
    spkLXSlider.setValue (p.source_lx, juce::dontSendNotification);
    spkLYSlider.setValue (p.source_ly, juce::dontSendNotification);
    spkLAngSlider.setValue (p.spkl_angle, juce::dontSendNotification);
    spkRXSlider.setValue (p.source_rx, juce::dontSendNotification);
    spkRYSlider.setValue (p.source_ry, juce::dontSendNotification);
    spkRAngSlider.setValue (p.spkr_angle, juce::dontSendNotification);
    micLXSlider.setValue (p.receiver_lx, juce::dontSendNotification);
    micLYSlider.setValue (p.receiver_ly, juce::dontSendNotification);
    micLAngSlider.setValue (p.micl_angle, juce::dontSendNotification);
    micRXSlider.setValue (p.receiver_rx, juce::dontSendNotification);
    micRYSlider.setValue (p.receiver_ry, juce::dontSendNotification);
    micRAngSlider.setValue (p.micr_angle, juce::dontSendNotification);
    setComboTo (micPatternCombo, p.mic_pattern, micOptions, 4);
    erOnlyButton.setToggleState (p.er_only, juce::dontSendNotification);
    sampleRateCombo.setSelectedId (p.sample_rate == 44100 ? 1 : 2, juce::dontSendNotification);
}

void IRSynthComponent::timerCallback()
{
    if (! widthValueLabel.hasKeyboardFocus (true))
        widthValueLabel.setText (juce::String (widthSlider.getValue(), 1), juce::dontSendNotification);
    if (! depthValueLabel.hasKeyboardFocus (true))
        depthValueLabel.setText (juce::String (depthSlider.getValue(), 1), juce::dontSendNotification);
    if (! heightValueLabel.hasKeyboardFocus (true))
        heightValueLabel.setText (juce::String (heightSlider.getValue(), 1), juce::dontSendNotification);
    // Sync sliders from floor plan when user drags pucks (keeps them in sync)
    auto t = floorPlanComponent.getTransducerState();
    spkLXSlider.setValue (t.cx[0], juce::dontSendNotification);
    spkLYSlider.setValue (t.cy[0], juce::dontSendNotification);
    spkLAngSlider.setValue (t.angle[0], juce::dontSendNotification);
    spkRXSlider.setValue (t.cx[1], juce::dontSendNotification);
    spkRYSlider.setValue (t.cy[1], juce::dontSendNotification);
    spkRAngSlider.setValue (t.angle[1], juce::dontSendNotification);
    micLXSlider.setValue (t.cx[2], juce::dontSendNotification);
    micLYSlider.setValue (t.cy[2], juce::dontSendNotification);
    micLAngSlider.setValue (t.angle[2], juce::dontSendNotification);
    micRXSlider.setValue (t.cx[3], juce::dontSendNotification);
    micRYSlider.setValue (t.cy[3], juce::dontSendNotification);
    micRAngSlider.setValue (t.angle[3], juce::dontSendNotification);
    floorPlanComponent.repaint();
    audienceReadout.setText (juce::String (audienceSlider.getValue(), 2), juce::dontSendNotification);
    diffusionReadout.setText (juce::String (diffusionSlider.getValue(), 2), juce::dontSendNotification);
    organReadout.setText (juce::String (organSlider.getValue(), 2), juce::dontSendNotification);
    balconiesReadout.setText (juce::String (balconiesSlider.getValue(), 2), juce::dontSendNotification);
    updateRT60Display();
}

void IRSynthComponent::buttonClicked (juce::Button* b)
{
    if (b == &previewButton && ! synthRunning.load())
        startSynthesis();
    else if (b == &acceptButton)
        onAccept();
    else if (b == &cancelButton)
        onCancel();
}

void IRSynthComponent::startSynthesis()
{
    // Sync floor plan drags to sliders before synthesis so params reflect latest positions
    auto t = floorPlanComponent.getTransducerState();
    spkLXSlider.setValue (t.cx[0], juce::dontSendNotification);
    spkLYSlider.setValue (t.cy[0], juce::dontSendNotification);
    spkLAngSlider.setValue (t.angle[0], juce::dontSendNotification);
    spkRXSlider.setValue (t.cx[1], juce::dontSendNotification);
    spkRYSlider.setValue (t.cy[1], juce::dontSendNotification);
    spkRAngSlider.setValue (t.angle[1], juce::dontSendNotification);
    micLXSlider.setValue (t.cx[2], juce::dontSendNotification);
    micLYSlider.setValue (t.cy[2], juce::dontSendNotification);
    micLAngSlider.setValue (t.angle[2], juce::dontSendNotification);
    micRXSlider.setValue (t.cx[3], juce::dontSendNotification);
    micRYSlider.setValue (t.cy[3], juce::dontSendNotification);
    micRAngSlider.setValue (t.angle[3], juce::dontSendNotification);

    IRSynthParams p = getParams();
    synthRunning = true;
    previewButton.setEnabled (false);
    acceptButton.setVisible (false);
    cancelButton.setVisible (false);
    pendingResult.reset();
    progressValue = 0.0;
    progressLabel.setText ("Starting…", juce::dontSendNotification);

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
            progressValue = 1.0;
            progressLabel.setText ("Done.", juce::dontSendNotification);

            if (result.success)
            {
                pendingResult = std::make_unique<IRSynthResult> (result);
                acceptButton.setVisible (true);
                cancelButton.setVisible (true);
            }
        });
    });
}

void IRSynthComponent::onAccept()
{
    if (pendingResult && pendingResult->success && onComplete)
        onComplete (*pendingResult);
    pendingResult.reset();
    acceptButton.setVisible (false);
    cancelButton.setVisible (false);
}

void IRSynthComponent::onCancel()
{
    pendingResult.reset();
    acceptButton.setVisible (false);
    cancelButton.setVisible (false);
    if (onCancelFn)
        onCancelFn();
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
