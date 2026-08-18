#include "DamageLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme damageTheme
    {
        .accentMuted = juce::Colour{0xffb34d4a},
        .accentBrightHi = juce::Colour{0xfff04c42},
        .accentBrightLo = juce::Colour{0xffce3f37},
        .badgeInkColour = juce::Colour{0xff241009},
        .displayTypeface = { BinaryData::RajdhaniBold_ttf, (size_t) BinaryData::RajdhaniBold_ttfSize, 1.276f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

DamageLookAndFeel::DamageLookAndFeel() : HardwarePanelLookAndFeel(damageTheme) {}

void DamageLookAndFeel::paintRotarySliderOverlay(juce::Graphics& g, juce::Point<float> centre, float radius,
                                                  float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    // Live gate-level tick, Gate knob only. Not part of the base knob spec (knobs are otherwise
    // never lit/coloured by a live value) -- a deliberate one-off addition since Damage's Gate
    // knob doubles as a level meter against its own threshold.
    if (auto* metered = dynamic_cast<LevelMeterSlider*>(&slider))
    {
        const auto tickInnerRadius = radius * 0.84f;
        const auto tickOuterRadius = radius * 0.98f;

        const auto rangeMin = (float) slider.getMinimum();
        const auto rangeMax = (float) slider.getMaximum();
        const auto levelNorm = juce::jlimit(0.0f, 1.0f,
                                             (metered->getLevelDb() - rangeMin) / (rangeMax - rangeMin));
        const auto meterAngle = rotaryStartAngle + levelNorm * (rotaryEndAngle - rotaryStartAngle);

        const auto p0 = centre.getPointOnCircumference(tickInnerRadius - 1.5f, meterAngle);
        const auto p1 = centre.getPointOnCircumference(tickOuterRadius + 2.5f, meterAngle);

        juce::Path meterPath;
        meterPath.startNewSubPath(p0);
        meterPath.lineTo(p1);
        juce::DropShadow(meterLiveColour.withAlpha(0.8f), 3, {0, 0}).drawForPath(g, meterPath);

        g.setColour(meterLiveColour);
        g.strokePath(meterPath, juce::PathStrokeType(2.5f));
    }
}
