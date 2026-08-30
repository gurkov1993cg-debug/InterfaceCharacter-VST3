#include "InterfaceCharacterProcessor.hpp"

#include "ParameterIds.hpp"
#include "PluginIds.hpp"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace interface_character::vst3 {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

float normalizedToDrive(ParamValue value) noexcept
{
    return static_cast<float>(-12.0 + std::clamp(value, 0.0, 1.0) * 30.0);
}

float normalizedToOutput(ParamValue value) noexcept
{
    return static_cast<float>(-12.0 + std::clamp(value, 0.0, 1.0) * 24.0);
}

ProfileId profileFromUiIndex(int index) noexcept
{
    switch (std::clamp(index, 0, 3)) {
        case 0: return ProfileId::PrismSoundLyra1;
        case 1: return ProfileId::UniversalAudioApollo;
        case 2: return ProfileId::ProToolsTdm88824;
        case 3: return ProfileId::ProToolsHdxHdIo;
        default: return ProfileId::PrismSoundLyra1;
    }
}

} // namespace

using namespace Steinberg;
using namespace Steinberg::Vst;

InterfaceCharacterProcessor::InterfaceCharacterProcessor()
{
    setControllerClass(kControllerUid);
}

tresult PLUGIN_API InterfaceCharacterProcessor::initialize(FUnknown* context)
{
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API InterfaceCharacterProcessor::setActive(TBool state)
{
    if (state)
        dsp_.reset();
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API InterfaceCharacterProcessor::setupProcessing(ProcessSetup& setup)
{
    const auto result = AudioEffect::setupProcessing(setup);
    if (result == kResultOk)
        dsp_.prepare(setup.sampleRate);
    return result;
}

tresult PLUGIN_API InterfaceCharacterProcessor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr
        || inputs[0] != SpeakerArr::kStereo || outputs[0] != SpeakerArr::kStereo) {
        return kResultFalse;
    }
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API InterfaceCharacterProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64
        ? kResultTrue : kResultFalse;
}

void InterfaceCharacterProcessor::readParameterChanges(IParameterChanges* changes) noexcept
{
    if (changes == nullptr)
        return;

    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        auto* queue = changes->getParameterData(i);
        if (queue == nullptr || queue->getPointCount() <= 0)
            continue;

        ParamValue value = 0.0;
        int32 sampleOffset = 0;
        if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != kResultTrue)
            continue;

        switch (queue->getParameterId()) {
            case kProfile: {
                const int uiIndex = std::clamp(
                    static_cast<int>(std::lround(value * 3.0)), 0, 3);
                parameters_.profile = profileFromUiIndex(uiIndex);
                break;
            }
            case kDrive:
                parameters_.driveDb = normalizedToDrive(value);
                break;
            case kAmount:
                parameters_.amount = static_cast<float>(std::clamp(value, 0.0, 1.0));
                break;
            case kMix:
                parameters_.mix = static_cast<float>(std::clamp(value, 0.0, 1.0));
                break;
            case kOutput:
                parameters_.outputDb = normalizedToOutput(value);
                break;
            case kBypass:
                parameters_.bypass = value > 0.5;
                break;
            default:
                break;
        }
    }
}

void InterfaceCharacterProcessor::updateDspParameters() noexcept
{
    dsp_.setParameters(parameters_);
}

tresult PLUGIN_API InterfaceCharacterProcessor::process(ProcessData& data)
{
    readParameterChanges(data.inputParameterChanges);
    updateDspParameters();

    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;

    auto& inputBus = data.inputs[0];
    auto& outputBus = data.outputs[0];
    const int32 channelCount = std::min(inputBus.numChannels, outputBus.numChannels);
    if (channelCount <= 0)
        return kResultOk;

    if (data.symbolicSampleSize == kSample32) {
        auto** in = inputBus.channelBuffers32;
        auto** out = outputBus.channelBuffers32;
        for (int32 channel = 0; channel < channelCount; ++channel) {
            if (in[channel] != out[channel])
                std::memcpy(out[channel], in[channel], sizeof(float) * data.numSamples);
        }
        if (!parameters_.bypass && parameters_.mix > 0.0f) {
            if (channelCount >= 2)
                dsp_.processStereo(out[0], out[1], static_cast<std::size_t>(data.numSamples));
            else
                dsp_.processMono(out[0], static_cast<std::size_t>(data.numSamples));
        }
    } else if (data.symbolicSampleSize == kSample64) {
        auto** in = inputBus.channelBuffers64;
        auto** out = outputBus.channelBuffers64;
        for (int32 channel = 0; channel < channelCount; ++channel) {
            if (in[channel] != out[channel])
                std::memcpy(out[channel], in[channel], sizeof(double) * data.numSamples);
        }
        if (!parameters_.bypass && parameters_.mix > 0.0f) {
            if (channelCount >= 2)
                dsp_.processStereo(out[0], out[1], static_cast<std::size_t>(data.numSamples));
            else
                dsp_.processMono(out[0], static_cast<std::size_t>(data.numSamples));
        }
    }

    outputBus.silenceFlags = inputBus.silenceFlags;
    return kResultOk;
}

tresult PLUGIN_API InterfaceCharacterProcessor::setState(IBStream* state)
{
    if (state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    uint32 version = 0;
    int32 profile = 0;
    int32 bypass = 0;
    if (!streamer.readInt32u(version) || version != 1u
        || !streamer.readInt32(profile)
        || !streamer.readFloat(parameters_.driveDb)
        || !streamer.readFloat(parameters_.amount)
        || !streamer.readFloat(parameters_.mix)
        || !streamer.readFloat(parameters_.outputDb)
        || !streamer.readInt32(bypass)) {
        return kResultFalse;
    }

    // State stores the stable ProfileId, not the UI list index, preserving
    // compatibility with the previous prototype's saved states.
    parameters_.profile = static_cast<ProfileId>(std::clamp(profile, 0, 3));
    parameters_.amount = std::clamp(parameters_.amount, 0.0f, 1.0f);
    parameters_.mix = std::clamp(parameters_.mix, 0.0f, 1.0f);
    parameters_.bypass = bypass != 0;
    updateDspParameters();
    return kResultOk;
}

tresult PLUGIN_API InterfaceCharacterProcessor::getState(IBStream* state)
{
    if (state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    streamer.writeInt32u(1);
    streamer.writeInt32(static_cast<int32>(parameters_.profile));
    streamer.writeFloat(parameters_.driveDb);
    streamer.writeFloat(parameters_.amount);
    streamer.writeFloat(parameters_.mix);
    streamer.writeFloat(parameters_.outputDb);
    streamer.writeInt32(parameters_.bypass ? 1 : 0);
    return kResultOk;
}

} // namespace interface_character::vst3
