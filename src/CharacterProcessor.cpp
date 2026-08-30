#include "CharacterProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace interface_character {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLyraLfMinus3Hz = 0.85f;
constexpr float kLyraNoiseRms = 1.7782794e-6f; // -115 dBFS RMS
constexpr float kLyraTpdfScale = 2.44948974f; // sqrt(6), unit TPDF -> requested RMS

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
        "Lyra 1 line-output/DAC spec model; published limits plus conservative harmonic allocation",
        0.00f, 0.00f, 80.0f, 18000.0f,
        // Published output THD is -107 dB at -0.1 dBFS. Prism does not
        // publish the harmonic split, so the tiny H2/H3 allocation below is
        // deliberately conservative and replaceable when a real unit is measured.
        0.0f, 2.0e-6f, -1.78e-5f, 0.0f, 0.0f,
        // -135 dB at 1 kHz, converted to linear voltage ratio.
        1.7782794e-7f,
        0, 0.0f
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

float lyraHfMinus3Hz(double sampleRate) noexcept
{
    const float sr = static_cast<float>(std::max(1000.0, sampleRate));

    // Prism publishes -3 dB points for 44.1/48/96/192 kHz. Standard
    // intermediate rates are scaled within the corresponding converter family.
    float cutoff = 0.0f;
    if (sr <= 50000.0f) {
        const float t = std::clamp((sr - 44100.0f) / (48000.0f - 44100.0f), 0.0f, 1.0f);
        cutoff = 22000.0f + t * (23900.0f - 22000.0f);
    } else if (sr <= 110000.0f) {
        cutoff = sr * (47800.0f / 96000.0f);
    } else {
        cutoff = sr * (76000.0f / 192000.0f);
    }

    return std::clamp(cutoff, 100.0f, sr * 0.499f);
}

float nextUniform01(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
}

} // namespace

const ProfileSpec& getProfileSpec(ProfileId id) noexcept
{
    for (const auto& profile : kProfiles) {
        if (profile.id == id)
            return profile;
    }
    return kProfiles.back();
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
    updateLyraFilterCoefficients();
    reset();
}

void CharacterProcessor::reset() noexcept
{
    left_ = {};
    right_ = {};
    left_.rngState = 0x6d2b79f5u;
    right_.rngState = 0x1b56c4e9u;
}

void CharacterProcessor::setParameters(const Parameters& parameters) noexcept
{
    parameters_ = parameters;
    const auto& spec = getProfileSpec(parameters_.profile);
    toneLowAlpha_ = safeAlpha(spec.lowShelfHz, sampleRate_);
    toneHighAlpha_ = safeAlpha(spec.highShelfHz, sampleRate_);
}

void CharacterProcessor::updateLyraFilterCoefficients() noexcept
{
    const float sr = static_cast<float>(sampleRate_);

    // First-order bilinear high-pass. fc=0.85 Hz gives about -0.049 dB at
    // 8 Hz and -3 dB below 1 Hz, matching Lyra's published LF limits closely.
    const float hpK = std::tan(kPi * kLyraLfMinus3Hz / sr);
    const float hpNorm = 1.0f / (1.0f + hpK);
    lyraHpB0_ = hpNorm;
    lyraHpB1_ = -hpNorm;
    lyraHpFeedback_ = (1.0f - hpK) * hpNorm;

    // 2nd-order Butterworth low-pass placed at Prism's published -3 dB point.
    // This primarily models the converter/reconstruction roll-off and its
    // associated phase rotation without inventing an audible EQ curve.
    const float cutoff = lyraHfMinus3Hz(sampleRate_);
    const float omega = 2.0f * kPi * cutoff / sr;
    const float sine = std::sin(omega);
    const float cosine = std::cos(omega);
    constexpr float q = 0.7071067811865475f;
    const float alpha = sine / (2.0f * q);
    const float a0 = 1.0f + alpha;

    lyraLpB0_ = ((1.0f - cosine) * 0.5f) / a0;
    lyraLpB1_ = (1.0f - cosine) / a0;
    lyraLpB2_ = lyraLpB0_;
    lyraLpA1_ = (-2.0f * cosine) / a0;
    lyraLpA2_ = (1.0f - alpha) / a0;
}

float CharacterProcessor::applyTone(float input, ChannelState& state,
                                    const ProfileSpec& spec) const noexcept
{
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

float CharacterProcessor::processLyraHighPass(float input, ChannelState& state) const noexcept
{
    const float output = lyraHpB0_ * input + lyraHpB1_ * state.lyraHpX1
                       + lyraHpFeedback_ * state.lyraHpY1;
    state.lyraHpX1 = input;
    state.lyraHpY1 = output;
    return output;
}

float CharacterProcessor::processLyraLowPass(float input, ChannelState& state) const noexcept
{
    const float output = lyraLpB0_ * input + state.lyraLpZ1;
    state.lyraLpZ1 = lyraLpB1_ * input - lyraLpA1_ * output + state.lyraLpZ2;
    state.lyraLpZ2 = lyraLpB2_ * input - lyraLpA2_ * output;
    return output;
}

float CharacterProcessor::nextLyraNoise(ChannelState& state) const noexcept
{
    const float tpdf = nextUniform01(state.rngState)
                     + nextUniform01(state.rngState) - 1.0f;
    return tpdf * (kLyraNoiseRms * kLyraTpdfScale);
}

float CharacterProcessor::processLyraSample(float input, ChannelState& state,
                                            const ProfileSpec& spec) noexcept
{
    const float amount = clamp01(parameters_.amount);
    const float driveGain = dbToGain(parameters_.driveDb);
    const float driven = input * driveGain;

    // No generic saturation is added. The polynomial is calibrated so that
    // near full scale the model lands around Prism's published -107 dB THD.
    // Exact H2/H3 phase and distribution remain measurement-dependent.
    float signal = input;
    signal += amount * (spec.secondHarmonic * driven * driven
                      + spec.thirdHarmonic * driven * driven * driven);

    // Published dynamic range is 115 dB. TPDF is used only as a neutral
    // placeholder spectrum until a real Lyra's residual-noise FFT is captured.
    signal += amount * nextLyraNoise(state);

    // The output-stage bandwidth is stateful, so this models amplitude and
    // phase behavior instead of acting as a static EQ curve.
    signal = processLyraHighPass(signal, state);
    signal = processLyraLowPass(signal, state);

    const float wet = clamp01(parameters_.mix);
    const float outputGain = dbToGain(parameters_.outputDb);
    return (input + wet * (signal - input)) * outputGain;
}

float CharacterProcessor::processSample(float input, ChannelState& state,
                                        const ProfileSpec& spec) noexcept
{
    if (spec.id == ProfileId::PrismSoundLyra1)
        return processLyraSample(input, state, spec);

    const float amount = clamp01(parameters_.amount);
    const float driveGain = dbToGain(parameters_.driveDb);

    float signal = applyTone(input, state, spec);

    const float envelopeTarget = std::abs(signal);
    const float envelopeAlpha = safeAlpha(12.0f, sampleRate_);
    state.envelope += envelopeAlpha * (envelopeTarget - state.envelope);
    const float memoryGain = 1.0f + spec.memory * state.envelope;

    const float curvature = 1.0f + 3.0f * spec.saturation;
    const float driven = signal * driveGain * memoryGain;
    const float normalized = std::tanh(driven * curvature)
                           / (curvature * std::max(0.001f, driveGain));

    float shaped = signal + amount * spec.saturation * (normalized - signal);

    const float nonlinearInput = signal * driveGain;
    shaped += amount * (spec.secondHarmonic * nonlinearInput * nonlinearInput
                      + spec.thirdHarmonic * nonlinearInput * nonlinearInput
                                               * nonlinearInput);
    shaped = dcBlock(shaped, state);

    const float clipThreshold = dbToGain(spec.clipThresholdDb);
    if (clipThreshold > 0.0f && std::abs(shaped) > clipThreshold)
        shaped = clipThreshold * std::tanh(shaped / clipThreshold);

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
