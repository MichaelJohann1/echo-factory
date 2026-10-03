#include "PresetBar.h"

namespace
{
    enum MenuIds
    {
        saveId = 1,
        showFolderId,
        factoryBaseId = 1000,
        userBaseId = 100000
    };

    void announceLoad (bool succeeded, const juce::String& name, const juce::String& hint = {})
    {
        auto text = (succeeded ? "Loaded preset " : "Could not load preset ") + name;

        if (succeeded && hint.isNotEmpty())
            text << ". " << hint;

        juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::high);
    }
}

PresetBar::PresetBar (PresetManager& manager)
    : presetManager (manager)
{
    menuButton.setDescription ("Opens the preset menu: factory presets, user presets and save options.");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);

    updatePresetName();
}

void PresetBar::updatePresetName()
{
    const auto name = presetManager.getCurrentPresetName();
    menuButton.setButtonText ("Preset: " + name);
    menuButton.setTitle ("Preset menu, current preset " + name);
}

void PresetBar::resized()
{
    menuButton.setBounds (getLocalBounds().reduced (0, 4));
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader ("Factory Presets");

    const auto& factory = presetManager.getFactoryPresets();

    if (factory.empty())
        menu.addItem (factoryBaseId, "(No factory presets)", false);

    for (size_t i = 0; i < factory.size(); ++i)
        menu.addItem (factoryBaseId + (int) i, factory[i].name);

    menu.addSeparator();

    const auto userPresets = presetManager.getUserPresets();
    const auto currentName = presetManager.getCurrentPresetName();

    juce::PopupMenu userMenu;

    if (userPresets.isEmpty())
        userMenu.addItem (userBaseId, "(No user presets saved yet)", false);

    for (int i = 0; i < userPresets.size(); ++i)
    {
        const auto name = userPresets[i].getFileNameWithoutExtension();
        userMenu.addItem (userBaseId + i, name, true, name == currentName);
    }

    menu.addSubMenu ("User Presets", userMenu);
    menu.addSeparator();
    menu.addItem (saveId, "Save Current Settings as User Preset...");
    menu.addItem (showFolderId, "Show User Presets Folder");

    juce::Component::SafePointer<PresetBar> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton),
                        [safeThis, userPresets] (int result)
    {
        if (safeThis == nullptr)
            return;

        auto& self = *safeThis;
        self.menuButton.grabKeyboardFocus();

        if (result == saveId)
        {
            if (self.onSaveRequested)
                self.onSaveRequested();
        }
        else if (result == showFolderId)
        {
            const auto dir = PresetManager::getUserPresetDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }
        else if (result >= userBaseId)
        {
            const auto index = result - userBaseId;

            if (juce::isPositiveAndBelow (index, userPresets.size()))
                announceLoad (self.presetManager.loadUserPreset (userPresets[index]),
                              userPresets[index].getFileNameWithoutExtension());
        }
        else if (result >= factoryBaseId)
        {
            const auto index = result - factoryBaseId;
            const auto& presets = self.presetManager.getFactoryPresets();

            if (juce::isPositiveAndBelow (index, (int) presets.size()))
                announceLoad (self.presetManager.loadFactoryPreset (index), presets[(size_t) index].name,
                              presets[(size_t) index].hint);
        }
    });
}
