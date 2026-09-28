#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PresetManager.h"

/**
    In-editor modal overlay for naming and saving a user preset.

    An overlay (rather than a separate DialogWindow) behaves reliably inside
    plug-in hosts and keeps screen-reader focus within the plug-in window.
    Return saves, Escape cancels. If the name is taken, the dialog asks for
    confirmation in place and the Save button becomes "Replace".
*/
class SavePresetDialog : public juce::Component
{
public:
    SavePresetDialog (PresetManager& manager, std::function<void (bool saved)> onClose);

    /** Adds the dialog to the parent, makes it modal and moves focus to the name field. */
    void showIn (juce::Component& parent);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void save();
    void close (bool saved);
    void setStatus (const juce::String& message);
    void clearOverwriteConfirmation();

    PresetManager& presetManager;
    std::function<void (bool)> onClose;

    juce::Label heading { {}, "Save User Preset" };
    juce::Label nameLabel { {}, "Preset name" };
    juce::TextEditor nameEditor;
    juce::Label statusLabel;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };

    juce::String pendingOverwriteName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavePresetDialog)
};
