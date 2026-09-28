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
    : AudioProcessorEditor (&p), processorRef (p), presetBar (p.presetManager), performPad (p.apvts)
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

    // ---- Perform pad -----------------------------------------------------
    // First after the preset bar, so it's one Tab from the top.
    performPad.setTitle ("Perform mode");
    performPad.setDescription ("Hold F to throw, D to freeze, S for tapestop, A for runaway, G for reverse. "
                               "Shift plus a key latches it; press the key again to release. "
                               "Up and down arrows change feedback, left and right arrows change delay time. "
                               "Backspace resets. Escape returns to the controls.");
    performPad.setExplicitFocusOrder (2);
    performPad.onReset = [this] { processorRef.resetPerformance(); };
    performPad.onNudgeFeedback = [this] (float delta) { processorRef.nudgeFeedback (delta); };
    performPad.onNudgeTime     = [this] (int direction) { processorRef.nudgeDelayTime (direction); };
    performPad.isHeldExternally = [this] (const juce::String& id) { return processorRef.isGestureHeldByMidi (id); };
    performPad.onExit  = [this]
    {
        juce::AccessibilityHandler::postAnnouncement ("Edit mode", juce::AccessibilityHandler::AnnouncementPriority::high);
        timeSlider.grabKeyboardFocus();
    };
    addAndMakeVisible (performPad);

    // ---- Delay time / division ------------------------------------------
    timeLabel.attachToComponent (&timeSlider, false);
    timeLabel.setJustificationType (juce::Justification::centred);
    timeSlider.setExplicitFocusOrder (3);
    addAndMakeVisible (timeSlider);

    // ---- Time smoothing --------------------------------------------------
    setupSlider (smoothingSlider, smoothingLabel, "Smoothing",
                 "How long the delay time takes to glide to a new setting, from 0 milliseconds (instant) to 2 seconds. "
                 "Longer settings give a smoother, tape-like pitch bend.");
    smoothingSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    smoothingSlider.setExplicitFocusOrder (4);
    smoothingAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::timeSmoothingMs.getParamID(), smoothingSlider);

    // ---- Sync ------------------------------------------------------------
    syncButton.setTitle ("Sync to host tempo");
    syncButton.setDescription ("When on, the delay time follows the host tempo as a note division.");
    syncButton.setWantsKeyboardFocus (true);
    syncButton.setExplicitFocusOrder (5);
    addAndMakeVisible (syncButton);
    syncButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::sync.getParamID(), syncButton);

    // ---- Ping-pong -------------------------------------------------------
    pingPongButton.setTitle ("Ping-Pong");
    pingPongButton.setDescription ("When on, echoes bounce between left and right, starting on the left. "
                                   "With feedback at zero you hear two repeats, one left and one right. Stereo only.");
    pingPongButton.setWantsKeyboardFocus (true);
    pingPongButton.setExplicitFocusOrder (6);
    addAndMakeVisible (pingPongButton);
    pingPongButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::pingPong.getParamID(), pingPongButton);

    // ---- Freeze ----------------------------------------------------------
    freezeButton.setTitle ("Freeze");
    freezeButton.setDescription ("When on, the delay stops taking in new audio and repeats what is in the buffer "
                                 "forever without fading. The dry signal still passes through.");
    freezeButton.setWantsKeyboardFocus (true);
    freezeButton.setExplicitFocusOrder (7);
    addAndMakeVisible (freezeButton);
    freezeButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::freeze.getParamID(), freezeButton);

    // Follows the sync parameter whether it's changed here, by automation or by a preset.
    syncWatcher = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter (Params::ID::sync.getParamID()),
        [this] (float value) { updateTimeControlForSync (value >= 0.5f); });
    syncWatcher->sendInitialUpdate();

    // ---- Feedback / Mix --------------------------------------------------
    setupPercentSlider (feedbackSlider, feedbackLabel, "Feedback", "Amount of the echo fed back into the delay, 0 to 95 percent.");
    feedbackSlider.setExplicitFocusOrder (8);
    feedbackAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::feedback.getParamID(), feedbackSlider);

    setupPercentSlider (mixSlider, mixLabel, "Mix", "Balance between the dry and delayed signal, 0 to 100 percent.");
    mixSlider.setExplicitFocusOrder (9);
    mixAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::mix.getParamID(), mixSlider);

    // ---- Stereo width ----------------------------------------------------
    setupSlider (widthSlider, widthLabel, "Stereo Width",
                 "Delays the right channel of the echoes by 0 to 100 milliseconds to widen them. "
                 "Does not affect the dry signal. Stereo only.");
    widthSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    widthSlider.setExplicitFocusOrder (10);
    widthAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::stereoWidthMs.getParamID(), widthSlider);

    // ---- Filters ---------------------------------------------------------
    setupFrequencySlider (lowCutSlider, lowCutLabel, "Low Cut",
                          "High-pass filter on the echoes, off or 21 hertz to 2 kilohertz. Turn fully down for off.",
                          Params::lowCutMinHz);
    lowCutSlider.setExplicitFocusOrder (11);
    lowCutAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::lowCut.getParamID(), lowCutSlider);

    setupFrequencySlider (highCutSlider, highCutLabel, "High Cut",
                          "Low-pass filter on the echoes, 500 hertz to 20 kilohertz or off. Turn fully up for off.",
                          Params::highCutMaxHz);
    highCutSlider.setExplicitFocusOrder (12);
    highCutAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::highCut.getParamID(), highCutSlider);

    filterPosLabel.setText ("Filter Position", juce::dontSendNotification);
    filterPosLabel.setJustificationType (juce::Justification::centred);
    filterPosLabel.attachToComponent (&filterPosBox, false);
    filterPosBox.addItemList (Params::getFilterPositionNames(), 1);
    filterPosBox.setTitle ("Filter Position");
    filterPosBox.setDescription ("In Feedback Loop filters every repeat, so echoes get progressively darker or thinner. "
                                 "Output Only filters the echoes once, without changing the feedback.");
    filterPosBox.setWantsKeyboardFocus (true);
    filterPosBox.setExplicitFocusOrder (13);
    addAndMakeVisible (filterPosBox);
    filterPosAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::filterPos.getParamID(), filterPosBox);

    // ---- Perform settings ------------------------------------------------
    inputModeLabel.setText ("Input", juce::dontSendNotification);
    inputModeLabel.setJustificationType (juce::Justification::centred);
    inputModeLabel.attachToComponent (&inputModeBox, false);
    inputModeBox.addItemList (Params::getInputModeNames(), 1);
    inputModeBox.setTitle ("Input");
    inputModeBox.setDescription ("Always sends the input into the delay. "
                                 "Throw Only sends it in only while Throw is held or latched.");
    inputModeBox.setWantsKeyboardFocus (true);
    inputModeBox.setExplicitFocusOrder (14);
    addAndMakeVisible (inputModeBox);
    inputModeAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::inputMode.getParamID(), inputModeBox);

    setupSlider (throwLevelSlider, throwLevelLabel, "Throw Level",
                 "Level of the input sent into the delay while Throw is on, minus 12 to plus 12 decibels.");
    throwLevelSlider.spokenTextFromValue = [] (double v) { return Params::formatDecibels ((float) v, true); };
    throwLevelSlider.setNumKeyboardSteps (24);
    throwLevelSlider.setExplicitFocusOrder (15);
    throwLevelAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::throwLevelDb.getParamID(), throwLevelSlider);

    setupSlider (freezeFadeSlider, freezeFadeLabel, "Freeze Fade",
                 "How long Freeze takes to fade in and out, from 10 milliseconds to 2 seconds. "
                 "Long fades swell into the frozen sound.");
    freezeFadeSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    freezeFadeSlider.setExplicitFocusOrder (16);
    freezeFadeAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::freezeFadeMs.getParamID(), freezeFadeSlider);

    resetButton.setTitle ("Reset");
    resetButton.setDescription ("Releases every gesture and latch, including Freeze, and undoes "
                                "feedback and delay time changes made with the arrow keys in Perform mode.");
    resetButton.setWantsKeyboardFocus (true);
    resetButton.setExplicitFocusOrder (17);
    resetButton.onClick = [this] { processorRef.resetPerformance(); };
    addAndMakeVisible (resetButton);

    // Announced however the reset happened: button, Backspace, host or MIDI.
    p.onPerformanceReset = []
    {
        juce::AccessibilityHandler::postAnnouncement ("Reset", juce::AccessibilityHandler::AnnouncementPriority::high);
    };

    p.onGestureLatched = [] (const juce::String& name, bool isOn)
    {
        juce::AccessibilityHandler::postAnnouncement (name + (isOn ? " latched" : " released"),
                                                      juce::AccessibilityHandler::AnnouncementPriority::high);
    };

    p.apvts.state.addListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    setWantsKeyboardFocus (false);
    setResizable (false, false);
    setSize (640, 710);
}

EchoFactoryEditor::~EchoFactoryEditor()
{
    processorRef.onPerformanceReset = nullptr;
    processorRef.onGestureLatched = nullptr;
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    processorRef.apvts.state.removeListener (this);
    cancelPendingUpdate();

    if (saveDialog != nullptr)
        saveDialog->exitModalState (0);
}

void EchoFactoryEditor::setupSlider (AccessibleSlider& slider, juce::Label& label,
                                    const juce::String& name, const juce::String& description)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);

    slider.setTitle (name);
    slider.setDescription (description);
    slider.setNumKeyboardSteps (100);
    addAndMakeVisible (slider);
}

void EchoFactoryEditor::setupPercentSlider (AccessibleSlider& slider, juce::Label& label,
                                           const juce::String& name, const juce::String& description)
{
    setupSlider (slider, label, name, description);
    slider.spokenTextFromValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " percent"; };
}

void EchoFactoryEditor::setupFrequencySlider (AccessibleSlider& slider, juce::Label& label, const juce::String& name,
                                             const juce::String& description, float offHz)
{
    setupSlider (slider, label, name, description);
    slider.spokenTextFromValue = [offHz] (double v) { return Params::formatFrequency ((float) v, offHz, true); };
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
    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &timeSlider, &smoothingSlider, &syncButton, &pingPongButton, &freezeButton, &feedbackSlider, &mixSlider,
                                                            &timeLabel, &smoothingLabel, &feedbackLabel, &mixLabel,
                                                            &widthSlider, &lowCutSlider, &highCutSlider, &filterPosBox,
                                                            &widthLabel, &lowCutLabel, &highCutLabel, &filterPosLabel,
                                                            &performPad, &throwLevelSlider, &freezeFadeSlider, &inputModeBox, &resetButton,
                                                            &throwLevelLabel, &freezeFadeLabel, &inputModeLabel })
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

    // Dividers above the width/filter row and the perform row.
    g.setColour (Colours::topBar);

    for (auto dividerY : { (float) widthLabel.getY() - 10.0f, (float) performPad.getY() - 10.0f })
        g.drawLine (16.0f, dividerY, (float) getWidth() - 16.0f, dividerY, 1.5f);
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

    // Row 1: delay controls
    auto row1 = area.removeFromTop (area.getHeight() / 3).withTrimmedTop (24).withTrimmedBottom (8); // top: room for labels
    const auto columnWidth = row1.getWidth() / 5;

    timeSlider.setBounds (row1.removeFromLeft (columnWidth).reduced (6, 0));
    smoothingSlider.setBounds (row1.removeFromLeft (columnWidth).reduced (6, 0));
    auto toggles = row1.removeFromLeft (columnWidth).withSizeKeepingCentre (columnWidth - 12, 120);
    syncButton.setBounds (toggles.removeFromTop (40));
    toggles.removeFromTop (4);
    pingPongButton.setBounds (toggles.removeFromTop (36));
    toggles.removeFromTop (4);
    freezeButton.setBounds (toggles.removeFromTop (36));
    feedbackSlider.setBounds (row1.removeFromLeft (columnWidth).reduced (6, 0));
    mixSlider.setBounds (row1.reduced (6, 0));

    // Row 2: stereo width and filters
    auto row2 = area.removeFromTop (area.getHeight() / 2).withTrimmedTop (32).withTrimmedBottom (8); // separator line + labels

    widthSlider.setBounds (row2.removeFromLeft (columnWidth).reduced (6, 0));
    lowCutSlider.setBounds (row2.removeFromLeft (columnWidth).reduced (6, 0));
    highCutSlider.setBounds (row2.removeFromLeft (columnWidth).reduced (6, 0));
    filterPosBox.setBounds (row2.withSizeKeepingCentre (juce::jmin (row2.getWidth() - 12, 200), 30));

    // Row 3: perform pad and its settings
    auto row3 = area.withTrimmedTop (12).withTrimmedBottom (8);

    performPad.setBounds (row3.removeFromLeft (columnWidth * 2).reduced (6, 0));
    row3.removeFromTop (20); // labels
    throwLevelSlider.setBounds (row3.removeFromLeft (columnWidth).reduced (6, 0));
    freezeFadeSlider.setBounds (row3.removeFromLeft (columnWidth).reduced (6, 0));
    auto lastColumn = row3.reduced (6, 0);
    inputModeBox.setBounds (lastColumn.removeFromTop (lastColumn.getHeight() / 2).withSizeKeepingCentre (lastColumn.getWidth(), 30));
    resetButton.setBounds (lastColumn.withSizeKeepingCentre (lastColumn.getWidth(), 34));
}
