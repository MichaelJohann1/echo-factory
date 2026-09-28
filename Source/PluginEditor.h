#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/AccessibleSlider.h"
#include "ui/PresetBar.h"
#include "ui/SavePresetDialog.h"

class EchoFactoryEditor : public juce::AudioProcessorEditor,
                          private juce::ValueTree::Listener,
                          private juce::AsyncUpdater,
                          private juce::FocusChangeListener
{
public:
    explicit EchoFactoryEditor (EchoFactoryProcessor&);
    ~EchoFactoryEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void setupSlider (AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description);
    void setupPercentSlider (AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description);
    void setupFrequencySlider (AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description, float offHz);
    void updateTimeControlForSync (bool synced);
    void showSaveDialog();
    void setMainControlsVisible (bool);

    // ValueTree::Listener — watches the preset name, including state restored by the host.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;

    // FocusChangeListener — repaints the visible keyboard focus outline.
    void globalFocusChanged (juce::Component*) override;

    EchoFactoryProcessor& processorRef;

    PresetBar presetBar;

    AccessibleSlider timeSlider, feedbackSlider, mixSlider;
    juce::Label timeLabel, feedbackLabel, mixLabel;
    juce::ToggleButton syncButton { "Sync to host tempo" };

    AccessibleSlider lowCutSlider, highCutSlider;
    juce::Label lowCutLabel, highCutLabel, filterPosLabel;
    juce::ComboBox filterPosBox;

    std::unique_ptr<SliderAttachment> timeAttachment, feedbackAttachment, mixAttachment;
    std::unique_ptr<SliderAttachment> lowCutAttachment, highCutAttachment;
    std::unique_ptr<ButtonAttachment> syncButtonAttachment;
    std::unique_ptr<ComboBoxAttachment> filterPosAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncWatcher;
    bool showingSyncDivisions = false;

    std::unique_ptr<SavePresetDialog> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryEditor)
};
