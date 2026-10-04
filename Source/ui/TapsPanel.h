#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AccessibleSlider.h"
#include "ControlGroup.h"

/**
    The Multi-Tap panel: one "Tap N" group per tap (up to 16), then Add Tap,
    in a scrolling list. Follows the TAPS tree in the plug-in state (Taps.h), so
    preset loads and host restores show up here too.

    Focus order runs down the list: each tap's Time, Level, Pan, Pitch, Reverse
    and Remove, then the next tap, then Add Tap last. Every control's
    title carries its tap number ("Tap 2 Level"), so Tab never just says "Level".
    Rows are only created and destroyed when taps are added or removed, so a
    focused control isn't pulled out from under the user.
*/
class TapsPanel : public ControlGroup,
                  private juce::ValueTree::Listener,
                  private juce::AsyncUpdater
{
public:
    explicit TapsPanel (juce::AudioProcessorValueTreeState&);
    ~TapsPanel() override;

    /** Tap Time knobs show note divisions while synced. */
    void setSynced (bool);

    /** Scrolls the list so a focused control inside it is visible. */
    void scrollToShow (juce::Component* focused);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TapRow;

    /** Plumbing that screen readers skip, so they go straight from Taps to each Tap group. */
    template <typename Base>
    struct Unannounced : public Base
    {
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::ignored);
        }
    };

    juce::ValueTree getTapsTree() const;
    void rebuildRows();
    void renumber();
    void layoutList();
    void addTap();
    void removeTap (const juce::ValueTree& tap);

    // ValueTree::Listener on the whole state, which survives replaceState.
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override; // rebuilds every row

    juce::AudioProcessorValueTreeState& apvts;
    bool synced = false;

    Unannounced<juce::Viewport> viewport;
    Unannounced<juce::Component> list;
    std::vector<std::unique_ptr<TapRow>> rows;
    juce::TextButton addButton { "Add Tap" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapsPanel)
};
