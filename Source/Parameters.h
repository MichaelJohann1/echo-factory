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
    }

    constexpr float minDelayMs = 1.0f;
    constexpr float maxDelayMs = 5000.0f;

    struct SyncDivision
    {
        const char* shortName;   // shown in the UI, e.g. "1/8D"
        const char* spokenName;  // read by screen readers, e.g. "1/8 dotted"
        double beats;            // length in quarter notes
    };

    const std::vector<SyncDivision>& getSyncDivisions();
    int getDefaultSyncDivisionIndex();

    juce::String formatMilliseconds (float ms, bool spoken);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
