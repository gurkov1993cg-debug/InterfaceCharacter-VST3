#include "core/DecayUniformity.hpp"

#include <cmath>
#include <iostream>

namespace {
void setBand(rt60::MeasurementResult& r, std::size_t i, float hz, float ms, float confidence = 0.95f)
{
    auto& b = r.bands[i];
    b.frequencyHz = hz;
    b.rt60Ms = ms;
    b.fitQuality = 0.98f;
    b.dynamicRangeDb = 55.0f;
    b.confidence = confidence;
    b.valid = true;
}
}

int main()
{
    rt60::MeasurementResult baseline, after;
    baseline.valid = after.valid = true;
    const float hz[] = {40, 50, 63, 80, 100, 125, 160, 200};
    const float before[] = {260, 290, 320, 420, 600, 780, 350, 300};
    const float matched[] = {260, 290, 305, 315, 310, 308, 300, 300};
    for (std::size_t i = 0; i < 8; ++i) {
        setBand(baseline, i, hz[i], before[i]);
        setBand(after, i, hz[i], matched[i]);
    }

    rt60::DecayUniformityConfig cfg;
    cfg.maxHz = 250.0f;
    const float target = rt60::DecayUniformity::chooseCommonTarget(baseline, cfg);
    std::cout << "autoTarget=" << target << " ms\n";
    if (!(target >= 280.0f && target <= 340.0f)) return 1;

    const auto report = rt60::DecayUniformity::evaluate(baseline, after, 300.0f, cfg);
    std::cout << "score=" << report.uniformityScore
              << " spread=" << report.spreadMs
              << " matched=" << report.withinCorrectableBands << "/" << report.correctableBands << "\n";
    if (!report.converged) return 2;
    if (report.uniformityScore < 85.0f) return 3;
    if (report.spreadMs > 70.0f) return 4;

    // A very low-confidence outlier must not drag the automatic target down.
    setBand(baseline, 8, 250.0f, 120.0f, 0.10f);
    const float target2 = rt60::DecayUniformity::chooseCommonTarget(baseline, cfg);
    if (std::abs(target2 - target) > 10.0f) return 5;

    // Verification data that disappears because SNR/confidence collapsed
    // must not make AUTO believe the room improved.
    auto unreliableAfter = after;
    unreliableAfter.bands[5].confidence = 0.10f; // 125 Hz was a long baseline mode
    const auto unreliable = rt60::DecayUniformity::evaluate(baseline, unreliableAfter, 300.0f, cfg);
    std::cout << "unreliableCorrectable=" << unreliable.unreliableCorrectableBands
              << " objective=" << unreliable.correctionObjectiveMs << " ms\n";
    if (unreliable.unreliableCorrectableBands != 1) return 6;
    if (unreliable.converged) return 7;
    if (unreliable.correctionObjectiveMs <= report.correctionObjectiveMs) return 8;
    return 0;
}
