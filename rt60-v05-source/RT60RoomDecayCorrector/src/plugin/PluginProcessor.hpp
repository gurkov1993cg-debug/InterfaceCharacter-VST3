#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/CorrectionEngine.hpp"
#include "core/DecayModel.hpp"

class RT60Processor final : public juce::AudioProcessor {
public:
    RT60Processor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "RT60 Room Decay Corrector"; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    rt60::DecayModel& model() noexcept { return model_; }
    bool reloadActiveProfile();
    int activeStageCount() const noexcept { return static_cast<int>(profile_.stages.size()); }
    bool hasActiveProfile() const noexcept { return !profile_.empty(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    static juce::File activeProfileFile();
    void applyProfileToModel();
    void configureBanks();

    rt60::DecayModel model_;
    rt60::CorrectionProfile profile_;
    rt60::CorrectionFilterBank leftBank_, rightBank_;
    double sampleRate_ = 48000.0;
};
