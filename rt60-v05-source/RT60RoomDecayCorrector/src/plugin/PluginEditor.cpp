#include "plugin/PluginEditor.hpp"

RT60Editor::RT60Editor(RT60Processor& p) : AudioProcessorEditor(&p), processor_(p)
{
    setSize(900, 610);
    graph_.setModel(&processor_.model());
    addAndMakeVisible(graph_);

    title_.setText("RT60 ROOM DECAY CORRECTOR v0.4", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    addAndMakeVisible(status_);
    addAndMakeVisible(reloadButton_);
    addAndMakeVisible(bypassButton_);
    bypassA_ = std::make_unique<ButtonAttachment>(processor_.apvts, "bypass", bypassButton_);

    reloadButton_.onClick = [this] {
        processor_.reloadActiveProfile();
        updateStatus();
        graph_.repaint();
    };
    updateStatus();
    startTimerHz(10);
}

void RT60Editor::updateStatus()
{
    if (processor_.hasActiveProfile())
        status_.setText("ACTIVE measured decay profile | " + juce::String(processor_.activeStageCount())
                        + " pole/zero correction stages", juce::dontSendNotification);
    else
        status_.setText("No measured profile. Run the standalone app: MEASURE ROOM -> AUTO CORRECT RT60.", juce::dontSendNotification);
}

void RT60Editor::timerCallback()
{
    graph_.repaint();
}

void RT60Editor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 20));
}

void RT60Editor::resized()
{
    auto r = getLocalBounds().reduced(18);
    title_.setBounds(r.removeFromTop(38));
    status_.setBounds(r.removeFromTop(28));
    graph_.setBounds(r.removeFromTop(430));
    r.removeFromTop(8);
    auto row = r.removeFromTop(38);
    reloadButton_.setBounds(row.removeFromLeft(260));
    row.removeFromLeft(12);
    bypassButton_.setBounds(row.removeFromLeft(120));
}
