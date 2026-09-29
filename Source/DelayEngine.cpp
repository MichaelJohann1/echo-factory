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

    // Reverb line lengths in ms (left, right: different for a wide, decorrelated wash).
    constexpr float diffuserMs[2][DiffusionNetwork::numLines] {
        { 15.1f, 17.9f, 20.3f, 22.7f, 25.3f, 27.9f, 30.7f, 33.1f, 36.1f, 38.9f, 41.3f, 44.3f, 47.1f, 50.9f, 54.1f, 57.7f },
        { 16.3f, 18.7f, 21.1f, 23.9f, 26.3f, 29.1f, 31.9f, 34.3f, 37.1f, 39.7f, 42.7f, 45.1f, 48.3f, 51.7f, 55.3f, 58.9f },
    };
    // Pre-diffuser allpass lengths in ms (left, right).
    constexpr float preDiffuserMs[2][DiffusionNetwork::numPreStages] {
        { 17.3f, 29.1f, 43.7f, 61.3f },
        { 19.1f, 31.3f, 41.9f, 58.7f },
    };

    // Diffusion turns the delay into a reverb. With the square of the knob, the
    // delay's own feedback is handed over to the reverb's tail, whose length
    // grows from a small room to the decay the feedback would have given
    // (RT60 at low frequencies, at least 2 s, at most 20 s; the top end dies
    // away twice as fast). At full, the delay feeds the reverb once and Feedback
    // sets how long it rings, so there are no separate repeats left to hear.
    constexpr float minDiffusionRt60 = 0.3f, roomDiffusionRt60 = 2.0f, maxDiffusionRt60 = 20.0f;
    constexpr float diffusionHighRatio = 0.5f;

    float onePoleCoef (float hz, double sampleRate)
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sampleRate);
    }
}

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    // The output head reads up to three delay times back while reversing (the
    // segment is read backwards from one to three delay times), plus the
    // tapestop head's trail of up to the stop plus spin-up growth.
    maxReadSamples = 3.0f * maxDelaySamples + (float) ((maxTapestopMs * (1.0f + spinUpRatio) * 0.5f + 10.0f) * 0.001 * sampleRate);
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

    for (size_t ch = 0; ch < 2; ++ch)
        diffusers[ch].prepare (diffuserMs[ch], preDiffuserMs[ch], sampleRate);

    diffusion.reset (sampleRate, 0.05);
    reverseAmount.reset (sampleRate, 0.03);
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

    diffusion.setCurrentAndTargetValue (diffusion.getTargetValue());
    reverseAmount.setCurrentAndTargetValue (reverseHeld ? 1.0f : 0.0f);
    reverseMix = reverseAmount.getCurrentValue();
    reversePhase = 0.0f;

    for (auto& network : diffusers)
        network.clear();

    diffusionActive = false;
    diffusionRt60 = 0.0f; // forces the tail length to be recalculated

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

void DelayEngine::advanceReverse (float delay)
{
    // Start each press at the top of a segment, but only from fully forward
    // so an in-progress crossfade doesn't jump.
    if (reverseHeld && reverseMix <= 0.0f)
        reversePhase = 0.0f;

    reverseAmount.setTargetValue (reverseHeld ? 1.0f : 0.0f);
    reverseMix = reverseAmount.getNextValue();

    // The heads move backwards at tape speed, so Tapestop slows them too.
    const auto segment = juce::jmax (2.0f, delay);
    reversePhase += tapeSpeed;

    while (reversePhase >= segment)
        reversePhase -= segment;
}

float DelayEngine::readHeard (int channel, float delay)
{
    const auto read = [this, channel] (float d)
    {
        return delayLine.popSample (channel, juce::jlimit (1.0f, maxReadSamples, d), false);
    };

    auto heard = read (delay + tapeOffset);

    if (spliceAmount > 0.0f)
        heard += spliceAmount * (read (delay + spliceOffset) - heard);

    if (reverseMix > 0.0f)
    {
        // Reading 2 samples further back for every sample forward plays the
        // segment backwards. Head B is half a segment behind head A; their
        // sin² windows sum to 1, so segment boundaries are never heard.
        const auto segment = juce::jmax (2.0f, delay);
        const auto phaseA = reversePhase;
        const auto phaseB = std::fmod (reversePhase + 0.5f * segment, segment);
        const auto windowA = juce::square (std::sin (juce::MathConstants<float>::pi * phaseA / segment));

        const auto reversed = windowA * read (delay + tapeOffset + 2.0f * phaseA)
                            + (1.0f - windowA) * read (delay + tapeOffset + 2.0f * phaseB);

        heard += reverseMix * (reversed - heard);
    }

    return heard;
}

float DelayEngine::reverberate (int channel, float echo)
{
    if (! diffusionActive)
        return echo;

    const auto reverb = diffusers[(size_t) channel].process (echo) * diffusionMakeup;
    return diffusionCos * echo + diffusionSin * reverb;
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
        // the loop bounded at the ceiling. The saturator comes in twice as fast as
        // the feedback, so it's fully in before feedback passes 100% and loud input
        // can't overshoot on the way in.
        const auto diffusionNow = diffusion.getNextValue();
        const auto handover = diffusionNow * diffusionNow; // share of the sustain given to the reverb
        const auto feedbackNow = feedback.getNextValue();
        const auto fb = feedbackNow * (1.0f - runaway) * (1.0f - handover)
                      + runaway * (runawayMinFeedback + runawayFeedbackRange * drive);
        const auto tapeAmount = juce::jmax (wearNow, juce::jmin (1.0f, 2.0f * runaway));
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
        advanceReverse (d);
        // Diffusion blends the echoes into the reverb with an equal-power
        // crossfade. At 0 the reverb is out of the path; its tail is cleared.
        if (diffusionNow > 0.0f)
        {
            if (! diffusionActive || ! juce::exactlyEqual (diffusionNow, diffusionSetting))
            {
                const auto theta = diffusionNow * juce::MathConstants<float>::halfPi;
                diffusionCos = std::cos (theta);
                diffusionSin = std::sin (theta);
                diffusionSetting = diffusionNow;
            }

            // The decay the delay's feedback gives: 60 dB at -20 log10(fb) dB per period.
            const auto delaySeconds = delaySamples.getCurrentValue() / (float) sampleRate;
            const auto feedbackRt60 = feedbackNow > 0.001f ? 3.0f * delaySeconds / -std::log10 (feedbackNow) : 0.0f;
            const auto washRt60 = juce::jlimit (roomDiffusionRt60, maxDiffusionRt60, feedbackRt60);
            const auto rt60 = minDiffusionRt60 + (washRt60 - minDiffusionRt60) * handover;

            // Recalculated when the tail length moves by more than 1%.
            if (std::abs (rt60 - diffusionRt60) > 0.01f * diffusionRt60)
            {
                for (auto& network : diffusers)
                    network.setDecay (rt60, diffusionHighRatio);

                diffusionMakeup = diffusers[0].getMakeup();

                diffusionRt60 = rt60;
            }

            diffusionActive = true;
        }
        else if (diffusionActive)
        {
            for (auto& network : diffusers)
                network.clear();

            diffusionActive = false;
        }
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
                const auto normalWrite = tape (ch, send * dry + fb * (filtersInLoop ? echo : delayed),
                                               tapeAmount, ceiling, darken);

                delayLine.pushSample (ch, normalWrite + frozen * (delayed - normalWrite));

                data[i] = dry * (1.0f - wet) + reverberate (ch, tapeGain * outputFilter (ch, heard)) * wet;
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
            const auto mixed = normalWrite[ch] + pingPong * (pingPongWrite[ch] - normalWrite[ch]);
            const auto write = tape (ch, mixed, tapeAmount, ceiling, darken);
            delayLine.pushSample (ch, write + frozen * (frozenWrite[ch] - write));
        }

        const float heardEcho[2] { reverberate (0, tapeGain * outputFilter (0, heard[0])),
                                   reverberate (1, tapeGain * outputFilter (1, heard[1])) };

        widthDelayLine.pushSample (0, heardEcho[1]);
        const auto rightEcho = widthDelayLine.popSample (0, width);

        left[i]  = dry[0] * (1.0f - wet) + heardEcho[0] * wet;
        right[i] = dry[1] * (1.0f - wet) + rightEcho * wet;
    }
}
