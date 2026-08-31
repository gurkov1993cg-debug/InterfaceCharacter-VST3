#pragma once

#include "core/DecayUniformity.hpp"
#include "core/MeasurementEngine.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace rt60 {

struct CorrectionStageSpec {
    float frequencyHz = 100.0f;
    float measuredT60Ms = 500.0f;
    float desiredT60Ms = 300.0f;
    float estimatedQ = 4.0f;
    float confidence = 1.0f;
    std::size_t sourceBand = 0;
};

struct CorrectionProfile {
    static constexpr int kFormatVersion = 4;
    float targetMs = 300.0f;
    float strength = 1.0f;
    bool autoTarget = false;
    std::array<float, kBandCount> measuredBandMs{};
    std::array<float, kBandCount> targetBandMs{};
    std::vector<CorrectionStageSpec> stages;

    bool empty() const noexcept { return stages.empty(); }
};

struct CorrectionOptimizationConfig {
    int rounds = 2;
    int zeroGridPoints = 7;
    int desiredGridPoints = 7;
    float initialZeroSearchFraction = 0.08f;
    float finalZeroSearchFraction = 0.025f;
};

struct CorrectionDesignConfig {
    float targetMs = 300.0f;
    bool autoTarget = false;
    float strength = 0.85f;
    float tolerance = 0.08f;
    float minCorrectionHz = 25.0f;
    float maxCorrectionHz = 1800.0f;
    float minConfidence = 0.45f;
    float maxT60ReductionPerPass = 0.55f;
    int maxNewStages = 18;
};

class CorrectionEngine {
public:
    CorrectionProfile design(const MeasurementResult& measurement,
                             const CorrectionDesignConfig& config) const;

    static float optimizeAgainstMeasurement(CorrectionProfile& profile,
                                            const MeasurementResult& baseline,
                                            const MeasurementEngine& analyser,
                                            const CorrectionOptimizationConfig& config = {});

    static bool refineProfile(CorrectionProfile& profile,
                              const MeasurementResult& measuredAfter,
                              float learningRate = 0.65f,
                              float toleranceFraction = 0.08f);

    static void appendIncremental(CorrectionProfile& accumulated,
                                  const CorrectionProfile& incremental,
                                  std::size_t maxTotalStages = 42,
                                  int maxStagesPerMode = 1);

    static std::string serialize(const CorrectionProfile& profile);
    static bool deserialize(const std::string& text, CorrectionProfile& profile);
};

class CorrectionFilterBank {
public:
    struct Stage {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    void configure(const CorrectionProfile& profile, double sampleRate);
    void reset() noexcept;
    float processSample(float sample) noexcept;
    void process(float* samples, int count) noexcept;
    bool active() const noexcept { return !stages_.empty(); }
    std::size_t stageCount() const noexcept { return stages_.size(); }

private:
    std::vector<Stage> stages_;
};

} // namespace rt60
