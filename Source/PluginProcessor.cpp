#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace IDs
{
    static const juce::String dryWet   { "drywet" };
    static const juce::String predelay { "predelay" };
    static const juce::String decay    { "decay" };
    static const juce::String stretch  { "stretch" };
    static const juce::String width    { "width" };
    static const juce::String modDepth { "moddepth" };
    static const juce::String modRate  { "modrate" };
    static const juce::String band0Freq { "b0freq" }, band0Gain { "b0gain" }, band0Q { "b0q" };
    static const juce::String band1Freq { "b1freq" }, band1Gain { "b1gain" }, band1Q { "b1q" };
    static const juce::String band2Freq { "b2freq" }, band2Gain { "b2gain" }, band2Q { "b2q" };
}

PingProcessor::PingProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    irManager.refresh();
}

PingProcessor::~PingProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout PingProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::dryWet,   "Dry / Wet",  0.0f, 1.0f, 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::predelay, "Predelay (ms)", 0.0f, 500.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::decay,    "Decay", 0.0f, 1.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::stretch,  "Stretch", 0.5f, 2.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::width,   "Width", 0.0f, 2.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::modDepth, "Mod Depth", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::modRate, "Mod Period (s)", 0.01f, 2.0f, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Freq, "Band 0 Freq (Hz)", 20.0f, 20000.0f, 400.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Gain, "Band 0 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Q, "Band 0 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Freq, "Band 1 Freq (Hz)", 20.0f, 20000.0f, 1000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Gain, "Band 1 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Q, "Band 1 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Freq, "Band 2 Freq (Hz)", 20.0f, 20000.0f, 4000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Gain, "Band 2 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Q, "Band 2 Q", 0.3f, 10.0f, 0.707f));
    return layout;
}

void PingProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;

    convolution.reset();
    convolution.prepare (spec);

    predelayLine.reset();
    predelayLine.setMaximumDelayInSamples ((int) (2.0 * sampleRate)); // up to 2 s
    predelayLine.prepare (spec);

    dryGain.reset();
    wetGain.reset();
    dryGain.prepare (spec);
    wetGain.prepare (spec);

    updateEQ();
    lowBand.prepare (spec);
    midBand.prepare (spec);
    highBand.prepare (spec);

    updateGains();
    updatePredelay();
    updateEQ();
}

void PingProcessor::releaseResources() {}

void PingProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2) return;

    updateGains();
    updatePredelay();
    updateEQ();

    // Dry copy
    juce::AudioBuffer<float> dryBuffer (numChannels, numSamples);
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1)
        dryBuffer.copyFrom (1, 0, buffer, 1, 0, numSamples);

    // Wet path: predelay -> convolution -> EQ -> width
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Predelay
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* ptr = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float in = ptr[i];
            predelayLine.pushSample (ch, in);
            ptr[i] = predelayLine.popSample (ch);
        }
    }

    // Convolution (stereo)
    convolution.process (context);

    // EQ
    lowBand.process (context);
    midBand.process (context);
    highBand.process (context);

    // Modulation: LFO on wet gain (period 0.01..2 s, depth 0..100%) — UI reversed: left = slow, right = fast
    float modRateRaw = apvts.getRawParameterValue (IDs::modRate)->load();
    float modPeriodSec = 2.01f - modRateRaw;
    float modDepth = apvts.getRawParameterValue (IDs::modDepth)->load();
    if (modDepth > 0.0001f && modPeriodSec > 0.0001f)
    {
        float phaseInc = (float) (1.0 / (modPeriodSec * currentSampleRate));
        for (int i = 0; i < numSamples; ++i)
        {
            float gain = 1.0f + modDepth * std::sin (2.0f * juce::MathConstants<float>::twoPi * lfoPhase);
            buffer.applyGain (0, i, 1, gain);
            buffer.applyGain (1, i, 1, gain);
            lfoPhase += phaseInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        }
    }

    float widthVal = apvts.getRawParameterValue (IDs::width)->load();
    applyWidth (buffer, widthVal);

    // Mix dry/wet: output = dry * dryBuffer + wet * wetBuffer (buffer is currently wet)
    float dry = dryGain.getGainLinear();
    float wet = wetGain.getGainLinear();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, wet);
        buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, dry);
    }
}

void PingProcessor::updateGains()
{
    float mix = apvts.getRawParameterValue (IDs::dryWet)->load();
    float dryVal = std::sqrt (1.0f - mix);
    float wetVal = std::sqrt (mix);
    dryGain.setGainLinear (dryVal);
    wetGain.setGainLinear (wetVal);
}

void PingProcessor::updatePredelay()
{
    float ms = apvts.getRawParameterValue (IDs::predelay)->load();
    float samples = (float) (currentSampleRate * ms * 0.001);
    predelayLine.setDelay (samples);
}

void PingProcessor::updateEQ()
{
    auto freq = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto gain = [this] (const juce::String& id) { return juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id)->load()); };
    auto q    = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    *lowBand.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band0Freq), q (IDs::band0Q), gain (IDs::band0Gain));
    *midBand.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band1Freq), q (IDs::band1Q), gain (IDs::band1Gain));
    *highBand.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band2Freq), q (IDs::band2Q), gain (IDs::band2Gain));
}

void PingProcessor::applyWidth (juce::AudioBuffer<float>& wet, float width)
{
    if (wet.getNumChannels() < 2) return;
    int n = wet.getNumSamples();
    float* L = wet.getWritePointer (0);
    float* R = wet.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        float m = (L[i] + R[i]) * 0.5f;
        float s = (L[i] - R[i]) * 0.5f;
        s *= width;
        L[i] = m + s;
        R[i] = m - s;
    }
}

void PingProcessor::loadIRFromFile (const juce::File& file)
{
    if (! file.existsAsFile()) return;
    lastLoadedIRFile = file;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) return;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    double rate = reader->sampleRate;
    loadIRFromBuffer (std::move (buf), rate);
}

void PingProcessor::loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate)
{
    if (buffer.getNumSamples() == 0) return;

    // Optional: apply reverse
    if (reverse)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* p = buffer.getWritePointer (ch);
            int n = buffer.getNumSamples();
            for (int i = 0, j = n - 1; i < j; ++i, --j)
                std::swap (p[i], p[j]);
        }
    }

    // Stretch: time-scale IR to 50%..200% of original length (1.0 = natural)
    float stretchFactor = apvts.getRawParameterValue (IDs::stretch)->load();
    int origLen = buffer.getNumSamples();
    int newLen = (int) (origLen * stretchFactor);
    if (newLen < 64) newLen = 64;
    if (newLen != origLen)
    {
        juce::AudioBuffer<float> stretched (buffer.getNumChannels(), newLen);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* src = buffer.getReadPointer (ch);
            float* dst = stretched.getWritePointer (ch);
            for (int i = 0; i < newLen; ++i)
            {
                float srcIdx = (float) i * (float) origLen / (float) newLen;
                int i0 = (int) srcIdx;
                int i1 = juce::jmin (i0 + 1, origLen - 1);
                float f = srcIdx - (float) i0;
                dst[i] = src[i0] * (1.0f - f) + src[i1] * f;
            }
        }
        buffer = std::move (stretched);
    }

    // Decay: exponential fade-out envelope (0% = flat, 100% = heavily damped) — UI reversed: left = more damped
    float decayParam = 1.0f - apvts.getRawParameterValue (IDs::decay)->load();
    int N = buffer.getNumSamples();
    if (decayParam > 0.001f && N > 0)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* p = buffer.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
            {
                float t = (float) i / (float) N;
                float env = std::exp (-decayParam * 6.0f * t);
                p[i] *= env;
            }
        }
    }

    currentIRBuffer = buffer;
    bool isStereo = buffer.getNumChannels() >= 2;
    convolution.loadImpulseResponse (std::move (buffer), bufferSampleRate,
                                    isStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::yes);
}

void PingProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("irIndex", selectedIRIndex);
        xml->setAttribute ("reverse", reverse);
        copyXmlToBinary (*xml, destData);
    }
}

void PingProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        selectedIRIndex = xml->getIntAttribute ("irIndex", -1);
        reverse = xml->getBoolAttribute ("reverse", false);
        if (selectedIRIndex >= 0)
        {
            auto file = irManager.getIRFileAt (selectedIRIndex);
            if (file.existsAsFile())
                loadIRFromFile (file);
        }
    }
}

double PingProcessor::getTailLengthSeconds() const
{
    int irSize = convolution.getCurrentIRSize();
    if (currentSampleRate > 0 && irSize > 0)
        return irSize / currentSampleRate;
    return 0.0;
}

juce::AudioProcessorEditor* PingProcessor::createEditor()
{
    return new PingEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PingProcessor();
}
