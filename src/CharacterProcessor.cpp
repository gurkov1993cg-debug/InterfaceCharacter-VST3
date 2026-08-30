#include "CharacterProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace interface_character {
namespace {

constexpr float kPi = 3.14159265358979323846f;

constexpr std::array<ProfileSpec, 4> kProfiles{{
    {
        ProfileId::UniversalAudioApollo,
        "UAD Apollo",
        "Apollo/UAD-style analog front-end starting profile",
        0.12f, -0.18f, 90.0f, 14500.0f,
        0.12f, 0.018f, 0.010f, 0.018f, 0.16f, 0.00020f, 0, 12.0f
    },
    {
        ProfileId::ProToolsTdm88824,
        "Pro Tools TDM / 888|24",
        "Legacy TDM-style fixed-point and converter starting profile",
        0.00f, -0.05f, 80.0f, 15500.0f,
        0.012f, 0.000f, 0.003f, 0.000f, 0.02f, 0.00010f, 24, 0.0f
    },
    {
        ProfileId::ProToolsHdxHdIo,
        "Pro Tools HDX / HD I/O",
        "High-headroom HDX/HD I/O starting profile",
        0.00f, -0.02f, 80.0f, 17000.0f,
        0.004f, 0.000f, 0.001f, 0.000f, 0.01f, 0.00004f, 0, 15.0f
    },
    {
        ProfileId::PrismSoundLyra1,
        "Prism Sound Lyra 1",
        "Reference/transparent Prism converter starting profile",
        0.00f, 0.00f, 80.0f, 18000.0f,
        0.001f, 0.000f, 0.0002f, 0.000f, 0.005f, 0.00002f, 0, 18.0f
    },
}};

float clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

float dbToGain(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

float safeAlpha(float frequency, double sampleRate) noexcept
{
    const auto sr = static_cast<float>(std::max(1000.0, sampleRate));
    const auto hz = std::clamp(frequency, 1.0f, sr * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

} // namespace

const ProfileSpec& getProfileSpec(ProfileId id) noexcept
{
    for (const auto& profile : kProfiles) {
        if (profile.id == id)
            return profile;
    }
    return kProfiles.front();
}

CharacterProcessor::CharacterProcessor()
{
    prepare(sampleRate_);
}

void CharacterProcessor::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1000.0, sampleRate);
    const auto& spec = getProfileSpec(parameters_.profile);
    toneLowAlpha_ = safeAlpha(spec.lowShelfHz, sampleRate_);
    toneHighAlpha_ = safeAlpha(spec.highShelfHz, sampleRate_);
    dcAlpha_ = safeAlpha(5.0f, sampleRate_);
    reset();
}

void CharacterProcessor::reset() noexcept
{
    left_ = {};
    right_ = {};
}

void CharacterProcessor::setParameters(const Parameters& parameters) noexcept
{
    parameters_ = parameters;
    const auto& spec = getProfileSpec(parameters_.profile);
    toneLowAlpha_ = safeAlpha(spec.lowShelfHz, sampleRate_);
    toneHighAlpha_ = safeAlpha(spec.highShelfHz, sampleRate_);
}

float CharacterProcessor::applyTone(float input, ChannelState& state,
                                    const ProfileSpec& spec) const noexcept
{
    // One-pole low/high shelves are sufficient for the first profiling pass.
    // They will later be replaced by measured minimum-phase curves.
    state.lowState += toneLowAlpha_ * (input - state.lowState);
    float output = input + (dbToGain(spec.lowShelfDb) - 1.0f) * state.lowState;

    state.highState += toneHighAlpha_ * (output - state.highState);
    const float highPart = output - state.highState;
    output += (dbToGain(spec.highShelfDb) - 1.0f) * highPart;
    return output;
}

float CharacterProcessor::dcBlock(float input, ChannelState& state) const noexcept
{
    state.dcState += dcAlpha_ * (input - state.dcState);
    return input - state.dcState;
}

float CharacterProcessor::quantizeIfNeeded(float input,
                                           const ProfileSpec& spec) const noexcept
{
    if (spec.quantizationBits <= 0)
        return input;

    const float scale = std::ldexp(1.0f, spec.quantizationBits - 1);
    return std::round(input * scale) / scale;
}

float CharacterProcessor::processSample(float input, ChannelState& state,
                                        const ProfileSpec& spec) noexcept
{
    const float amount = clamp01(parameters_.amount);
    const float driveGain = dbToGain(parameters_.driveDb);

    float signal = applyTone(input, state, spec);

    // A slowly changing envelope gives the nonlinear stage a small amount of
    // memory.  This prevents the prototype from behaving like a static EQ
    // followed by a generic waveshaper.
    const float envelopeTarget = std::abs(signal);
    const float envelopeAlpha = safeAlpha(12.0f, sampleRate_);
    state.envelope += envelopeAlpha * (envelopeTarget - state.envelope);
    const float memoryGain = 1.0f + spec.memory * state.envelope;

    const float curvature = 1.0f + 3.0f * spec.saturation;
    const float driven = signal * driveGain * memoryGain;
    const float normalized = std::tanh(driven * curvature)
                           / (curvature * std::max(0.001f, driveGain));

    float shaped = signal + amount * spec.saturation * (normalized - signal);

    // Even harmonics model asymmetry.  The DC blocker below removes the
    // static offset while retaining the audible level-dependent movement.
    const float nonlinearInput = signal * driveGain;
    shaped += amount * (spec.secondHarmonic * nonlinearInput * nonlinearInput
                      + spec.thirdHarmonic * nonlinearInput * nonlinearInput
                                               * nonlinearInput);
    shaped = dcBlock(shaped, state);

    const float clipThreshold = dbToGain(spec.clipThresholdDb);
    if (clipThreshold > 0.0f && std::abs(shaped) > clipThreshold) {
        shaped = clipThreshold * std::tanh(shaped / clipThreshold);
    }

    shaped = quantizeIfNeeded(shaped, spec);
    state.previousInput = signal;

    const float wet = clamp01(parameters_.mix);
    const float outputGain = dbToGain(parameters_.outputDb);
    return (input + wet * (shaped - input)) * outputGain;
}

void CharacterProcessor::processMono(float* samples,
                                     std::size_t numSamples) noexcept
{
    if (samples == nullptr || numSamples == 0)
        return;

    if (parameters_.bypass || parameters_.mix <= 0.0f)
        return;

    const auto& spec = getProfileSpec(parameters_.profile);
    for (std::size_t i = 0; i < numSamples; ++i)
        samples[i] = processSample(samples[i], left_, spec);
}

void CharacterProcessor::processStereo(float* left, float* right,
                                       std::size_t numSamples) noexcept
{
    if (left == nullptr || numSamples == 0)
        return;

    if (right == nullptr) {
        processMono(left, numSamples);
        return;
    }

    if (parameters_.bypass || parameters_.mix <= 0.0f)
        return;

    const auto& spec = getProfileSpec(parameters_.profile);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float wetLeft = processSample(left[i], left_, spec);
        const float wetRight = processSample(right[i], right_, spec);
        const float crossfeed = clamp01(spec.crossfeed);

        left[i] = wetLeft * (1.0f - crossfeed) + wetRight * crossfeed;
        right[i] = wetRight * (1.0f - crossfeed) + wetLeft * crossfeed;
    }
}

void CharacterProcessor::processMono(double* samples,
                                     std::size_t numSamples) noexcept
{
    if (samples == nullptr || numSamples == 0)
        return;

    if (parameters_.bypass || parameters_.mix <= 0.0f)
        return;

    const auto& spec = getProfileSpec(parameters_.profile);
    for (std::size_t i = 0; i < numSamples; ++i)
        samples[i] = static_cast<double>(processSample(static_cast<float>(samples[i]), left_, spec));
}

void CharacterProcessor::processStereo(double* left, double* right,
                                       std::size_t numSamples) noexcept
{
    if (left == nullptr || numSamples == 0)
        return;

    if (right == nullptr) {
        processMono(left, numSamples);
        return;
    }

    if (parameters_.bypass || parameters_.mix <= 0.0f)
        return;

    const auto& spec = getProfileSpec(parameters_.profile);
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float wetLeft = processSample(static_cast<float>(left[i]), left_, spec);
        const float wetRight = processSample(static_cast<float>(right[i]), right_, spec);
        const float crossfeed = clamp01(spec.crossfeed);

        left[i] = static_cast<double>(wetLeft * (1.0f - crossfeed) + wetRight * crossfeed);
        right[i] = static_cast<double>(wetRight * (1.0f - crossfeed) + wetLeft * crossfeed);
    }
}

} // namespace interface_character
