#include "DelayEngine.h"

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelaySamples) + 2);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    delayLine.prepare (spec);

    maxWidthSamples = (float) (maxWidthMs * 0.001 * sampleRate);
    widthDelayLine.setMaximumDelayInSamples ((int) std::ceil (maxWidthSamples) + 2);
    widthDelayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });

    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutFilter.prepare (spec);
    highCutFilter.prepare (spec);

    delaySmoothingSamples = -1; // forces the ramp length to be recalculated for this sample rate
    setDelaySmoothingMs (50.0f);
    lowCutHz.reset (sampleRate, 0.05);
    highCutHz.reset (sampleRate, 0.05);
    feedback.reset (sampleRate, 0.02);
    mix.reset (sampleRate, 0.02);
    freezeAmount.reset (sampleRate, 0.02);
    pingPongAmount.reset (sampleRate, 0.02);
    widthSamples.reset (sampleRate, 0.05);

    reset();
}

void DelayEngine::reset()
{
    delayLine.reset();
    widthDelayLine.reset();
    lowCutFilter.reset();
    highCutFilter.reset();
    lowCutHz.setCurrentAndTargetValue (lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue (highCutHz.getTargetValue());
    lowCutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    delaySamples.setCurrentAndTargetValue (delaySamples.getTargetValue());
    feedback.setCurrentAndTargetValue (feedback.getTargetValue());
    mix.setCurrentAndTargetValue (mix.getTargetValue());
    freezeAmount.setCurrentAndTargetValue (freezeAmount.getTargetValue());
    pingPongAmount.setCurrentAndTargetValue (pingPongAmount.getTargetValue());
    widthSamples.setCurrentAndTargetValue (widthSamples.getTargetValue());
}

void DelayEngine::setDelayMs (float ms)
{
    const auto samples = juce::jlimit (1.0f, maxDelaySamples, (float) (ms * 0.001 * sampleRate));
    delaySamples.setTargetValue (samples);
}

void DelayEngine::setDelaySmoothingMs (float ms)
{
    const auto numSamples = juce::roundToInt (juce::jmax (0.0f, ms) * 0.001 * sampleRate);

    if (numSamples == delaySmoothingSamples)
        return;

    delaySmoothingSamples = numSamples;

    // SmoothedValue::reset() snaps to the target, so carry on from wherever
    // the glide currently is, now at the new speed.
    const auto current = delaySamples.getCurrentValue();
    const auto target  = delaySamples.getTargetValue();
    delaySamples.reset (numSamples);
    delaySamples.setCurrentAndTargetValue (current);
    delaySamples.setTargetValue (target);
}

void DelayEngine::setStereoWidthMs (float ms)
{
    widthSamples.setTargetValue (juce::jlimit (0.0f, maxWidthSamples, (float) (ms * 0.001 * sampleRate)));
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
    const auto isStereo    = numChannels >= 2;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frozen = freezeAmount.getNextValue();
        const auto fb  = feedback.getNextValue();
        const auto wet = mix.getNextValue();
        const auto pingPong = isStereo ? pingPongAmount.getNextValue() : 0.0f;
        const auto width = widthSamples.getNextValue();

        // Whole-sample delay while frozen: repeated fractional interpolation
        // would slowly dull the looping audio.
        auto d = delaySamples.getNextValue();
        if (frozen > 0.0f)
            d = std::round (d);

        if (lowCutHz.isSmoothing())  lowCutFilter.setCutoffFrequency (lowCutHz.getNextValue());
        if (highCutHz.isSmoothing()) highCutFilter.setCutoffFrequency (highCutHz.getNextValue());

        if (! isStereo)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                const auto dry = data[i];
                const auto delayed = delayLine.popSample (ch, d);

                const auto echo = filter (ch, delayed);
                const auto normalWrite = dry + fb * (filtersInLoop ? echo : delayed);

                delayLine.pushSample (ch, normalWrite + frozen * (delayed - normalWrite));

                data[i] = dry * (1.0f - wet) + echo * wet;
            }

            continue;
        }

        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const float dry[2] { left[i], right[i] };
        const float delayed[2] { delayLine.popSample (0, d), delayLine.popSample (1, d) };
        const float echo[2] { filter (0, delayed[0]), filter (1, delayed[1]) };
        const float looped[2] { filtersInLoop ? echo[0] : delayed[0], filtersInLoop ? echo[1] : delayed[1] };

        // Normal: each channel feeds back into itself.
        const float normalWrite[2] { dry[0] + fb * looped[0], dry[1] + fb * looped[1] };

        // Ping-pong: mono input -> left, left -> right at full level, right -> left via feedback.
        const float pingPongWrite[2] { 0.5f * (dry[0] + dry[1]) + fb * looped[1], looped[0] };

        // Frozen: loop the buffer as it is, swapping sides in ping-pong so it keeps bouncing.
        const float frozenWrite[2] { delayed[0] + pingPong * (delayed[1] - delayed[0]),
                                     delayed[1] + pingPong * (delayed[0] - delayed[1]) };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto write = normalWrite[ch] + pingPong * (pingPongWrite[ch] - normalWrite[ch]);
            delayLine.pushSample (ch, write + frozen * (frozenWrite[ch] - write));
        }

        widthDelayLine.pushSample (0, echo[1]);
        const auto rightEcho = widthDelayLine.popSample (0, width);

        left[i]  = dry[0] * (1.0f - wet) + echo[0] * wet;
        right[i] = dry[1] * (1.0f - wet) + rightEcho * wet;
    }
}
