#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayEngine.h"
#include "PresetManager.h"

class EchoFactoryProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::Timer
{
public:
    EchoFactoryProcessor();
    ~EchoFactoryProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.1; } // two max-length ping-pong repeats + width

    // One host-visible program; presets are handled by PresetManager.
    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { apvts };

    /** Releases every gesture and latch, including Freeze. Message thread only. */
    void resetPerformance();

    /** Called on the message thread after any reset, including one from the host or MIDI. */
    std::function<void()> onPerformanceReset;

private:
    // The Reset parameter can be set from any thread, so it only raises a flag
    // that the timer handles on the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    std::atomic<bool> resetRequested { false };

    float getTargetDelayMs() const;
    void updateEngineParameters();

    DelayEngine engine;

    std::atomic<float>* delayTimeParam    = nullptr;
    std::atomic<float>* smoothingParam    = nullptr;
    std::atomic<float>* syncParam         = nullptr;
    std::atomic<float>* syncDivisionParam = nullptr;
    std::atomic<float>* feedbackParam     = nullptr;
    std::atomic<float>* mixParam          = nullptr;
    std::atomic<float>* lowCutParam       = nullptr;
    std::atomic<float>* highCutParam      = nullptr;
    std::atomic<float>* filterPosParam    = nullptr;
    std::atomic<float>* freezeParam       = nullptr;
    std::atomic<float>* pingPongParam     = nullptr;
    std::atomic<float>* widthParam        = nullptr;
    std::atomic<float>* throwParam        = nullptr;
    std::atomic<float>* inputModeParam    = nullptr;
    std::atomic<float>* throwLevelParam   = nullptr;
    std::atomic<float>* freezeFadeParam   = nullptr;

    double hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryProcessor)
};
