#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "core/DecayModel.hpp"

class RT60Graph final : public juce::Component {
public:
    void setModel(const rt60::DecayModel* model) noexcept { model_ = model; repaint(); }
    void paint(juce::Graphics& g) override;

private:
    float xForFrequency(float hz, juce::Rectangle<float> area) const;
    float yForMs(float ms, juce::Rectangle<float> area) const;
    void drawCurve(juce::Graphics&, juce::Rectangle<float>, int kind, float thickness);
    const rt60::DecayModel* model_ = nullptr;
};
