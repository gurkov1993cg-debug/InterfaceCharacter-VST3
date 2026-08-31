#include "plugin/PluginProcessor.hpp"
#include "plugin/PluginEditor.hpp"

juce::AudioProcessorValueTreeState::ParameterLayout RT60Processor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    return {p.begin(), p.end()};
}

RT60Processor::RT60Processor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createParameters())
{
    model_.setDemoRoom();
}

bool RT60Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}

juce::File RT60Processor::activeProfileFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Room Decay Lab")
        .getChildFile("RT60 Room Decay Corrector")
        .getChildFile("active_profile.rt60");
}

void RT60Processor::applyProfileToModel()
{
    if (profile_.empty())
        return;
    model_.setStrength(profile_.strength);
    model_.setMaxCutDb(18.0f);
    for (std::size_t i = 0; i < rt60::kBandCount; ++i) {
        if (profile_.measuredBandMs[i] > 0.0f)
            model_.setMeasured(i, profile_.measuredBandMs[i]);
        if (profile_.targetBandMs[i] > 0.0f)
            model_.setTarget(i, profile_.targetBandMs[i]);
    }
}

void RT60Processor::configureBanks()
{
    leftBank_.configure(profile_, sampleRate_);
    rightBank_.configure(profile_, sampleRate_);
}

bool RT60Processor::reloadActiveProfile()
{
    const auto file = activeProfileFile();
    if (!file.existsAsFile()) {
        profile_ = {};
        configureBanks();
        return false;
    }

    rt60::CorrectionProfile loaded;
    if (!rt60::CorrectionEngine::deserialize(file.loadFileAsString().toStdString(), loaded) || loaded.empty()) {
        profile_ = {};
        configureBanks();
        return false;
    }

    profile_ = std::move(loaded);
    applyProfileToModel();
    configureBanks();
    return true;
}

void RT60Processor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    reloadActiveProfile();
}

void RT60Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (apvts.getRawParameterValue("bypass")->load() > 0.5f || profile_.empty())
        return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* data = buffer.getWritePointer(ch);
        if (ch == 0)
            leftBank_.process(data, buffer.getNumSamples());
        else
            rightBank_.process(data, buffer.getNumSamples());
    }
}

juce::AudioProcessorEditor* RT60Processor::createEditor() { return new RT60Editor(*this); }

void RT60Processor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void RT60Processor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size)) apvts.replaceState(juce::ValueTree::fromXml(*xml));
    reloadActiveProfile();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RT60Processor(); }
