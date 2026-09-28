#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

EchoFactoryProcessor::EchoFactoryProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EchoFactoryState", Params::createLayout())
{
    delayTimeParam    = apvts.getRawParameterValue (Params::ID::delayTimeMs.getParamID());
    smoothingParam    = apvts.getRawParameterValue (Params::ID::timeSmoothingMs.getParamID());
    syncParam         = apvts.getRawParameterValue (Params::ID::sync.getParamID());
    syncDivisionParam = apvts.getRawParameterValue (Params::ID::syncDivision.getParamID());
    feedbackParam     = apvts.getRawParameterValue (Params::ID::feedback.getParamID());
    mixParam          = apvts.getRawParameterValue (Params::ID::mix.getParamID());
    lowCutParam       = apvts.getRawParameterValue (Params::ID::lowCut.getParamID());
    highCutParam      = apvts.getRawParameterValue (Params::ID::highCut.getParamID());
    filterPosParam    = apvts.getRawParameterValue (Params::ID::filterPos.getParamID());
    freezeParam       = apvts.getRawParameterValue (Params::ID::freeze.getParamID());
    pingPongParam     = apvts.getRawParameterValue (Params::ID::pingPong.getParamID());
    widthParam        = apvts.getRawParameterValue (Params::ID::stereoWidthMs.getParamID());
}

bool EchoFactoryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void EchoFactoryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels(), Params::maxDelayMs, Params::maxStereoWidthMs);
    updateEngineParameters();
    engine.reset(); // jump straight to the current settings, no glide on start-up
}

void EchoFactoryProcessor::reset()
{
    engine.reset();
}

float EchoFactoryProcessor::getTargetDelayMs() const
{
    if (syncParam->load() < 0.5f)
        return delayTimeParam->load();

    const auto& divisions = Params::getSyncDivisions();
    const auto index = juce::jlimit (0, (int) divisions.size() - 1, juce::roundToInt (syncDivisionParam->load()));
    const auto ms = divisions[(size_t) index].beats * 60000.0 / hostBpm;

    return juce::jlimit (Params::minDelayMs, Params::maxDelayMs, (float) ms);
}

void EchoFactoryProcessor::updateEngineParameters()
{
    engine.setDelaySmoothingMs (smoothingParam->load());
    engine.setDelayMs (getTargetDelayMs());
    engine.setFeedback (feedbackParam->load() * 0.01f);
    engine.setMix (mixParam->load() * 0.01f);

    // The range extremes mean "Off" (0 bypasses the filter).
    const auto lowCut  = lowCutParam->load();
    const auto highCut = highCutParam->load();
    engine.setLowCutHz  (lowCut  <= Params::lowCutMinHz  ? 0.0f : lowCut);
    engine.setHighCutHz (highCut >= Params::highCutMaxHz ? 0.0f : highCut);
    engine.setFiltersInFeedbackLoop (juce::roundToInt (filterPosParam->load()) == (int) Params::FilterPosition::inFeedbackLoop);
    engine.setFrozen (freezeParam->load() >= 0.5f);
    engine.setPingPong (pingPongParam->load() >= 0.5f);
    engine.setStereoWidthMs (widthParam->load());
}

void EchoFactoryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;

    updateEngineParameters();
    engine.process (buffer);
}

juce::AudioProcessorEditor* EchoFactoryProcessor::createEditor()
{
    return new EchoFactoryEditor (*this);
}

void EchoFactoryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EchoFactoryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EchoFactoryProcessor();
}
