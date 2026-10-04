#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
    A read head on a delay line, with its own tape transport: Tapestop, Reverse
    and Pitch. It doesn't own the line; read() is given a function that reads
    the line at a delay in samples.

    Tapestop slows the head to a stop, fading out as it goes, then spins back up
    in half the stop time. Once fully stopped and silent, the head jumps to
    where the spin-up will land exactly on the loop; if released early, a short
    splice brings it back.

    Reverse: two heads read backwards over segments one delay time long, half a
    segment apart, with sin² windows that always sum to 1. The heads slow with
    Tapestop.

    Pitch: two heads sweep a short window at (1 - ratio) samples per sample,
    half a window apart, with sin² windows. It's crossfaded in, so at 0
    semitones the head reads exactly what it would without it. While reversed,
    pitch is done by speed instead: the reverse heads read back (1 + ratio)
    samples per sample, which plays backwards at exactly `ratio` speed. (The
    window shifter on top of reverse would cancel it out: at +12 the heads
    would stand still.)

    The state is shared by all channels: call advance() once per sample, then
    read() for each channel.
*/
class ReadHead
{
public:
    static constexpr float minTapestopMs = 50.0f, maxTapestopMs = 2000.0f;
    static constexpr float spinUpRatio   = 0.5f;  // spin-up takes half the stop time
    static constexpr float pitchWindowMs = 50.0f;

    /** The furthest beyond its delay this head reads, apart from Reverse, which reads up to
        (1 + ratio) delays further: 2 unpitched, 3 an octave up. */
    static float maxExtraSamples (double sampleRate)
    {
        return (float) ((maxTapestopMs * (1.0f + spinUpRatio) * 0.5f + pitchWindowMs + 10.0f) * 0.001 * sampleRate);
    }

    void prepare (double sampleRate);

    /** Jumps to the current settings: stopped if Tapestop is on, reversed if Reverse is on. */
    void reset();

    void setTapestop (bool shouldStop)      { tapestopHeld = shouldStop; }
    void setReverse (bool shouldReverse)    { reverseHeld = shouldReverse; }
    void setTapestopTimeMs (float ms);
    void setPitchSemitones (float semitones);

    /** Moves the transport, reverse and pitch heads on by one sample. */
    void advance (float delay);

    /** Level from Tapestop: fades to silence as the tape stops. */
    float getGain() const
    {
        return tapeSpeed >= silentBelow ? 1.0f
                                        : [] (float x) { return x * x * (3.0f - 2.0f * x); } (tapeSpeed / silentBelow);
    }

    /** What this head hears at `delay` samples; readAt (float delaySamples) reads the line. */
    template <typename ReadAt>
    float read (ReadAt&& readAt, float delay) const
    {
        const auto at = [this, &readAt] (float d) { return pitchMix > 0.0f ? readPitched (readAt, d) : readAt (d); };

        auto heard = at (delay + tapeOffset);

        if (spliceAmount > 0.0f)
            heard += spliceAmount * (at (delay + spliceOffset) - heard);

        if (reverseMix > 0.0f)
        {
            // Reading 2 samples further back for every sample forward plays the
            // segment backwards, and 1 + ratio plays it backwards at `ratio`
            // speed, which is how pitch works while reversed (no window shifter).
            // Head B is half a segment behind head A; their sin² windows sum
            // to 1, so segment boundaries are never heard.
            const auto segment = juce::jmax (2.0f, delay);
            const auto phaseA = reversePhase;
            const auto phaseB = std::fmod (reversePhase + 0.5f * segment, segment);
            const auto windowA = juce::square (std::sin (juce::MathConstants<float>::pi * phaseA / segment));
            const auto rate = 1.0f + reverseSpeed;

            const auto reversed = windowA * readAt (delay + tapeOffset + rate * phaseA)
                                + (1.0f - windowA) * readAt (delay + tapeOffset + rate * phaseB);

            heard += reverseMix * (reversed - heard);
        }

        return heard;
    }

private:
    template <typename ReadAt>
    float readPitched (ReadAt& readAt, float d) const
    {
        const auto phaseB = std::fmod (pitchPhase + 0.5f * pitchWindow, pitchWindow);
        const auto windowA = juce::square (std::sin (juce::MathConstants<float>::pi * pitchPhase / pitchWindow));
        const auto shifted = windowA * readAt (d + pitchPhase) + (1.0f - windowA) * readAt (d + phaseB);

        if (pitchMix >= 1.0f)
            return shifted;

        const auto plain = readAt (d);
        return plain + pitchMix * (shifted - plain);
    }

    void advanceTapestop (float delay);
    void advanceReverse (float delay);
    void advancePitch();

    static constexpr float silentBelow = 0.3f;  // speed under which the output fades out
    static constexpr float spliceMs    = 50.0f;

    // Tapestop: speed 1 is normal, offset is how far the head trails the loop.
    bool tapestopHeld = false, tapestopWasHeld = false;
    float tapeSpeed = 1.0f, tapeOffset = 0.0f, stopStep = 0.0f, startStep = 0.0f;
    float spliceAmount = 0.0f, spliceOffset = 0.0f, spliceStep = 0.0f;

    // Reverse: phase through the current segment, and how far in reverse is (0..1).
    bool reverseHeld = false;
    float reversePhase = 0.0f, reverseMix = 0.0f, reverseSpeed = 1.0f;
    juce::SmoothedValue<float> reverseAmount, reverseSpeedTarget { 1.0f };

    // Pitch: phase through the window, the ratio being played, and how far in the shift is.
    float pitchWindow = 1.0f, pitchPhase = 0.0f, pitchRatio = 1.0f, pitchMix = 0.0f;
    bool pitchOn = false;
    juce::SmoothedValue<float> pitchAmount;

    double sampleRate = 44100.0;
};
