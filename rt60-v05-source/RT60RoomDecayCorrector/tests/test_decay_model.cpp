#include "core/DecayModel.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    rt60::DecayModel model;
    model.setUniformTarget(300.0f);
    model.setStrength(1.0f);

    const auto& bands = model.bands();
    assert(bands[4].measuredMs > 300.0f); // 63 Hz demo mode
    assert(std::abs(bands[4].predictedMs - 300.0f) < 0.01f);
    assert(bands[4].correctionDb < 0.0f);

    model.setMeasured(16, 220.0f); // 1 kHz already shorter than target
    assert(std::abs(model.bands()[16].predictedMs - 220.0f) < 0.01f);
    assert(model.bands()[16].correctionDb == 0.0f);

    model.setStrength(0.0f);
    assert(std::abs(model.bands()[4].predictedMs - model.bands()[4].measuredMs) < 0.01f);

    std::cout << "RT60 decay model tests passed\n";
}
