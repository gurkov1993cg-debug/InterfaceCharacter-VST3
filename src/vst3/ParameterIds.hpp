#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace interface_character::vst3 {

enum : Steinberg::Vst::ParamID {
    kProfile = 100,
    kDrive = 101,
    kAmount = 102,
    kMix = 103,
    kOutput = 104,
    kBypass = 105,
};

} // namespace interface_character::vst3
