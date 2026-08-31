#include "core/CorrectionEngine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <sstream>

namespace rt60 {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr std::array<float, kBandCount> kCenters{{
    25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200,
    250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
    2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000
}};

std::size_t nextPow2(std::size_t value)
{
    std::size_t n = 1;
    while (n < value) n <<= 1u;
    return n;
}

void fft(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1u;
        for (; j & bit; bit >>= 1u) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1u) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

struct PeakInfo {
    float frequencyHz = 0.0f;
    float estimatedQ = 4.0f;
    double power = 0.0;
};

struct ModalDecayEstimate {
    float t60Ms = 0.0f;
    float fitQuality = 0.0f;
    float snrDb = 0.0f;
    bool valid = false;
};

struct SimpleBiquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    float process(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

SimpleBiquad makeBandPass(double sampleRate, double frequency, double q)
{
    const double w = 2.0 * kPi * frequency / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    SimpleBiquad f;
    f.b0 = alpha / a0;
    f.b1 = 0.0;
    f.b2 = -alpha / a0;
    f.a1 = (-2.0 * std::cos(w)) / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

ModalDecayEstimate estimateModalDecay(const std::vector<float>& ir, double sampleRate,
                                      float frequencyHz, float broadBandT60Ms)
{
    ModalDecayEstimate out;
    if (ir.size() < static_cast<std::size_t>(sampleRate * 0.35) || frequencyHz < 20.0f)
        return out;

    // Choose Q so the analysis filter itself dies substantially faster than
    // the room mode. That keeps the late envelope dominated by the room pole.
    const double roomSec = std::clamp(static_cast<double>(broadBandT60Ms) / 1000.0, 0.12, 4.0);
    const double q = std::clamp(0.30 * roomSec * kPi * frequencyHz / 6.90775527898, 4.0, 20.0);
    auto bp = makeBandPass(sampleRate, frequencyHz, q);
    std::vector<float> band(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i)
        band[i] = bp.process(ir[i]);

    const std::size_t start = std::min(band.size(), static_cast<std::size_t>(sampleRate * 0.12));
    const std::size_t win = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 0.020));
    const std::size_t hop = std::max<std::size_t>(32, static_cast<std::size_t>(sampleRate * 0.010));
    if (start + win >= band.size()) return out;

    std::vector<double> times, levels;
    for (std::size_t i = start; i + win <= band.size(); i += hop) {
        double e = 0.0;
        for (std::size_t j = 0; j < win; ++j) {
            const double v = band[i + j];
            e += v * v;
        }
        const double rms = std::sqrt(e / static_cast<double>(win));
        times.push_back((static_cast<double>(i) + 0.5 * win) / sampleRate);
        levels.push_back(20.0 * std::log10(std::max(rms, 1.0e-15)));
        if (times.back() - times.front() > 2.5)
            break;
    }
    if (levels.size() < 12) return out;

    const double ref = *std::max_element(levels.begin(), levels.end());
    // Estimate late noise from the final 10% of windows and only fit points
    // comfortably above that floor. A real mode must have enough late-field
    // dynamic range to separate its decay from microphone/background noise.
    const std::size_t tailStart = levels.size() * 9 / 10;
    std::vector<double> tailLevels(levels.begin() + static_cast<std::ptrdiff_t>(tailStart), levels.end());
    std::sort(tailLevels.begin(), tailLevels.end());
    const double tail = tailLevels[tailLevels.size() / 2];
    const double snrDb = ref - tail;
    out.snrDb = static_cast<float>(snrDb);
    if (snrDb < 12.0)
        return out;

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const double rel = levels[i] - ref;
        if (rel > -3.0 || rel < -30.0 || levels[i] < tail + 8.0)
            continue;
        const double x = times[i], y = rel;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        ++count;
    }
    if (count < 8) return out;
    const double n = static_cast<double>(count);
    const double den = n * sxx - sx * sx;
    if (std::abs(den) < 1.0e-18) return out;
    const double slope = (n * sxy - sx * sy) / den;
    const double intercept = (sy - slope * sx) / n;
    if (slope >= -2.0) return out;

    double ssRes = 0.0, ssTot = 0.0;
    const double mean = sy / n;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const double rel = levels[i] - ref;
        if (rel > -3.0 || rel < -30.0 || levels[i] < tail + 8.0)
            continue;
        const double pred = intercept + slope * times[i];
        ssRes += (rel - pred) * (rel - pred);
        ssTot += (rel - mean) * (rel - mean);
    }
    const double r2 = ssTot > 1.0e-12 ? 1.0 - ssRes / ssTot : 0.0;
    const double t60 = -60.0 / slope * 1000.0;
    if (std::isfinite(t60) && t60 > 50.0 && t60 < 6000.0 && r2 >= 0.90) {
        out.t60Ms = static_cast<float>(t60);
        out.fitQuality = static_cast<float>(r2);
        out.valid = true;
    }
    return out;
}

PeakInfo dominantPeak(const std::vector<float>& ir, double sampleRate, float center)
{
    PeakInfo result{center, 4.0f, 0.0};
    if (ir.empty() || sampleRate <= 0.0) return result;

    const std::size_t start = std::min(ir.size(), static_cast<std::size_t>(sampleRate * 0.015));
    const std::size_t available = ir.size() > start ? ir.size() - start : 0;
    const std::size_t copyCount = std::min<std::size_t>(available, static_cast<std::size_t>(sampleRate * 2.5));
    if (copyCount < 128) return result;

    const std::size_t n = nextPow2(copyCount);
    std::vector<std::complex<double>> bins(n);
    for (std::size_t i = 0; i < copyCount; ++i) {
        const double w = copyCount > 1
            ? 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(copyCount - 1))
            : 1.0;
        bins[i] = static_cast<double>(ir[start + i]) * w;
    }
    fft(bins);

    const double edge = std::pow(2.0, 1.0 / 6.0);
    const double low = center / edge;
    const double high = center * edge;
    std::size_t first = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(low * n / sampleRate)));
    std::size_t last = std::min<std::size_t>(n / 2 - 2, static_cast<std::size_t>(std::floor(high * n / sampleRate)));
    if (last <= first) return result;

    std::size_t best = first;
    double bestPower = 0.0;
    for (std::size_t k = first; k <= last; ++k) {
        const double power = std::norm(bins[k - 1]) + std::norm(bins[k]) + std::norm(bins[k + 1]);
        if (power > bestPower) {
            bestPower = power;
            best = k;
        }
    }

    // Sub-bin quadratic interpolation in log-power. Long-decay room modes are
    // extremely sensitive to frequency error; a plain FFT-bin centre can
    // leave enough uncancelled pole energy to dominate the late tail.
    const double p0 = std::log(std::max(std::norm(bins[best - 1]), 1.0e-30));
    const double p1 = std::log(std::max(std::norm(bins[best]), 1.0e-30));
    const double p2 = std::log(std::max(std::norm(bins[best + 1]), 1.0e-30));
    const double denom = p0 - 2.0 * p1 + p2;
    double delta = 0.0;
    if (std::abs(denom) > 1.0e-12)
        delta = std::clamp(0.5 * (p0 - p2) / denom, -0.5, 0.5);
    result.frequencyHz = static_cast<float>((static_cast<double>(best) + delta) * sampleRate / n);
    result.power = bestPower;

    const double peakBinPower = std::max(std::norm(bins[best]), 1.0e-30);
    const double half = peakBinPower * 0.5;
    std::size_t left = best, right = best;
    while (left > first && std::norm(bins[left]) > half) --left;
    while (right < last && std::norm(bins[right]) > half) ++right;
    const double bandwidth = static_cast<double>(std::max<std::size_t>(1, right - left)) * sampleRate / n;
    result.estimatedQ = static_cast<float>(std::clamp(result.frequencyHz / bandwidth, 0.7, 300.0));
    return result;
}

bool nearSameMode(float a, float b)
{
    if (a <= 0.0f || b <= 0.0f) return false;
    return std::abs(std::log2(a / b)) < (1.0f / 5.0f); // cluster adjacent 1/3-octave leakage around one physical mode
}

float desiredForPass(float measured, float target, float strength,
                     float confidence, float maxReduction)
{
    strength = std::clamp(strength, 0.0f, 1.0f);
    confidence = std::clamp(confidence, 0.0f, 1.0f);
    maxReduction = std::clamp(maxReduction, 0.10f, 0.80f);

    // Low-confidence modes move more cautiously. A single pass is also capped
    // so imperfect real-room pole estimates cannot erase a band in one jump.
    const float effectiveStrength = strength * (0.55f + 0.45f * confidence);
    float desired = measured + effectiveStrength * (target - measured);
    desired = std::max(desired, measured * (1.0f - maxReduction));
    return std::max(target, desired);
}

CorrectionFilterBank::Stage makeStage(const CorrectionStageSpec& spec, double sampleRate)
{
    CorrectionFilterBank::Stage s;
    if (sampleRate <= 1000.0 || spec.frequencyHz <= 0.0f || spec.frequencyHz >= sampleRate * 0.47)
        return s;

    const double measuredSec = std::max(0.06, static_cast<double>(spec.measuredT60Ms) / 1000.0);
    const double desiredSec = std::clamp(static_cast<double>(spec.desiredT60Ms) / 1000.0, 0.05, measuredSec);
    if (desiredSec >= measuredSec * 0.995)
        return s;

    const double log001 = std::log(0.001);
    const double rRoom = std::exp(log001 / (sampleRate * measuredSec));
    const double rDesired = std::exp(log001 / (sampleRate * desiredSec));
    const double w = 2.0 * kPi * static_cast<double>(spec.frequencyHz) / sampleRate;
    const double c = std::cos(w);

    s.b0 = 1.0;
    s.b1 = -2.0 * rRoom * c;
    s.b2 = rRoom * rRoom;
    s.a1 = -2.0 * rDesired * c;
    s.a2 = rDesired * rDesired;

    // The correction is strictly non-boosting over a dense frequency grid.
    double maxMag = 0.0;
    for (int i = 0; i <= 1024; ++i) {
        const double omega = kPi * static_cast<double>(i) / 1024.0;
        const std::complex<double> z1(std::cos(-omega), std::sin(-omega));
        const std::complex<double> z2 = z1 * z1;
        const auto num = s.b0 + s.b1 * z1 + s.b2 * z2;
        const auto den = 1.0 + s.a1 * z1 + s.a2 * z2;
        if (std::abs(den) > 1.0e-12)
            maxMag = std::max(maxMag, std::abs(num / den));
    }
    if (maxMag > 1.0) {
        const double scale = 1.0 / maxMag;
        s.b0 *= scale; s.b1 *= scale; s.b2 *= scale;
    }
    return s;
}

} // namespace

CorrectionProfile CorrectionEngine::design(const MeasurementResult& measurement,
                                           const CorrectionDesignConfig& config) const
{
    CorrectionProfile profile;
    profile.autoTarget = config.autoTarget;
    profile.strength = std::clamp(config.strength, 0.0f, 1.0f);

    DecayUniformityConfig ucfg;
    ucfg.minHz = config.minCorrectionHz;
    ucfg.maxHz = config.maxCorrectionHz;
    ucfg.minConfidence = config.minConfidence;
    profile.targetMs = config.autoTarget
        ? DecayUniformity::chooseCommonTarget(measurement, ucfg)
        : std::clamp(config.targetMs, 80.0f, 2000.0f);

    struct Candidate {
        CorrectionStageSpec spec;
        float score = 0.0f;
        double peakPower = 0.0;
    };
    std::vector<Candidate> candidates;

    for (std::size_t i = 0; i < kBandCount; ++i) {
        const auto& band = measurement.bands[i];
        const float measured = band.valid ? band.rt60Ms : 0.0f;
        profile.measuredBandMs[i] = measured;
        profile.targetBandMs[i] = profile.targetMs;

        if (!band.valid || band.confidence < config.minConfidence)
            continue;
        if (measured <= profile.targetMs * (1.0f + config.tolerance))
            continue;
        if (kCenters[i] < config.minCorrectionHz || kCenters[i] > config.maxCorrectionHz)
            continue;

        const auto peak = dominantPeak(measurement.impulseResponse, measurement.sampleRate, kCenters[i]);
        const auto modal = estimateModalDecay(measurement.impulseResponse, measurement.sampleRate,
                                               peak.frequencyHz, measured);
        // Do not create a cancellation pole from a frequency-response peak
        // alone. If the late-field ring cannot be fitted reliably, leave it
        // untouched and ask the closed loop for better data instead.
        if (!modal.valid)
            continue;

        Candidate c;
        c.spec.sourceBand = i;
        c.spec.measuredT60Ms = modal.t60Ms;
        const float snrQuality = std::clamp((modal.snrDb - 10.0f) / 25.0f, 0.0f, 1.0f);
        c.spec.confidence = std::clamp(band.confidence * modal.fitQuality
                                      * (0.35f + 0.65f * snrQuality), 0.0f, 1.0f);
        if (c.spec.confidence < config.minConfidence)
            continue;
        c.spec.estimatedQ = peak.estimatedQ;
        c.spec.desiredT60Ms = desiredForPass(c.spec.measuredT60Ms, profile.targetMs,
                                             profile.strength, c.spec.confidence,
                                             config.maxT60ReductionPerPass);
        c.spec.frequencyHz = peak.frequencyHz;
        c.peakPower = peak.power;

        const float edge = static_cast<float>(std::pow(2.0, 1.0 / 6.0));
        c.spec.frequencyHz = std::clamp(c.spec.frequencyHz, kCenters[i] / edge, kCenters[i] * edge);
        const float excess = measured / std::max(80.0f, profile.targetMs) - 1.0f;
        c.score = excess * band.confidence * std::clamp(c.spec.estimatedQ / 8.0f, 0.35f, 2.0f);
        candidates.push_back(c);
    }

    double strongestPower = 0.0;
    for (const auto& c : candidates) strongestPower = std::max(strongestPower, c.peakPower);
    if (strongestPower > 0.0) {
        const double floorPower = strongestPower * 0.001; // -30 dB power; reject adjacent-band leakage/noise
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [floorPower](const Candidate& c) { return c.peakPower < floorPower; }), candidates.end());
    }

    std::sort(candidates.begin(), candidates.end(), [strongestPower](const Candidate& a, const Candidate& b) {
        const double wa = strongestPower > 0.0 ? std::sqrt(a.peakPower / strongestPower) : 1.0;
        const double wb = strongestPower > 0.0 ? std::sqrt(b.peakPower / strongestPower) : 1.0;
        return a.score * wa > b.score * wb;
    });

    for (const auto& candidate : candidates) {
        bool duplicate = false;
        for (const auto& existing : profile.stages) {
            if (nearSameMode(existing.frequencyHz, candidate.spec.frequencyHz)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            profile.stages.push_back(candidate.spec);
        if (static_cast<int>(profile.stages.size()) >= config.maxNewStages)
            break;
    }
    return profile;
}

float CorrectionEngine::optimizeAgainstMeasurement(CorrectionProfile& profile,
                                                   const MeasurementResult& baseline,
                                                   const MeasurementEngine& analyser,
                                                   const CorrectionOptimizationConfig& config)
{
    if (profile.stages.empty() || baseline.impulseResponse.empty())
        return 0.0f;

    // Start from the user's/common target. The first optimization phase must
    // identify the room pole itself; allowing the replacement pole to wander
    // at the same time creates ambiguous solutions.
    for (auto& stage : profile.stages)
        stage.desiredT60Ms = std::min(profile.targetMs, stage.measuredT60Ms * 0.98f);

    auto objectiveForStage = [&](std::size_t stageIndex) -> float {
        auto simulated = baseline.impulseResponse;
        CorrectionFilterBank bank;
        bank.configure(profile, baseline.sampleRate);
        bank.process(simulated.data(), static_cast<int>(simulated.size()));
        const auto& stage = profile.stages[stageIndex];
        const float center = stage.sourceBand < kBandCount ? kCenters[stage.sourceBand] : stage.frequencyHz;
        const auto metric = analyser.analyseBandAtFrequency(simulated, center);
        if (!metric.valid)
            return 1.0e6f;
        const float error = metric.rt60Ms - profile.targetMs;
        float cost = std::abs(error);
        if (error < -profile.targetMs * 0.10f)
            cost += 2.0f * std::abs(error);
        cost += (1.0f - metric.confidence) * 25.0f;
        return cost;
    };

    const int rounds = std::clamp(config.rounds, 1, 3);
    for (int round = 0; round < rounds; ++round) {
        const float t = rounds == 1 ? 1.0f : static_cast<float>(round) / static_cast<float>(rounds - 1);
        const float search = config.initialZeroSearchFraction
                           + t * (config.finalZeroSearchFraction - config.initialZeroSearchFraction);

        // Phase A: identify/correct the room-pole radius while the replacement
        // pole is fixed at the requested target.
        for (std::size_t si = 0; si < profile.stages.size(); ++si) {
            auto& stage = profile.stages[si];
            const float baseZero = stage.measuredT60Ms;
            const float heldDesired = std::min(profile.targetMs, baseZero * 0.98f);
            stage.desiredT60Ms = heldDesired;
            float bestCost = objectiveForStage(si);
            float bestZero = baseZero;
            const int zg = std::max(3, config.zeroGridPoints | 1);
            for (int i = 0; i < zg; ++i) {
                const float u = 2.0f * i / static_cast<float>(zg - 1) - 1.0f;
                stage.measuredT60Ms = std::max(60.0f, baseZero * (1.0f + u * search));
                stage.desiredT60Ms = std::min(profile.targetMs, stage.measuredT60Ms * 0.98f);
                const float cost = objectiveForStage(si);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestZero = stage.measuredT60Ms;
                }
            }
            stage.measuredT60Ms = bestZero;
            stage.desiredT60Ms = std::min(profile.targetMs, bestZero * 0.98f);
        }

        // Phase B: only a small fine-tune of the replacement pole is allowed.
        // This compensates for overlapping real-room modes without turning the
        // search into an arbitrary EQ fit.
        for (std::size_t si = 0; si < profile.stages.size(); ++si) {
            auto& stage = profile.stages[si];
            float bestCost = objectiveForStage(si);
            float bestDesired = stage.desiredT60Ms;
            const float minDesired = std::max(55.0f, profile.targetMs * 0.78f);
            const float maxDesired = std::min(stage.measuredT60Ms * 0.98f, profile.targetMs * 1.22f);
            const int dg = std::max(3, config.desiredGridPoints | 1);
            for (int i = 0; i < dg; ++i) {
                const float u = i / static_cast<float>(dg - 1);
                stage.desiredT60Ms = minDesired + u * (maxDesired - minDesired);
                const float cost = objectiveForStage(si);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestDesired = stage.desiredT60Ms;
                }
            }
            stage.desiredT60Ms = bestDesired;
        }
    }

    auto simulated = baseline.impulseResponse;
    CorrectionFilterBank bank;
    bank.configure(profile, baseline.sampleRate);
    bank.process(simulated.data(), static_cast<int>(simulated.size()));
    const auto result = analyser.analyseImpulseResponse(simulated);
    DecayUniformityConfig ucfg;
    ucfg.maxHz = 1800.0f;
    ucfg.minConfidence = 0.30f;
    const auto report = DecayUniformity::evaluate(baseline, result, profile.targetMs, ucfg);
    return report.uniformityScore;
}

bool CorrectionEngine::refineProfile(CorrectionProfile& profile,
                                     const MeasurementResult& measuredAfter,
                                     float learningRate,
                                     float toleranceFraction)
{
    if (profile.stages.empty())
        return false;
    learningRate = std::clamp(learningRate, 0.10f, 1.0f);
    toleranceFraction = std::clamp(toleranceFraction, 0.02f, 0.25f);
    bool changed = false;

    for (auto& stage : profile.stages) {
        const BandDecayMetrics* best = nullptr;
        float bestDistance = 1.0e9f;
        for (const auto& b : measuredAfter.bands) {
            if (!b.valid || b.confidence < 0.30f || b.frequencyHz <= 0.0f)
                continue;
            const float d = std::abs(std::log2(b.frequencyHz / stage.frequencyHz));
            if (d < bestDistance) {
                bestDistance = d;
                best = &b;
            }
        }
        if (best == nullptr || bestDistance > 0.22f)
            continue;

        // Retune from the decay measured at the exact modal frequency, not
        // merely the nearest 1/3-octave summary. If the exact late-field fit
        // is not trustworthy, freeze this stage for the pass.
        const auto modal = estimateModalDecay(measuredAfter.impulseResponse,
                                              measuredAfter.sampleRate,
                                              stage.frequencyHz,
                                              best->rt60Ms);
        if (!modal.valid)
            continue;

        const float afterMs = modal.t60Ms;
        const float target = profile.targetMs;
        const float relativeError = (afterMs - target) / std::max(80.0f, target);
        if (std::abs(relativeError) <= toleranceFraction)
            continue;

        // Feedback control on the replacement pole. Unlike stacking another
        // cancellation stage, this can move in BOTH directions: if a previous
        // pass over-damped the mode, desiredT60 is increased again.
        const float snrQuality = std::clamp((modal.snrDb - 10.0f) / 25.0f, 0.0f, 1.0f);
        const float confidence = std::clamp(best->confidence * stage.confidence
                                            * modal.fitQuality
                                            * (0.35f + 0.65f * snrQuality), 0.10f, 1.0f);
        if (confidence < 0.25f)
            continue;
        const float exponent = learningRate * confidence;
        const float ratio = std::clamp(target / std::max(60.0f, afterMs), 0.72f, 1.38f);
        float proposed = stage.desiredT60Ms * std::pow(ratio, exponent);

        const float maxStepUp = stage.desiredT60Ms * 1.12f;
        const float maxStepDown = stage.desiredT60Ms * 0.88f;
        proposed = std::clamp(proposed, maxStepDown, maxStepUp);
        proposed = std::clamp(proposed,
                              std::max(55.0f, target * 0.70f),
                              stage.measuredT60Ms * 0.995f);
        if (std::abs(proposed - stage.desiredT60Ms) > 0.5f) {
            stage.desiredT60Ms = proposed;
            changed = true;
        }
    }
    return changed;
}

void CorrectionEngine::appendIncremental(CorrectionProfile& accumulated,
                                         const CorrectionProfile& incremental,
                                         std::size_t maxTotalStages,
                                         int maxStagesPerMode)
{
    const bool hadStages = !accumulated.stages.empty();
    accumulated.targetMs = incremental.targetMs > 0.0f ? incremental.targetMs : accumulated.targetMs;
    accumulated.strength = incremental.strength;
    accumulated.autoTarget = incremental.autoTarget;
    if (!hadStages)
        accumulated.measuredBandMs = incremental.measuredBandMs;
    accumulated.targetBandMs = incremental.targetBandMs;

    for (const auto& stage : incremental.stages) {
        if (accumulated.stages.size() >= maxTotalStages)
            break;
        int sameMode = 0;
        for (const auto& existing : accumulated.stages)
            if (nearSameMode(existing.frequencyHz, stage.frequencyHz))
                ++sameMode;
        if (sameMode < maxStagesPerMode)
            accumulated.stages.push_back(stage);
    }
}

std::string CorrectionEngine::serialize(const CorrectionProfile& profile)
{
    std::ostringstream out;
    out << std::setprecision(9);
    out << "RT60_PROFILE " << CorrectionProfile::kFormatVersion << '\n';
    out << "TARGET " << profile.targetMs << '\n';
    out << "STRENGTH " << profile.strength << '\n';
    out << "AUTO_TARGET " << (profile.autoTarget ? 1 : 0) << '\n';
    out << "BANDS";
    for (float v : profile.measuredBandMs) out << ' ' << v;
    out << '\n';
    out << "TARGETS";
    for (float v : profile.targetBandMs) out << ' ' << v;
    out << '\n';
    for (const auto& s : profile.stages)
        out << "STAGE " << s.frequencyHz << ' ' << s.measuredT60Ms << ' '
            << s.desiredT60Ms << ' ' << s.estimatedQ << ' ' << s.confidence << ' '
            << s.sourceBand << '\n';
    return out.str();
}

bool CorrectionEngine::deserialize(const std::string& text, CorrectionProfile& profile)
{
    std::istringstream in(text);
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "RT60_PROFILE" || (version != 3 && version != 4))
        return false;

    CorrectionProfile parsed;
    while (in >> tag) {
        if (tag == "TARGET") {
            in >> parsed.targetMs;
        } else if (tag == "STRENGTH") {
            in >> parsed.strength;
        } else if (tag == "AUTO_TARGET" && version >= 4) {
            int v = 0; in >> v; parsed.autoTarget = v != 0;
        } else if (tag == "BANDS") {
            for (auto& v : parsed.measuredBandMs) in >> v;
        } else if (tag == "TARGETS") {
            for (auto& v : parsed.targetBandMs) in >> v;
        } else if (tag == "STAGE") {
            CorrectionStageSpec s;
            if (version >= 4)
                in >> s.frequencyHz >> s.measuredT60Ms >> s.desiredT60Ms
                   >> s.estimatedQ >> s.confidence >> s.sourceBand;
            else
                in >> s.frequencyHz >> s.measuredT60Ms >> s.desiredT60Ms >> s.sourceBand;
            if (in && s.frequencyHz > 0.0f)
                parsed.stages.push_back(s);
        } else {
            std::string rest;
            std::getline(in, rest);
        }
    }
    profile = std::move(parsed);
    return true;
}

void CorrectionFilterBank::configure(const CorrectionProfile& profile, double sampleRate)
{
    stages_.clear();
    stages_.reserve(profile.stages.size());
    for (const auto& spec : profile.stages) {
        auto stage = makeStage(spec, sampleRate);
        if (std::abs(stage.b1) > 1.0e-12 || std::abs(stage.b2) > 1.0e-12
            || std::abs(stage.a1) > 1.0e-12 || std::abs(stage.a2) > 1.0e-12)
            stages_.push_back(stage);
    }
    reset();
}

void CorrectionFilterBank::reset() noexcept
{
    for (auto& s : stages_) s.z1 = s.z2 = 0.0;
}

float CorrectionFilterBank::processSample(float sample) noexcept
{
    double x = sample;
    for (auto& s : stages_) {
        const double y = s.b0 * x + s.z1;
        s.z1 = s.b1 * x - s.a1 * y + s.z2;
        s.z2 = s.b2 * x - s.a2 * y;
        x = y;
    }
    return static_cast<float>(x);
}

void CorrectionFilterBank::process(float* samples, int count) noexcept
{
    if (samples == nullptr || count <= 0) return;
    for (int i = 0; i < count; ++i)
        samples[i] = processSample(samples[i]);
}

} // namespace rt60
