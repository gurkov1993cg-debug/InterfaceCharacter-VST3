#pragma once

#include "Profile.hpp"

#include <cstddef>

namespace interface_character {

struct Parameters {
    ProfileId profile = ProfileId::UniversalAudioApollo;
    float driveDb = 0.0f;
    float amount = 1.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
    bool bypass = false;
};

class CharacterProcessor final {
public:
    CharacterProcessor();

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const Parameters& parameters) noexcept;

    void processMono(float* samples, std::size_t numSamples) noexcept;
    void processStereo(float* left, float* right, std::size_t numSamples) noexcept;
    void processMono(double* samples, std::size_t numSamples) noexcept;
    void processStereo(double* left, double* right, std::size_t numSamples) noexcept;

private:
    struct ChannelState {
        float lowState = 0.0f;
        float highState = 0.0f;
        float dcState = 0.0f;
        float previousInput = 0.0f;
        float envelope = 0.0f;
    };

    float processSample(float input, ChannelState& state,
                        const ProfileSpec& spec) noexcept;
    float applyTone(float input, ChannelState& state,
                    const ProfileSpec& spec) const noexcept;
    float dcBlock(float input, ChannelState& state) const noexcept;
    float quantizeIfNeeded(float input, const ProfileSpec& spec) const noexcept;

    double sampleRate_ = 48000.0;
    float toneLowAlpha_ = 0.0f;
    float toneHighAlpha_ = 0.0f;
    float dcAlpha_ = 0.0f;
    Parameters parameters_{};
    ChannelState left_{};
    ChannelState right_{};
};

} // namespace interface_character
