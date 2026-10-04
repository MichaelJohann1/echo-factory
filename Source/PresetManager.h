#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Factory presets are built in code (PresetManager.cpp), each with a spoken
    hint about how to play it. User presets are XML files in
    ~/Documents/ZBAudio/Echo Factory/Presets.
*/
class PresetManager
{
public:
    struct FactoryPreset
    {
        juce::String name;
        juce::String hint; // read out after "Loaded preset <name>"
        juce::String xml;
    };

    enum class SaveResult { ok, invalidName, alreadyExists, writeFailed };

    static inline const juce::String fileExtension { ".echopreset" };
    static inline const juce::Identifier presetNameProperty { "presetName" };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File getUserPresetDirectory();

    const std::vector<FactoryPreset>& getFactoryPresets() const { return factoryPresets; }
    juce::Array<juce::File> getUserPresets() const;

    bool loadFactoryPreset (int index);
    bool loadUserPreset (const juce::File& file);

    SaveResult saveUserPreset (const juce::String& name, bool overwrite);
    juce::File getUserPresetFile (const juce::String& name) const;
    static juce::String sanitiseName (const juce::String& name);

    juce::String getCurrentPresetName() const;
    void setCurrentPresetName (const juce::String& name);

private:
    /** A preset from parameter values in real units; anything left out is the default.
        Taps (Taps::makeTap) are optional; without them the preset has one default tap. */
    void addFactoryPreset (const juce::String& name, const juce::String& hint,
                           std::initializer_list<std::pair<const juce::ParameterID*, float>> values,
                           std::initializer_list<juce::ValueTree> taps = {});

    bool applyState (const juce::ValueTree& newState, const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<FactoryPreset> factoryPresets;
};
