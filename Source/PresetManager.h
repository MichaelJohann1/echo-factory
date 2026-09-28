#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Factory presets live in memory (empty for now; later from BinaryData).
    User presets are XML files in ~/Documents/ZBAudio/Echo Factory/Presets.
*/
class PresetManager
{
public:
    struct FactoryPreset
    {
        juce::String name;
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
    bool applyState (const juce::ValueTree& newState, const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<FactoryPreset> factoryPresets;
};
