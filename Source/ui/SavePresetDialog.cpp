#include "SavePresetDialog.h"

SavePresetDialog::SavePresetDialog (PresetManager& manager, std::function<void (bool)> closeCallback)
    : presetManager (manager), onClose (std::move (closeCallback))
{
    setTitle ("Save User Preset");
    setDescription ("Enter a name, then press Return to save or Escape to cancel.");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setWantsKeyboardFocus (false);

    heading.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    heading.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (heading);

    nameLabel.attachToComponent (&nameEditor, false);
    addAndMakeVisible (nameLabel);

    nameEditor.setTitle ("Preset name");
    nameEditor.setDescription ("Name for the new user preset");
    nameEditor.setText (presetManager.getCurrentPresetName() == "Default" ? juce::String() : presetManager.getCurrentPresetName(), false);
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setExplicitFocusOrder (1);
    nameEditor.onTextChange = [this]
    {
        clearOverwriteConfirmation();
        saveButton.setEnabled (PresetManager::sanitiseName (nameEditor.getText()).isNotEmpty());
    };
    nameEditor.onReturnKey = [this] { save(); };
    nameEditor.onEscapeKey = [this] { close (false); };
    addAndMakeVisible (nameEditor);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffd166));
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);
    addChildComponent (statusLabel);

    saveButton.setExplicitFocusOrder (2);
    saveButton.setEnabled (PresetManager::sanitiseName (nameEditor.getText()).isNotEmpty());
    saveButton.onClick = [this] { save(); };
    addAndMakeVisible (saveButton);

    cancelButton.setExplicitFocusOrder (3);
    cancelButton.onClick = [this] { close (false); };
    addAndMakeVisible (cancelButton);
}

std::unique_ptr<juce::AccessibilityHandler> SavePresetDialog::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

void SavePresetDialog::showIn (juce::Component& parent)
{
    parent.addAndMakeVisible (this);
    setBounds (parent.getLocalBounds());
    enterModalState (false);

    juce::Component::SafePointer<juce::TextEditor> editor (&nameEditor);
    juce::MessageManager::callAsync ([editor]
    {
        if (editor != nullptr)
            editor->grabKeyboardFocus();
    });
}

void SavePresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.75f));

    const auto panel = getLocalBounds().withSizeKeepingCentre (380, 220).toFloat();
    g.setColour (juce::Colour (0xff23272e));
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (juce::Colour (0xff5b6472));
    g.drawRoundedRectangle (panel, 8.0f, 1.5f);
}

void SavePresetDialog::resized()
{
    auto panel = getLocalBounds().withSizeKeepingCentre (380, 220).reduced (20, 16);

    heading.setBounds (panel.removeFromTop (30));
    panel.removeFromTop (26); // room for the attached "Preset name" label
    nameEditor.setBounds (panel.removeFromTop (30));
    panel.removeFromTop (6);

    auto buttons = panel.removeFromBottom (32);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (10);
    saveButton.setBounds (buttons.removeFromRight (100));

    statusLabel.setBounds (panel);
}

bool SavePresetDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close (false);
        return true;
    }

    return false;
}

void SavePresetDialog::setStatus (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setVisible (message.isNotEmpty());

    if (message.isNotEmpty())
        juce::AccessibilityHandler::postAnnouncement (message, juce::AccessibilityHandler::AnnouncementPriority::high);
}

void SavePresetDialog::clearOverwriteConfirmation()
{
    if (pendingOverwriteName.isEmpty())
        return;

    pendingOverwriteName.clear();
    saveButton.setButtonText ("Save");
    saveButton.setTitle ("Save");
    setStatus ({});
}

void SavePresetDialog::save()
{
    const auto name = PresetManager::sanitiseName (nameEditor.getText());
    const auto overwrite = pendingOverwriteName.isNotEmpty() && pendingOverwriteName == name;

    switch (presetManager.saveUserPreset (name, overwrite))
    {
        case PresetManager::SaveResult::ok:
            juce::AccessibilityHandler::postAnnouncement ("Saved preset " + name,
                                                          juce::AccessibilityHandler::AnnouncementPriority::high);
            close (true);
            return;

        case PresetManager::SaveResult::invalidName:
            setStatus ("Please enter a preset name.");
            nameEditor.grabKeyboardFocus();
            return;

        case PresetManager::SaveResult::alreadyExists:
            pendingOverwriteName = name;
            saveButton.setButtonText ("Replace");
            saveButton.setTitle ("Replace");
            setStatus ("A preset named \"" + name + "\" already exists. Press Replace to overwrite it, or change the name.");
            saveButton.grabKeyboardFocus();
            return;

        case PresetManager::SaveResult::writeFailed:
            setStatus ("Could not write the preset file to " + PresetManager::getUserPresetDirectory().getFullPathName());
            return;
    }
}

void SavePresetDialog::close (bool saved)
{
    exitModalState (saved ? 1 : 0);
    setVisible (false);

    // The callback usually deletes this dialog, and we may be inside a TextEditor
    // or Button callback right now, so run it after this call stack unwinds.
    if (auto callback = std::move (onClose))
        juce::MessageManager::callAsync ([callback, saved] { callback (saved); });
}
