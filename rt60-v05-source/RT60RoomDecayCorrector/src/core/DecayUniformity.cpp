#include "core/DecayUniformity.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace rt60 {
namespace {

bool reliable(const BandDecayMetrics& b, const DecayUniformityConfig& cfg)
{
    return b.valid && b.frequencyHz >= cfg.minHz && b.frequencyHz <= cfg.maxHz
        && b.confidence >= cfg.minConfidence && b.rt60Ms > 0.0f;
}

float percentile(std::vector<float> values, float p)
{
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    p = std::clamp(p, 0.0f, 1.0f);
    const float index = p * static_cast<float>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(index));
    const auto hi = static_cast<std::size_t>(std::ceil(index));
    if (lo == hi) return values[lo];
    const float frac = index - static_cast<float>(lo);
    return values[lo] + frac * (values[hi] - values[lo]);
}

} // namespace

float DecayUniformity::chooseCommonTarget(const MeasurementResult& measurement,
                                          const DecayUniformityConfig& config)
{
    std::vector<float> values;
    values.reserve(kBandCount);
    for (const auto& b : measurement.bands) {
        if (reliable(b, config))
            values.push_back(b.rt60Ms);
    }
    if (values.size() < 3)
        return 300.0f;

    // Use the lower quartile instead of the absolute shortest band. This gives
    // the corrector a common target that can normally be reached by reducing
    // long decays, without chasing one anomalously dry/noisy band.
    float target = percentile(values, config.autoTargetPercentile);
    target = std::clamp(target, config.minTargetMs, config.maxTargetMs);
    return std::round(target / 5.0f) * 5.0f;
}

DecayUniformityReport DecayUniformity::evaluate(const MeasurementResult& baseline,
                                                const MeasurementResult& current,
                                                float targetMs,
                                                const DecayUniformityConfig& config)
{
    DecayUniformityReport r;
    r.targetMs = targetMs;
    std::vector<float> errors;
    std::vector<float> correctableErrors;
    std::vector<float> values;
    errors.reserve(kBandCount);
    correctableErrors.reserve(kBandCount);
    values.reserve(kBandCount);

    const float tol = std::max(12.0f, targetMs * config.toleranceFraction);
    const float hardLong = targetMs * (1.0f + config.hardLongFraction);
    const float hardShort = targetMs * (1.0f - config.hardLongFraction);

    for (std::size_t i = 0; i < kBandCount; ++i) {
        const auto& base = baseline.bands[i];
        const auto& now = current.bands[i];
        if (!reliable(base, config))
            continue;

        const bool wasCorrectable = base.rt60Ms > targetMs + tol;
        if (wasCorrectable)
            ++r.correctableBands;
        else if (base.rt60Ms < targetMs - tol)
            ++r.naturallyShortBands;

        // A noisy or invalid verification measurement must never look like an
        // improvement simply because a difficult band disappeared from the
        // statistics. Keep the denominator anchored to the trusted baseline.
        if (!reliable(now, config)) {
            if (wasCorrectable)
                ++r.unreliableCorrectableBands;
            continue;
        }

        ++r.reliableBands;
        values.push_back(now.rt60Ms);
        const float err = now.rt60Ms - targetMs;
        errors.push_back(err);
        r.worstLongMs = std::max(r.worstLongMs, err);
        r.worstShortMs = std::min(r.worstShortMs, err);

        if (wasCorrectable) {
            correctableErrors.push_back(err);
            if (std::abs(err) <= tol)
                ++r.withinCorrectableBands;
            else if (now.rt60Ms < hardShort)
                ++r.overshotCorrectableBands;
        }
    }

    if (!errors.empty()) {
        double absSum = 0.0, sqSum = 0.0;
        for (float e : errors) {
            absSum += std::abs(e);
            sqSum += static_cast<double>(e) * e;
        }
        r.meanAbsoluteErrorMs = static_cast<float>(absSum / errors.size());
        r.rmsErrorMs = static_cast<float>(std::sqrt(sqSum / errors.size()));
        const auto mm = std::minmax_element(values.begin(), values.end());
        r.spreadMs = *mm.second - *mm.first;
        const float normalized = r.rmsErrorMs / std::max(80.0f, targetMs);
        r.uniformityScore = 100.0f * std::clamp(1.0f - normalized, 0.0f, 1.0f);
    }

    if (r.correctableBands > 0) {
        double absSum = 0.0, sqSum = 0.0;
        for (float e : correctableErrors) {
            absSum += std::abs(e);
            sqSum += static_cast<double>(e) * e;
        }

        // Penalise a verification band that became untrustworthy. The
        // corrector should request a cleaner measurement or stop safely,
        // rather than interpreting missing data as a successful correction.
        const float missingError = std::max(2.0f * tol, targetMs * 0.30f);
        absSum += static_cast<double>(r.unreliableCorrectableBands) * missingError;
        sqSum += static_cast<double>(r.unreliableCorrectableBands)
               * missingError * missingError;

        r.correctableMeanAbsoluteErrorMs = static_cast<float>(absSum / r.correctableBands);
        r.correctableRmsErrorMs = static_cast<float>(std::sqrt(sqSum / r.correctableBands));

        // Objective used by the real closed-loop controller. Overshooting a
        // mode is deliberately more expensive than leaving a small amount of
        // excess decay, because DSP cannot restore acoustic energy that was
        // over-cancelled reliably at every nearby listening position.
        const float overshootPenalty = static_cast<float>(r.overshotCorrectableBands)
                                     * targetMs * 0.18f / r.correctableBands;
        const float unreliablePenalty = static_cast<float>(r.unreliableCorrectableBands)
                                      * targetMs * 0.12f / r.correctableBands;
        r.correctionObjectiveMs = r.correctableRmsErrorMs
                                + overshootPenalty + unreliablePenalty;
    }

    const int required = r.correctableBands == 0 ? 0
        : static_cast<int>(std::ceil(config.requiredWithinFraction * r.correctableBands));
    r.converged = r.correctableBands == 0
        || (r.withinCorrectableBands >= std::max(1, required)
            && r.overshotCorrectableBands == 0
            && r.unreliableCorrectableBands == 0
            && r.worstLongMs <= hardLong - targetMs);
    return r;
}

} // namespace rt60
