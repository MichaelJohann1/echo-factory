#include "DelayEngine.h"

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelaySamples) + 2);
    delayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) });

    delaySamples.reset (sampleRate, 0.05);
    feedback.reset (sampleRate, 0.02);
    mix.reset (sampleRate, 0.02);

    reset();
}

void DelayEngine::reset()
{
    delayLine.reset();
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

void DelayEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto d   = delaySamples.getNextValue();
        const auto fb  = feedback.getNextValue();
        const auto wet = mix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto dry = data[i];
            const auto delayed = delayLine.popSample (ch, d);

            delayLine.pushSample (ch, dry + fb * delayed);
            data[i] = dry * (1.0f - wet) + delayed * wet;
        }
    }
}
