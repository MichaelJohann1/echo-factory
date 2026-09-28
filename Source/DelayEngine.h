#pragma once

#include <juce_dsp/juce_dsp.h>

/** Stereo feedback delay with smoothed time, feedback and mix. */
class DelayEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, float maxDelayMs);
    void reset();

    void setDelayMs (float ms);
    void setFeedback (float amount01);
    void setMix (float wet01);

    void process (juce::AudioBuffer<float>& buffer);

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> delaySamples, feedback, mix;
    double sampleRate = 44100.0;
    float maxDelaySamples = 1.0f;
};
