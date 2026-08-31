#include "core/DecayModel.hpp"

#include <algorithm>
#include <cmath>

namespace rt60 {
namespace {
constexpr std::array<float, kBandCount> kCenters{{
    25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200,
    250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
    2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000
}};

float clampMs(float value) noexcept
{
    return std::clamp(value, 80.0f, 3000.0f);
}
}

DecayModel::DecayModel()
{
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        bands_[i].frequencyHz = kCenters[i];
        bands_[i].measuredMs = 320.0f;
        bands_[i].targetMs = 300.0f;
    }
    setDemoRoom();
}

void DecayModel::setUniformTarget(float milliseconds) noexcept
{
    const float target = clampMs(milliseconds);
    for (auto& band : bands_)
        band.targetMs = target;
    recalculate();
}

void DecayModel::setTarget(std::size_t band, float milliseconds) noexcept
{
    if (band >= bands_.size())
        return;
    bands_[band].targetMs = clampMs(milliseconds);
    recalculate();
}

void DecayModel::setMeasured(std::size_t band, float milliseconds) noexcept
{
    if (band >= bands_.size())
        return;
    bands_[band].measuredMs = clampMs(milliseconds);
    recalculate();
}


void DecayModel::setMeasuredAfter(std::size_t band, float milliseconds) noexcept
{
    if (band >= bands_.size())
        return;
    bands_[band].measuredAfterMs = clampMs(milliseconds);
    bands_[band].hasMeasuredAfter = true;
}

void DecayModel::clearMeasuredAfter() noexcept
{
    for (auto& band : bands_) {
        band.measuredAfterMs = 0.0f;
        band.hasMeasuredAfter = false;
    }
}

void DecayModel::setStrength(float normalized) noexcept
{
    strength_ = std::clamp(normalized, 0.0f, 1.0f);
    recalculate();
}

void DecayModel::setMaxCutDb(float db) noexcept
{
    maxCutDb_ = std::clamp(db, 0.0f, 18.0f);
    recalculate();
}

void DecayModel::setDemoRoom() noexcept
{
    // Deliberately uneven small-room decay: modal bass plus shorter HF tail.
    constexpr std::array<float, kBandCount> demo{{
        760, 820, 910, 840, 730, 610, 560, 510, 440, 390,
        360, 340, 325, 315, 305, 295, 285, 275, 265, 255,
        245, 235, 225, 215, 205, 195, 185, 175, 165
    }};
    for (std::size_t i = 0; i < bands_.size(); ++i)
        bands_[i].measuredMs = demo[i];
    clearMeasuredAfter();
    recalculate();
}

void DecayModel::recalculate() noexcept
{
    for (auto& band : bands_) {
        const float measured = clampMs(band.measuredMs);
        const float target = clampMs(band.targetMs);

        if (measured <= target || strength_ <= 0.0f) {
            // Do not boost a band merely because its natural decay is shorter.
            band.predictedMs = measured;
            band.correctionDb = 0.0f;
            continue;
        }

        const float desired = measured + strength_ * (target - measured);
        band.predictedMs = std::max(target, desired);

        // Excitation-reduction estimate. 0 dB means leave the band alone.
        // This is intentionally capped because software cannot remove energy
        // that is already reverberating in the room.
        const float ratio = std::clamp(target / measured, 0.01f, 1.0f);
        const float idealCut = 20.0f * std::log10(ratio);
        band.correctionDb = std::clamp(idealCut * strength_, -maxCutDb_, 0.0f);
    }
}

} // namespace rt60
