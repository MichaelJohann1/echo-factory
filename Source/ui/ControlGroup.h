#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    An invisible container that groups related controls for screen readers.

    - Exposed with the group role, so VoiceOver reads "Delay, group" and lets the
      user step into it (VO-Shift-Down) or skip past it.
    - A focus container but not a keyboard focus container: Tab still runs through
      every control inside, while screen readers see a nested tree.
    - Explicit focus order numbers of the children are local to the group; the
      group's own number places it among its siblings.
    - Draws nothing and passes mouse clicks through to its children.
*/
class ControlGroup : public juce::Component
{
public:
    ControlGroup (const juce::String& title, const juce::String& description)
    {
        setTitle (title);
        setDescription (description);
        setFocusContainerType (FocusContainerType::focusContainer);
        setInterceptsMouseClicks (false, true);
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlGroup)
};
