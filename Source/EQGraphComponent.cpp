#include "EQGraphComponent.h"
#include "PluginProcessor.h"

// ── static data ──────────────────────────────────────────────────────────────

// Display order: Low Shelf | Peak 1 | Peak 2 | Peak 3 | High Shelf
const char* EQGraphComponent::bandIds[5][3] = {
    { "b3freq", "b3gain", "b3q" },  // 0 – Low Shelf
    { "b0freq", "b0gain", "b0q" },  // 1 – Peak 1
    { "b1freq", "b1gain", "b1q" },  // 2 – Peak 2
    { "b2freq", "b2gain", "b2q" },  // 3 – Peak 3
    { "b4freq", "b4gain", "b4q" }   // 4 – High Shelf
};

const bool EQGraphComponent::bandIsShelf[5] = { true, false, false, false, true };

// Per-band colours: purple | blue | amber | green | pink-red
static const juce::Colour bandCols[5] = {
    juce::Colour (0xffa060d0),
    juce::Colour (0xff5090d0),
    juce::Colour (0xffe8a84a),
    juce::Colour (0xff50c090),
    juce::Colour (0xffd06060)
};

static const char* bandNames[5] =
    { "LOW", "MID 1", "MID 2", "MID 3", "HIGH" };

// ── ctor ─────────────────────────────────────────────────────────────────────

EQGraphComponent::EQGraphComponent (juce::AudioProcessorValueTreeState& state,
                                     PingProcessor* proc)
    : apvts (state), processor (proc)
{
    for (int b = 0; b < numBands; ++b)
    {
        // Frequency range per band
        const double freqMin = (b == 0) ? 20.0   : (b == 4) ? 2000.0  : 50.0;
        const double freqMax = (b == 0) ? 1200.0 : (b == 4) ? 20000.0 : 16000.0;
        const double freqMid = (b == 0) ? 200.0  : (b == 4) ? 6000.0  : 1000.0;
        const double qMax    = bandIsShelf[b] ? 2.0 : 10.0;

        // Freq knob
        setupKnob (knobs[b][0], b, 0);
        knobs[b][0].setRange (freqMin, freqMax, 1.0);
        knobs[b][0].setSkewFactorFromMidPoint (freqMid);

        // Gain knob
        setupKnob (knobs[b][1], b, 1);
        knobs[b][1].setRange (-12.0, 12.0, 0.1);

        // Q / Slope knob
        setupKnob (knobs[b][2], b, 2);
        knobs[b][2].setRange (0.3, qMax, 0.01);

        // Parameter-name labels ("FREQ", "GAIN", "Q"/"SLOPE")
        for (int k = 0; k < 3; ++k)
        {
            addAndMakeVisible (knobLabels[b][k]);
            knobLabels[b][k].setJustificationType (juce::Justification::centred);
            knobLabels[b][k].setColour (juce::Label::textColourId, juce::Colour (0xff808080));
            knobLabels[b][k].setFont (juce::FontOptions (8.5f));
        }
        knobLabels[b][0].setText ("FREQ",                         juce::dontSendNotification);
        knobLabels[b][1].setText ("GAIN",                         juce::dontSendNotification);
        knobLabels[b][2].setText (bandIsShelf[b] ? "SLOPE" : "Q", juce::dontSendNotification);

        // Live value readout labels (accent orange, one per knob)
        for (int k = 0; k < 3; ++k)
        {
            addAndMakeVisible (knobReadouts[b][k]);
            knobReadouts[b][k].setJustificationType (juce::Justification::centred);
            knobReadouts[b][k].setColour (juce::Label::textColourId, juce::Colour (0xffe8a84a));
            knobReadouts[b][k].setFont (juce::FontOptions (9.0f));
        }

        // Band-name labels (column header)
        addAndMakeVisible (bandNameLabel[b]);
        bandNameLabel[b].setText (bandNames[b], juce::dontSendNotification);
        bandNameLabel[b].setJustificationType (juce::Justification::centred);
        bandNameLabel[b].setFont (juce::FontOptions (8.5f));
        bandNameLabel[b].setColour (juce::Label::textColourId, bandCols[b]);
    }

    // Spectrum analyser
    if (processor)
    {
        forwardFFT     = std::make_unique<juce::dsp::FFT> (spectrumFftOrder);
        spectrumWindow = std::make_unique<juce::dsp::WindowingFunction<float>> (
                             spectrumFftSize,
                             juce::dsp::WindowingFunction<float>::hann);
        spectrumFftData.resize  (spectrumFftSize * 2, 0.0f);
        spectrumScopeData.resize (spectrumScopeSize,   0.0f);
    }

    startTimerHz (30);
}

// ── knob setup helper ────────────────────────────────────────────────────────

void EQGraphComponent::setupKnob (juce::Slider& s, int band, int param)
{
    addAndMakeVisible (s);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId,    bandCols[band]);
    s.setColour (juce::Slider::thumbColourId,               bandCols[band]);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2a2a));

    s.onValueChange = [this, &s, band, param]
    {
        if (auto* p = apvts.getParameter (bandIds[band][param]))
            p->setValueNotifyingHost (
                apvts.getParameterRange (bandIds[band][param])
                     .convertTo0to1 ((float) s.getValue()));
        repaint();
    };
}

// ── resized ──────────────────────────────────────────────────────────────────

void EQGraphComponent::resized()
{
    // ── Layout constants ──────────────────────────────────────────────────────
    // Knob size chosen to match the main-UI "small knob" (wet output, etc.)
    static constexpr int knobSz    = 42;   // rotary knob diameter
    static constexpr int readoutH  = 13;   // live value-readout label height
    static constexpr int lblH      = 10;   // parameter-name label ("FREQ") height
    static constexpr int kGap      = 2;    // gap between knob bottom and readout
    static constexpr int lblGap    = 1;    // gap between readout and param label
    static constexpr int bandLblH  = 13;   // band-name header height
    // Height of one complete row (knob + readout + label)
    static constexpr int oneRow    = knobSz + kGap + readoutH + lblGap + lblH;
    // Gain row drops an extra amount to create the zig-zag offset
    static constexpr int gainYShift = 9;
    // Vertical gap between successive rows
    static constexpr int rowGap    = 5;
    // Total control-strip height:
    //   header | top-pad | FREQ row | gap | [gain drop] | GAIN row | gap | Q row
    static constexpr int ctrlH = bandLblH + 4
                               + oneRow + rowGap
                               + gainYShift
                               + oneRow + rowGap
                               + oneRow;

    auto b = getLocalBounds().reduced (4);

    // Reserve control strip from the bottom; remainder becomes the graph area.
    // Trim 20 px from the top of the graph to push the visible display down.
    auto ctrlArea = b.removeFromBottom (ctrlH - 75);  // -75: extends chart bottom by 75 px, shifts all controls down 75 px
    graphBounds   = b.withTrimmedTop (60).toFloat();

    const int totalW  = ctrlArea.getWidth();
    const int colW    = totalW / numBands;
    // Gain knobs are shifted right by ~1/5 of a column (creates the zig-zag feel)
    const int gainXOff = colW / 5;

    // Absolute Y positions for each parameter row.
    // FREQ row is pushed down 10 px from the natural header position.
    // GAIN and Q rows are pushed down 5 px each (in absolute terms), achieved by
    // subtracting 5 from the FREQ→GAIN inter-row spacing so that GAIN's absolute
    // shift is +5 px (not +10 like FREQ), and Q follows GAIN at the same absolute +5.
    const int freqRowY = ctrlArea.getY() + bandLblH + 4 + 10;           // +10 px
    const int gainRowY = freqRowY - 5 + oneRow + rowGap + gainYShift;   // +5 px absolute
    const int qRowY    = gainRowY + oneRow + rowGap - gainYShift;        // +5 px absolute (follows gain)

    // Per-row fine-tuning offsets (applied on top of the row base positions above)
    static constexpr int freqDX = -10;  // Freq knobs: 10 px left
    static constexpr int freqDY = -5;   // Freq knobs: +70 intended, -75 to cancel ctrlArea shift
    static constexpr int gainDX = +20;  // Gain knobs: 20 px right (added to gainXOff)
    static constexpr int gainDY = -45;  // Gain knobs: +30 intended, -75 to cancel ctrlArea shift
    static constexpr int qDX    = -10;  // Q knobs: 10 px left
    static constexpr int qDY    = -65;  // Q knobs: +10 intended, -75 to cancel ctrlArea shift

    for (int i = 0; i < numBands; ++i)
    {
        const int colX = ctrlArea.getX() + i * colW;
        const int cx   = colX + colW / 2;         // column centre (FREQ + Q)
        const int gcx  = cx + gainXOff;            // gain centre (shifted right)

        // ── Band-name header ──────────────────────────────────────────────
        bandNameLabel[i].setBounds (colX, ctrlArea.getY(), colW, bandLblH);

        // ── FREQ row (param 0) ────────────────────────────────────────────
        const int fY = freqRowY + freqDY;
        knobs      [i][0].setBounds (cx  + freqDX - knobSz / 2, fY, knobSz, knobSz);
        knobReadouts[i][0].setBounds (colX + freqDX, fY + knobSz + kGap, colW, readoutH);
        knobLabels  [i][0].setBounds (colX + freqDX, fY + knobSz + kGap + readoutH + lblGap, colW, lblH);

        // ── GAIN row (param 1) — offset right and down ────────────────────
        const int gY = gainRowY + gainDY;
        knobs      [i][1].setBounds (gcx + gainDX - knobSz / 2, gY, knobSz, knobSz);
        knobReadouts[i][1].setBounds (colX + gainXOff + gainDX, gY + knobSz + kGap, colW, readoutH);
        knobLabels  [i][1].setBounds (colX + gainXOff + gainDX, gY + knobSz + kGap + readoutH + lblGap, colW, lblH);

        // ── Q / SLOPE row (param 2) ───────────────────────────────────────
        const int qY = qRowY + qDY;
        knobs      [i][2].setBounds (cx  + qDX - knobSz / 2, qY, knobSz, knobSz);
        knobReadouts[i][2].setBounds (colX + qDX, qY + knobSz + kGap, colW, readoutH);
        knobLabels  [i][2].setBounds (colX + qDX, qY + knobSz + kGap + readoutH + lblGap, colW, lblH);
    }
}

// ── graph area ───────────────────────────────────────────────────────────────

juce::Rectangle<float> EQGraphComponent::getGraphArea() const
{
    return graphBounds;
}

// ── paint ────────────────────────────────────────────────────────────────────

void EQGraphComponent::paint (juce::Graphics& g)
{
    auto graphArea = getGraphArea();

    // Graph background
    g.setColour (juce::Colour (0xff1e1e1e));
    g.fillRoundedRectangle (graphArea, 4.0f);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (graphArea, 4.0f, 0.8f);

    const float margin = 8.0f;
    auto inner  = graphArea.reduced (margin);
    const float w = inner.getWidth();
    const float h = inner.getHeight();

    // ── live spectrum ────────────────────────────────────────────────
    if (processor && ! spectrumScopeData.empty())
    {
        const float specW    = graphArea.getWidth() * 2.0f;
        const float specH    = graphArea.getHeight();
        const float specLeft = graphArea.getX();
        const float specBot  = graphArea.getBottom();

        juce::Path sp;
        sp.startNewSubPath (specLeft, specBot);
        for (int i = 0; i < spectrumScopeSize; ++i)
        {
            float nx = (float) i / (float) (spectrumScopeSize - 1);
            float x  = specLeft + nx * specW;
            float lv = juce::jlimit (0.0f, 1.0f, spectrumScopeData[(size_t) i] * 2.0f);
            sp.lineTo (x, specBot - lv * specH);
        }
        sp.lineTo (specLeft + specW, specBot);
        sp.closeSubPath();
        g.saveState();
        g.reduceClipRegion (graphArea.toNearestInt());
        g.setColour (juce::Colour (0xff505050).withAlpha (0.50f));
        g.fillPath (sp);
        g.restoreState();
    }

    // ── grid lines ──────────────────────────────────────────────────
    g.setColour (juce::Colour (0xff2a2a2a));
    for (int i = 1; i <= 4; ++i)
        g.drawVerticalLine ((int) (inner.getX() + w * (float) i / 5.0f),
                            inner.getY(), inner.getBottom());
    g.drawHorizontalLine ((int) (inner.getY() + h * 0.5f),
                          inner.getX(), inner.getRight());

    // ── frequency-response curve ────────────────────────────────────
    {
        const int n = (int) w;
        juce::Path path;
        for (int i = 0; i <= n; ++i)
        {
            float x    = inner.getX() + (float) i / (float) n * w;
            float freq = minFreq * std::pow (maxFreq / minFreq, (float) i / (float) n);
            float gDb  = getResponseAt (freq);
            float y    = inner.getBottom()
                         - (gDb - minGain) / (maxGain - minGain) * h;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffe8a84a).withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    // ── band handles ────────────────────────────────────────────────
    for (int b = 0; b < numBands; ++b)
    {
        float freq = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gain = apvts.getRawParameterValue (bandIds[b][1])->load();
        float px, py;
        freqGainToXY (freq, gain, px, py);

        const bool active = (b == selectedBand || b == draggingBand);
        const float r     = active ? 6.5f : 5.5f;

        if (bandIsShelf[b])
        {
            // Diamond marker for shelf bands
            juce::Path diamond;
            diamond.addPolygon ({ px, py }, 4, r, juce::MathConstants<float>::pi * 0.25f);
            g.setColour (bandCols[b]);
            g.fillPath (diamond);
            g.setColour (active ? juce::Colours::white
                                : juce::Colours::white.withAlpha (0.5f));
            g.strokePath (diamond, juce::PathStrokeType (active ? 1.0f : 0.7f));

            // Shelf indicator: tinted horizontal line from node edge to graph border
            g.setColour (bandCols[b].withAlpha (0.30f));
            if (b == 0)  // low shelf: line from left edge to node
                g.drawHorizontalLine ((int) py, inner.getX(), px);
            else         // high shelf: line from node to right edge
                g.drawHorizontalLine ((int) py, px, inner.getRight());
        }
        else
        {
            // Filled circle for peak bands
            g.setColour (bandCols[b]);
            g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);
            g.setColour (active ? juce::Colours::white
                                : juce::Colours::white.withAlpha (0.5f));
            g.drawEllipse (px - r, py - r, r * 2.0f, r * 2.0f,
                           active ? 1.0f : 0.7f);
        }
    }

    // ── control-area separator line ─────────────────────────────────
    float sepY = graphArea.getBottom() + 2.0f;
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawHorizontalLine ((int) sepY,
                          (float) getLocalBounds().getX(),
                          (float) getLocalBounds().getRight());
}

// ── mouse ─────────────────────────────────────────────────────────────────────

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
    if (! graphArea.contains (e.position)) return;

    const float margin = 8.0f;
    auto inner = graphArea.reduced (margin);

    float freq, gain;
    xyToFreqGain (e.position.getX() - inner.getX(),
                  e.position.getY() - inner.getY(),
                  freq, gain);

    // Clamp frequency to the band's specific range
    const float freqMinB = bandIsShelf[draggingBand]
                           ? ((draggingBand == 0) ? 20.0f : 2000.0f)
                           : 50.0f;
    const float freqMaxB = bandIsShelf[draggingBand]
                           ? ((draggingBand == 0) ? 1200.0f : 20000.0f)
                           : 16000.0f;
    freq = juce::jlimit (freqMinB, freqMaxB, freq);

    if (auto* pf = apvts.getParameter (bandIds[draggingBand][0]))
        pf->setValueNotifyingHost (
            apvts.getParameterRange (bandIds[draggingBand][0]).convertTo0to1 (freq));
    if (auto* pg = apvts.getParameter (bandIds[draggingBand][1]))
        pg->setValueNotifyingHost (
            apvts.getParameterRange (bandIds[draggingBand][1]).convertTo0to1 (gain));

    syncKnobsFromParams();
    repaint();
}

void EQGraphComponent::mouseUp (const juce::MouseEvent&)
{
    draggingBand = -1;
    repaint();
}

// ── helpers ───────────────────────────────────────────────────────────────────

void EQGraphComponent::syncKnobsFromParams()
{
    for (int b = 0; b < numBands; ++b)
    {
        float f = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gd= apvts.getRawParameterValue (bandIds[b][1])->load();
        float q = apvts.getRawParameterValue (bandIds[b][2])->load();

        if (std::abs ((float) knobs[b][0].getValue() - f)  > 0.5f)
            knobs[b][0].setValue (f,  juce::dontSendNotification);
        if (std::abs ((float) knobs[b][1].getValue() - gd) > 0.005f)
            knobs[b][1].setValue (gd, juce::dontSendNotification);
        if (std::abs ((float) knobs[b][2].getValue() - q)  > 0.001f)
            knobs[b][2].setValue (q,  juce::dontSendNotification);
    }
    updateReadouts();
}

// ── value readout formatting ──────────────────────────────────────────────────

void EQGraphComponent::updateReadouts()
{
    for (int b = 0; b < numBands; ++b)
    {
        // ── Frequency ──────────────────────────────────────────────────────
        float f = (float) knobs[b][0].getValue();
        juce::String fStr;
        if (f >= 1000.0f)
            fStr = juce::String (f / 1000.0f, 1) + " kHz";
        else
            fStr = juce::String ((int) std::round (f)) + " Hz";
        knobReadouts[b][0].setText (fStr, juce::dontSendNotification);

        // ── Gain ───────────────────────────────────────────────────────────
        float g = (float) knobs[b][1].getValue();
        juce::String gStr = (g >= 0.0f ? "+" : "") + juce::String (g, 1) + " dB";
        knobReadouts[b][1].setText (gStr, juce::dontSendNotification);

        // ── Q / Slope ──────────────────────────────────────────────────────
        float q = (float) knobs[b][2].getValue();
        knobReadouts[b][2].setText (juce::String (q, 2), juce::dontSendNotification);
    }
}

void EQGraphComponent::timerCallback()
{
    if (processor && forwardFFT)
        drawNextFrameOfSpectrum();

    syncKnobsFromParams();
    repaint();  // keep EQ curve and spectrum in sync with any parameter changes
}

// ── hit-test ──────────────────────────────────────────────────────────────────

int EQGraphComponent::hitTestBand (juce::Point<float> pos) const
{
    const float margin     = 8.0f;
    auto inner             = getGraphArea().reduced (margin);
    const float hitRadius  = 14.0f;

    for (int b = 0; b < numBands; ++b)
    {
        float freq = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gain = apvts.getRawParameterValue (bandIds[b][1])->load();
        float px, py;
        freqGainToXY (freq, gain, px, py);
        if (pos.getDistanceFrom ({ px, py }) <= hitRadius)
            return b;
    }
    return -1;
}

// ── coordinate mapping ───────────────────────────────────────────────────────

void EQGraphComponent::freqGainToXY (float freqHz, float gainDb,
                                      float& x, float& y) const
{
    const float margin = 8.0f;
    auto inner = getGraphArea().reduced (margin);
    x = inner.getX()
        + std::log (freqHz / minFreq) / std::log (maxFreq / minFreq)
          * inner.getWidth();
    y = inner.getBottom()
        - (gainDb - minGain) / (maxGain - minGain) * inner.getHeight();
}

void EQGraphComponent::xyToFreqGain (float x, float y,
                                      float& freqHz, float& gainDb) const
{
    const float margin = 8.0f;
    auto inner = getGraphArea().reduced (margin);
    float nx   = juce::jlimit (0.0f, 1.0f, x / inner.getWidth());
    freqHz  = minFreq * std::pow (maxFreq / minFreq, nx);
    gainDb  = juce::jlimit (minGain, maxGain,
                  maxGain - (y / inner.getHeight()) * (maxGain - minGain));
}

// ── frequency-response approximation ────────────────────────────────────────

float EQGraphComponent::getResponseAt (float freqHz) const
{
    float sumDb = 0.0f;
    const float invLog2 = 1.0f / std::log (2.0f);

    for (int b = 0; b < numBands; ++b)
    {
        float fc  = apvts.getRawParameterValue (bandIds[b][0])->load();
        float gDb = apvts.getRawParameterValue (bandIds[b][1])->load();
        float q   = apvts.getRawParameterValue (bandIds[b][2])->load();

        if (bandIsShelf[b])
        {
            // Sigmoid-based shelf approximation (matches biquad shelf shape well)
            float logDist = std::log (freqHz / fc) * invLog2;
            float slope   = q * 1.8f;
            if (b == 0)   // Low shelf: full gain below fc, tapers above
                sumDb += gDb * 0.5f * (1.0f - std::tanh (logDist * slope));
            else          // High shelf: full gain above fc, tapers below
                sumDb += gDb * 0.5f * (1.0f + std::tanh (logDist * slope));
        }
        else
        {
            // Peak: Gaussian approximation
            float logDist = std::log (freqHz / fc) * invLog2;
            float bump    = std::exp (-0.5f * logDist * logDist * q * q);
            sumDb += gDb * bump;
        }
    }
    return sumDb;
}

// ── spectrum analyser ─────────────────────────────────────────────────────────

void EQGraphComponent::drawNextFrameOfSpectrum()
{
    if (! processor || ! forwardFFT
        || spectrumFftData.size() < (size_t) spectrumFftSize * 2)
        return;

    int n = processor->pullSpectrumSamples (spectrumFftData.data(), spectrumFftSize);
    if (n == 0) return;

    spectrumWindow->multiplyWithWindowingTable (spectrumFftData.data(), spectrumFftSize);
    for (size_t i = (size_t) spectrumFftSize; i < spectrumFftData.size(); ++i)
        spectrumFftData[i] = 0.0f;
    forwardFFT->performFrequencyOnlyForwardTransform (spectrumFftData.data());

    const float mindB = -60.0f;
    const float maxdB =   0.0f;
    for (int i = 0; i < spectrumScopeSize; ++i)
    {
        float skew    = 1.0f - std::exp (std::log (1.0f - (float) i
                                         / (float) spectrumScopeSize) * 0.2f);
        int   fftIdx  = juce::jlimit (0, spectrumFftSize / 2,
                            (int) (skew * (float) spectrumFftSize * 0.5f));
        float mag     = spectrumFftData[(size_t) fftIdx];
        float levelDb = juce::Decibels::gainToDecibels (mag)
                        - juce::Decibels::gainToDecibels ((float) spectrumFftSize);
        spectrumScopeData[(size_t) i] =
            juce::jmap (juce::jlimit (mindB, maxdB, levelDb), mindB, maxdB, 0.0f, 1.0f);
    }
}
