#include "WaveformComponent.h"
#include "PluginProcessor.h"
#include "PingBinaryData.h"

namespace
{
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour waveFill   { 0x28e8a84a };
    const juce::Colour waveLine   { 0xffe8e8e8 };
    const juce::Colour textDim    { 0xff909090 };
    const juce::Colour trimLineColour { 0xffe8a84a };
    constexpr float waveformGain = 1.5f;
}

WaveformComponent::WaveformComponent (PingProcessor& p) : processor (p) {}

juce::Rectangle<float> WaveformComponent::getWaveformInnerBounds() const
{
    auto b = getLocalBounds().toFloat().reduced (juce::jmin (10.0f, getWidth() / 40.0f));
    return b;
}

float WaveformComponent::trimPositionToFraction (float x) const
{
    auto inner = getWaveformInnerBounds();
    if (inner.getWidth() <= 0) return 0.0f;
    float frac = (x - inner.getX()) / inner.getWidth();
    return juce::jlimit (0.0f, 0.95f, frac);
}

void WaveformComponent::paint (juce::Graphics& g)
{
    const auto& buf = processor.getCurrentIRBuffer();
    auto inner = getWaveformInnerBounds();

    if (buf.getNumSamples() == 0)
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("No IR loaded", getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    int numChannels = buf.getNumChannels();
    int numSamples = buf.getNumSamples();
    // True-stereo 4ch: display as 2 (L = ch0+ch1, R = ch2+ch3) to match output
    int displayChannels = (numChannels >= 4) ? 2 : numChannels;
    const int gapBetweenChannels = 2;
    int totalChannelHeight = (int) inner.getHeight() - (displayChannels > 1 ? gapBetweenChannels * (displayChannels - 1) : 0);
    float hPerCh = (float) totalChannelHeight / (float) juce::jmax (1, displayChannels);

    g.setColour (panelBorder.withAlpha (0.4f));
    for (int i = 1; i <= 3; ++i)
    {
        float x = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
    }
    for (int i = 1; i <= 2; ++i)
    {
        float y = inner.getY() + inner.getHeight() * (float) i / 3.0f;
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
    }

    const float* ch0 = buf.getNumChannels() > 0 ? buf.getReadPointer (0) : nullptr;
    const float* ch1 = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : nullptr;
    const float* ch2 = buf.getNumChannels() > 2 ? buf.getReadPointer (2) : nullptr;
    const float* ch3 = buf.getNumChannels() > 3 ? buf.getReadPointer (3) : nullptr;

    for (int dispCh = 0; dispCh < displayChannels; ++dispCh)
    {
        int chY = (int) (inner.getY() + dispCh * (hPerCh + gapBetweenChannels));
        auto area = inner.withHeight ((int) hPerCh).withY ((float) chY).reduced (3);
        juce::Path path;
        juce::Path fillPath;
        float centreY = area.getCentreY();
        path.startNewSubPath ((float) area.getX(), centreY);
        fillPath.startNewSubPath ((float) area.getX(), centreY);
        for (int x = 0; x < area.getWidth(); ++x)
        {
            int sampleIdx = (int) ((double) x / (double) area.getWidth() * numSamples);
            if (sampleIdx >= numSamples) sampleIdx = numSamples - 1;
            float level;
            if (numChannels >= 4)
                level = (dispCh == 0) ? (ch0[sampleIdx] + ch1[sampleIdx]) * 0.5f : (ch2[sampleIdx] + ch3[sampleIdx]) * 0.5f;
            else
                level = (dispCh == 0 && ch0) ? ch0[sampleIdx] : (dispCh == 1 && ch1) ? ch1[sampleIdx] : 0.0f;
            level *= waveformGain;
            float y = centreY - level * area.getHeight();
            float px = (float) area.getX() + x;
            path.lineTo (px, y);
            fillPath.lineTo (px, y);
        }
        fillPath.lineTo ((float) area.getRight(), centreY);
        fillPath.closeSubPath();
        g.setColour (waveFill);
        g.fillPath (fillPath);
        g.setColour (waveLine);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }

    // Reverse trim line overlay (only when Reverse is engaged)
    if (processor.getReverse() && buf.getNumSamples() > 0)
    {
        float trimFrac = processor.getReverseTrim();
        float lineX = inner.getX() + trimFrac * inner.getWidth();
        g.setColour (trimLineColour);
        g.drawLine (lineX, inner.getY(), lineX, inner.getBottom(), 2.0f);
        // Draggable handle hint - slightly thicker at centre
        g.fillRect (lineX - 2.0f, inner.getCentreY() - 6.0f, 4.0f, 12.0f);
    }
}

void WaveformComponent::mouseDown (juce::MouseEvent const& e)
{
    if (! processor.getReverse() || processor.getCurrentIRBuffer().getNumSamples() == 0)
        return;
    auto inner = getWaveformInnerBounds();
    if (inner.contains (e.position.toFloat()))
    {
        draggingTrim = true;
        float frac = trimPositionToFraction (e.position.x);
        processor.setReverseTrim (frac);
        repaint();
    }
}

void WaveformComponent::mouseDrag (juce::MouseEvent const& e)
{
    if (! draggingTrim) return;
    float frac = trimPositionToFraction (e.position.x);
    processor.setReverseTrim (frac);
    repaint();
}

void WaveformComponent::mouseUp (juce::MouseEvent const& e)
{
    juce::ignoreUnused (e);
    if (draggingTrim)
    {
        draggingTrim = false;
        if (onTrimChanged)
            onTrimChanged();
    }
}
