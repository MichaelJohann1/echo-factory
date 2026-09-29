#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DiffusionNetwork.h"

/**
    Stereo feedback delay with smoothed time, feedback and mix, plus 12 dB/oct
    low cut (high-pass) and high cut (low-pass) filters on the delayed signal.

    Filters in the feedback loop shape every repeat a little more; output-only
    filters shape the echoes once without affecting what is fed back.

    Freeze crossfades the write path from (input + feedback) to the buffer's own
    unfiltered output, so the current contents loop forever without decaying.

    Ping-pong (stereo only) feeds the mono input into the left line, passes the
    left echo at full level into the right line, and feeds the right echo back
    into the left line through the feedback amount. With zero feedback that
    gives exactly two repeats: one left, then one right.

    The input send scales how much of the input is written into the delay; the
    processor uses it for Throw and the Throw Only input mode. The dry signal
    is not affected.

    Stereo width delays the right channel of the echoes by a few milliseconds,
    on the output only, so it doesn't build up in the feedback loop.

    Wear adds tape character to the write path: a soft saturator with a
    ceiling, gentle darkening on every pass, and wow and flutter on the delay
    time. Each is crossfaded in by the Wear amount, so 0 is exactly the clean
    delay. Wow and flutter fade out while frozen so the loop stays in time.

    Runaway raises feedback above 100% and pushes the saturator fully in. The
    saturator's ceiling keeps the loop bounded, and a DC blocker stops any
    offset from building up.

    Tapestop moves a separate output read head, so the feedback loop (and a
    frozen loop) is never touched: only what you hear slows to a stop, fading
    out as it goes, then spins back up in half the stop time. Once fully
    stopped and silent, the head jumps to where the spin-up will land exactly
    on the live loop; if released early, a short splice brings it back. The
    output has its own copy of the filters, fed the same signal when idle.

    Diffusion turns the delay into a reverb. The heard echoes are blended
    (equal power) into a DiffusionNetwork reverb per channel, after the loop.
    As it goes up, the delay's own feedback is handed over to the reverb's
    tail, whose length is set from what the feedback would have given; at
    full, the delay feeds the reverb once, so no separate repeats are left.
    The reverb is never inside the loop, so the loop can't run away.

    Reverse also lives on the output head: two heads read backwards over
    segments one delay time long, half a segment apart, with sin² windows that
    always sum to 1. The loop is untouched, so a frozen loop plays backwards and
    comes back as it was. The heads slow with Tapestop.
*/
class DelayEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs);
    void reset();

    void setDelayMs (float ms);

    /** How long a delay time change takes to reach its new value. 0 jumps instantly. */
    void setDelaySmoothingMs (float ms);
    void setFeedback (float amount01);
    void setMix (float wet01);

    /** Pass 0 to bypass a filter. */
    void setLowCutHz (float hz);
    void setHighCutHz (float hz);
    void setFiltersInFeedbackLoop (bool inLoop) { filtersInLoop = inLoop; }

    /** While frozen, input is no longer written and the buffer loops at full level. */
    void setFrozen (bool shouldFreeze) { freezeAmount.setTargetValue (shouldFreeze ? 1.0f : 0.0f); }
    void setFreezeFadeMs (float ms);

    /** Linear gain of the input into the delay line. */
    void setInputSend (float gain) { inputSend.setTargetValue (juce::jmax (0.0f, gain)); }

    void setPingPong (bool shouldPingPong) { pingPongAmount.setTargetValue (shouldPingPong ? 1.0f : 0.0f); }
    void setStereoWidthMs (float ms);

    void setWear (float amount01)          { wear.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }
    void setRunaway (bool shouldRunAway)   { runawayAmount.setTargetValue (shouldRunAway ? 1.0f : 0.0f); }
    void setRunawayDrive (float amount01)  { runawayDrive.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }

    void setTapestop (bool shouldStop)     { tapestopHeld = shouldStop; }
    void setReverse (bool shouldReverse)   { reverseHeld = shouldReverse; }
    void setDiffusion (float amount01)     { diffusion.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }
    void setTapestopTimeMs (float ms);

    void process (juce::AudioBuffer<float>& buffer);

private:
    float filter (int channel, float sample);
    float outputFilter (int channel, float sample);

    /** Advances the tapestop transport and the reverse heads by one sample. */
    void advanceTapestop (float delay);
    void advanceReverse (float delay);

    /** Blends the heard echo into the reverb; untouched while Diffusion is 0. */
    float reverberate (int channel, float echo);


    /** What the output read head hears. Call before the loop's popSample for this sample. */
    float readHeard (int channel, float delay);

    /** DC blocking, saturation and darkening, each scaled so 0 leaves the sample untouched. */
    float tape (int channel, float sample, float amount, float ceiling, float darken);

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> widthDelayLine;
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter, highCutFilter;
    juce::dsp::StateVariableTPTFilter<float> lowCutOutFilter, highCutOutFilter;
    juce::SmoothedValue<float> delaySamples, feedback, mix, freezeAmount, pingPongAmount, widthSamples, inputSend;
    juce::SmoothedValue<float> wear, runawayAmount, runawayDrive, diffusion, reverseAmount;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz { 20.0f }, highCutHz { 20000.0f };
    bool lowCutOn = false, highCutOn = false, filtersInLoop = true;
    int delaySmoothingSamples = -1, freezeFadeSamples = -1;
    double sampleRate = 44100.0;
    float maxDelaySamples = 1.0f, maxWidthSamples = 0.0f;

    // Tape state
    float dcState[2] {}, darkState[2] {};
    float dcCoef = 0.0f, darkCoefMin = 1.0f;
    float wowPhase = 0.0f, flutterPhase = 0.0f, wowIncrement = 0.0f, flutterIncrement = 0.0f;
    float drift = 0.0f, driftTarget = 0.0f, driftCoef = 0.0f;
    int driftCountdown = 0;
    juce::Random random;

    // Tapestop state: speed 1 is normal, offset is how far the output head trails the loop.
    bool tapestopHeld = false, tapestopWasHeld = false;
    float tapeSpeed = 1.0f, tapeOffset = 0.0f, stopStep = 0.0f, startStep = 0.0f;
    float spliceAmount = 0.0f, spliceOffset = 0.0f, spliceStep = 0.0f;
    float maxReadSamples = 1.0f;

    // Reverse: phase through the current segment, and how far in reverse is (0..1).
    bool reverseHeld = false;
    float reversePhase = 0.0f, reverseMix = 0.0f;

    std::array<DiffusionNetwork, 2> diffusers;
    float diffusionCos = 1.0f, diffusionSin = 0.0f, diffusionSetting = 0.0f, diffusionRt60 = 0.0f, diffusionMakeup = 1.0f;
    bool diffusionActive = false;
};
