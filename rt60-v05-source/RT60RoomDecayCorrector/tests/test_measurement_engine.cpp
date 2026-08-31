#include "core/MeasurementEngine.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
std::uint32_t rng = 0x12345678u;
float noise()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (static_cast<float>(rng >> 8) / 8388608.0f) - 1.0f;
}
}

int main()
{
    rt60::MeasurementConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.sweepSeconds = 1.0f;
    cfg.preSilenceSeconds = 0.05f;
    cfg.tailSeconds = 0.10f;
    rt60::MeasurementEngine engine(cfg);

    const auto excitation = engine.makeExcitation();
    assert(!excitation.empty());
    const auto ir = engine.deconvolve(excitation, excitation);
    assert(!ir.empty());
    const auto peak = std::max_element(ir.begin(), ir.end(), [](float a, float b) {
        return std::abs(a) < std::abs(b);
    });
    assert(std::abs(*peak) > 0.5f);

    // Broadband synthetic room with ~0.60 s RT60.
    constexpr float expectedSeconds = 0.60f;
    const std::size_t n = static_cast<std::size_t>(2.0 * cfg.sampleRate);
    std::vector<float> synthetic(n);
    const double alpha = 6.90775527898 / expectedSeconds;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / cfg.sampleRate;
        synthetic[i] = noise() * static_cast<float>(std::exp(-alpha * t));
    }
    synthetic[0] += 1.0f;

    const auto result = engine.analyseImpulseResponse(synthetic);
    assert(result.valid);
    // Mid bands should land in the right order of magnitude despite stochastic excitation.
    const auto& oneK = result.bands[16];
    assert(oneK.valid);
    assert(oneK.rt60Ms > 400.0f && oneK.rt60Ms < 850.0f);
    assert(oneK.dynamicRangeDb > 25.0f);
    assert(oneK.confidence > 0.45f);

    std::cout << "RT60 measurement engine tests passed; 1 kHz RT60="
              << oneK.rt60Ms << " ms\n";
}
