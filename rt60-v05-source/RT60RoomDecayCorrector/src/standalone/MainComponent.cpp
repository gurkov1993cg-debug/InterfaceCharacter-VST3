#include "standalone/MainComponent.hpp"

#include <algorithm>
#include <cmath>

MainComponent::MainComponent()
    : deviceSelector_(deviceManager_, 1, 2, 1, 2, false, false, true, false)
{
    setSize(1180, 850);
    title_.setText("RT60 ROOM DECAY CORRECTOR v0.4 - INTELLIGENT DECAY MATCH", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title_);

    graph_.setModel(&model_);
    addAndMakeVisible(graph_);
    addAndMakeVisible(deviceSelector_);

    auto setup = [this](juce::Slider& s, double min, double max, double step) {
        s.setRange(min, max, step);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 24);
        addAndMakeVisible(s);
    };
    setup(targetSlider_, 100.0, 1200.0, 5.0);
    setup(strengthSlider_, 25.0, 100.0, 1.0);
    setup(maxCutSlider_, 100.0, 2000.0, 25.0); // repurposed as max correction frequency
    setup(sweepLevelSlider_, -40.0, -6.0, 1.0);
    targetSlider_.setValue(300.0);
    strengthSlider_.setValue(85.0);
    maxCutSlider_.setValue(1200.0);
    sweepLevelSlider_.setValue(-18.0);
    targetSlider_.setTextValueSuffix(" ms");
    strengthSlider_.setTextValueSuffix(" %");
    maxCutSlider_.setTextValueSuffix(" Hz");
    sweepLevelSlider_.setTextValueSuffix(" dBFS");

    targetLabel_.setText("Target RT60", juce::dontSendNotification);
    strengthLabel_.setText("Decay correction strength", juce::dontSendNotification);
    maxCutLabel_.setText("Max correction frequency", juce::dontSendNotification);
    sweepLevelLabel_.setText("Measurement sweep", juce::dontSendNotification);
    for (auto* l : {&targetLabel_, &strengthLabel_, &maxCutLabel_, &sweepLevelLabel_})
        addAndMakeVisible(*l);

    addAndMakeVisible(demoButton_);
    addAndMakeVisible(measureButton_);
    addAndMakeVisible(autoCorrectButton_);
    addAndMakeVisible(remeasureButton_);
    addAndMakeVisible(correctionToggle_);
    addAndMakeVisible(autoTargetToggle_);
    autoTargetToggle_.setToggleState(true, juce::dontSendNotification);
    autoCorrectButton_.setEnabled(false);
    remeasureButton_.setEnabled(false);
    correctionToggle_.setEnabled(false);

    status_.setText("Select microphone input and monitor output, then press MEASURE ROOM.", juce::dontSendNotification);
    addAndMakeVisible(status_);

    targetSlider_.onValueChange = [this] { refreshModel(); };
    strengthSlider_.onValueChange = [this] { refreshModel(); };
    sweepLevelSlider_.onValueChange = [this] { updateMeasurementConfigFromDevice(); };
    demoButton_.onClick = [this] {
        model_.setDemoRoom();
        refreshModel();
        baselineMeasurement_ = {};
        autoCorrectButton_.setEnabled(false);
        remeasureButton_.setEnabled(!correctionProfile_.empty());
        status_.setText("Demo curve loaded. Measure the real room before auto correction.", juce::dontSendNotification);
    };
    measureButton_.onClick = [this] { startMeasurement(false); };
    autoCorrectButton_.onClick = [this] { startAutoCorrection(); };
    remeasureButton_.onClick = [this] { startMeasurement(true); };
    autoTargetToggle_.onClick = [this] {
        targetSlider_.setEnabled(!autoTargetToggle_.getToggleState());
        status_.setText(autoTargetToggle_.getToggleState()
            ? "AUTO TARGET enabled: the first valid room measurement will choose one common decay target."
            : "Manual target enabled.", juce::dontSendNotification);
    };
    targetSlider_.setEnabled(false);
    correctionToggle_.onClick = [this] {
        correctionEnabled_ = correctionToggle_.getToggleState() && !correctionProfile_.empty();
        status_.setText(correctionEnabled_ ? "Correction profile ACTIVE." : "Correction profile BYPASSED.", juce::dontSendNotification);
    };

    const auto error = deviceManager_.initialise(1, 2, nullptr, true);
    if (error.isNotEmpty())
        status_.setText("Audio device error: " + error, juce::dontSendNotification);
    deviceManager_.addAudioCallback(this);
    updateMeasurementConfigFromDevice();
    loadActiveProfile();
    refreshModel();
    startTimerHz(20);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager_.removeAudioCallback(this);
}

juce::File MainComponent::activeProfileFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Room Decay Lab")
        .getChildFile("RT60 Room Decay Corrector")
        .getChildFile("active_profile.rt60");
}

void MainComponent::saveActiveProfile()
{
    if (correctionProfile_.empty())
        return;
    auto file = activeProfileFile();
    file.getParentDirectory().createDirectory();
    const auto encoded = rt60::CorrectionEngine::serialize(correctionProfile_);
    file.replaceWithText(juce::String::fromUTF8(encoded.c_str()));
}

void MainComponent::loadActiveProfile()
{
    auto file = activeProfileFile();
    if (!file.existsAsFile())
        return;
    rt60::CorrectionProfile loaded;
    if (!rt60::CorrectionEngine::deserialize(file.loadFileAsString().toStdString(), loaded) || loaded.empty())
        return;

    correctionProfile_ = std::move(loaded);
    targetSlider_.setValue(correctionProfile_.targetMs, juce::dontSendNotification);
    strengthSlider_.setValue(correctionProfile_.strength * 100.0f, juce::dontSendNotification);
    autoTargetToggle_.setToggleState(correctionProfile_.autoTarget, juce::dontSendNotification);
    targetSlider_.setEnabled(!correctionProfile_.autoTarget);
    for (std::size_t i = 0; i < rt60::kBandCount; ++i) {
        if (correctionProfile_.measuredBandMs[i] > 0.0f)
            model_.setMeasured(i, correctionProfile_.measuredBandMs[i]);
        if (correctionProfile_.targetBandMs[i] > 0.0f)
            model_.setTarget(i, correctionProfile_.targetBandMs[i]);
    }
    configureCorrectionBanks();
    correctionEnabled_ = true;
    correctionToggle_.setEnabled(true);
    correctionToggle_.setToggleState(true, juce::dontSendNotification);
    remeasureButton_.setEnabled(true);
    status_.setText("Loaded active correction profile: " + juce::String((int)correctionProfile_.stages.size()) + " decay stages.", juce::dontSendNotification);
}

void MainComponent::refreshModel()
{
    model_.setUniformTarget(static_cast<float>(targetSlider_.getValue()));
    model_.setStrength(static_cast<float>(strengthSlider_.getValue()) / 100.0f);
    model_.setMaxCutDb(18.0f);
    graph_.repaint();
}

void MainComponent::updateMeasurementConfigFromDevice()
{
    auto cfg = measurement_.config();
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        cfg.sampleRate = device->getCurrentSampleRate();
    cfg.level = std::pow(10.0f, static_cast<float>(sweepLevelSlider_.getValue()) / 20.0f);
    measurement_.setConfig(cfg);
}

void MainComponent::configureCorrectionBanks()
{
    double sampleRate = measurement_.config().sampleRate;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        sampleRate = device->getCurrentSampleRate();
    correctionLeft_.configure(correctionProfile_, sampleRate);
    correctionRight_.configure(correctionProfile_, sampleRate);
}

void MainComponent::startMeasurement(bool afterCorrection)
{
    if (measuring_.load())
        return;
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        status_.setText("No active audio device.", juce::dontSendNotification);
        return;
    }
    if (afterCorrection && (correctionProfile_.empty() || !correctionEnabled_)) {
        status_.setText("No active correction. Measure the room and press AUTO CORRECT RT60 first.", juce::dontSendNotification);
        return;
    }

    updateMeasurementConfigFromDevice();
    excitation_ = measurement_.makeExcitation();
    capture_.assign(excitation_.size(), 0.0f);
    measurementPosition_.store(0);
    measurementFinished_.store(false);
    measurementIsAfter_ = afterCorrection;
    if (afterCorrection) {
        correctionLeft_.reset();
        correctionRight_.reset();
    }
    measuring_.store(true);
    measureButton_.setEnabled(false);
    autoCorrectButton_.setEnabled(false);
    remeasureButton_.setEnabled(false);
    demoButton_.setEnabled(false);
    correctionToggle_.setEnabled(false);
    autoTargetToggle_.setEnabled(false);
    status_.setText(afterCorrection
        ? "Measuring THROUGH active decay correction... keep the room quiet."
        : "Measuring uncorrected room... keep the room quiet.", juce::dontSendNotification);
}

void MainComponent::startAutoCorrection()
{
    if (!baselineMeasurement_.valid || measuring_.load()) {
        status_.setText("Run a valid MEASURE ROOM measurement first.", juce::dontSendNotification);
        return;
    }

    // Preserve the user's previous accepted profile until a newly measured
    // pass proves that the replacement is genuinely better.
    profileBeforeAuto_ = correctionProfile_;
    correctionWasEnabledBeforeAuto_ = correctionEnabled_;
    bestCorrectionProfile_ = {};
    bestVerifiedMeasurement_ = {};
    bestVerifiedReport_ = {};
    haveBestVerifiedPass_ = false;
    bestVerifiedPass_ = 0;

    const auto baselineReport = evaluateUniformity(baselineMeasurement_);
    baselineObjectiveMs_ = baselineReport.correctionObjectiveMs;
    bestObjectiveMs_ = baselineObjectiveMs_;

    rt60::CorrectionDesignConfig cfg;
    cfg.targetMs = static_cast<float>(targetSlider_.getValue());
    cfg.autoTarget = autoTargetToggle_.getToggleState();
    cfg.strength = static_cast<float>(strengthSlider_.getValue()) / 100.0f;
    cfg.maxCorrectionHz = static_cast<float>(maxCutSlider_.getValue());
    cfg.minConfidence = 0.45f;
    cfg.maxT60ReductionPerPass = 0.55f;
    cfg.maxNewStages = 18;

    correctionProfile_ = correctionEngine_.design(baselineMeasurement_, cfg);
    targetSlider_.setValue(correctionProfile_.targetMs, juce::dontSendNotification);
    refreshModel();
    if (correctionProfile_.empty()) {
        status_.setText("No trusted long-decay modes need active correction at the selected target.", juce::dontSendNotification);
        return;
    }

    rt60::CorrectionOptimizationConfig optCfg;
    optCfg.rounds = 2;
    optCfg.zeroGridPoints = 7;
    optCfg.desiredGridPoints = 7;
    digitalTwinScore_ = rt60::CorrectionEngine::optimizeAgainstMeasurement(
        correctionProfile_, baselineMeasurement_, measurement_, optCfg);

    configureCorrectionBanks();
    // Deliberately do NOT overwrite the persisted profile yet. The first
    // corrected sweep has to verify a real acoustic improvement first.
    correctionEnabled_ = true;
    correctionToggle_.setToggleState(true, juce::dontSendNotification);
    correctionToggle_.setEnabled(true);
    model_.clearMeasuredAfter();
    autoCorrecting_ = true;
    autoIteration_ = 1;
    status_.setText("Digital-twin correction optimized (model score " + juce::String(digitalTwinScore_, 0)
                    + "%). Starting REAL re-measurement through the correction...", juce::dontSendNotification);
    startMeasurement(true);
}

rt60::DecayUniformityReport MainComponent::evaluateUniformity(const rt60::MeasurementResult& result) const
{
    rt60::DecayUniformityConfig cfg;
    cfg.maxHz = static_cast<float>(maxCutSlider_.getValue());
    cfg.minConfidence = 0.45f;
    cfg.toleranceFraction = 0.10f;
    cfg.requiredWithinFraction = 0.90f;
    return rt60::DecayUniformity::evaluate(baselineMeasurement_, result,
                                           static_cast<float>(targetSlider_.getValue()), cfg);
}

void MainComponent::applyRecommendedTarget(const rt60::MeasurementResult& result)
{
    if (!autoTargetToggle_.getToggleState())
        return;
    rt60::DecayUniformityConfig cfg;
    cfg.maxHz = static_cast<float>(maxCutSlider_.getValue());
    cfg.minConfidence = 0.45f;
    const float target = rt60::DecayUniformity::chooseCommonTarget(result, cfg);
    targetSlider_.setValue(target, juce::dontSendNotification);
    refreshModel();
}

void MainComponent::restoreBestVerifiedPass()
{
    if (!haveBestVerifiedPass_)
        return;

    correctionProfile_ = bestCorrectionProfile_;
    latestMeasurement_ = bestVerifiedMeasurement_;
    configureCorrectionBanks();
    correctionEnabled_ = true;
    correctionToggle_.setToggleState(true, juce::dontSendNotification);
    correctionToggle_.setEnabled(true);
    for (std::size_t i = 0; i < bestVerifiedMeasurement_.bands.size(); ++i) {
        const auto& b = bestVerifiedMeasurement_.bands[i];
        if (b.valid)
            model_.setMeasuredAfter(i, b.rt60Ms);
    }
    refreshModel();
}

void MainComponent::abortUnhelpfulAutoCorrection(const juce::String& reason)
{
    autoCorrecting_ = false;

    // No newly generated profile earned verification. Restore exactly what
    // the user had before AUTO was started, and never persist the rejected
    // candidate.
    correctionProfile_ = profileBeforeAuto_;
    correctionEnabled_ = correctionWasEnabledBeforeAuto_ && !correctionProfile_.empty();
    configureCorrectionBanks();
    correctionToggle_.setToggleState(correctionEnabled_, juce::dontSendNotification);
    correctionToggle_.setEnabled(!correctionProfile_.empty());
    model_.clearMeasuredAfter();
    refreshModel();

    measureButton_.setEnabled(true);
    demoButton_.setEnabled(true);
    autoCorrectButton_.setEnabled(baselineMeasurement_.valid);
    remeasureButton_.setEnabled(!correctionProfile_.empty() && correctionEnabled_);
    autoTargetToggle_.setEnabled(true);
    status_.setText("AUTO stopped safely: " + reason
                    + " | rejected pass was NOT saved.", juce::dontSendNotification);
}

void MainComponent::finishAutoCorrection(const rt60::MeasurementResult& result,
                                         bool hitIterationLimit,
                                         const juce::String& stopReason)
{
    const auto report = evaluateUniformity(result);
    autoCorrecting_ = false;
    if (!correctionProfile_.empty())
        saveActiveProfile();
    correctionToggle_.setEnabled(true);
    correctionToggle_.setToggleState(true, juce::dontSendNotification);
    correctionEnabled_ = true;

    juce::String msg = "AUTO decay match complete | target " + juce::String(report.targetMs, 0) + " ms | "
                     + juce::String(report.withinCorrectableBands) + "/" + juce::String(report.correctableBands)
                     + " correctable bands matched | spread " + juce::String(report.spreadMs, 0)
                     + " ms | REAL score " + juce::String(report.uniformityScore, 0) + "% | model "
                     + juce::String(digitalTwinScore_, 0) + "% | "
                     + juce::String((int)correctionProfile_.stages.size()) + " stages";
    if (bestVerifiedPass_ > 0)
        msg += " | best measured pass " + juce::String(bestVerifiedPass_)
             + " | objective " + juce::String(report.correctionObjectiveMs, 1) + " ms";
    if (hitIterationLimit)
        msg += " | maximum verification passes reached";
    if (stopReason.isNotEmpty())
        msg += " | " + stopReason;
    status_.setText(msg, juce::dontSendNotification);
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

    if (!measuring_.load(std::memory_order_acquire))
        return;

    std::size_t pos = measurementPosition_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i) {
        if (pos >= excitation_.size()) {
            measuring_.store(false, std::memory_order_release);
            measurementFinished_.store(true, std::memory_order_release);
            break;
        }

        const float raw = excitation_[pos];
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] == nullptr)
                continue;
            float out = raw;
            if (measurementIsAfter_ && correctionEnabled_)
                out = (ch == 0 ? correctionLeft_ : correctionRight_).processSample(out);
            outputChannelData[ch][i] = out;
        }

        float in = 0.0f;
        int validInputs = 0;
        for (int ch = 0; ch < numInputChannels; ++ch) {
            if (inputChannelData[ch] != nullptr) {
                in += inputChannelData[ch][i];
                ++validInputs;
            }
        }
        if (validInputs > 0)
            in /= static_cast<float>(validInputs);
        capture_[pos] = in;
        ++pos;
    }
    measurementPosition_.store(pos, std::memory_order_release);
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr) {
        auto cfg = measurement_.config();
        cfg.sampleRate = device->getCurrentSampleRate();
        measurement_.setConfig(cfg);
        if (!correctionProfile_.empty())
            configureCorrectionBanks();
    }
}

void MainComponent::audioDeviceStopped()
{
    measuring_.store(false);
}

void MainComponent::timerCallback()
{
    if (measuring_.load()) {
        const auto total = std::max<std::size_t>(1, excitation_.size());
        const auto current = measurementPosition_.load();
        const int percent = static_cast<int>(100.0 * static_cast<double>(current) / static_cast<double>(total));
        status_.setText((measurementIsAfter_ ? "Corrected verification" : "Room measurement")
                        + juce::String(" in progress: ") + juce::String(percent) + "%", juce::dontSendNotification);
    }
    if (measurementFinished_.exchange(false))
        finishMeasurementOnMessageThread();
}

void MainComponent::finishMeasurementOnMessageThread()
{
    status_.setText("Analysing impulse response and RT60 decay...", juce::dontSendNotification);
    const auto result = measurement_.analyseCapture(capture_);
    latestMeasurement_ = result;

    int validBands = 0;
    for (std::size_t i = 0; i < result.bands.size(); ++i) {
        if (!result.bands[i].valid)
            continue;
        ++validBands;
        if (measurementIsAfter_)
            model_.setMeasuredAfter(i, result.bands[i].rt60Ms);
        else
            model_.setMeasured(i, result.bands[i].rt60Ms);
    }
    refreshModel();

    if (!measurementIsAfter_) {
        baselineMeasurement_ = result;
        model_.clearMeasuredAfter();
        if (result.valid)
            applyRecommendedTarget(result);
        autoCorrectButton_.setEnabled(result.valid);
    }

    if (measurementIsAfter_ && autoCorrecting_) {
        const auto report = evaluateUniformity(result);

        const float referenceObjective = haveBestVerifiedPass_ ? bestObjectiveMs_ : baselineObjectiveMs_;
        const float currentObjective = report.correctionObjectiveMs;
        const float meaningfulDelta = std::max(6.0f, referenceObjective * 0.03f);
        const float regressionDelta = std::max(10.0f, referenceObjective * 0.06f);
        const float improvement = referenceObjective - currentObjective;
        const bool verificationReliable = report.correctableBands == 0
            || report.unreliableCorrectableBands == 0;
        const bool materiallyBetter = verificationReliable && improvement >= meaningfulDelta;
        const bool regressed = currentObjective > referenceObjective + regressionDelta
            || (haveBestVerifiedPass_
                && report.overshotCorrectableBands > bestVerifiedReport_.overshotCorrectableBands);

        if (!haveBestVerifiedPass_) {
            if (!verificationReliable) {
                abortUnhelpfulAutoCorrection("verification lost trusted decay bands; improve measurement SNR");
                return;
            }
            if (!report.converged && !materiallyBetter) {
                abortUnhelpfulAutoCorrection("first corrected pass did not produce a meaningful measured improvement");
                return;
            }

            bestCorrectionProfile_ = correctionProfile_;
            bestVerifiedMeasurement_ = result;
            bestVerifiedReport_ = report;
            bestObjectiveMs_ = currentObjective;
            bestVerifiedPass_ = autoIteration_;
            haveBestVerifiedPass_ = true;
            saveActiveProfile();
        } else if (regressed) {
            restoreBestVerifiedPass();
            finishAutoCorrection(bestVerifiedMeasurement_, false,
                "rollback: latest pass regressed, restored best measured profile");
            return;
        } else if (materiallyBetter || report.converged) {
            bestCorrectionProfile_ = correctionProfile_;
            bestVerifiedMeasurement_ = result;
            bestVerifiedReport_ = report;
            bestObjectiveMs_ = currentObjective;
            bestVerifiedPass_ = autoIteration_;
            saveActiveProfile();
        } else {
            restoreBestVerifiedPass();
            finishAutoCorrection(bestVerifiedMeasurement_, false,
                "stopped: further pass gave no meaningful measured improvement");
            return;
        }

        if (report.converged) {
            finishAutoCorrection(result, false, "target tolerance reached with verified measurement");
        } else if (autoIteration_ >= kMaxAutoIterations) {
            restoreBestVerifiedPass();
            finishAutoCorrection(bestVerifiedMeasurement_, true,
                "kept best verified result instead of forcing the target");
        } else {
            const bool retuned = rt60::CorrectionEngine::refineProfile(correctionProfile_, result, 0.28f, 0.08f);

            rt60::CorrectionDesignConfig cfg;
            cfg.targetMs = correctionProfile_.targetMs;
            cfg.autoTarget = false; // freeze the baseline target
            cfg.strength = std::min(0.65f, static_cast<float>(strengthSlider_.getValue()) / 100.0f);
            cfg.maxCorrectionHz = static_cast<float>(maxCutSlider_.getValue());
            cfg.minConfidence = 0.50f;
            cfg.maxT60ReductionPerPass = 0.30f;
            cfg.maxNewStages = 6;
            auto incremental = correctionEngine_.design(result, cfg);
            const auto beforeCount = correctionProfile_.stages.size();
            rt60::CorrectionEngine::appendIncremental(correctionProfile_, incremental, 36, 1);
            const bool addedNewMode = correctionProfile_.stages.size() != beforeCount;

            if (!retuned && !addedNewMode) {
                restoreBestVerifiedPass();
                finishAutoCorrection(bestVerifiedMeasurement_, false,
                    "stopped: no safe high-confidence correction change remained");
            } else {
                configureCorrectionBanks();
                saveActiveProfile();
                ++autoIteration_;
                status_.setText("Feedback pass " + juce::String(autoIteration_) + ": "
                                + juce::String(report.withinCorrectableBands) + "/"
                                + juce::String(report.correctableBands) + " matched, spread "
                                + juce::String(report.spreadMs, 0) + " ms. Retuning measured modes...",
                                juce::dontSendNotification);
                startMeasurement(true);
                return;
            }
        }
    }

    measureButton_.setEnabled(true);
    demoButton_.setEnabled(true);
    autoCorrectButton_.setEnabled(baselineMeasurement_.valid && !autoCorrecting_);
    remeasureButton_.setEnabled(!correctionProfile_.empty() && correctionEnabled_ && !autoCorrecting_);
    correctionToggle_.setEnabled(!correctionProfile_.empty() && !autoCorrecting_);
    autoTargetToggle_.setEnabled(!autoCorrecting_);

    if (!autoCorrecting_) {
        juce::String message = "Measured " + juce::String(validBands) + " RT60 bands | peak "
                             + juce::String(result.peakDbFs, 1) + " dBFS";
        if (result.clipped)
            message += " | WARNING: input clipped - lower sweep or mic gain";
        else if (validBands < 8)
            message += " | too little usable decay data; raise SNR or extend tail";
        else if (!measurementIsAfter_) {
            int trusted = 0;
            for (const auto& b : result.bands) if (b.valid && b.confidence >= 0.45f) ++trusted;
            message += " | trusted bands " + juce::String(trusted)
                    + " | common target " + juce::String((int)targetSlider_.getValue()) + " ms"
                    + " | ready for INTELLIGENT AUTO";
        }
        else if (!status_.getText().startsWith("AUTO decay match"))
            message += " | corrected Measured After updated";
        if (!status_.getText().startsWith("AUTO decay match"))
            status_.setText(message, juce::dontSendNotification);
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 20));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced(20);
    title_.setBounds(r.removeFromTop(42));

    auto top = r.removeFromTop(500);
    auto graphArea = top.removeFromLeft(static_cast<int>(top.getWidth() * 0.75f));
    graph_.setBounds(graphArea);
    top.removeFromLeft(12);
    deviceSelector_.setBounds(top);

    r.removeFromTop(10);
    auto setRow = [](juce::Rectangle<int> row, juce::Label& label, juce::Slider& slider) {
        label.setBounds(row.removeFromLeft(210));
        slider.setBounds(row);
    };
    setRow(r.removeFromTop(36), targetLabel_, targetSlider_);
    setRow(r.removeFromTop(36), strengthLabel_, strengthSlider_);
    setRow(r.removeFromTop(36), maxCutLabel_, maxCutSlider_);
    setRow(r.removeFromTop(36), sweepLevelLabel_, sweepLevelSlider_);

    auto buttons = r.removeFromTop(40);
    demoButton_.setBounds(buttons.removeFromLeft(140));
    buttons.removeFromLeft(8);
    measureButton_.setBounds(buttons.removeFromLeft(180));
    buttons.removeFromLeft(8);
    autoCorrectButton_.setBounds(buttons.removeFromLeft(200));
    buttons.removeFromLeft(8);
    remeasureButton_.setBounds(buttons.removeFromLeft(175));
    buttons.removeFromLeft(8);
    autoTargetToggle_.setBounds(buttons.removeFromLeft(140));
    buttons.removeFromLeft(8);
    correctionToggle_.setBounds(buttons.removeFromLeft(180));
    status_.setBounds(r.removeFromTop(34));
}
