#include "DelayEngine.h"

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelaySamples) + 2);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    delayLine.prepare (spec);

    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutFilter.prepare (spec);
    highCutFilter.prepare (spec);

    delaySamples.reset (sampleRate, 0.05);
    lowCutHz.reset (sampleRate, 0.05);
    highCutHz.reset (sampleRate, 0.05);
    feedback.reset (sampleRate, 0.02);
    mix.reset (sampleRate, 0.02);

    reset();
}

void DelayEngine::reset()
{
    delayLine.reset();
    lowCutFilter.reset();
    highCutFilter.reset();
    lowCutHz.setCurrentAndTargetValue (lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue (highCutHz.getTargetValue());
    lowCutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    delaySamples.setCurrentAndTargetValue (delaySamples.getTargetValue());
    feedback.setCurrentAndTargetValue (feedback.getTargetValue());
    mix.setCurrentAndTargetValue (mix.getTargetValue());
}

void DelayEngine::setDelayMs (float ms)
{
    const auto samples = juce::jlimit (1.0f, maxDelaySamples, (float) (ms * 0.001 * sampleRate));
    delaySamples.setTargetValue (samples);
}

void DelayEngine::setFeedback (float amount01) { feedback.setTargetValue (juce::jlimit (0.0f, 0.95f, amount01)); }
void DelayEngine::setMix (float wet01)         { mix.setTargetValue (juce::jlimit (0.0f, 1.0f, wet01)); }

void DelayEngine::setLowCutHz (float hz)
{
    lowCutOn = hz > 0.0f;

    if (lowCutOn)
        lowCutHz.setTargetValue (juce::jlimit (10.0f, (float) sampleRate * 0.45f, hz));
}

void DelayEngine::setHighCutHz (float hz)
{
    highCutOn = hz > 0.0f;

    if (highCutOn)
        highCutHz.setTargetValue (juce::jlimit (10.0f, (float) sampleRate * 0.45f, hz));
}

float DelayEngine::filter (int channel, float sample)
{
    // Filters always run so their state stays warm and switching them on doesn't click.
    const auto low  = lowCutFilter.processSample (channel, sample);
    const auto afterLow = lowCutOn ? low : sample;
    const auto high = highCutFilter.processSample (channel, afterLow);
    return highCutOn ? high : afterLow;
}

void DelayEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto d   = delaySamples.getNextValue();
        const auto fb  = feedback.getNextValue();
        const auto wet = mix.getNextValue();

        if (lowCutHz.isSmoothing())  lowCutFilter.setCutoffFrequency (lowCutHz.getNextValue());
        if (highCutHz.isSmoothing()) highCutFilter.setCutoffFrequency (highCutHz.getNextValue());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto dry = data[i];
            const auto delayed = delayLine.popSample (ch, d);

            float echo;

            if (filtersInLoop)
            {
                echo = filter (ch, delayed);
                delayLine.pushSample (ch, dry + fb * echo);
            }
            else
            {
                delayLine.pushSample (ch, dry + fb * delayed);
                echo = filter (ch, delayed);
            }

            data[i] = dry * (1.0f - wet) + echo * wet;
        }
    }
}
