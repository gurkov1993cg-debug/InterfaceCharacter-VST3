#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "core/CorrectionEngine.hpp"
#include "core/DecayUniformity.hpp"
#include "core/DecayModel.hpp"
#include "core/MeasurementEngine.hpp"
#include "ui/RT60Graph.hpp"

#include <atomic>
#include <vector>

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshModel();
    void startMeasurement(bool afterCorrection);
    void finishMeasurementOnMessageThread();
    void updateMeasurementConfigFromDevice();
    void startAutoCorrection();
    void configureCorrectionBanks();
    void saveActiveProfile();
    void loadActiveProfile();
    void finishAutoCorrection(const rt60::MeasurementResult& result,
                              bool hitIterationLimit,
                              const juce::String& stopReason = {});
    void restoreBestVerifiedPass();
    void abortUnhelpfulAutoCorrection(const juce::String& reason);
    rt60::DecayUniformityReport evaluateUniformity(const rt60::MeasurementResult& result) const;
    void applyRecommendedTarget(const rt60::MeasurementResult& result);
    static juce::File activeProfileFile();

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void timerCallback() override;

    rt60::DecayModel model_;
    rt60::MeasurementEngine measurement_;
    rt60::CorrectionEngine correctionEngine_;
    rt60::CorrectionProfile correctionProfile_;
    rt60::CorrectionProfile profileBeforeAuto_;
    rt60::CorrectionProfile bestCorrectionProfile_;
    rt60::CorrectionFilterBank correctionLeft_, correctionRight_;
    rt60::MeasurementResult baselineMeasurement_;
    rt60::MeasurementResult latestMeasurement_;
    rt60::MeasurementResult bestVerifiedMeasurement_;
    rt60::DecayUniformityReport bestVerifiedReport_;
    RT60Graph graph_;

    juce::AudioDeviceManager deviceManager_;
    juce::AudioDeviceSelectorComponent deviceSelector_;

    juce::Slider targetSlider_;
    juce::Slider strengthSlider_;
    juce::Slider maxCutSlider_;
    juce::Slider sweepLevelSlider_;
    juce::Label title_;
    juce::Label targetLabel_, strengthLabel_, maxCutLabel_, sweepLevelLabel_, status_;
    juce::TextButton demoButton_{"LOAD DEMO"};
    juce::TextButton measureButton_{"MEASURE ROOM"};
    juce::TextButton autoCorrectButton_{"AUTO CORRECT RT60"};
    juce::TextButton remeasureButton_{"RE-MEASURE AFTER"};
    juce::ToggleButton correctionToggle_{"CORRECTION ACTIVE"};
    juce::ToggleButton autoTargetToggle_{"AUTO TARGET"};

    std::vector<float> excitation_;
    std::vector<float> capture_;
    std::atomic<std::size_t> measurementPosition_{0};
    std::atomic<bool> measuring_{false};
    std::atomic<bool> measurementFinished_{false};
    bool measurementIsAfter_ = false;
    bool autoCorrecting_ = false;
    bool correctionEnabled_ = false;
    bool correctionWasEnabledBeforeAuto_ = false;
    bool haveBestVerifiedPass_ = false;
    int autoIteration_ = 0;
    int bestVerifiedPass_ = 0;
    float digitalTwinScore_ = 0.0f;
    float baselineObjectiveMs_ = 0.0f;
    float bestObjectiveMs_ = 0.0f;
    static constexpr int kMaxAutoIterations = 6;
};
