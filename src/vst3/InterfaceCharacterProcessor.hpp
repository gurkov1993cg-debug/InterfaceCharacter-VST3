#pragma once

#include "CharacterProcessor.hpp"

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace interface_character::vst3 {

class InterfaceCharacterProcessor final : public Steinberg::Vst::AudioEffect {
public:
    InterfaceCharacterProcessor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(
            new InterfaceCharacterProcessor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    void readParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void updateDspParameters() noexcept;

    interface_character::CharacterProcessor dsp_;
    interface_character::Parameters parameters_{};
};

} // namespace interface_character::vst3
