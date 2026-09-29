#include "DelayEngine.h"

namespace
{
    // Wear
    constexpr float wearMinCeiling   = 0.35f;  // saturator ceiling at full Wear
    constexpr float darkestHz        = 4500.0f; // one-pole cutoff at full Wear
    constexpr float maxWowMs         = 1.0f;
    constexpr float maxFlutterMs     = 0.03f;
    constexpr float wowHz            = 0.7f;
    constexpr float flutterHz        = 7.3f;
    constexpr float driftHz          = 0.5f;
    constexpr float driftIntervalSec = 0.4f;

    // Runaway: feedback from 110% (drive 0) to 160% (drive 1), ceiling from 0.5 down to 0.3.
    constexpr float runawayMinFeedback = 1.1f,  runawayFeedbackRange = 0.5f;
    constexpr float runawayMaxCeiling  = 0.5f,  runawayCeilingRange  = 0.2f;

    constexpr float dcBlockHz = 5.0f;

    // Tapestop
    constexpr float minTapestopMs = 50.0f, maxTapestopMs = 2000.0f;
    constexpr float spinUpRatio   = 0.5f;  // spin-up takes half the stop time
    constexpr float silentBelow   = 0.3f;  // speed under which the output fades out
    constexpr float spliceMs      = 50.0f;

    float onePoleCoef (float hz, double sampleRate)
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sampleRate);
    }
}

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    // Headroom for the tapestop head, which trails by up to the stop plus spin-up growth.
    maxReadSamples = maxDelaySamples + (float) ((maxTapestopMs * (1.0f + spinUpRatio) * 0.5f + 10.0f) * 0.001 * sampleRate);
    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxReadSamples) + 2);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    delayLine.prepare (spec);

    maxWidthSamples = (float) (maxWidthMs * 0.001 * sampleRate);
    widthDelayLine.setMaximumDelayInSamples ((int) std::ceil (maxWidthSamples) + 2);
    widthDelayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });

    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutFilter.prepare (spec);
    highCutFilter.prepare (spec);
    lowCutOutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutOutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutOutFilter.prepare (spec);
    highCutOutFilter.prepare (spec);

    spliceStep = 1.0f / (spliceMs * 0.001f * (float) sampleRate);
    setTapestopTimeMs (500.0f);

    delaySmoothingSamples = -1; // forces the ramp length to be recalculated for this sample rate
    setDelaySmoothingMs (50.0f);
    lowCutHz.reset (sampleRate, 0.05);
    highCutHz.reset (sampleRate, 0.05);
    feedback.reset (sampleRate, 0.02);
    mix.reset (sampleRate, 0.02);
    freezeFadeSamples = -1;
    setFreezeFadeMs (20.0f);
    inputSend.reset (sampleRate, 0.01);
    wear.reset (sampleRate, 0.05);
    runawayAmount.reset (sampleRate, 0.08);
    runawayDrive.reset (sampleRate, 0.05);

    dcCoef      = onePoleCoef (dcBlockHz, sampleRate);
    darkCoefMin = onePoleCoef (darkestHz, sampleRate);
    driftCoef   = onePoleCoef (driftHz, sampleRate);
    wowIncrement     = juce::MathConstants<float>::twoPi * wowHz / (float) sampleRate;
    flutterIncrement = juce::MathConstants<float>::twoPi * flutterHz / (float) sampleRate;
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
    lowCutOutFilter.reset();
    highCutOutFilter.reset();
    lowCutHz.setCurrentAndTargetValue (lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue (highCutHz.getTargetValue());
    lowCutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    lowCutOutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutOutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    delaySamples.setCurrentAndTargetValue (delaySamples.getTargetValue());
    feedback.setCurrentAndTargetValue (feedback.getTargetValue());
    mix.setCurrentAndTargetValue (mix.getTargetValue());
    freezeAmount.setCurrentAndTargetValue (freezeAmount.getTargetValue());
    pingPongAmount.setCurrentAndTargetValue (pingPongAmount.getTargetValue());
    inputSend.setCurrentAndTargetValue (inputSend.getTargetValue());
    wear.setCurrentAndTargetValue (wear.getTargetValue());
    runawayAmount.setCurrentAndTargetValue (runawayAmount.getTargetValue());
    runawayDrive.setCurrentAndTargetValue (runawayDrive.getTargetValue());

    for (int ch = 0; ch < 2; ++ch)
        dcState[ch] = darkState[ch] = 0.0f;

    wowPhase = flutterPhase = drift = driftTarget = 0.0f;
    driftCountdown = 0;

    tapeSpeed = tapestopHeld ? 0.0f : 1.0f;
    tapeOffset = spliceAmount = spliceOffset = 0.0f;
    tapestopWasHeld = tapestopHeld;
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

void DelayEngine::setFreezeFadeMs (float ms)
{
    const auto numSamples = juce::jmax (1, juce::roundToInt (ms * 0.001 * sampleRate));

    if (numSamples == freezeFadeSamples)
        return;

    freezeFadeSamples = numSamples;

    // As with the delay time, keep any fade in progress going from where it is.
    const auto current = freezeAmount.getCurrentValue();
    const auto target  = freezeAmount.getTargetValue();
    freezeAmount.reset (numSamples);
    freezeAmount.setCurrentAndTargetValue (current);
    freezeAmount.setTargetValue (target);
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

float DelayEngine::outputFilter (int channel, float sample)
{
    // Same chain as filter(), for the output head. While tapestop is idle it
    // sees exactly the same samples, so it produces exactly the same output.
    const auto low  = lowCutOutFilter.processSample (channel, sample);
    const auto afterLow = lowCutOn ? low : sample;
    const auto high = highCutOutFilter.processSample (channel, afterLow);
    return highCutOn ? high : afterLow;
}

void DelayEngine::setTapestopTimeMs (float ms)
{
    const auto stopSamples = juce::jlimit (minTapestopMs, maxTapestopMs, ms) * 0.001f * (float) sampleRate;
    stopStep  = 1.0f / stopSamples;
    startStep = 1.0f / (stopSamples * spinUpRatio);
}

void DelayEngine::advanceTapestop (float delay)
{
    if (tapestopWasHeld && ! tapestopHeld && tapeSpeed <= 0.0f)
    {
        // Released while stopped (and silent): jump the head ahead by exactly
        // what the spin-up will lose, so it lands back on the loop.
        const auto spinUpSamples = 1.0f / startStep;
        tapeOffset = juce::jmax ((spinUpSamples - 1.0f) * -0.5f, 1.0f - delay);
    }

    tapestopWasHeld = tapestopHeld;

    if (tapestopHeld)
        tapeSpeed = juce::jmax (0.0f, tapeSpeed - stopStep);
    else if (tapeSpeed < 1.0f)
        tapeSpeed = juce::jmin (1.0f, tapeSpeed + startStep);

    // The head trails further while it's slower than the tape; once stopped
    // and silent there's nothing to track.
    if (tapeSpeed > 0.0f)
        tapeOffset += 1.0f - tapeSpeed;

    // Back at speed but not quite on the loop (released early, or clamped):
    // splice back to it.
    if (tapeSpeed >= 1.0f && ! juce::approximatelyEqual (tapeOffset, 0.0f))
    {
        spliceOffset = tapeOffset;
        spliceAmount = 1.0f;
        tapeOffset = 0.0f;
    }

    spliceAmount = juce::jmax (0.0f, spliceAmount - spliceStep);
}

float DelayEngine::readHeard (int channel, float delay)
{
    auto heard = delayLine.popSample (channel, juce::jlimit (1.0f, maxReadSamples, delay + tapeOffset), false);

    if (spliceAmount > 0.0f)
    {
        const auto old = delayLine.popSample (channel, juce::jlimit (1.0f, maxReadSamples, delay + spliceOffset), false);
        heard += spliceAmount * (old - heard);
    }

    return heard;
}

float DelayEngine::tape (int channel, float sample, float amount, float ceiling, float darken)
{
    dcState[channel] += dcCoef * (sample - dcState[channel]);
    const auto blocked = sample - amount * dcState[channel];

    const auto saturated = ceiling * std::tanh (blocked / ceiling);
    const auto shaped = blocked + amount * (saturated - blocked);

    // Coefficient 1 passes the sample straight through.
    const auto coef = 1.0f - darken * (1.0f - darkCoefMin);
    darkState[channel] += coef * (shaped - darkState[channel]);
    return darkState[channel];
}

void DelayEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    const auto isStereo    = numChannels >= 2;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frozen = freezeAmount.getNextValue();
        const auto wearNow = wear.getNextValue();
        const auto runaway = runawayAmount.getNextValue();
        const auto drive   = runawayDrive.getNextValue();

        // Runaway takes feedback past 100% and the saturator fully in, which keeps
        // the loop bounded at the ceiling. Ramping both together keeps it bounded
        // on the way in too: the linear part of the loop gain stays below 1.
        const auto fb = feedback.getNextValue() * (1.0f - runaway)
                      + runaway * (runawayMinFeedback + runawayFeedbackRange * drive);
        const auto tapeAmount = juce::jmax (wearNow, runaway);
        const auto ceiling = (1.0f - (1.0f - wearMinCeiling) * wearNow) * (1.0f - runaway)
                           + runaway * (runawayMaxCeiling - runawayCeilingRange * drive);
        const auto darken = juce::jmax (wearNow, 0.5f * runaway);
        const auto wet = mix.getNextValue();
        const auto send = inputSend.getNextValue();
        const auto pingPong = isStereo ? pingPongAmount.getNextValue() : 0.0f;
        const auto width = widthSamples.getNextValue();

        // Wow (slow, with random drift) and flutter (fast), faded out while frozen.
        if (--driftCountdown <= 0)
        {
            driftTarget = random.nextFloat() * 2.0f - 1.0f;
            driftCountdown = (int) (driftIntervalSec * sampleRate);
        }

        drift += driftCoef * (driftTarget - drift);
        wowPhase += wowIncrement;
        flutterPhase += flutterIncrement;
        if (wowPhase >= juce::MathConstants<float>::twoPi)     wowPhase -= juce::MathConstants<float>::twoPi;
        if (flutterPhase >= juce::MathConstants<float>::twoPi) flutterPhase -= juce::MathConstants<float>::twoPi;

        const auto msToSamples = (float) (0.001 * sampleRate);
        const auto wowDepth = wearNow * wearNow * maxWowMs * msToSamples;
        const auto flutterDepth = wearNow * maxFlutterMs * msToSamples;
        const auto modulation = (1.0f - frozen) * (wowDepth * (0.6f * std::sin (wowPhase) + 0.4f * drift)
                                                   + flutterDepth * std::sin (flutterPhase));

        // Whole-sample delay while frozen: repeated fractional interpolation
        // would slowly dull the looping audio.
        auto d = juce::jlimit (1.0f, maxDelaySamples, delaySamples.getNextValue() + modulation);
        if (frozen > 0.0f)
            d = std::round (d);

        if (lowCutHz.isSmoothing())
        {
            const auto hz = lowCutHz.getNextValue();
            lowCutFilter.setCutoffFrequency (hz);
            lowCutOutFilter.setCutoffFrequency (hz);
        }

        if (highCutHz.isSmoothing())
        {
            const auto hz = highCutHz.getNextValue();
            highCutFilter.setCutoffFrequency (hz);
            highCutOutFilter.setCutoffFrequency (hz);
        }

        advanceTapestop (d);
        const auto tapeGain = tapeSpeed >= silentBelow ? 1.0f
                            : [] (float x) { return x * x * (3.0f - 2.0f * x); } (tapeSpeed / silentBelow);

        if (! isStereo)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                const auto dry = data[i];
                const auto heard = readHeard (ch, d);
                const auto delayed = delayLine.popSample (ch, d);

                const auto echo = filter (ch, delayed);
                const auto normalWrite = tape (ch, send * dry + fb * (filtersInLoop ? echo : delayed), tapeAmount, ceiling, darken);

                delayLine.pushSample (ch, normalWrite + frozen * (delayed - normalWrite));

                data[i] = dry * (1.0f - wet) + tapeGain * outputFilter (ch, heard) * wet;
            }

            continue;
        }

        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const float dry[2] { left[i], right[i] };
        const float heard[2] { readHeard (0, d), readHeard (1, d) };
        const float delayed[2] { delayLine.popSample (0, d), delayLine.popSample (1, d) };
        const float echo[2] { filter (0, delayed[0]), filter (1, delayed[1]) };
        const float looped[2] { filtersInLoop ? echo[0] : delayed[0], filtersInLoop ? echo[1] : delayed[1] };

        // Normal: each channel feeds back into itself.
        const float normalWrite[2] { send * dry[0] + fb * looped[0], send * dry[1] + fb * looped[1] };

        // Ping-pong: mono input -> left, left -> right at full level, right -> left via feedback.
        const float pingPongWrite[2] { 0.5f * send * (dry[0] + dry[1]) + fb * looped[1], looped[0] };

        // Frozen: loop the buffer as it is, swapping sides in ping-pong so it keeps bouncing.
        const float frozenWrite[2] { delayed[0] + pingPong * (delayed[1] - delayed[0]),
                                     delayed[1] + pingPong * (delayed[0] - delayed[1]) };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto write = tape (ch, normalWrite[ch] + pingPong * (pingPongWrite[ch] - normalWrite[ch]), tapeAmount, ceiling, darken);
            delayLine.pushSample (ch, write + frozen * (frozenWrite[ch] - write));
        }

        const float heardEcho[2] { tapeGain * outputFilter (0, heard[0]), tapeGain * outputFilter (1, heard[1]) };

        widthDelayLine.pushSample (0, heardEcho[1]);
        const auto rightEcho = widthDelayLine.popSample (0, width);

        left[i]  = dry[0] * (1.0f - wet) + heardEcho[0] * wet;
        right[i] = dry[1] * (1.0f - wet) + rightEcho * wet;
    }
}
