#pragma once

#include <juce_dsp/juce_dsp.h>

/**
    Stereo feedback delay with smoothed time, feedback and mix, plus 12 dB/oct
    low cut (high-pass) and high cut (low-pass) filters on the delayed signal.

    Filters in the feedback loop shape every repeat a little more; output-only
    filters shape the echoes once without affecting what is fed back.

    Freeze crossfades the write path from (input + feedback) to the buffer's own
    unfiltered output, so the current contents loop forever without decaying.
*/
class DelayEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, float maxDelayMs);
    void reset();

    void setDelayMs (float ms);
    void setFeedback (float amount01);
    void setMix (float wet01);

    /** Pass 0 to bypass a filter. */
    void setLowCutHz (float hz);
    void setHighCutHz (float hz);
    void setFiltersInFeedbackLoop (bool inLoop) { filtersInLoop = inLoop; }

    /** While frozen, input is no longer written and the buffer loops at full level. */
    void setFrozen (bool shouldFreeze) { freezeAmount.setTargetValue (shouldFreeze ? 1.0f : 0.0f); }

    void process (juce::AudioBuffer<float>& buffer);

private:
    float filter (int channel, float sample);

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter, highCutFilter;
    juce::SmoothedValue<float> delaySamples, feedback, mix, freezeAmount;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz { 20.0f }, highCutHz { 20000.0f };
    bool lowCutOn = false, highCutOn = false, filtersInLoop = true;
    double sampleRate = 44100.0;
    float maxDelaySamples = 1.0f;
};
