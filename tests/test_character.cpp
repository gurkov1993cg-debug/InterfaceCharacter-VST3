#include "CharacterProcessor.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

using interface_character::CharacterProcessor;
using interface_character::Parameters;
using interface_character::ProfileId;
using interface_character::getProfileSpec;

namespace {

constexpr float pi = 3.14159265358979323846f;

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

std::vector<float> makeSine(float frequency, float sampleRate, std::size_t count,
                            float amplitude = 0.5f)
{
    std::vector<float> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = amplitude * std::sin(2.0f * pi * frequency
                                        * static_cast<float>(i) / sampleRate);
    return result;
}

float rms(const std::vector<float>& data, std::size_t begin = 0)
{
    double sum = 0.0;
    for (std::size_t i = begin; i < data.size(); ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    const auto count = data.size() - begin;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

float gainDb(float output, float input)
{
    return 20.0f * std::log10(output / input);
}

void testLyraIsDefault()
{
    Parameters parameters;
    assert(parameters.profile == ProfileId::PrismSoundLyra1);
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

void testLyraLfPublishedPoint()
{
    constexpr float sr = 48000.0f;
    auto input = makeSine(8.0f, sr, static_cast<std::size_t>(sr * 6.0f), 0.25f);
    auto output = input;

    CharacterProcessor processor;
    processor.prepare(sr);
    Parameters parameters;
    parameters.amount = 0.0f; // isolate bandwidth/phase model
    processor.setParameters(parameters);
    processor.processMono(output.data(), output.size());

    const std::size_t begin = static_cast<std::size_t>(sr * 3.0f);
    const float db = gainDb(rms(output, begin), rms(input, begin));
    assert(db > -0.07f && db < -0.03f);
}

void testLyraHfMinus3Point48k()
{
    constexpr float sr = 48000.0f;
    auto input = makeSine(23900.0f, sr, static_cast<std::size_t>(sr), 0.25f);
    auto output = input;

    CharacterProcessor processor;
    processor.prepare(sr);
    Parameters parameters;
    parameters.amount = 0.0f;
    processor.setParameters(parameters);
    processor.processMono(output.data(), output.size());

    const std::size_t begin = 4096;
    const float db = gainDb(rms(output, begin), rms(input, begin));
    assert(db > -3.2f && db < -2.8f);
}

void testLyraNoiseFloor()
{
    constexpr float sr = 48000.0f;
    std::vector<float> silence(static_cast<std::size_t>(sr * 2.0f), 0.0f);

    CharacterProcessor processor;
    processor.prepare(sr);
    Parameters parameters;
    parameters.amount = 1.0f;
    processor.setParameters(parameters);
    processor.processMono(silence.data(), silence.size());

    const float noiseDb = 20.0f * std::log10(rms(silence, 4096));
    assert(noiseDb > -117.0f && noiseDb < -113.0f);
}

void testLyraCrosstalkAt1k()
{
    constexpr float sr = 48000.0f;
    auto left = makeSine(1000.0f, sr, static_cast<std::size_t>(sr), 0.5f);
    std::vector<float> right(left.size(), 0.0f);

    CharacterProcessor processor;
    processor.prepare(sr);
    Parameters parameters;
    parameters.amount = 0.0f; // no noise, isolate crosstalk
    processor.setParameters(parameters);
    processor.processStereo(left.data(), right.data(), left.size());

    const float ratio = rms(right, 4096) / rms(left, 4096);
    const float db = 20.0f * std::log10(ratio);
    assert(db > -136.0f && db < -134.0f);
}

void testLyraAddsOnlyTinyMidbandDifference()
{
    auto input = makeSine(1000.0f, 48000.0f, 8192, 0.9f);
    const auto original = input;

    CharacterProcessor processor;
    processor.prepare(48000.0);
    Parameters parameters;
    parameters.profile = ProfileId::PrismSoundLyra1;
    processor.setParameters(parameters);
    processor.processMono(input.data(), input.size());

    assert(maxDifference(input, original) > 1.0e-7f);
    assert(maxDifference(input, original) < 2.0e-3f);
    assert(finiteBuffer(input));
}

} // namespace

int main()
{
    testLyraIsDefault();
    testBypassAndMix();
    testAllProfilesAreFinite();
    testLyraLfPublishedPoint();
    testLyraHfMinus3Point48k();
    testLyraNoiseFloor();
    testLyraCrosstalkAt1k();
    testLyraAddsOnlyTinyMidbandDifference();
    std::cout << "Interface Character / Prism Lyra 1 DSP tests passed\n";
    return 0;
}
