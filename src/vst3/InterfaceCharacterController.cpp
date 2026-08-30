#include "InterfaceCharacterController.hpp"

#include "ParameterIds.hpp"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>

namespace interface_character::vst3 {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

class ProfileParameter final : public StringListParameter {
public:
    ProfileParameter()
    : StringListParameter(STR16("Profile"), kProfile)
    {
        appendString(STR16("Universal Audio Apollo"));
        appendString(STR16("Pro Tools TDM / 888|24"));
        appendString(STR16("Pro Tools HDX / HD I/O"));
        appendString(STR16("Prism Sound Lyra 1"));
    }
};

} // namespace

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API InterfaceCharacterController::initialize(FUnknown* context)
{
    const auto result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(new ProfileParameter());
    parameters.addParameter(STR16("Drive"), STR16("dB"), kStepCountContinuous,
                            12.0 / 30.0, ParameterInfo::kCanAutomate, kDrive);
    parameters.addParameter(STR16("Amount"), STR16("%"), kStepCountContinuous,
                            1.0, ParameterInfo::kCanAutomate, kAmount);
    parameters.addParameter(STR16("Mix"), STR16("%"), kStepCountContinuous,
                            1.0, ParameterInfo::kCanAutomate, kMix);
    parameters.addParameter(STR16("Output"), STR16("dB"), kStepCountContinuous,
                            0.5, ParameterInfo::kCanAutomate, kOutput);
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
                            kBypass);

    return kResultOk;
}

tresult PLUGIN_API InterfaceCharacterController::setComponentState(IBStream* state)
{
    if (state == nullptr)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    uint32 version = 0;
    int32 profile = 0;
    float driveDb = 0.0f;
    float amount = 1.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
    int32 bypass = 0;

    if (!streamer.readInt32u(version) || version != 1u
        || !streamer.readInt32(profile)
        || !streamer.readFloat(driveDb)
        || !streamer.readFloat(amount)
        || !streamer.readFloat(mix)
        || !streamer.readFloat(outputDb)
        || !streamer.readInt32(bypass)) {
        return kResultFalse;
    }

    setParamNormalized(kProfile, std::clamp(static_cast<ParamValue>(profile) / 3.0, 0.0, 1.0));
    setParamNormalized(kDrive, std::clamp((static_cast<ParamValue>(driveDb) + 12.0) / 30.0,
                                          0.0, 1.0));
    setParamNormalized(kAmount, std::clamp(static_cast<ParamValue>(amount), 0.0, 1.0));
    setParamNormalized(kMix, std::clamp(static_cast<ParamValue>(mix), 0.0, 1.0));
    setParamNormalized(kOutput, std::clamp((static_cast<ParamValue>(outputDb) + 12.0) / 24.0,
                                           0.0, 1.0));
    setParamNormalized(kBypass, bypass != 0 ? 1.0 : 0.0);
    return kResultOk;
}

} // namespace interface_character::vst3
