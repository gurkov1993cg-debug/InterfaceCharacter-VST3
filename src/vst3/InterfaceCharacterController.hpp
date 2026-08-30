#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace interface_character::vst3 {

class InterfaceCharacterController final : public Steinberg::Vst::EditController {
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(
            new InterfaceCharacterController());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) SMTG_OVERRIDE;

    OBJ_METHODS(InterfaceCharacterController, EditController)
    REFCOUNT_METHODS(EditController)
};

} // namespace interface_character::vst3
