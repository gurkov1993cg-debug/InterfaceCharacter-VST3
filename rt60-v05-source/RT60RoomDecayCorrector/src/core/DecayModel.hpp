#pragma once

#include <array>
#include <cstddef>

namespace rt60 {

constexpr std::size_t kBandCount = 29;

struct BandState {
    float frequencyHz = 1000.0f;
    float measuredMs = 300.0f;
    float targetMs = 300.0f;
    float predictedMs = 300.0f;
    float correctionDb = 0.0f;
    float measuredAfterMs = 0.0f;
    bool hasMeasuredAfter = false;
};

class DecayModel {
public:
    DecayModel();

    void setUniformTarget(float milliseconds) noexcept;
    void setTarget(std::size_t band, float milliseconds) noexcept;
    void setMeasured(std::size_t band, float milliseconds) noexcept;
    void setMeasuredAfter(std::size_t band, float milliseconds) noexcept;
    void clearMeasuredAfter() noexcept;
    void setStrength(float normalized) noexcept;
    void setMaxCutDb(float db) noexcept;
    void setDemoRoom() noexcept;

    float strength() const noexcept { return strength_; }
    float maxCutDb() const noexcept { return maxCutDb_; }
    const std::array<BandState, kBandCount>& bands() const noexcept { return bands_; }

    void recalculate() noexcept;

private:
    std::array<BandState, kBandCount> bands_{};
    float strength_ = 0.75f;
    float maxCutDb_ = 9.0f;
};

} // namespace rt60
