#include "TapsPanel.h"
#include "TimeKnob.h"
#include "../Parameters.h"
#include "../Taps.h"

namespace
{
    // Columns, shared by the header and every row.
    constexpr int labelWidth = 50, knobWidth = 72, toggleWidth = 96, removeWidth = 74;
    constexpr int headerHeight = 22, rowHeight = 78, addButtonHeight = 32;

    juce::NormalisableRange<double> toDouble (const juce::NormalisableRange<float>& r)
    {
        return { r.start, r.end, r.interval, r.skew };
    }

    void announce (const juce::String& text)
    {
        juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::high);
    }
}

//==============================================================================
class TapsPanel::TapRow : public ControlGroup
{
public:
    TapRow (TapsPanel& panel, juce::ValueTree tapTree, bool isSynced)
        : ControlGroup ("Tap", {}), tap (std::move (tapTree)), synced (isSynced)
    {
        for (auto* slider : { &time, &level, &pan, &pitch })
        {
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 4, 18);
            addAndMakeVisible (slider);
        }

        level.setNormalisableRange (toDouble (Taps::getLevelRange()));
        level.textFromValueFunction = [] (double v) { return Taps::formatLevel ((float) v, false); };
        level.spokenTextFromValue   = [] (double v) { return Taps::formatLevel ((float) v, true); };
        level.valueFromTextFunction = [] (const juce::String& text)
        {
            return text.trim().equalsIgnoreCase ("off") ? (double) Taps::minLevelDb : (double) text.getFloatValue();
        };
        level.getValueObject().referTo (tap.getPropertyAsValue (Taps::ID::levelDb, nullptr));

        pan.setNormalisableRange (toDouble (Taps::getPanRange()));
        pan.textFromValueFunction = [] (double v) { return Taps::formatPan ((float) v, false); };
        pan.spokenTextFromValue   = [] (double v) { return Taps::formatPan ((float) v, true); };
        pan.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            const auto amount = (double) t.getFloatValue();
            return t.contains ("l") && ! t.startsWith ("-") ? -amount : amount;
        };
        pan.setNumKeyboardSteps (40); // 5 percent a step
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.getValueObject().referTo (tap.getPropertyAsValue (Taps::ID::pan, nullptr));

        pitch.setNormalisableRange (toDouble (Taps::getPitchRange()));
        pitch.textFromValueFunction = [] (double v) { return Taps::formatSemitones ((float) v, false); };
        pitch.spokenTextFromValue   = [] (double v) { return Taps::formatSemitones ((float) v, true); };
        pitch.valueFromTextFunction = [] (const juce::String& text) { return (double) text.getFloatValue(); };
        pitch.setNumKeyboardSteps ((int) (2.0f * Taps::maxPitchSemitones));
        pitch.setDoubleClickReturnValue (true, 0.0);
        pitch.getValueObject().referTo (tap.getPropertyAsValue (Taps::ID::pitch, nullptr));

        // The text boxes were made before their text functions were set.
        for (auto* slider : { &level, &pan, &pitch })
            slider->updateText();

        reverse.setWantsKeyboardFocus (true);
        reverse.getToggleStateValue().referTo (tap.getPropertyAsValue (Taps::ID::reverse, nullptr));
        addAndMakeVisible (reverse);

        remove.setWantsKeyboardFocus (true);
        remove.onClick = [safePanel = juce::Component::SafePointer<TapsPanel> (&panel), tree = tap]
        {
            // Deferred: removing the tap deletes this button.
            juce::MessageManager::callAsync ([safePanel, tree]
            {
                if (safePanel != nullptr)
                    safePanel->removeTap (tree);
            });
        };
        addAndMakeVisible (remove);

        // Left to right, top to bottom.
        int order = 1;
        for (auto* c : std::initializer_list<juce::Component*> { &time, &level, &pan, &pitch, &reverse, &remove })
            c->setExplicitFocusOrder (order++);

        bindTime();
    }

    void setNumber (int newNumber)
    {
        number = newNumber;
        const auto name = "Tap " + juce::String (number);

        setTitle (name);
        setDescription ("Time, level, pan, pitch and reverse for tap " + juce::String (number) + ", and a button to remove it");

        level.setTitle (name + " Level");
        level.setDescription ("Volume of this tap, off or minus 60 to plus 6 decibels. Turn fully down for off.");
        pan.setTitle (name + " Pan");
        pan.setDescription ("Where this tap sits, from 100 percent left to 100 percent right. Stereo only.");
        pitch.setTitle (name + " Pitch");
        pitch.setDescription ("Pitch of this tap, from minus 12 to plus 12 semitones. 12 semitones is an octave.");
        reverse.setTitle (name + " Reverse");
        reverse.setDescription ("When on, this tap always plays backwards. Reverse in Perform mode also reverses every tap.");
        remove.setTitle ("Remove " + name);
        remove.setDescription ("Removes this tap. The taps after it move up one number.");

        updateTimeTitle();
        repaint();
    }

    void setSynced (bool isSynced)
    {
        if (synced == isSynced)
            return;

        synced = isSynced;
        bindTime();
    }

    juce::ValueTree tap;
    AccessibleSlider time, level, pan, pitch;
    juce::ToggleButton reverse { "Reverse" };
    juce::TextButton remove { "Remove" };

    void paint (juce::Graphics& g) override
    {
        // On screen only; the group's title already says it.
        g.setColour (findColour (juce::Label::textColourId));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("Tap " + juce::String (number), getLocalBounds().removeFromLeft (labelWidth).withTrimmedBottom (18),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 3);
        area.removeFromLeft (labelWidth);

        for (auto* slider : { &time, &level, &pan, &pitch })
            slider->setBounds (area.removeFromLeft (knobWidth).reduced (2, 0));

        reverse.setBounds (area.removeFromLeft (toggleWidth).withSizeKeepingCentre (toggleWidth - 4, 32));

        remove.setBounds (area.removeFromLeft (removeWidth).withSizeKeepingCentre (removeWidth - 6, 28));
    }

private:
    /** Points the Time knob at the time or the division, depending on Sync. */
    void bindTime()
    {
        // Detach first, so the new range never clamps and writes back the old property.
        time.getValueObject().referTo (juce::Value());

        if (synced)
            time.setNormalisableRange ({ 0.0, (double) Params::getSyncDivisions().size() - 1.0, 1.0 });
        else
            time.setNormalisableRange (toDouble (Params::getDelayTimeRange()));

        TimeKnob::configure (time, synced);
        time.getValueObject().referTo (tap.getPropertyAsValue (synced ? Taps::ID::division : Taps::ID::timeMs, nullptr));
        updateTimeTitle();
    }

    void updateTimeTitle()
    {
        const auto name = "Tap " + juce::String (number);

        if (synced)
        {
            time.setTitle (name + " Division");
            time.setDescription ("When this tap plays, as a note division of the host tempo, from 1/64 note to 2 bars.");
        }
        else
        {
            time.setTitle (name + " Time");
            time.setDescription ("When this tap plays, from 1 millisecond to 5 seconds.");
        }
    }

    bool synced = false;
    int number = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapRow)
};

//==============================================================================
TapsPanel::TapsPanel (juce::AudioProcessorValueTreeState& state)
    : ControlGroup ("Taps", "Up to 16 taps, each with its own time, level, pan, pitch and reverse, then Add Tap"),
      apvts (state)
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setWantsKeyboardFocus (false); // Tab goes straight to the taps
    viewport.setExplicitFocusOrder (1);
    addAndMakeVisible (viewport);

    // Not a focus container: the panel's traversal goes straight through the
    // viewport and list to each Tap group and Add Tap.
    addButton.setTitle ("Add Tap");
    addButton.setDescription ("Adds a tap after the last one, continuing the spacing of the last two taps. Up to 16 taps.");
    addButton.setWantsKeyboardFocus (true);
    addButton.onClick = [this] { addTap(); };
    list.addAndMakeVisible (addButton);

    apvts.state.addListener (this);
    rebuildRows();
}

TapsPanel::~TapsPanel()
{
    apvts.state.removeListener (this);
    cancelPendingUpdate();
}

juce::ValueTree TapsPanel::getTapsTree() const
{
    return apvts.state.getChildWithName (Taps::ID::taps);
}

void TapsPanel::setSynced (bool isSynced)
{
    synced = isSynced;

    for (auto& row : rows)
        row->setSynced (synced);

    repaint(); // the Time header
}

void TapsPanel::rebuildRows()
{
    rows.clear();

    for (const auto& tap : getTapsTree())
        if (tap.hasType (Taps::ID::tap) && (int) rows.size() < Taps::maxTaps)
            rows.push_back (std::make_unique<TapRow> (*this, tap, synced));

    for (auto& row : rows)
        list.addAndMakeVisible (*row);

    renumber();
}

void TapsPanel::renumber()
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        rows[i]->setNumber ((int) i + 1);
        rows[i]->setExplicitFocusOrder ((int) i + 1);
    }

    addButton.setExplicitFocusOrder ((int) rows.size() + 1);
    layoutList();
}

void TapsPanel::layoutList()
{
    const auto width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness());
    auto y = 0;

    for (auto& row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight;
    }

    addButton.setBounds (labelWidth, y + 8, 2 * knobWidth, addButtonHeight);
    list.setSize (width, y + addButtonHeight + 16);
}

void TapsPanel::addTap()
{
    auto taps = getTapsTree();

    if (! taps.isValid())
        return;

    // Stays focusable at the limit, so it can say why nothing happened.
    if (taps.getNumChildren() >= Taps::maxTaps)
    {
        announce ("Maximum of " + juce::String (Taps::maxTaps) + " taps reached");
        return;
    }

    taps.appendChild (Taps::makeNextTap (taps), nullptr); // the listener adds the row

    if (rows.empty())
        return;

    announce ("Tap " + juce::String ((int) rows.size()) + " added");
    rows.back()->time.grabKeyboardFocus();
}

void TapsPanel::removeTap (const juce::ValueTree& tap)
{
    auto taps = getTapsTree();
    const auto index = taps.indexOf (tap);

    if (index < 0)
        return;

    taps.removeChild (index, nullptr); // the listener removes the row

    const auto left = (int) rows.size();
    announce ("Tap " + juce::String (index + 1) + " removed. "
              + (left == 0 ? juce::String ("No taps left")
                           : juce::String (left) + (left == 1 ? " tap left" : " taps left")));

    // Focus the tap that moved into its place, or the one before, or Add Tap.
    if (index < left)
        rows[(size_t) index]->time.grabKeyboardFocus();
    else if (left > 0)
        rows.back()->time.grabKeyboardFocus();
    else
        addButton.grabKeyboardFocus();
}

void TapsPanel::scrollToShow (juce::Component* focused)
{
    if (focused == nullptr || ! list.isParentOf (focused))
        return;

    // Show the whole row the control is in.
    auto* target = focused;
    while (target->getParentComponent() != &list)
        target = target->getParentComponent();

    const auto top = target->getY(), bottom = target->getBottom();
    const auto viewTop = viewport.getViewPositionY(), viewHeight = viewport.getMaximumVisibleHeight();

    if (top < viewTop)
        viewport.setViewPosition (0, top);
    else if (bottom > viewTop + viewHeight)
        viewport.setViewPosition (0, bottom - viewHeight);
}

void TapsPanel::paint (juce::Graphics& g)
{
    // Column headings, on screen only: every control's title says what it is.
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (juce::FontOptions (14.0f));

    auto header = getLocalBounds().removeFromTop (headerHeight);
    header.removeFromLeft (labelWidth);

    for (const auto* heading : { synced ? "Division" : "Time", "Level", "Pan", "Pitch" })
        g.drawText (heading, header.removeFromLeft (knobWidth), juce::Justification::centred);
}

void TapsPanel::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    layoutList();
}

void TapsPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        triggerAsyncUpdate();
        return;
    }

    if (child.hasType (Taps::ID::taps))
    {
        rebuildRows();
        return;
    }

    if (! parent.hasType (Taps::ID::taps) || ! child.hasType (Taps::ID::tap) || (int) rows.size() >= Taps::maxTaps)
        return;

    const auto index = parent.indexOf (child);
    auto row = std::make_unique<TapRow> (*this, child, synced);
    list.addAndMakeVisible (*row);
    rows.insert (rows.begin() + juce::jlimit (0, (int) rows.size(), index), std::move (row));
    renumber();
}

void TapsPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index)
{
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        triggerAsyncUpdate();
        return;
    }

    if (child.hasType (Taps::ID::taps))
    {
        rebuildRows();
        return;
    }

    if (! parent.hasType (Taps::ID::taps))
        return;

    if (juce::isPositiveAndBelow (index, (int) rows.size()) && rows[(size_t) index]->tap == child)
    {
        rows.erase (rows.begin() + index);
        renumber();
    }
    else
    {
        rebuildRows();
    }
}

void TapsPanel::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (Taps::ID::taps))
        triggerAsyncUpdate();
}

void TapsPanel::valueTreeRedirected (juce::ValueTree&)
{
    // replaceState (preset load or host restore), possibly from another thread.
    triggerAsyncUpdate();
}

void TapsPanel::handleAsyncUpdate()
{
    rebuildRows();
}
