#include "WaveformComponent.h"
#include "PluginProcessor.h"
#include "PingBinaryData.h"

namespace
{
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour waveFill   { 0x288cd6ef };
    const juce::Colour waveLine   { 0xffd8e8f4 };
    const juce::Colour textDim    { 0xff909090 };
    const juce::Colour trimLineColour { 0xff8cd6ef };
    constexpr float waveformMargin = 0.9f;  // fraction of half-height used (leaves a small border)
    constexpr float dBFloor        = -60.0f; // level that maps to zero height (one full RT60 of range)
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

    // Find the true peak across every sample in every displayed channel.
    // This must match exactly what the per-pixel max scan below will find, so both
    // use the same channel-mixing formula.
    float peakSample = 1.0e-6f;  // non-zero floor prevents divide-by-zero on silence
    for (int i = 0; i < numSamples; ++i)
    {
        if (numChannels >= 4)
        {
            peakSample = juce::jmax (peakSample, std::abs ((ch0[i] + ch1[i]) * 0.5f));
            peakSample = juce::jmax (peakSample, std::abs ((ch2[i] + ch3[i]) * 0.5f));
        }
        else
        {
            if (ch0) peakSample = juce::jmax (peakSample, std::abs (ch0[i]));
            if (ch1) peakSample = juce::jmax (peakSample, std::abs (ch1[i]));
        }
    }
    for (int dispCh = 0; dispCh < displayChannels; ++dispCh)
    {
        int chY = (int) (inner.getY() + dispCh * (hPerCh + gapBetweenChannels));
        auto area = inner.withHeight ((int) hPerCh).withY ((float) chY).reduced (3);
        const int pixelW = (int) area.getWidth();
        float centreY = area.getCentreY();
        float halfH   = area.getHeight() * 0.5f;

        // For each pixel: scan every sample in its range for the peak absolute value,
        // then convert to dB relative to the overall peak.  This gives the classic
        // IR "ski-slope" decay shape and makes short/long rooms look equally useful.
        auto pixelPeakAbs = [&] (int x) -> float
        {
            int s0 = (int) ((double) x      / pixelW * numSamples);
            int s1 = (int) ((double)(x + 1) / pixelW * numSamples);
            s0 = juce::jlimit (0, numSamples - 1, s0);
            s1 = juce::jlimit (s0, numSamples - 1, s1);
            float pk = 0.0f;
            for (int i = s0; i <= s1; ++i)
            {
                float s;
                if (numChannels >= 4)
                    s = (dispCh == 0) ? (ch0[i] + ch1[i]) * 0.5f : (ch2[i] + ch3[i]) * 0.5f;
                else
                    s = (dispCh == 0 && ch0) ? ch0[i] : (dispCh == 1 && ch1) ? ch1[i] : 0.0f;
                pk = juce::jmax (pk, std::abs (s));
            }
            return pk;
        };

        // Convert a peak absolute value to a height fraction in [0, 1].
        // 0 dB (== overall peak)  →  1.0  (full half-height)
        // dBFloor                 →  0.0  (centre line)
        auto peakToFrac = [&] (float pk) -> float
        {
            if (pk <= 0.0f) return 0.0f;
            float dB = 20.0f * std::log10 (pk / peakSample);
            dB = juce::jmax (dB, dBFloor);
            return (dB - dBFloor) / (-dBFloor);   // maps [dBFloor, 0] → [0, 1]
        };

        juce::Path fillPath;
        juce::Path topPath;

        // Forward pass — top edge of the symmetric fill
        for (int x = 0; x < pixelW; ++x)
        {
            float deviation = peakToFrac (pixelPeakAbs (x)) * halfH * waveformMargin;
            float px  = area.getX() + (float) x;
            float yTop = centreY - deviation;

            if (x == 0)
            {
                fillPath.startNewSubPath (px, centreY);
                fillPath.lineTo (px, yTop);
                topPath.startNewSubPath (px, yTop);
            }
            else
            {
                fillPath.lineTo (px, yTop);
                topPath.lineTo (px, yTop);
            }
        }

        // Reverse pass — bottom edge mirrors the top
        for (int x = pixelW - 1; x >= 0; --x)
        {
            float deviation = peakToFrac (pixelPeakAbs (x)) * halfH * waveformMargin;
            float px = area.getX() + (float) x;
            fillPath.lineTo (px, centreY + deviation);
        }
        fillPath.closeSubPath();

        g.setColour (waveFill);
        g.fillPath (fillPath);
        g.setColour (waveLine);
        g.strokePath (topPath, juce::PathStrokeType (1.8f));
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
