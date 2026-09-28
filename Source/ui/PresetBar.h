#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PresetManager.h"

/**
    Top bar: a single "Preset: <name>" button that opens the preset menu.

    Menu layout:
      Factory Presets (header) + items, or "(No factory presets)"
      User Presets  >  submenu of files in the user preset folder
      ---
      Save Current Settings as User Preset...
      Show User Presets Folder
*/
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager& manager);

    /** Called when the user chooses "Save Current Settings as User Preset...". */
    std::function<void()> onSaveRequested;

    /** Refreshes the button text from the preset manager. */
    void updatePresetName();

    juce::Button& getMenuButton() { return menuButton; }

    void resized() override;

private:
    void showMenu();

    PresetManager& presetManager;
    juce::TextButton menuButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
