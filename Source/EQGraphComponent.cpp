#include "EQGraphComponent.h"
#include "PluginProcessor.h"

const char* EQGraphComponent::bandIds[3][3] = {
    { "b0freq", "b0gain", "b0q" },
    { "b1freq", "b1gain", "b1q" },
    { "b2freq", "b2gain", "b2q" }
};

EQGraphComponent::EQGraphComponent (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    juce::Colour textCol (0xffe0e0e0);
    juce::Colour boxBg (0xff2a2a2a);

    addAndMakeVisible (freqSlider);
    freqSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    freqSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 56, 20);
    freqSlider.setRange (20.0, 20000.0, 1.0);
    freqSlider.setSkewFactorFromMidPoint (1000.0);
    freqSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < numBands)
            if (auto* p = apvts.getParameter (bandIds[selectedBand][0]))
                p->setValueNotifyingHost (apvts.getParameterRange (bandIds[selectedBand][0]).convertTo0to1 ((float) freqSlider.getValue()));
        repaint();
    };
    freqSlider.setColour (juce::Slider::textBoxTextColourId, textCol);
    freqSlider.setColour (juce::Slider::textBoxBackgroundColourId, boxBg);

    addAndMakeVisible (gainSlider);
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 52, 20);
    gainSlider.setRange (-12.0, 12.0, 0.01);
    gainSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < numBands)
            if (auto* p = apvts.getParameter (bandIds[selectedBand][1]))
                p->setValueNotifyingHost (apvts.getParameterRange (bandIds[selectedBand][1]).convertTo0to1 ((float) gainSlider.getValue()));
        repaint();
    };
    gainSlider.setColour (juce::Slider::textBoxTextColourId, textCol);
    gainSlider.setColour (juce::Slider::textBoxBackgroundColourId, boxBg);

    addAndMakeVisible (qSlider);
    qSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    qSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 48, 20);
    qSlider.setRange (0.3, 10.0, 0.01);
    qSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < numBands)
            if (auto* p = apvts.getParameter (bandIds[selectedBand][2]))
                p->setValueNotifyingHost (apvts.getParameterRange (bandIds[selectedBand][2]).convertTo0to1 ((float) qSlider.getValue()));
        repaint();
    };
    qSlider.setColour (juce::Slider::textBoxTextColourId, textCol);
    qSlider.setColour (juce::Slider::textBoxBackgroundColourId, boxBg);

    for (auto* lbl : { &freqLabel, &gainLabel, &qLabel })
    {
        addAndMakeVisible (lbl);
        lbl->setJustificationType (juce::Justification::centredLeft);
        lbl->setColour (juce::Label::textColourId, juce::Colour (0xff909090));
        lbl->setFont (juce::FontOptions (10.0f));
    }
    freqLabel.setText ("FREQUENCY", juce::dontSendNotification);
    gainLabel.setText ("GAIN", juce::dontSendNotification);
    qLabel.setText ("Q", juce::dontSendNotification);

    auto addBandButton = [this, textCol, boxBg] (juce::TextButton& btn, int band)
    {
        addAndMakeVisible (btn);
        btn.onClick = [this, band] { selectedBand = band; syncSlidersFromParams(); repaint(); };
        btn.setColour (juce::TextButton::buttonColourId, boxBg);
        btn.setColour (juce::TextButton::textColourOffId, textCol);
    };
    addBandButton (band1Button, 0);
    addBandButton (band2Button, 1);
    addBandButton (band3Button, 2);

    startTimerHz (20);
}

static constexpr int bandButtonRowH = 20;
static constexpr int controlRowH = 32;

juce::Rectangle<float> EQGraphComponent::getGraphArea() const
{
    auto bounds = getLocalBounds().toFloat().reduced (4);
    bounds.removeFromBottom (bandButtonRowH + controlRowH);
    return bounds;
}

void EQGraphComponent::paint (juce::Graphics& g)
{
    auto graphArea = getGraphArea();

    g.setColour (juce::Colour (0xff1e1e1e));
    g.fillRoundedRectangle (graphArea, 4);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (graphArea, 4, 0.8f);

    float margin = 8;
    auto inner = graphArea.reduced (margin);
    float w = inner.getWidth();
    float h = inner.getHeight();

    g.setColour (juce::Colour (0xff2a2a2a));
    for (int i = 1; i <= 4; ++i)
    {
        float x = inner.getX() + w * (float) i / 5.0f;
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
    }
    g.drawHorizontalLine ((int) (inner.getY() + h * 0.5f), inner.getX(), inner.getRight());

    float f0 = minFreq, f1 = maxFreq;
    int n = (int) w;
    juce::Path path;
    for (int i = 0; i <= n; ++i)
    {
        float x = inner.getX() + (float) i / (float) n * w;
        float freq = f0 * std::pow (f1 / f0, (float) i / (float) n);
        float gainDb = getResponseAt (freq);
        float y = inner.getBottom() - (gainDb - minGain) / (maxGain - minGain) * h;
        if (i == 0) path.startNewSubPath (x, y);
        else path.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xffe8a84a).withAlpha (0.5f));
    g.strokePath (path, juce::PathStrokeType (1.5f));

    juce::Colour bandCols[] = { juce::Colour (0xffa050c0), juce::Colour (0xff50a050), juce::Colour (0xffc08040) };
    for (int b = 0; b < numBands; ++b)
    {
        float freq = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gain = apvts.getRawParameterValue (bandIds[b][1])->load();
        float px, py;
        freqGainToXY (freq, gain, px, py);
        float r = (b == selectedBand || b == draggingBand) ? 6.0f : 5.0f;
        g.setColour (bandCols[b]);
        g.fillEllipse (px - r, py - r, r * 2, r * 2);
        g.setColour (juce::Colours::white);
        g.drawEllipse (px - r, py - r, r * 2, r * 2, 0.8f);
    }
}

void EQGraphComponent::resized()
{
    auto b = getLocalBounds().reduced (4);
    auto bottom = b.removeFromBottom (bandButtonRowH + controlRowH);
    auto bandRow = bottom.removeFromTop (bandButtonRowH);
    int bw = juce::jmax (28, bandRow.getWidth() / 6);
    band1Button.setBounds (bandRow.removeFromLeft (bw).reduced (2));
    band2Button.setBounds (bandRow.removeFromLeft (bw).reduced (2));
    band3Button.setBounds (bandRow.removeFromLeft (bw).reduced (2));

    auto ctrlRow = bottom;
    int colW = ctrlRow.getWidth() / 3;
    auto col1 = ctrlRow.removeFromLeft (colW).reduced (2);
    auto col2 = ctrlRow.removeFromLeft (colW).reduced (2);
    auto col3 = ctrlRow.reduced (2);
    int labelH = 14;
    freqLabel.setBounds (col1.removeFromTop (labelH));
    freqSlider.setBounds (col1);
    gainLabel.setBounds (col2.removeFromTop (labelH));
    gainSlider.setBounds (col2);
    qLabel.setBounds (col3.removeFromTop (labelH));
    qSlider.setBounds (col3);
}

void EQGraphComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingBand = hitTestBand (e.position);
    if (draggingBand >= 0)
        selectedBand = draggingBand;
    repaint();
}

void EQGraphComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBand < 0) return;
    auto graphArea = getGraphArea();
    float mx = e.position.getX();
    float my = e.position.getY();
    if (! graphArea.contains (mx, my)) return;
    float margin = 8;
    auto inner = graphArea.reduced (margin);
    float freq, gain;
    xyToFreqGain (mx - inner.getX(), my - inner.getY(), freq, gain);
    if (auto* pf = apvts.getParameter (bandIds[draggingBand][0]))
        pf->setValueNotifyingHost (apvts.getParameterRange (bandIds[draggingBand][0]).convertTo0to1 (freq));
    if (auto* pg = apvts.getParameter (bandIds[draggingBand][1]))
        pg->setValueNotifyingHost (apvts.getParameterRange (bandIds[draggingBand][1]).convertTo0to1 (gain));
    syncSlidersFromParams();
    if (draggingBand >= 0 && draggingBand < numBands)
    {
        float f = apvts.getRawParameterValue (bandIds[draggingBand][0])->load();
        float g = apvts.getRawParameterValue (bandIds[draggingBand][1])->load();
        float q = apvts.getRawParameterValue (bandIds[draggingBand][2])->load();
        freqLabel.setText ("FREQUENCY " + juce::String (juce::roundToInt (f)) + " Hz", juce::dontSendNotification);
        gainLabel.setText ("GAIN " + juce::String (g, 2) + " dB", juce::dontSendNotification);
        qLabel.setText ("Q " + juce::String (q, 3), juce::dontSendNotification);
    }
    repaint();
}

void EQGraphComponent::mouseUp (const juce::MouseEvent&)
{
    draggingBand = -1;
    repaint();
}

void EQGraphComponent::syncSlidersFromParams()
{
    if (selectedBand < 0 || selectedBand >= numBands) return;
    float f = apvts.getRawParameterValue (bandIds[selectedBand][0])->load();
    float g = apvts.getRawParameterValue (bandIds[selectedBand][1])->load();
    float q = apvts.getRawParameterValue (bandIds[selectedBand][2])->load();
    if (std::abs ((float) freqSlider.getValue() - f) > 0.5f)
        freqSlider.setValue (f, juce::dontSendNotification);
    if (std::abs ((float) gainSlider.getValue() - g) > 0.001f)
        gainSlider.setValue (g, juce::dontSendNotification);
    if (std::abs ((float) qSlider.getValue() - q) > 0.001f)
        qSlider.setValue (q, juce::dontSendNotification);
}

void EQGraphComponent::timerCallback()
{
    juce::Colour boxBg (0xff2a2a2a);
    juce::Colour accent (0xffe8a84a);
    for (int i = 0; i < numBands; ++i)
    {
        juce::TextButton* b = (i == 0) ? &band1Button : (i == 1) ? &band2Button : &band3Button;
        b->setColour (juce::TextButton::buttonColourId, i == selectedBand ? accent.withAlpha (0.4f) : boxBg);
    }
    if (selectedBand >= 0 && selectedBand < numBands)
    {
        syncSlidersFromParams();
        float f = apvts.getRawParameterValue (bandIds[selectedBand][0])->load();
        float g = apvts.getRawParameterValue (bandIds[selectedBand][1])->load();
        float q = apvts.getRawParameterValue (bandIds[selectedBand][2])->load();
        freqLabel.setText ("FREQUENCY " + juce::String (juce::roundToInt (f)) + " Hz", juce::dontSendNotification);
        gainLabel.setText ("GAIN " + juce::String (g, 2) + " dB", juce::dontSendNotification);
        qLabel.setText ("Q " + juce::String (q, 3), juce::dontSendNotification);
    }
}

int EQGraphComponent::hitTestBand (juce::Point<float> pos) const
{
    auto graphArea = getGraphArea();
    float margin = 8;
    auto inner = graphArea.reduced (margin);
    float w = inner.getWidth();
    float h = inner.getHeight();
    const float hitRadius = 12.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float freq = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gain = apvts.getRawParameterValue (bandIds[b][1])->load();
        float px = inner.getX() + std::log (freq / minFreq) / std::log (maxFreq / minFreq) * w;
        float py = inner.getBottom() - (gain - minGain) / (maxGain - minGain) * h;
        if (pos.getDistanceFrom (juce::Point<float> (px, py)) <= hitRadius)
            return b;
    }
    return -1;
}

void EQGraphComponent::freqGainToXY (float freqHz, float gainDb, float& x, float& y) const
{
    auto graphArea = getGraphArea();
    float margin = 8;
    auto inner = graphArea.reduced (margin);
    float w = inner.getWidth();
    float h = inner.getHeight();
    x = inner.getX() + std::log (freqHz / minFreq) / std::log (maxFreq / minFreq) * w;
    y = inner.getBottom() - (gainDb - minGain) / (maxGain - minGain) * h;
}

void EQGraphComponent::xyToFreqGain (float x, float y, float& freqHz, float& gainDb) const
{
    auto graphArea = getGraphArea();
    float margin = 8;
    auto inner = graphArea.reduced (margin);
    float w = inner.getWidth();
    float h = inner.getHeight();
    float nx = juce::jlimit (0.0f, 1.0f, x / w);
    freqHz = minFreq * std::pow (maxFreq / minFreq, nx);
    gainDb = juce::jlimit (minGain, maxGain, maxGain - (y / h) * (maxGain - minGain));
}

float EQGraphComponent::getResponseAt (float freqHz) const
{
    float sumDb = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        float fc = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gDb = apvts.getRawParameterValue (bandIds[b][1])->load();
        float q = apvts.getRawParameterValue (bandIds[b][2])->load();
        float logDist = std::log (freqHz / fc) * (float) (1.0 / std::log (2.0));
        float bump = std::exp (-0.5f * logDist * logDist * q * q);
        sumDb += gDb * bump;
    }
    return sumDb;
}
