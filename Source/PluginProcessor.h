#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayEngine.h"
#include "PresetManager.h"

class EchoFactoryProcessor : public juce::AudioProcessor
{
public:
    EchoFactoryProcessor();
    ~EchoFactoryProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 5.0; }

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

private:
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

    double hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryProcessor)
};
