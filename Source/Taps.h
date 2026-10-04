#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayEngine.h"

/**
    The multi-tap taps, up to maxTaps. They come and go, so they aren't host
    parameters: they live in the state tree as a TAPS child of the APVTS state, with one TAP
    child per tap, in order. They're saved with the session and in presets.

    This is the single source of truth for the tap identifiers, ranges,
    defaults and value text, as Params is for the parameters.
*/
namespace Taps
{
    namespace ID
    {
        inline const juce::Identifier taps     { "TAPS" };
        inline const juce::Identifier tap      { "TAP" };
        inline const juce::Identifier uid      { "uid" };
        inline const juce::Identifier timeMs   { "timeMs" };
        inline const juce::Identifier division { "division" };
        inline const juce::Identifier levelDb  { "levelDb" };
        inline const juce::Identifier pan      { "pan" };
        inline const juce::Identifier pitch    { "pitch" };
        inline const juce::Identifier reverse  { "reverse" };
    }

    constexpr int maxTaps = DelayEngine::maxTaps;

    // Level's minimum is "Off".
    constexpr float minLevelDb = -60.0f, maxLevelDb = 6.0f, defaultLevelDb = 0.0f;
    constexpr float maxPan = 100.0f;              // -100 is hard left, 100 hard right
    constexpr float maxPitchSemitones = 12.0f;
    constexpr float defaultTimeMs = 250.0f;
    constexpr int   defaultDivision = 4;          // 1/16 note

    juce::NormalisableRange<float> getLevelRange();
    juce::NormalisableRange<float> getPanRange();
    juce::NormalisableRange<float> getPitchRange();

    /** "Off" at the minimum, otherwise e.g. "-6.0 dB" / "minus 6 decibels". */
    juce::String formatLevel (float db, bool spoken);

    /** e.g. "C" / "centre", "30 L" / "30 percent left". */
    juce::String formatPan (float pan, bool spoken);

    /** e.g. "+7 st" / "plus 7 semitones". */
    juce::String formatSemitones (float semitones, bool spoken);

    /** A new tap with a fresh uid. */
    juce::ValueTree makeTap (float timeMs = defaultTimeMs, int division = defaultDivision, float levelDb = defaultLevelDb,
                             float pan = 0.0f, int pitch = 0, bool reverse = false);

    /** The tap Add Tap creates: like the last one, continuing the spacing of the last two. */
    juce::ValueTree makeNextTap (const juce::ValueTree& taps);

    /** Adds a TAPS child with one default tap if the state has none (new instances, older
        sessions and presets), and trims one with more than maxTaps. */
    void ensureTree (juce::ValueTree& state);

    /** The taps as the engine wants them; only the first maxTaps. */
    std::vector<DelayEngine::TapSettings> toEngineSettings (const juce::ValueTree& taps);
}
