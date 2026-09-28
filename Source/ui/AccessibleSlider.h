#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    A Slider that works well with screen readers and keyboards.

    - Screen readers hear spokenTextFromValue ("400 milliseconds") rather than the
      compact text shown on screen ("400 ms").
    - Steps are made in normalised (skewed) space, so one arrow press or VoiceOver
      increment moves a musically sensible amount across the whole range, instead
      of e.g. 1 ms out of 5000.
    - Keys: arrows = 1 step, Shift+arrows = fine step, Page Up/Down = 10 steps,
      Home/End = minimum/maximum.
*/
class AccessibleSlider : public juce::Slider
{
public:
    AccessibleSlider()
    {
        setWantsKeyboardFocus (true);
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    }

    /** Text read by screen readers. Falls back to the on-screen text if not set. */
    std::function<juce::String (double)> spokenTextFromValue;

    /** Number of arrow-key steps across the full range (use numChoices - 1 for choice parameters). */
    void setNumKeyboardSteps (int numSteps)
    {
        numKeyboardSteps = juce::jmax (1, numSteps);
    }

    juce::String getSpokenText()
    {
        return spokenTextFromValue != nullptr ? spokenTextFromValue (getValue())
                                              : getTextFromValue (getValue());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();

        if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
            return false;

        const auto step = 1.0 / numKeyboardSteps;

        if (code == juce::KeyPress::homeKey)     { setValue (getMinimum(), juce::sendNotificationSync); return true; }
        if (code == juce::KeyPress::endKey)      { setValue (getMaximum(), juce::sendNotificationSync); return true; }
        if (code == juce::KeyPress::pageUpKey)   { stepBy (step * 10.0); return true; }
        if (code == juce::KeyPress::pageDownKey) { stepBy (-step * 10.0); return true; }

        const auto fine = mods.isShiftDown() ? 0.1 : 1.0;

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)   { stepBy (step * fine);  return true; }
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { stepBy (-step * fine); return true; }

        return false;
    }

    /** Moves by a proportion of the (skewed) range, always by at least one interval. */
    void stepBy (double proportionDelta)
    {
        const auto current  = getValue();
        const auto interval = getInterval();
        const auto proportion = juce::jlimit (0.0, 1.0, valueToProportionOfLength (current) + proportionDelta);
        auto target = snapValue (proportionOfLengthToValue (proportion), notDragging);

        // Make sure small steps on coarse ranges (e.g. choices) still move.
        if (interval > 0.0 && std::abs (target - current) < interval * 0.5)
            target = current + (proportionDelta > 0.0 ? interval : proportionDelta < 0.0 ? -interval : 0.0);

        juce::Slider::ScopedDragNotification drag (*this);
        setValue (juce::jlimit (getMinimum(), getMaximum(), target), juce::sendNotificationSync);
    }

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<Handler> (*this);
    }

private:
    class Handler : public juce::AccessibilityHandler
    {
    public:
        explicit Handler (AccessibleSlider& s)
            : AccessibilityHandler (s, juce::AccessibilityRole::slider, {},
                                    Interfaces { std::make_unique<Value> (s) }),
              slider (s) {}

        juce::String getHelp() const override { return slider.getTooltip(); }

    private:
        // Exposes the value as a 0..1 proportion so VoiceOver/NVDA increments
        // follow the same steps as the keyboard.
        class Value : public juce::AccessibilityValueInterface
        {
        public:
            explicit Value (AccessibleSlider& s) : slider (s) {}

            bool isReadOnly() const override { return ! slider.isEnabled(); }
            double getCurrentValue() const override { return slider.valueToProportionOfLength (slider.getValue()); }
            juce::String getCurrentValueAsString() const override { return slider.getSpokenText(); }

            void setValue (double newProportion) override
            {
                slider.stepBy (newProportion - getCurrentValue());
            }

            void setValueAsString (const juce::String& text) override
            {
                juce::Slider::ScopedDragNotification drag (slider);
                slider.setValue (slider.getValueFromText (text), juce::sendNotificationSync);
            }

            AccessibleValueRange getRange() const override
            {
                return { { 0.0, 1.0 }, 1.0 / slider.numKeyboardSteps };
            }

        private:
            AccessibleSlider& slider;
        };

        AccessibleSlider& slider;
    };

    int numKeyboardSteps = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleSlider)
};
