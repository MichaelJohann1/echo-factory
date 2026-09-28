#include "PerformPad.h"
#include "../Parameters.h"

namespace
{
    const juce::Colour cellOff  { 0xff2b3038 };
    const juce::Colour cellOn   { 0xff4fc3f7 };
    const juce::Colour textOff  { 0xfff2f4f7 };
    const juce::Colour textOn   { 0xff1a1d22 };

    void announce (const juce::String& text)
    {
        juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::high);
    }
}

PerformPad::PerformPad (juce::AudioProcessorValueTreeState& apvts)
{
    auto param = [&apvts] (const juce::ParameterID& id) { return apvts.getParameter (id.getParamID()); };

    // In keyboard order, left to right, so the screen matches the hand.
    gestures = {
        { 'A', "Runaway",  param (Params::ID::runaway) },
        { 'S', "Tapestop", param (Params::ID::tapestop) },
        { 'D', "Freeze",   param (Params::ID::freeze) },
        { 'F', "Throw",    param (Params::ID::throwGesture) },
        { 'G', "Reverse",  param (Params::ID::reverse) },
    };

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

PerformPad::~PerformPad()
{
    stopTimer();
}

void PerformPad::setOn (Gesture& g, bool shouldBeOn)
{
    if (isOn (g) == shouldBeOn)
        return;

    g.param->beginChangeGesture();
    g.param->setValueNotifyingHost (shouldBeOn ? 1.0f : 0.0f);
    g.param->endChangeGesture();
}

bool PerformPad::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (code == juce::KeyPress::escapeKey)
    {
        releaseAllHeld();

        if (onExit != nullptr)
            onExit();

        return true;
    }

    if (code == juce::KeyPress::backspaceKey)
    {
        if (onReset != nullptr)
            onReset();

        return true;
    }

    // Leave shortcuts that use other modifiers (and Tab) to the host and VoiceOver.
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    const auto upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    for (auto& g : gestures)
    {
        if (g.key != upper)
            continue;

        if (g.keyDown)
            return true; // auto-repeat

        g.keyDown = true;

        if (mods.isShiftDown())
        {
            const auto nowOn = ! isOn (g);
            setOn (g, nowOn);
            announce (g.name + (nowOn ? " latched" : " released"));
            return true;
        }

        g.heldByKey = true;

        if (isOn (g))
            announce (g.name + " released"); // latched: goes off when the key comes up
        else
            setOn (g, true);

        return true;
    }

    return false;
}

bool PerformPad::keyStateChanged (bool)
{
    auto handled = false;

    for (auto& g : gestures)
    {
        if (g.keyDown && ! juce::KeyPress::isKeyCurrentlyDown ((int) g.key))
        {
            g.keyDown = false;
            handled = true;

            if (g.heldByKey)
            {
                g.heldByKey = false;
                setOn (g, false);
            }
        }
    }

    return handled;
}

void PerformPad::releaseAllHeld()
{
    for (auto& g : gestures)
    {
        g.keyDown = false;

        if (g.heldByKey || g.heldByMouse)
        {
            g.heldByKey = g.heldByMouse = false;
            setOn (g, false);
        }
    }
}

void PerformPad::focusGained (FocusChangeType)
{
    repaint();
}

void PerformPad::focusLost (FocusChangeType)
{
    releaseAllHeld();
    repaint();
}

PerformPad::Gesture* PerformPad::gestureAt (juce::Point<int> position)
{
    for (int i = 0; i < (int) gestures.size(); ++i)
        if (getCellBounds (i).contains (position))
            return &gestures[(size_t) i];

    return nullptr;
}

void PerformPad::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (auto* g = gestureAt (e.getPosition()))
    {
        g->heldByMouse = true;
        setOn (*g, true);
    }
}

void PerformPad::mouseUp (const juce::MouseEvent&)
{
    for (auto& g : gestures)
    {
        if (g.heldByMouse)
        {
            g.heldByMouse = false;
            setOn (g, false);
        }
    }
}

juce::Rectangle<int> PerformPad::getCellBounds (int index) const
{
    auto area = getLocalBounds().withTrimmedTop (24);
    area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 96));
    const auto cellWidth = area.getWidth() / (int) gestures.size();
    return area.withX (area.getX() + index * cellWidth).withWidth (cellWidth).reduced (3);
}

void PerformPad::timerCallback()
{
    // Follows the parameters, so latches set by the host, MIDI or Reset show too.
    auto changed = false;

    for (auto& g : gestures)
    {
        const auto on = isOn (g);
        changed = changed || on != g.shownOn;
        g.shownOn = on;
    }

    if (changed)
        repaint();
}

void PerformPad::paint (juce::Graphics& g)
{
    g.setColour (textOff);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (hasKeyboardFocus (false) ? "PERFORM  (keys active, Esc to leave)" : "PERFORM  (focus here to play keys)",
                getLocalBounds().removeFromTop (20), juce::Justification::centredLeft);

    for (int i = 0; i < (int) gestures.size(); ++i)
    {
        const auto& gesture = gestures[(size_t) i];
        const auto cell = getCellBounds (i).toFloat();

        g.setColour (gesture.shownOn ? cellOn : cellOff);
        g.fillRoundedRectangle (cell, 6.0f);

        auto text = cell.reduced (4.0f);
        g.setColour (gesture.shownOn ? textOn : textOff);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (juce::String::charToString (gesture.key), text.removeFromTop (text.getHeight() * 0.55f),
                    juce::Justification::centredBottom);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (gesture.name, text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
    }
}

std::unique_ptr<juce::AccessibilityHandler> PerformPad::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}
