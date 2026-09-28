#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    The Perform pad: one focusable control that plays the performance gestures
    from the computer keyboard (layout in docs/perform-map.md).

    - Hold a key: the gesture is on while held. Not announced; the sound is the feedback.
    - Shift + key: toggles a latch, announced ("Freeze latched" / "Freeze released").
    - Plain key on a latched gesture: releases it when the key comes up, announced.
    - Backspace: onReset. Escape: onExit.
    - Losing focus releases every held gesture so nothing sticks on.

    Each cell can also be held with the mouse.
*/
class PerformPad : public juce::Component,
                   private juce::Timer
{
public:
    explicit PerformPad (juce::AudioProcessorValueTreeState& apvts);
    ~PerformPad() override;

    std::function<void()> onReset;
    std::function<void()> onExit;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Gesture
    {
        juce::juce_wchar key;
        juce::String name;
        juce::RangedAudioParameter* param;
        bool keyDown = false;    // physical key, to ignore auto-repeat
        bool heldByKey = false;  // momentary press that turns off on key-up
        bool heldByMouse = false;
        bool shownOn = false;
    };

    static bool isOn (const Gesture& g) { return g.param->getValue() >= 0.5f; }
    static void setOn (Gesture&, bool shouldBeOn);
    void releaseAllHeld();
    Gesture* gestureAt (juce::Point<int>);
    juce::Rectangle<int> getCellBounds (int index) const;
    void timerCallback() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::vector<Gesture> gestures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformPad)
};
