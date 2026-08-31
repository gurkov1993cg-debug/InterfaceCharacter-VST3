#include "ui/RT60Graph.hpp"

#include <cmath>

float RT60Graph::xForFrequency(float hz, juce::Rectangle<float> area) const
{
    const float minLog = std::log10(20.0f);
    const float maxLog = std::log10(20000.0f);
    return area.getX() + area.getWidth() * (std::log10(hz) - minLog) / (maxLog - minLog);
}

float RT60Graph::yForMs(float ms, juce::Rectangle<float> area) const
{
    const float clamped = juce::jlimit(80.0f, 1200.0f, ms);
    return juce::jmap(clamped, 80.0f, 1200.0f, area.getBottom(), area.getY());
}

void RT60Graph::drawCurve(juce::Graphics& g, juce::Rectangle<float> area, int kind, float thickness)
{
    if (model_ == nullptr) return;
    juce::Path p;
    bool first = true;
    for (const auto& band : model_->bands()) {
        float value = band.measuredMs;
        if (kind == 1) value = band.targetMs;
        if (kind == 2) value = band.predictedMs;
        if (kind == 3) {
            if (!band.hasMeasuredAfter) continue;
            value = band.measuredAfterMs;
        }
        const auto x = xForFrequency(band.frequencyHz, area);
        const auto y = yForMs(value, area);
        if (first) { p.startNewSubPath(x, y); first = false; }
        else p.lineTo(x, y);
    }
    g.strokePath(p, juce::PathStrokeType(thickness));
}

void RT60Graph::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 23, 28));
    auto area = getLocalBounds().toFloat().reduced(56.0f, 34.0f);

    g.setColour(juce::Colour::fromRGB(48, 54, 64));
    for (float ms : {200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f}) {
        const float y = yForMs(ms, area);
        g.drawHorizontalLine((int)y, area.getX(), area.getRight());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(juce::String((int)ms) + " ms", 4, (int)y - 9, 48, 18, juce::Justification::centredRight);
        g.setColour(juce::Colour::fromRGB(48, 54, 64));
    }

    for (float hz : {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f}) {
        const float x = xForFrequency(hz, area);
        g.drawVerticalLine((int)x, area.getY(), area.getBottom());
        g.setColour(juce::Colours::lightgrey);
        juce::String label = hz >= 1000.0f ? juce::String(hz / 1000.0f, hz == 1000.0f ? 0 : 1) + "k" : juce::String((int)hz);
        g.drawText(label, (int)x - 22, (int)area.getBottom() + 5, 44, 18, juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(48, 54, 64));
    }

    g.setColour(juce::Colour::fromRGB(240, 145, 70));
    drawCurve(g, area, 0, 2.5f); // measured
    g.setColour(juce::Colour::fromRGB(120, 190, 255));
    drawCurve(g, area, 1, 2.0f); // target
    g.setColour(juce::Colour::fromRGB(110, 220, 150));
    drawCurve(g, area, 2, 2.5f); // predicted
    g.setColour(juce::Colour::fromRGB(220, 120, 235));
    drawCurve(g, area, 3, 2.5f); // measured after

    g.setFont(15.0f);
    g.setColour(juce::Colour::fromRGB(240, 145, 70)); g.drawText("Measured", 64, 7, 90, 20, juce::Justification::left);
    g.setColour(juce::Colour::fromRGB(120, 190, 255)); g.drawText("Target", 160, 7, 70, 20, juce::Justification::left);
    g.setColour(juce::Colour::fromRGB(110, 220, 150)); g.drawText("Model Estimate", 236, 7, 130, 20, juce::Justification::left);
    g.setColour(juce::Colour::fromRGB(220, 120, 235)); g.drawText("Measured After", 374, 7, 130, 20, juce::Justification::left);
}
