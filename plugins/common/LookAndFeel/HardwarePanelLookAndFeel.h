#pragma once

#include "HardwarePanelTheme.h"

namespace wildjag
{
    // Shared "hardware panel" (Roland RE-501-inspired) chrome for all Wild Jag plugins: chassis
    // texture, panel gradients, section/badge styling, flat matte knobs, cream LED pushbuttons,
    // fader styling, font loading/metrics. Every draw*/get*Font method here is the COPY-VERBATIM
    // rendering logic that used to be duplicated per plugin -- a plugin now only supplies a
    // HardwarePanelTheme (accent colours + typefaces) and, rarely, a paintRotarySliderOverlay()
    // override for a knob decoration beyond the standard cap/ticks/pointer (e.g. Damage's live
    // gate-level tick).
    //
    // Building a NEW plugin's HTML mockup before writing any C++? See MOCKUP_GROUND_TRUTH.md in
    // this same folder first -- it pulls the exact knob geometry/stroke weights and per-label
    // font/colour choices out of this file and PluginEditor.cpp into one place, since guessing
    // them from a screenshot has repeatedly produced a visibly-wrong mockup (dots instead of
    // lines, wrong font on value readouts, wrong dim-text colours).
    class HardwarePanelLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        explicit HardwarePanelLookAndFeel(HardwarePanelTheme theme);

        void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

        void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;

        void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        // Same flat pushbutton chrome as drawToggleButton, minus the LED and on/off colour state -
        // for momentary-trigger buttons (e.g. Flux's Shift), which have no persistent "engaged"
        // state to indicate and so shouldn't get the universal engaged-LED treatment every toggle
        // button otherwise has.
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&,
                             bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;

        void drawLabel(juce::Graphics&, juce::Label&) override;
        void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
        void drawComboBoxTextWhenNothingSelected(juce::Graphics&, juce::ComboBox&, juce::Label&) override;

        juce::Font getLabelFont(juce::Label&) override;
        juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Label* createSliderTextBox(juce::Slider&) override;

        // Exposed so the editor can style the hand-painted brand wordmark, tag line, section
        // badges, and footer text with the same embedded typefaces the widgets use.
        juce::Font getDisplayFont(float height) const;
        juce::Font getSmallPrintFont(float height) const;

        // Exposed so hand-painted chrome in PluginEditor (section badges) reads the accent/ink
        // colours from here rather than duplicating the hex literals in two files.
        juce::Colour getAccentColour() const { return theme.accentMuted; }
        juce::Colour getBadgeInkColour() const { return theme.badgeInkColour; }

        // Exposed so a hand-painted list (e.g. Flux's Stages list -- a static LED + text row per
        // stage count, not a Component per row) can match the same LED colours drawToggleButton's
        // own pushbutton LEDs use, rather than a second copy of the hex literals. Fixed values,
        // not part of the per-plugin theme -- every plugin's LED is the same universal red/grey.
        juce::Colour getLedOnColour() const;
        juce::Colour getLedOffColour() const;

        // Every pushbutton's JUCE bounds must be this much larger than its visual size on each
        // side, so drawToggleButton's drop shadow has room to render without being clipped by the
        // component's own bounds. See PluginEditor::resized().
        static constexpr float buttonShadowMargin = 6.0f;

    protected:
        // The one extension point for a knob decoration beyond the standard cap/ticks/pointer.
        // Called at the end of drawRotarySlider with the same geometry it just computed, in the
        // same coordinate space. Default: no-op. Damage overrides this to draw its live
        // gate-level tick (see LevelMeterSlider).
        virtual void paintRotarySliderOverlay(juce::Graphics&, juce::Point<float> centre, float radius,
                                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) {}

        // The vertical fader thumb's width, given the full slider component bounds. Default
        // (0.8x bounds width) matches Caverns/Corrosion/Gradient's paired Dry/Wet faders. Damage
        // overrides to 0.4x for its slimmer single fader; Flux overrides to 0.8x-20px for its
        // wider single Blend fader. Not plugin-uniform despite looking that way at a glance --
        // verified by a pre/post-migration pixel diff per plugin, not by inspection alone.
        virtual float getLinearSliderThumbWidth(juce::Rectangle<float> bounds) const { return bounds.getWidth() * 0.8f; }

        // The slider text box's font height. Default (10.5px) matches every plugin except
        // Gradient, which overrides to 10.0px to match its mockup's .knob-value. Same
        // not-actually-uniform story as getLinearSliderThumbWidth above.
        virtual float getSliderTextBoxFontHeight() const { return 10.5f; }

        juce::Colour accentBrightHi() const { return theme.accentBrightHi; }
        juce::Colour accentBrightLo() const { return theme.accentBrightLo; }

    private:
        HardwarePanelTheme theme;
        juce::Typeface::Ptr displayTypeface;
        juce::Typeface::Ptr smallPrintTypeface;
    };
}
