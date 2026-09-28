#include "Parameters.h"

namespace Params
{
const std::vector<SyncDivision>& getSyncDivisions()
{
    static const std::vector<SyncDivision> divisions {
        { "1/64",   "1/64 note",           0.0625 },
        { "1/32T",  "1/32 triplet",        0.125 * 2.0 / 3.0 },
        { "1/32",   "1/32 note",           0.125 },
        { "1/16T",  "1/16 triplet",        0.25 * 2.0 / 3.0 },
        { "1/16",   "1/16 note",           0.25 },
        { "1/16D",  "1/16 dotted",         0.375 },
        { "1/8T",   "1/8 triplet",         0.5 * 2.0 / 3.0 },
        { "1/8",    "1/8 note",            0.5 },
        { "1/8D",   "1/8 dotted",          0.75 },
        { "1/4T",   "1/4 triplet",         2.0 / 3.0 },
        { "1/4",    "1/4 note",            1.0 },
        { "1/4D",   "1/4 dotted",          1.5 },
        { "1/2T",   "1/2 triplet",         4.0 / 3.0 },
        { "1/2",    "1/2 note",            2.0 },
        { "1/2D",   "1/2 dotted",          3.0 },
        { "1 bar",  "1 bar",               4.0 },
        { "2 bars", "2 bars",              8.0 },
    };
    return divisions;
}

int getDefaultSyncDivisionIndex() { return 7; } // 1/8 note

juce::String formatMilliseconds (float ms, bool spoken)
{
    if (ms >= 1000.0f)
    {
        const auto seconds = juce::String (ms / 1000.0f, 2);
        return spoken ? seconds + " seconds" : seconds + " s";
    }

    const auto value = juce::String (juce::roundToInt (ms));
    return spoken ? value + (value == "1" ? " millisecond" : " milliseconds") : value + " ms";
}

const juce::StringArray& getFilterPositionNames()
{
    static const juce::StringArray names { "In Feedback Loop", "Output Only" };
    return names;
}

juce::String formatFrequency (float hz, float offHz, bool spoken)
{
    if (juce::approximatelyEqual (hz, offHz))
        return "Off";

    if (hz >= 1000.0f)
    {
        const auto khz = juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2);
        return khz + (spoken ? " kilohertz" : " kHz");
    }

    return juce::String (juce::roundToInt (hz)) + (spoken ? " hertz" : " Hz");
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    NormalisableRange<float> timeRange { minDelayMs, maxDelayMs, 1.0f };
    timeRange.setSkewForCentre (500.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::delayTimeMs, "Delay Time", timeRange, 400.0f,
        AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return formatMilliseconds (v, false); })
            .withValueFromStringFunction ([] (const String& text)
            {
                const auto t = text.trim().toLowerCase();
                const auto number = t.getFloatValue();
                const bool isSeconds = (t.endsWith ("s") && ! t.endsWith ("ms")) || t.contains ("sec");
                return isSeconds ? number * 1000.0f : number;
            })));

    layout.add (std::make_unique<AudioParameterBool> (
        ID::sync, "Tempo Sync", false,
        AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool v, int) { return v ? String ("On") : String ("Off"); })));

    StringArray divisionNames;
    for (const auto& d : getSyncDivisions())
        divisionNames.add (d.spokenName);

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::syncDivision, "Sync Division", divisionNames, getDefaultSyncDivisionIndex()));

    auto percentAttributes = []
    {
        return AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
            .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); });
    };

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::feedback, "Feedback", NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes()));

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::mix, "Mix", NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes()));

    auto frequencyAttributes = [] (float offHz)
    {
        return AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([offHz] (float v, int) { return formatFrequency (v, offHz, false); })
            .withValueFromStringFunction ([offHz] (const String& text)
            {
                const auto t = text.trim().toLowerCase();

                if (t == "off")
                    return offHz;

                const auto number = t.getFloatValue();
                return t.contains ("k") ? number * 1000.0f : number;
            });
    };

    NormalisableRange<float> lowCutRange { lowCutMinHz, lowCutMaxHz, 1.0f };
    lowCutRange.setSkewForCentre (200.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::lowCut, "Low Cut", lowCutRange, lowCutMinHz, frequencyAttributes (lowCutMinHz)));

    NormalisableRange<float> highCutRange { highCutMinHz, highCutMaxHz, 1.0f };
    highCutRange.setSkewForCentre (3000.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::highCut, "High Cut", highCutRange, highCutMaxHz, frequencyAttributes (highCutMaxHz)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::filterPos, "Filter Position", getFilterPositionNames(), (int) FilterPosition::inFeedbackLoop));

    return layout;
}
}
