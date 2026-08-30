#include "InterfaceCharacterController.hpp"
#include "InterfaceCharacterProcessor.hpp"
#include "PluginIds.hpp"

#include "public.sdk/source/main/pluginfactory.h"

#define STRING_PLUGIN_NAME "Interface Character"
#define STRING_COMPANY_NAME "Interface Character Lab"
#define STRING_COMPANY_URL ""
#define STRING_COMPANY_EMAIL ""

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace interface_character::vst3;

BEGIN_FACTORY_DEF(STRING_COMPANY_NAME, STRING_COMPANY_URL, STRING_COMPANY_EMAIL)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               STRING_PLUGIN_NAME,
               Vst::kDistributable,
               "Fx",
               "0.1.0",
               kVstVersionString,
               InterfaceCharacterProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUid),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               STRING_PLUGIN_NAME " Controller",
               0,
               "",
               "0.1.0",
               kVstVersionString,
               InterfaceCharacterController::createInstance)

END_FACTORY
