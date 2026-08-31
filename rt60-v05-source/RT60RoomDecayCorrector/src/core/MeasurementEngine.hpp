#pragma once

#include "core/DecayModel.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace rt60 {

struct MeasurementConfig {
    double sampleRate = 48000.0;
    float startHz = 20.0f;
    float endHz = 20000.0f;
    float sweepSeconds = 6.0f;
    float preSilenceSeconds = 0.25f;
    float tailSeconds = 2.0f;
    float level = 0.20f; // approximately -14 dBFS peak
};

struct BandDecayMetrics {
    float frequencyHz = 1000.0f;
    float edtMs = 0.0f;
    float t20Ms = 0.0f;
    float t30Ms = 0.0f;
    float rt60Ms = 0.0f;
    float fitQuality = 0.0f;
    float noiseFloorDb = -120.0f;
    float dynamicRangeDb = 0.0f;
    float confidence = 0.0f;
    bool valid = false;
};

struct MeasurementResult {
    double sampleRate = 48000.0;
    std::vector<float> impulseResponse;
    std::array<BandDecayMetrics, kBandCount> bands{};
    float peakDbFs = -120.0f;
    bool clipped = false;
    bool valid = false;
};

class MeasurementEngine {
public:
    explicit MeasurementEngine(MeasurementConfig config = {});

    void setConfig(const MeasurementConfig& config) noexcept;
    const MeasurementConfig& config() const noexcept { return config_; }

    std::vector<float> makeExcitation() const;
    std::vector<float> makeSweepOnly() const;

    MeasurementResult analyseCapture(const std::vector<float>& captured) const;
    MeasurementResult analyseImpulseResponse(const std::vector<float>& impulseResponse) const;
    BandDecayMetrics analyseBandAtFrequency(const std::vector<float>& impulseResponse, float frequencyHz) const;

    // Exposed for deterministic testing and offline workflows.
    std::vector<float> deconvolve(const std::vector<float>& captured,
                                  const std::vector<float>& excitation) const;

private:
    MeasurementConfig config_{};
};

} // namespace rt60
