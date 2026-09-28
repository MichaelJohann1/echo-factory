#include "PluginEditor.h"
#include "Parameters.h"

namespace Colours
{
    const juce::Colour background { 0xff1a1d22 };
    const juce::Colour topBar     { 0xff23272e };
    const juce::Colour text       { 0xfff2f4f7 };
    const juce::Colour accent     { 0xff4fc3f7 };
    const juce::Colour focus      { 0xffffd166 };
}

EchoFactoryEditor::EchoFactoryEditor (EchoFactoryProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), presetBar (p.presetManager)
{
    setTitle ("Echo Factory");
    setDescription ("Echo Factory delay by ZBAudio");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    auto& lf = getLookAndFeel();
    lf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lf.setColour (juce::Slider::thumbColourId, Colours::accent);
    lf.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    lf.setColour (juce::Label::textColourId, Colours::text);
    lf.setColour (juce::ToggleButton::textColourId, Colours::text);
    lf.setColour (juce::ToggleButton::tickColourId, Colours::accent);

    // ---- Top bar ---------------------------------------------------------
    presetBar.setExplicitFocusOrder (1);
    presetBar.onSaveRequested = [this] { showSaveDialog(); };
    addAndMakeVisible (presetBar);

    // ---- Delay time / division ------------------------------------------
    timeLabel.attachToComponent (&timeSlider, false);
    timeLabel.setJustificationType (juce::Justification::centred);
    timeSlider.setExplicitFocusOrder (2);
    addAndMakeVisible (timeSlider);

    // ---- Sync ------------------------------------------------------------
    syncButton.setTitle ("Sync to host tempo");
    syncButton.setDescription ("When on, the delay time follows the host tempo as a note division.");
    syncButton.setWantsKeyboardFocus (true);
    syncButton.setExplicitFocusOrder (3);
    addAndMakeVisible (syncButton);
    syncButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::sync.getParamID(), syncButton);

    // Follows the sync parameter whether it's changed here, by automation or by a preset.
    syncWatcher = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter (Params::ID::sync.getParamID()),
        [this] (float value) { updateTimeControlForSync (value >= 0.5f); });
    syncWatcher->sendInitialUpdate();

    // ---- Feedback / Mix --------------------------------------------------
    setupPercentSlider (feedbackSlider, feedbackLabel, "Feedback", "Amount of the echo fed back into the delay, 0 to 95 percent.");
    feedbackSlider.setExplicitFocusOrder (4);
    feedbackAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::feedback.getParamID(), feedbackSlider);

    setupPercentSlider (mixSlider, mixLabel, "Mix", "Balance between the dry and delayed signal, 0 to 100 percent.");
    mixSlider.setExplicitFocusOrder (5);
    mixAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::mix.getParamID(), mixSlider);

    p.apvts.state.addListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    setWantsKeyboardFocus (false);
    setResizable (false, false);
    setSize (520, 300);
}

EchoFactoryEditor::~EchoFactoryEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    processorRef.apvts.state.removeListener (this);
    cancelPendingUpdate();

    if (saveDialog != nullptr)
        saveDialog->exitModalState (0);
}

void EchoFactoryEditor::setupPercentSlider (AccessibleSlider& slider, juce::Label& label,
                                           const juce::String& name, const juce::String& description)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);

    slider.setTitle (name);
    slider.setDescription (description);
    slider.setNumKeyboardSteps (100);
    slider.spokenTextFromValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " percent"; };
    addAndMakeVisible (slider);
}

void EchoFactoryEditor::updateTimeControlForSync (bool synced)
{
    if (timeAttachment != nullptr && synced == showingSyncDivisions)
        return;

    showingSyncDivisions = synced;
    timeAttachment.reset();

    if (synced)
    {
        const auto& divisions = Params::getSyncDivisions();

        timeAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, Params::ID::syncDivision.getParamID(), timeSlider);

        // Compact names on screen, full names for screen readers.
        timeSlider.textFromValueFunction = [&divisions] (double v)
        {
            return juce::String (divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, juce::roundToInt (v))].shortName);
        };
        timeSlider.spokenTextFromValue = [&divisions] (double v)
        {
            return juce::String (divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, juce::roundToInt (v))].spokenName);
        };
        timeSlider.setNumKeyboardSteps ((int) divisions.size() - 1);

        timeLabel.setText ("Delay Division", juce::dontSendNotification);
        timeSlider.setTitle ("Delay Division");
        timeSlider.setDescription ("Delay length as a note division of the host tempo, from 1/64 note to 2 bars.");
    }
    else
    {
        timeAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, Params::ID::delayTimeMs.getParamID(), timeSlider);

        timeSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
        timeSlider.setNumKeyboardSteps (100);

        timeLabel.setText ("Delay Time", juce::dontSendNotification);
        timeSlider.setTitle ("Delay Time");
        timeSlider.setDescription ("Delay time from 1 millisecond to 5 seconds.");
    }

    timeSlider.updateText();

    if (auto* handler = timeSlider.getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void EchoFactoryEditor::showSaveDialog()
{
    if (saveDialog != nullptr)
        return;

    juce::Component::SafePointer<EchoFactoryEditor> safeThis (this);

    saveDialog = std::make_unique<SavePresetDialog> (processorRef.presetManager, [safeThis] (bool)
    {
        if (safeThis == nullptr)
            return;

        safeThis->saveDialog.reset();
        safeThis->setMainControlsVisible (true);
        safeThis->presetBar.getMenuButton().grabKeyboardFocus();
    });

    // Hide the controls behind the dialog so screen readers can't wander into them.
    setMainControlsVisible (false);
    saveDialog->showIn (*this);
}

void EchoFactoryEditor::setMainControlsVisible (bool shouldBeVisible)
{
    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &timeSlider, &syncButton, &feedbackSlider, &mixSlider,
                                                            &timeLabel, &feedbackLabel, &mixLabel })
        c->setVisible (shouldBeVisible);
}

void EchoFactoryEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == PresetManager::presetNameProperty)
        triggerAsyncUpdate();
}

void EchoFactoryEditor::valueTreeRedirected (juce::ValueTree&)
{
    triggerAsyncUpdate();
}

void EchoFactoryEditor::handleAsyncUpdate()
{
    presetBar.updatePresetName();
}

void EchoFactoryEditor::globalFocusChanged (juce::Component*)
{
    repaint();
}

void EchoFactoryEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    g.setColour (Colours::topBar);
    g.fillRect (getLocalBounds().removeFromTop (48));

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("ECHO FACTORY", getLocalBounds().removeFromTop (48).reduced (16, 0), juce::Justification::centredLeft);
}

void EchoFactoryEditor::paintOverChildren (juce::Graphics& g)
{
    // Visible focus indicator for sighted keyboard users.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused == nullptr || focused == this || ! isParentOf (focused) || ! focused->isShowing())
        return;

    if (saveDialog != nullptr && saveDialog->isParentOf (focused))
        return; // the dialog's own widgets show their focus

    const auto area = getLocalArea (focused, focused->getLocalBounds()).toFloat().expanded (3.0f);
    g.setColour (Colours::focus);
    g.drawRoundedRectangle (area, 6.0f, 2.0f);
}

void EchoFactoryEditor::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (48).reduced (16, 6);
    top.removeFromLeft (170); // title text
    presetBar.setBounds (top);

    area.reduce (16, 16);
    area.removeFromTop (24); // room for the labels attached above the sliders

    const auto columnWidth = area.getWidth() / 4;

    auto timeColumn = area.removeFromLeft (columnWidth);
    timeSlider.setBounds (timeColumn.reduced (6, 0));

    auto syncColumn = area.removeFromLeft (columnWidth);
    syncButton.setBounds (syncColumn.withSizeKeepingCentre (columnWidth - 12, 32));

    feedbackSlider.setBounds (area.removeFromLeft (columnWidth).reduced (6, 0));
    mixSlider.setBounds (area.reduced (6, 0));
}
