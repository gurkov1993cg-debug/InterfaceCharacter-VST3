#pragma once

#include "Profile.hpp"

#include <cstddef>
#include <cstdint>

namespace interface_character {

struct Parameters {
    ProfileId profile = ProfileId::PrismSoundLyra1;
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

        float lyraHpX1 = 0.0f;
        float lyraHpY1 = 0.0f;
        float lyraLpZ1 = 0.0f;
        float lyraLpZ2 = 0.0f;
        std::uint32_t rngState = 1u;
    };

    float processSample(float input, ChannelState& state,
                        const ProfileSpec& spec) noexcept;
    float processLyraSample(float input, ChannelState& state,
                            const ProfileSpec& spec) noexcept;
    float applyTone(float input, ChannelState& state,
                    const ProfileSpec& spec) const noexcept;
    float dcBlock(float input, ChannelState& state) const noexcept;
    float quantizeIfNeeded(float input, const ProfileSpec& spec) const noexcept;
    float processLyraHighPass(float input, ChannelState& state) const noexcept;
    float processLyraLowPass(float input, ChannelState& state) const noexcept;
    float nextLyraNoise(ChannelState& state) const noexcept;
    void updateLyraFilterCoefficients() noexcept;

    double sampleRate_ = 48000.0;
    float toneLowAlpha_ = 0.0f;
    float toneHighAlpha_ = 0.0f;
    float dcAlpha_ = 0.0f;

    float lyraHpB0_ = 1.0f;
    float lyraHpB1_ = -1.0f;
    float lyraHpFeedback_ = 0.0f;
    float lyraLpB0_ = 1.0f;
    float lyraLpB1_ = 0.0f;
    float lyraLpB2_ = 0.0f;
    float lyraLpA1_ = 0.0f;
    float lyraLpA2_ = 0.0f;

    Parameters parameters_{};
    ChannelState left_{};
    ChannelState right_{};
};

} // namespace interface_character
