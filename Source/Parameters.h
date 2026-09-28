#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    namespace ID
    {
        inline const juce::ParameterID delayTimeMs  { "delayTimeMs",  1 };
        inline const juce::ParameterID sync         { "sync",         1 };
        inline const juce::ParameterID syncDivision { "syncDivision", 1 };
        inline const juce::ParameterID feedback     { "feedback",     1 };
        inline const juce::ParameterID mix          { "mix",          1 };
        inline const juce::ParameterID lowCut       { "lowCut",       1 };
        inline const juce::ParameterID highCut      { "highCut",      1 };
        inline const juce::ParameterID filterPos    { "filterPos",    1 };
        inline const juce::ParameterID freeze       { "freeze",       1 };
    }

    constexpr float minDelayMs = 1.0f;
    constexpr float maxDelayMs = 5000.0f;

    // At these extremes the filter is bypassed and reads as "Off".
    constexpr float lowCutMinHz  = 20.0f;
    constexpr float lowCutMaxHz  = 2000.0f;
    constexpr float highCutMinHz = 500.0f;
    constexpr float highCutMaxHz = 20000.0f;

    enum class FilterPosition { inFeedbackLoop = 0, outputOnly = 1 };
    const juce::StringArray& getFilterPositionNames();

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

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
