#include "CharacterProcessor.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

using interface_character::CharacterProcessor;
using interface_character::Parameters;
using interface_character::ProfileId;

namespace {

bool finiteBuffer(const std::vector<float>& buffer)
{
    for (const float sample : buffer) {
        if (!std::isfinite(sample))
            return false;
    }
    return true;
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    assert(a.size() == b.size());
    float result = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        result = std::max(result, std::abs(a[i] - b[i]));
    return result;
}

std::vector<float> makeSine(float frequency, float sampleRate, std::size_t count)
{
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = 0.5f * std::sin(2.0f * pi * frequency
                                    * static_cast<float>(i) / sampleRate);
    return result;
}

void testBypassAndMix()
{
    auto input = makeSine(440.0f, 48000.0f, 2048);
    const auto original = input;

    CharacterProcessor processor;
    processor.prepare(48000.0);
    Parameters parameters;
    parameters.mix = 0.0f;
    processor.setParameters(parameters);
    processor.processMono(input.data(), input.size());

    assert(maxDifference(input, original) == 0.0f);
}

void testAllProfilesAreFinite()
{
    const ProfileId profiles[] = {
        ProfileId::UniversalAudioApollo,
        ProfileId::ProToolsTdm88824,
        ProfileId::ProToolsHdxHdIo,
        ProfileId::PrismSoundLyra1,
    };

    for (const auto profile : profiles) {
        auto left = makeSine(80.0f, 48000.0f, 4096);
        auto right = makeSine(440.0f, 48000.0f, 4096);

        CharacterProcessor processor;
        processor.prepare(48000.0);
        Parameters parameters;
        parameters.profile = profile;
        parameters.driveDb = 6.0f;
        processor.setParameters(parameters);
        processor.processStereo(left.data(), right.data(), left.size());

        assert(finiteBuffer(left));
        assert(finiteBuffer(right));
    }
}

void testApolloProfileAddsAControlledDifference()
{
    auto input = makeSine(220.0f, 48000.0f, 4096);
    const auto original = input;

    CharacterProcessor processor;
    processor.prepare(48000.0);
    Parameters parameters;
    parameters.profile = ProfileId::UniversalAudioApollo;
    parameters.driveDb = 12.0f;
    processor.setParameters(parameters);
    processor.processMono(input.data(), input.size());

    assert(maxDifference(input, original) > 1.0e-5f);
    assert(finiteBuffer(input));
}

} // namespace

int main()
{
    testBypassAndMix();
    testAllProfilesAreFinite();
    testApolloProfileAddsAControlledDifference();
    std::cout << "Interface Character DSP tests passed\n";
    return 0;
}
