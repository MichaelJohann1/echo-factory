# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Echo Factory is a JUCE 9 delay effect by ZBAudio, built as AU, VST3 and Standalone. Its identity is set at the top of `CMakeLists.txt`: manufacturer code `Zbau`, plugin code `Ecfy`, bundle ID `com.zbaudio.echofactory`. Don't change the codes, because that breaks saved host sessions.

**Screen-reader accessibility is a hard requirement.** Every control needs:
- an accessible title (`setTitle`)
- a description (`setDescription`)
- spoken value text with units
- keyboard focus
- an `setExplicitFocusOrder` slot

## Build

This Mac has only the Command Line Tools, not Xcode, so use the Makefile generator. `-G Xcode` fails.

```bash
git submodule update --init          # JUCE lives in ./JUCE
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 10
```

- `COPY_PLUGIN_AFTER_BUILD` installs the AU and VST3 into `~/Library/Audio/Plug-Ins`.
- The Standalone app is at `build/EchoFactory_artefacts/Debug/Standalone/Echo Factory.app`.
- The Standalone app has no host tempo, so tempo sync falls back to 120 BPM there.
- The Standalone app needs `MICROPHONE_PERMISSION_ENABLED`, because otherwise macOS silently gives it zero input. It's ad-hoc signed, so macOS may ask for mic access again after a rebuild.
- There are no unit tests. Validation is:
  - **Warnings:** the build should produce none from `Source/`. JUCE's recommended warning flags are on.
  - **AU:** run `killall -9 AudioComponentRegistrar; auval -v aufx Ecfy Zbau`.
  - **Accessibility:** a manual VoiceOver pass in the Standalone app. osascript has no assistive access here, so the accessibility tree can't be inspected automatically.

## Architecture

- **Parameters** (`Source/Parameters.*`): the single source of truth for parameter IDs, ranges, defaults and value↔text functions.
  - The sync-division table has two names per entry: a short one for the screen and a spoken one for screen readers ("1/8D" vs "1/8 dotted").
  - `formatMilliseconds` and `formatFrequency` take a `spoken` flag and are shared by the parameter text and the UI.
  - Filter "Off" is the end of the range: Low Cut at its minimum and High Cut at its maximum. The processor maps those values to 0, which bypasses the filter in the engine.
- **DSP** (`DelayEngine`): plain C++ with no JUCE parameter knowledge. `EchoFactoryProcessor::updateEngineParameters()` pushes raw APVTS values into it once per block.
  - Every value is smoothed with `SmoothedValue`. The delay-time ramp length comes from the `timeSmoothingMs` parameter. `setDelaySmoothingMs` keeps the glide going from its current position, because `SmoothedValue::reset` jumps straight to the target.
  - The per-sample loop does pop → filter → push, with the write path depending on filter position (in feedback loop or output only).
  - Ping-pong (stereo only) writes the mono input to the left line, sends the left echo to the right line at unity, and sends the right echo back to the left line scaled by feedback. So feedback 0 still gives two repeats, first left then right. Mode switches crossfade via `pingPongAmount`. While frozen in ping-pong, the two lines swap each pass so the audio keeps bouncing.
  - Stereo width is a separate short delay line on the right echo, applied on the output only, never in the feedback loop.
  - Freeze crossfades the write path to the buffer's own unfiltered output, and rounds the delay to whole samples while frozen.
  - Filters always run, even when bypassed, so switching them on doesn't click.
  - Wear and Runaway both go through `DelayEngine::tape()` on the write path (the frozen path skips it). `tape()` applies a DC blocker, a `tanh` saturator with a ceiling, and one-pole darkening. Each is crossfaded by an amount, so Wear 0 is bit-exact clean. Wear also adds wow, flutter and random drift to the delay time, which fade out while frozen.
  - Runaway ramps feedback to 110–160% while pushing the saturator fully in. The loop stays bounded because the linear part of the loop gain stays below 1 throughout the ramp. Check any change here with an offline test before trusting it by ear.
- **Performance** (`docs/perform-map.md` is the spec for keys and MIDI):
  - Each gesture (Throw, Freeze, Tapestop, Runaway, Reverse) is a bool parameter. Held versus latched only exists in the UI and MIDI layers.
  - Throw is applied through `DelayEngine::setInputSend`. The processor sends Throw Level while throwing, and otherwise unity, or 0 in Throw Only mode.
  - The Reset parameter can change on any thread, so it only sets a flag. The processor's timer then calls `resetPerformance()` on the message thread, which fires `onPerformanceReset` so the editor can announce it.
  - MIDI uses a fixed map (no MIDI learn). On the audio thread, `handleMidi` only records events. Held notes and the sustain pedal act immediately: the engine sees a gesture as on if its parameter is on or MIDI holds it. Latch notes, Reset and CCs set flags or pending values, and the timer applies them to parameters on the message thread.
  - The AU is pinned to `aufx` even though it accepts MIDI, so it doesn't break sessions. auval warns about this, which is expected.
  - Perform-mode arrow keys go through `nudgeFeedback` and `nudgeDelayTime`, which remember the value before the first nudge so Reset can restore it.
  - `ui/PerformPad` is the focusable keyboard surface. It tracks physical keys to ignore auto-repeat, and releases held gestures when it loses focus.
- **State and presets:**
  - All state is the APVTS `ValueTree`, and the current preset name is stored as a property on it (`presetName`).
  - `PresetManager` writes and reads that tree as XML `.echopreset` files in `~/Documents/ZBAudio/Echo Factory/Presets`.
  - On load, any parameter the file doesn't contain is reset to its default. This way new parameters don't inherit stale values from older presets.
  - Factory presets are an in-memory list, currently empty.
  - The editor follows preset-name changes, including state the host restores, with a `ValueTree::Listener` that triggers an `AsyncUpdater`.
- **Editor accessibility:**
  - `ui/AccessibleSlider.h` supplies a custom `AccessibilityHandler`:
    - The value is exposed as a 0–1 proportion, so VoiceOver increments match keyboard steps in skewed space.
    - `spokenTextFromValue` is read to screen readers, separately from the on-screen text box.
    - Keys: arrows, Shift for fine, Page Up/Down, Home/End.
    - Use `setNumKeyboardSteps(numChoices - 1)` for choice parameters.
  - The Delay Time knob is shared between two parameters. A `ParameterAttachment` on `sync` swaps its `SliderAttachment` between `delayTimeMs` and `syncDivision`. It also swaps the knob's title, text functions and step count.
    - Use the value passed to the callback. The APVTS raw value is still stale when that callback fires.
  - `SavePresetDialog` is an in-editor modal overlay, not a `DialogWindow`, because that's more reliable in hosts.
    - While it's open, the editor hides the main controls so screen readers can't reach them.
    - The close callback runs through `MessageManager::callAsync` because it deletes the dialog.
  - Keyboard focus is shown by `EchoFactoryEditor::paintOverChildren`, which is driven by a `FocusChangeListener`.
  - Focus order is set with explicit numbers in the editor constructor. Renumber them when inserting a control.
  - Status changes such as loading and saving presets are announced with `AccessibilityHandler::postAnnouncement`.
