#include "ReadHead.h"

void ReadHead::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    spliceStep = 1.0f / (spliceMs * 0.001f * (float) sampleRate);
    pitchWindow = pitchWindowMs * 0.001f * (float) sampleRate;
    reverseAmount.reset (sampleRate, 0.03);
    reverseSpeedTarget.reset (sampleRate, 0.05);
    pitchAmount.reset (sampleRate, 0.03);
    setTapestopTimeMs (500.0f);
    reset();
}

void ReadHead::reset()
{
    tapeSpeed = tapestopHeld ? 0.0f : 1.0f;
    tapeOffset = spliceAmount = spliceOffset = 0.0f;
    tapestopWasHeld = tapestopHeld;

    reverseAmount.setCurrentAndTargetValue (reverseHeld ? 1.0f : 0.0f);
    reverseMix = reverseAmount.getCurrentValue();
    reversePhase = 0.0f;
    reverseSpeedTarget.setCurrentAndTargetValue (pitchOn ? pitchRatio : 1.0f);
    reverseSpeed = reverseSpeedTarget.getCurrentValue();

    pitchAmount.setCurrentAndTargetValue (pitchOn ? 1.0f : 0.0f);
    pitchMix = pitchAmount.getCurrentValue();
    pitchPhase = 0.0f;
}

void ReadHead::setTapestopTimeMs (float ms)
{
    const auto stopSamples = juce::jlimit (minTapestopMs, maxTapestopMs, ms) * 0.001f * (float) sampleRate;
    stopStep  = 1.0f / stopSamples;
    startStep = 1.0f / (stopSamples * spinUpRatio);
}

void ReadHead::setPitchSemitones (float semitones)
{
    pitchOn = std::abs (semitones) > 0.001f;

    // Off keeps the last ratio while the shift fades out.
    if (pitchOn)
        pitchRatio = std::pow (2.0f, semitones / 12.0f);
}

void ReadHead::advance (float delay)
{
    advanceTapestop (delay);
    advanceReverse (delay);

    if (pitchOn || pitchMix > 0.0f)
        advancePitch();
}

void ReadHead::advanceTapestop (float delay)
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

void ReadHead::advanceReverse (float delay)
{
    // Start each press at the top of a segment, but only from fully forward
    // so an in-progress crossfade doesn't jump.
    if (reverseHeld && reverseMix <= 0.0f)
        reversePhase = 0.0f;

    reverseAmount.setTargetValue (reverseHeld ? 1.0f : 0.0f);
    reverseMix = reverseAmount.getNextValue();

    // Pitch while reversed is the speed the heads read back at; glides when it changes.
    reverseSpeedTarget.setTargetValue (pitchOn ? pitchRatio : 1.0f);
    reverseSpeed = reverseSpeedTarget.getNextValue();

    // The heads move backwards at tape speed, so Tapestop slows them too.
    const auto segment = juce::jmax (2.0f, delay);
    reversePhase += tapeSpeed;

    while (reversePhase >= segment)
        reversePhase -= segment;
}

void ReadHead::advancePitch()
{
    pitchAmount.setTargetValue (pitchOn ? 1.0f : 0.0f);
    pitchMix = pitchAmount.getNextValue();

    if (pitchMix <= 0.0f)
    {
        pitchPhase = 0.0f;
        return;
    }

    // Reading `ratio` samples per sample means the delay changes by (1 - ratio).
    pitchPhase += 1.0f - pitchRatio;

    while (pitchPhase >= pitchWindow) pitchPhase -= pitchWindow;
    while (pitchPhase < 0.0f)         pitchPhase += pitchWindow;
}
