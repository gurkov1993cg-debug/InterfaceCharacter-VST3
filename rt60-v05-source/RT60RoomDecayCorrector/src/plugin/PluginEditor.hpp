#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "plugin/PluginProcessor.hpp"
#include "ui/RT60Graph.hpp"

class RT60Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RT60Editor(RT60Processor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void updateStatus();
    RT60Processor& processor_;
    RT60Graph graph_;
    juce::Label title_, status_;
    juce::TextButton reloadButton_{"RELOAD MEASURED PROFILE"};
    juce::ToggleButton bypassButton_{"BYPASS"};
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> bypassA_;
};
