#include "core/CorrectionEngine.hpp"
#include "core/MeasurementEngine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeModalRoom(double sampleRate, float frequencyHz, float t60Ms, float seconds)
{
    const std::size_t n = static_cast<std::size_t>(sampleRate * seconds);
    std::vector<float> h(n, 0.0f);
    const double r = std::exp(std::log(0.001) / (sampleRate * (t60Ms / 1000.0)));
    const double a1 = -2.0 * r * std::cos(2.0 * kPi * frequencyHz / sampleRate);
    const double a2 = r * r;
    double y1 = 0.0, y2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = i == 0 ? 1.0 : 0.0;
        const double y = x - a1 * y1 - a2 * y2;
        h[i] = static_cast<float>(y);
        y2 = y1;
        y1 = y;
    }
    float peak = 0.0f;
    for (float v : h) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f)
        for (auto& v : h) v /= peak;
    return h;
}

float bandRt60Near(const rt60::MeasurementResult& r, float frequency)
{
    float bestDistance = 1.0e9f;
    float value = 0.0f;
    for (const auto& b : r.bands) {
        const float d = std::abs(std::log(b.frequencyHz / frequency));
        if (b.valid && d < bestDistance) {
            bestDistance = d;
            value = b.rt60Ms;
        }
    }
    return value;
}
}

int main()
{
    constexpr double fs = 48000.0;
    constexpr float frequency = 63.0f;
    constexpr float roomT60 = 900.0f;
    constexpr float targetT60 = 300.0f;

    rt60::MeasurementConfig cfg;
    cfg.sampleRate = fs;
    rt60::MeasurementEngine measurement(cfg);

    auto room = makeModalRoom(fs, frequency, roomT60, 2.0f);
    const auto before = measurement.analyseImpulseResponse(room);
    const float beforeMs = bandRt60Near(before, frequency);

    rt60::CorrectionProfile profile;
    profile.targetMs = targetT60;
    profile.stages.push_back({frequency, roomT60, targetT60, 8.0f, 1.0f, 4});

    rt60::CorrectionFilterBank corrector;
    corrector.configure(profile, fs);
    corrector.process(room.data(), static_cast<int>(room.size()));
    const auto after = measurement.analyseImpulseResponse(room);
    const float afterMs = bandRt60Near(after, frequency);

    std::cout << "single mode before=" << beforeMs << " after=" << afterMs << " ms\n";
    if (!(beforeMs > 650.0f && beforeMs < 1200.0f)) return 1;
    if (!(afterMs > 260.0f && afterMs < 340.0f)) return 2;

    const std::string encoded = rt60::CorrectionEngine::serialize(profile);
    rt60::CorrectionProfile decoded;
    if (!rt60::CorrectionEngine::deserialize(encoded, decoded)) return 3;
    if (decoded.stages.size() != 1) return 4;

    auto roomA = makeModalRoom(fs, 63.0f, 900.0f, 2.0f);
    auto roomB = makeModalRoom(fs, 125.0f, 700.0f, 2.0f);
    std::vector<float> original(roomA.size(), 0.0f);
    for (std::size_t i = 0; i < original.size(); ++i)
        original[i] = roomA[i] + 0.65f * roomB[i];

    const auto multiBefore = measurement.analyseImpulseResponse(original);
    rt60::CorrectionDesignConfig designCfg;
    designCfg.targetMs = targetT60;
    designCfg.strength = 0.85f;
    designCfg.maxCorrectionHz = 500.0f;
    designCfg.minConfidence = 0.35f;
    designCfg.maxT60ReductionPerPass = 0.50f;
    rt60::CorrectionEngine engine;
    auto intelligentProfile = engine.design(multiBefore, designCfg);
    if (intelligentProfile.stages.size() != 2) return 5;

    rt60::CorrectionOptimizationConfig optCfg;
    optCfg.rounds = 2;
    optCfg.zeroGridPoints = 7;
    optCfg.desiredGridPoints = 7;
    const float modelScore = rt60::CorrectionEngine::optimizeAgainstMeasurement(
        intelligentProfile, multiBefore, measurement, optCfg);

    auto corrected = original;
    rt60::CorrectionFilterBank bank;
    bank.configure(intelligentProfile, fs);
    bank.process(corrected.data(), static_cast<int>(corrected.size()));
    const auto multiAfter = measurement.analyseImpulseResponse(corrected);
    const float after63 = bandRt60Near(multiAfter, 63.0f);
    const float after125 = bandRt60Near(multiAfter, 125.0f);

    std::cout << "intelligent stages=" << intelligentProfile.stages.size()
              << " modelScore=" << modelScore
              << " after63=" << after63 << " after125=" << after125 << "\n";
    if (!(after63 > 260.0f && after63 < 340.0f)) return 6;
    if (!(after125 > 260.0f && after125 < 340.0f)) return 7;
    if (modelScore < 80.0f) return 8;

    // Real feedback must be bidirectional. If a verified pass is still too
    // long, strengthen the replacement pole; if it overshoots, relax it.
    rt60::CorrectionProfile feedbackLong;
    feedbackLong.targetMs = targetT60;
    feedbackLong.stages.push_back({frequency, roomT60, 480.0f, 8.0f, 1.0f, 4});
    auto longRoom = makeModalRoom(fs, frequency, roomT60, 2.0f);
    rt60::CorrectionFilterBank longBank;
    longBank.configure(feedbackLong, fs);
    longBank.process(longRoom.data(), static_cast<int>(longRoom.size()));
    const auto measuredLong = measurement.analyseImpulseResponse(longRoom);
    const float desiredBeforeLong = feedbackLong.stages.front().desiredT60Ms;
    if (!rt60::CorrectionEngine::refineProfile(feedbackLong, measuredLong, 0.65f, 0.08f)) return 9;
    if (!(feedbackLong.stages.front().desiredT60Ms < desiredBeforeLong)) return 10;

    rt60::CorrectionProfile feedbackShort;
    feedbackShort.targetMs = targetT60;
    feedbackShort.stages.push_back({frequency, roomT60, 210.0f, 8.0f, 1.0f, 4});
    auto shortRoom = makeModalRoom(fs, frequency, roomT60, 2.0f);
    rt60::CorrectionFilterBank shortBank;
    shortBank.configure(feedbackShort, fs);
    shortBank.process(shortRoom.data(), static_cast<int>(shortRoom.size()));
    const auto measuredShort = measurement.analyseImpulseResponse(shortRoom);
    const float desiredBeforeShort = feedbackShort.stages.front().desiredT60Ms;
    if (!rt60::CorrectionEngine::refineProfile(feedbackShort, measuredShort, 0.65f, 0.08f)) return 11;
    if (!(feedbackShort.stages.front().desiredT60Ms > desiredBeforeShort)) return 12;
    return 0;
}
