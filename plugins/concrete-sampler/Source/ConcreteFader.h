#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"

// A vertical hardware fader - translated from ~/code/lcd-mockup's Fader.tsx/.module.css, for the
// Volume section's Sample/Master levels. Generic over a plain get/set float (not a
// RangedAudioParameter like ConcreteKnob) since neither Sample nor Master Volume is backed by an
// APVTS parameter yet - see PluginEditor's own comment on why. Linear, not log-scaled like
// Cutoff - a volume fader's whole range is meant to feel evenly spaced.
class ConcreteFader : public juce::Component
{
public:
    ConcreteFader(ConcreteLookAndFeel& lookAndFeelIn, juce::String label, float minValue, float maxValue,
                  std::function<float()> getValue, std::function<void(float)> setValue,
                  std::function<juce::String(float)> formatValue);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> trackBounds() const noexcept;
    void updateFromY(float y);

    ConcreteLookAndFeel& lookAndFeel;
    juce::String label;
    float minValue, maxValue;
    std::function<float()> getValue;
    std::function<void(float)> setValue;
    std::function<juce::String(float)> formatValue;
};
