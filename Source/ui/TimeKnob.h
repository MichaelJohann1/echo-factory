#pragma once

#include "AccessibleSlider.h"
#include "../Parameters.h"

/**
    Sets up a delay-time knob to show either a time or a sync division: the
    on-screen and spoken text, and the number of keyboard steps. Shared by the
    main Delay Time knob and every tap's Time knob, so they all swap the same way.
    The caller sets the range (or an attachment does) and the title.
*/
namespace TimeKnob
{
    inline void configure (AccessibleSlider& slider, bool synced)
    {
        if (synced)
        {
            const auto& divisions = Params::getSyncDivisions();
            const auto nameAt = [&divisions] (double v)
            {
                return divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, juce::roundToInt (v))];
            };

            // Compact names on screen, full names for screen readers.
            slider.textFromValueFunction = [nameAt] (double v) { return juce::String (nameAt (v).shortName); };
            slider.spokenTextFromValue   = [nameAt] (double v) { return juce::String (nameAt (v).spokenName); };
            slider.valueFromTextFunction = [&divisions] (const juce::String& text)
            {
                for (size_t i = 0; i < divisions.size(); ++i)
                    if (text.trim().equalsIgnoreCase (divisions[i].shortName) || text.trim().equalsIgnoreCase (divisions[i].spokenName))
                        return (double) i;

                return 0.0;
            };
            slider.setNumKeyboardSteps ((int) divisions.size() - 1);
        }
        else
        {
            slider.textFromValueFunction = [] (double v) { return Params::formatMilliseconds ((float) v, false); };
            slider.spokenTextFromValue   = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
            slider.valueFromTextFunction = [] (const juce::String& text) { return (double) Params::parseMilliseconds (text); };
            slider.setNumKeyboardSteps (100);
        }

        slider.updateText();

        if (auto* handler = slider.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }
}
