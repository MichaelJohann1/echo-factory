#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    namespace ID
    {
        inline const juce::ParameterID delayTimeMs  { "delayTimeMs",  1 };
        inline const juce::ParameterID timeSmoothingMs { "timeSmoothingMs", 1 };
        inline const juce::ParameterID sync         { "sync",         1 };
        inline const juce::ParameterID syncDivision { "syncDivision", 1 };
        inline const juce::ParameterID feedback     { "feedback",     1 };
        inline const juce::ParameterID mix          { "mix",          1 };
        inline const juce::ParameterID lowCut       { "lowCut",       1 };
        inline const juce::ParameterID highCut      { "highCut",      1 };
        inline const juce::ParameterID filterPos    { "filterPos",    1 };
        inline const juce::ParameterID freeze       { "freeze",       1 };
        inline const juce::ParameterID pingPong     { "pingPong",     1 };
        inline const juce::ParameterID stereoWidthMs { "stereoWidthMs", 1 };

        // Performance gestures (see docs/perform-map.md).
        inline const juce::ParameterID throwGesture { "throw",        1 };
        inline const juce::ParameterID tapestop     { "tapestop",     1 };
        inline const juce::ParameterID runaway      { "runaway",      1 };
        inline const juce::ParameterID reverse      { "reverse",      1 };
        inline const juce::ParameterID reset        { "reset",        1 };
        inline const juce::ParameterID inputMode    { "inputMode",    1 };
        inline const juce::ParameterID throwLevelDb { "throwLevelDb", 1 };
        inline const juce::ParameterID freezeFadeMs { "freezeFadeMs", 1 };
    }

    /** The performance gestures, in MIDI note order (Throw, Freeze, Tapestop, Runaway, Reverse). */
    const std::array<const juce::ParameterID*, 5>& getGestureIDs();

    constexpr float minDelayMs = 1.0f;
    constexpr float maxDelayMs = 5000.0f;

    // How long the delay time takes to glide to a new value.
    constexpr float maxTimeSmoothingMs     = 2000.0f;
    constexpr float defaultTimeSmoothingMs = 50.0f;

    // Extra delay on the right channel of the echoes.
    constexpr float maxStereoWidthMs = 100.0f;

    // Send level into the delay while Throw is on.
    constexpr float minThrowLevelDb     = -12.0f;
    constexpr float maxThrowLevelDb     = 12.0f;
    constexpr float defaultThrowLevelDb = 6.0f;

    // How long Freeze takes to fade in and out.
    constexpr float minFreezeFadeMs     = 10.0f;
    constexpr float maxFreezeFadeMs     = 2000.0f;
    constexpr float defaultFreezeFadeMs = 20.0f;

    // At these extremes the filter is bypassed and reads as "Off".
    constexpr float lowCutMinHz  = 20.0f;
    constexpr float lowCutMaxHz  = 2000.0f;
    constexpr float highCutMinHz = 500.0f;
    constexpr float highCutMaxHz = 20000.0f;

    enum class FilterPosition { inFeedbackLoop = 0, outputOnly = 1 };
    const juce::StringArray& getFilterPositionNames();

    enum class InputMode { always = 0, throwOnly = 1 };
    const juce::StringArray& getInputModeNames();

    struct SyncDivision
    {
        const char* shortName;   // shown in the UI, e.g. "1/8D"
        const char* spokenName;  // read by screen readers, e.g. "1/8 dotted"
        double beats;            // length in quarter notes
    };

    const std::vector<SyncDivision>& getSyncDivisions();
    int getDefaultSyncDivisionIndex();

    juce::String formatMilliseconds (float ms, bool spoken);

    /** "Off" when offHz is reached, otherwise e.g. "250 Hz" / "2.5 kilohertz". */
    juce::String formatFrequency (float hz, float offHz, bool spoken);

    /** e.g. "+6.0 dB" / "plus 6 decibels". */
    juce::String formatDecibels (float db, bool spoken);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
