#pragma once

#include "core/MeasurementEngine.hpp"

#include <cstddef>

namespace rt60 {

struct DecayUniformityConfig {
    float minHz = 25.0f;
    float maxHz = 1800.0f;
    float minConfidence = 0.45f;
    float toleranceFraction = 0.10f;
    float hardLongFraction = 0.22f;
    float requiredWithinFraction = 0.90f;
    float autoTargetPercentile = 0.25f;
    float minTargetMs = 120.0f;
    float maxTargetMs = 900.0f;
};

struct DecayUniformityReport {
    float targetMs = 300.0f;
    int reliableBands = 0;
    int correctableBands = 0;
    int withinCorrectableBands = 0;
    int overshotCorrectableBands = 0;
    int unreliableCorrectableBands = 0;
    int naturallyShortBands = 0;
    float meanAbsoluteErrorMs = 0.0f;
    float rmsErrorMs = 0.0f;
    float correctableMeanAbsoluteErrorMs = 0.0f;
    float correctableRmsErrorMs = 0.0f;
    float correctionObjectiveMs = 0.0f;
    float spreadMs = 0.0f;
    float worstLongMs = 0.0f;
    float worstShortMs = 0.0f;
    float uniformityScore = 0.0f; // 0..100
    bool converged = false;
};

class DecayUniformity {
public:
    static float chooseCommonTarget(const MeasurementResult& measurement,
                                    const DecayUniformityConfig& config = {});

    static DecayUniformityReport evaluate(const MeasurementResult& baseline,
                                          const MeasurementResult& current,
                                          float targetMs,
                                          const DecayUniformityConfig& config = {});
};

} // namespace rt60
