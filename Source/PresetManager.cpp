#include "PresetManager.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    // Factory presets go here, e.g. { "Slapback", BinaryData::slapback_echopreset }.
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("ZBAudio")
               .getChildFile ("Echo Factory")
               .getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::getUserPresets() const
{
    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*" + fileExtension);

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

bool PresetManager::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresets.size()))
        return false;

    const auto& preset = factoryPresets[(size_t) index];

    if (auto xml = juce::parseXML (preset.xml))
        return applyState (juce::ValueTree::fromXml (*xml), preset.name);

    return false;
}

bool PresetManager::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        return applyState (juce::ValueTree::fromXml (*xml), file.getFileNameWithoutExtension());

    return false;
}

bool PresetManager::applyState (const juce::ValueTree& newState, const juce::String& name)
{
    if (! newState.hasType (apvts.state.getType()))
        return false;

    apvts.replaceState (newState.createCopy());
    setCurrentPresetName (name);
    return true;
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim();
}

juce::File PresetManager::getUserPresetFile (const juce::String& name) const
{
    return getUserPresetDirectory().getChildFile (sanitiseName (name) + fileExtension);
}

PresetManager::SaveResult PresetManager::saveUserPreset (const juce::String& name, bool overwrite)
{
    const auto cleanName = sanitiseName (name);

    if (cleanName.isEmpty())
        return SaveResult::invalidName;

    const auto file = getUserPresetFile (cleanName);

    if (file.existsAsFile() && ! overwrite)
        return SaveResult::alreadyExists;

    if (! getUserPresetDirectory().createDirectory())
        return SaveResult::writeFailed;

    auto state = apvts.copyState();
    state.setProperty (presetNameProperty, cleanName, nullptr);

    const auto xml = state.createXml();

    if (xml == nullptr || ! xml->writeTo (file))
        return SaveResult::writeFailed;

    setCurrentPresetName (cleanName);
    return SaveResult::ok;
}

juce::String PresetManager::getCurrentPresetName() const
{
    return apvts.state.getProperty (presetNameProperty, "Default").toString();
}

void PresetManager::setCurrentPresetName (const juce::String& name)
{
    apvts.state.setProperty (presetNameProperty, name, nullptr);
}
