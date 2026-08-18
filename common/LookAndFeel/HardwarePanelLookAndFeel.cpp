#include "HardwarePanelLookAndFeel.h"

namespace
{
    // Panel face (recessed control surface)
    const juce::Colour panelColourTop{0xff262d2f};
    const juce::Colour panelColourBottom{0xff171c1d};

    // Knobs -- flat/matte, no gradient, no lit value ring (deliberate hardware-authenticity choice)
    const juce::Colour knobFillColour{0xff16191a};
    const juce::Colour tickColour{0xff818f8c};
    const juce::Colour pointerColour{0xffc7c0ac};

    // Pushbuttons + LEDs
    const juce::Colour buttonOffColour{0xffcec8b7};
    const juce::Colour buttonOnColour{0xffdcd6c4};
    const juce::Colour buttonBorderColour{0xff625e51};
    const juce::Colour buttonTextColour{0xff2b2822};
    const juce::Colour ledOffColour{0xff8a8578};
    const juce::Colour ledOnColour{0xffe4463c}; // always red -- universal "engaged" indicator, independent of accent

    // ComboBox
    const juce::Colour comboFillTop{0xff1b2123};
    const juce::Colour comboFillBottom{0xff121718};
    const juce::Colour comboOutline{0xff3a4245};

    // Fader
    const juce::Colour faderTrackTop{0xff14191a};
    const juce::Colour faderTrackBottom{0xff1c2224};
    const juce::Colour faderThumbTop{0xff191d1e};
    const juce::Colour faderThumbBottom{0xff0c0e0f};

    const juce::Colour textColour{0xffe7f3f1};
    const juce::Colour textColourDim{0xffdcece9};
}

namespace wildjag
{

HardwarePanelLookAndFeel::HardwarePanelLookAndFeel(HardwarePanelTheme themeIn) : theme(themeIn)
{
    if (theme.displayTypeface.data != nullptr)
        displayTypeface = juce::Typeface::createSystemTypefaceFor(theme.displayTypeface.data,
                                                                    theme.displayTypeface.dataSize);
    if (theme.smallPrintTypeface.data != nullptr)
        smallPrintTypeface = juce::Typeface::createSystemTypefaceFor(theme.smallPrintTypeface.data,
                                                                       theme.smallPrintTypeface.dataSize);

    setColour(juce::ResizableWindow::backgroundColourId, panelColourBottom);
    // Matches the mockup's .knob-value -- dimmer than the .knob-name label colour (#dcece9, via
    // Label::textColourId below), not the same near-white for both.
    setColour(juce::Slider::textBoxTextColourId, theme.sliderTextBoxTextColour);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, textColourDim);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, theme.accentMuted);
    setColour(juce::ToggleButton::textColourId, buttonTextColour);
    setColour(juce::ComboBox::backgroundColourId, comboFillTop);
    setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    setColour(juce::ComboBox::outlineColourId, comboOutline);
    setColour(juce::ComboBox::arrowColourId, theme.accentMuted);
    setColour(juce::PopupMenu::backgroundColourId, comboFillBottom);
    setColour(juce::PopupMenu::textColourId, textColour);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme.accentMuted);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void HardwarePanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPosProportional, float rotaryStartAngle,
                                                 float rotaryEndAngle, juce::Slider& slider)
{
    // The area passed in here is often taller than it is wide: the editor reserves extra room
    // below the knob for a separately-positioned name label (matching the mockup's DOM order --
    // knob, then name, then value -- rather than a plugin-standard name-above-knob layout), so
    // the circle is flush-topped to a square using the width, leaving that extra height blank.
    juce::ignoreUnused(height);
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) width).reduced(4.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto capRadius = radius * 0.72f;
    const auto tickInnerRadius = radius * 0.84f;
    const auto tickOuterRadius = radius * 0.98f;

    // Static, printed-on-panel tick marks -- not lit by value, like real silkscreened dial
    // markings. A deliberate hardware-authenticity choice: real analog dials don't have a
    // glowing progress ring, only a pointer and fixed print.
    constexpr int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const auto t = (float) i / (float) (numTicks - 1);
        const auto tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const bool isMajor = (i == 0 || i == numTicks - 1 || i == (numTicks - 1) / 2);

        const auto p0 = centre.getPointOnCircumference(tickInnerRadius, tickAngle);
        const auto p1 = centre.getPointOnCircumference(tickOuterRadius, tickAngle);

        g.setColour(tickColour.withAlpha(isMajor ? 0.85f : 0.55f));
        g.drawLine({p0, p1}, isMajor ? 2.2f : 1.5f);
    }

    // Flat, matte knob cap -- no gradient, darker than the panel for contrast.
    g.setColour(knobFillColour);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawEllipse(centre.x - capRadius + 0.75f, centre.y - capRadius + 0.75f,
                  (capRadius - 0.75f) * 2.0f, (capRadius - 0.75f) * 2.0f, 1.0f);

    // Thin inset ring, separate from the outer edge stroke above -- a second, smaller circle
    // sitting inside the cap (mockup: <circle r="capR-5.5" stroke="rgba(0,0,0,0.4)"/>), not just
    // a highlight right at the rim.
    const auto insetRadius = capRadius - 5.5f;
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawEllipse(centre.x - insetRadius, centre.y - insetRadius, insetRadius * 2.0f, insetRadius * 2.0f, 1.0f);

    // Fluted grip rim.
    constexpr int numFlutes = 26;
    for (int i = 0; i < numFlutes; ++i)
    {
        const auto fluteAngle = (juce::MathConstants<float>::twoPi * (float) i) / (float) numFlutes;
        const auto p0 = centre.getPointOnCircumference(capRadius - 3.0f, fluteAngle);
        const auto p1 = centre.getPointOnCircumference(capRadius, fluteAngle);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawLine({p0, p1}, 1.0f);
    }

    // Pointer line -- muted cream, blunt-ended, not a glossy UI stroke.
    const auto pInner = centre.getPointOnCircumference(capRadius * 0.32f, angle);
    const auto pOuter = centre.getPointOnCircumference(capRadius - 5.0f, angle);
    g.setColour(pointerColour.withAlpha(0.88f));
    g.drawLine({pInner, pOuter}, 2.6f);

    paintRotarySliderOverlay(g, centre, radius, rotaryStartAngle, rotaryEndAngle, slider);
}

void HardwarePanelLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float minSliderPos, float maxSliderPos,
                                                 juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    const auto trackWidth = juce::jmin(12.0f, bounds.getWidth() * 0.3f);
    const auto track = juce::Rectangle<float>(bounds.getCentreX() - trackWidth * 0.5f, bounds.getY(),
                                               trackWidth, bounds.getHeight());

    g.setGradientFill(juce::ColourGradient(faderTrackTop, track.getX(), track.getY(),
                                            faderTrackBottom, track.getX(), track.getBottom(), false));
    g.fillRoundedRectangle(track, trackWidth * 0.5f);
    g.setColour(comboOutline);
    g.drawRoundedRectangle(track, trackWidth * 0.5f, 1.0f);

    const auto fill = juce::Rectangle<float>(track.getX(), sliderPos, track.getWidth(), track.getBottom() - sliderPos);

    // Soft glow around the fill bar, matching the mockup's box-shadow(0 0 10px accent @ 50%) --
    // without this it's just a flat filled bar with no sense of the accent colour actually
    // glowing against the dark track/panel.
    // Radius kept modest (not the mockup's full 10px) -- a component's paint() clips to its own
    // bounds, and at/near full value the fill's top edge sits right at the component's own top
    // edge, so a wide blur would clip there anyway; this is as large as it can go without that.
    if (fill.getHeight() > 0.0f)
    {
        juce::Path fillPath;
        fillPath.addRoundedRectangle(fill, trackWidth * 0.5f);
        juce::DropShadow(accentBrightHi().withAlpha(0.5f), 6, {0, 0}).drawForPath(g, fillPath);
    }

    g.setGradientFill(juce::ColourGradient(accentBrightHi(), fill.getX(), fill.getY(),
                                            accentBrightLo(), fill.getX(), fill.getBottom(), false));
    g.fillRoundedRectangle(fill, trackWidth * 0.5f);

    // Dark, thick, sharp-edged thumb -- deliberately not a pale rounded pill.
    const auto thumbWidth = getLinearSliderThumbWidth(bounds);
    const auto thumbHeight = 11.0f;
    const auto thumb = juce::Rectangle<float>(bounds.getCentreX() - thumbWidth * 0.5f, sliderPos - thumbHeight * 0.5f,
                                               thumbWidth, thumbHeight);
    g.setGradientFill(juce::ColourGradient(faderThumbTop, thumb.getX(), thumb.getY(),
                                            faderThumbBottom, thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle(thumb, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(thumb, 1.0f, 1.0f);
}

void HardwarePanelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                 bool shouldDrawButtonAsHighlighted, bool)
{
    // A component's own paint() is clipped to its local bounds, so a component sized exactly to
    // the visible button rect clips off most of a blurred drop shadow drawn around it. Every
    // button's JUCE bounds are set buttonShadowMargin px larger than its visual size on every
    // side (see PluginEditor::resized()) specifically to leave room for this shadow to render
    // in full -- inset by that same margin here to get back to the actual visual button rect.
    const auto bounds = button.getLocalBounds().toFloat().reduced(buttonShadowMargin);
    constexpr float cornerSize = 3.0f;
    const auto isOn = button.getToggleState();

    juce::Path buttonPath;
    buttonPath.addRoundedRectangle(bounds, cornerSize);

    juce::DropShadow shadow(juce::Colours::black.withAlpha(0.55f), 6, {0, 3});
    shadow.drawForPath(g, buttonPath);

    g.setColour(isOn ? buttonOnColour : buttonOffColour);
    g.fillPath(buttonPath);

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawLine(bounds.getX() + cornerSize, bounds.getY() + 0.5f, bounds.getRight() - cornerSize, bounds.getY() + 0.5f, 1.0f);

    g.setColour(buttonBorderColour);
    g.strokePath(buttonPath, juce::PathStrokeType(1.0f));

    constexpr float ledDiameter = 9.0f;
    const auto font = getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    g.setFont(font);
    const auto text = button.getButtonText().toUpperCase();
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, text);

    auto contentBounds = bounds.withSizeKeepingCentre(ledDiameter + 8.0f + textWidth, bounds.getHeight());

    const auto ledBounds = contentBounds.removeFromLeft(ledDiameter).withSizeKeepingCentre(ledDiameter, ledDiameter);
    if (isOn)
    {
        // A soft blurred glow (matching the mockup's box-shadow blur), not a crisp ring outline.
        juce::Path ledPath;
        ledPath.addEllipse(ledBounds);
        juce::DropShadow(ledOnColour.withAlpha(0.85f), 7, {0, 0}).drawForPath(g, ledPath);
    }
    g.setColour(isOn ? ledOnColour : ledOffColour);
    g.fillEllipse(ledBounds);

    contentBounds.removeFromLeft(8.0f);
    g.setColour(buttonTextColour.withAlpha(shouldDrawButtonAsHighlighted ? 1.0f : 0.92f));
    g.drawText(text, contentBounds, juce::Justification::centredLeft);
}

void HardwarePanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                     bool, bool shouldDrawButtonAsDown)
{
    // Same chrome as drawToggleButton's background above (flat fill, drop shadow, top highlight,
    // border) minus the LED - see the header comment on why this exists as a separate method.
    const auto bounds = button.getLocalBounds().toFloat().reduced(buttonShadowMargin);
    constexpr float cornerSize = 3.0f;

    juce::Path buttonPath;
    buttonPath.addRoundedRectangle(bounds, cornerSize);

    juce::DropShadow shadow(juce::Colours::black.withAlpha(0.55f), 6, {0, 3});
    shadow.drawForPath(g, buttonPath);

    // No persistent on/off state to show, but a momentary flash to buttonOnColour while actually
    // held down still gives the click some tactile feedback.
    g.setColour(shouldDrawButtonAsDown ? buttonOnColour : buttonOffColour);
    g.fillPath(buttonPath);

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawLine(bounds.getX() + cornerSize, bounds.getY() + 0.5f, bounds.getRight() - cornerSize, bounds.getY() + 0.5f, 1.0f);

    g.setColour(buttonBorderColour);
    g.strokePath(buttonPath, juce::PathStrokeType(1.0f));
}

void HardwarePanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                               bool shouldDrawButtonAsHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(buttonShadowMargin);
    g.setFont(getDisplayFont(11.0f).withExtraKerningFactor(0.06f));
    g.setColour(buttonTextColour.withAlpha(shouldDrawButtonAsHighlighted ? 1.0f : 0.92f));
    g.drawText(button.getButtonText().toUpperCase(), bounds, juce::Justification::centred);
}

void HardwarePanelLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    // LookAndFeel_V4's default drawLabel unconditionally draws an outline rect using
    // Label::outlineColourId - in practice that box kept showing up around slider value
    // readouts even with the colour set to transparentBlack, so this just never draws one.
    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = getLabelFont(label);

        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(font);

        const auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                          juce::jmax(1, (int) ((float) textArea.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());
    }
}

void HardwarePanelLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                             int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(1.0f);
    constexpr float cornerSize = 5.0f;

    g.setGradientFill(juce::ColourGradient(comboFillTop, bounds.getX(), bounds.getY(),
                                            comboFillBottom, bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, cornerSize);
    g.setColour(comboOutline);
    g.drawRoundedRectangle(bounds, cornerSize, 1.0f);

    const auto arrowZone = bounds.removeFromRight(bounds.getHeight());
    juce::Path arrow;
    arrow.addTriangle(arrowZone.getCentreX() - 4.0f, arrowZone.getCentreY() - 2.5f,
                       arrowZone.getCentreX() + 4.0f, arrowZone.getCentreY() - 2.5f,
                       arrowZone.getCentreX(), arrowZone.getCentreY() + 3.5f);
    g.setColour(box.isEnabled() ? theme.accentMuted : comboOutline);
    g.fillPath(arrow);
}

void HardwarePanelLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    // Default JUCE inset here is 1px, flush against the left edge -- the mockup's .combo has
    // padding:0 10px, so its text sits well clear of the border.
    constexpr int leftInset = 10;   // matches the mockup's .combo{padding:0 10px} exactly
    label.setBounds(leftInset, 1, box.getWidth() - box.getHeight() - leftInset, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void HardwarePanelLookAndFeel::drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    // The default here (LookAndFeel_V2) always draws this placeholder at 50% alpha, regardless
    // of ComboBox::textColourId -- a reasonable "nothing selected" convention in general, but the
    // mockup's static design text has no such state to depict, so halving it only reads as
    // "wrong colour", not as an intentional empty-state cue.
    g.setColour(box.findColour(juce::ComboBox::textColourId));
    g.setFont(getComboBoxFont(box));

    // g here is in the ComboBox's own coordinate space, not the Label's -- label.getLocalBounds()
    // is always (0,0,w,h) regardless of where positionComboBoxText() actually placed the label,
    // so using it here silently ignores that left inset and always draws flush-left. getBounds()
    // (the label's bounds *within its parent*, i.e. the combo) is the one that's actually correct
    // in this coordinate space.
    const auto textArea = getLabelBorderSize(label).subtractedFrom(label.getBounds());
    g.drawFittedText(box.getTextWhenNothingSelected(), textArea, label.getJustificationType(),
                      juce::jmax(1, (int) ((float) textArea.getHeight() / getComboBoxFont(box).getHeight())),
                      label.getMinimumHorizontalScale());
}

juce::Font HardwarePanelLookAndFeel::getDisplayFont(float height) const
{
    if (displayTypeface != nullptr)
        return juce::Font(juce::FontOptions(displayTypeface).withHeight(height * theme.displayTypeface.heightCorrectionRatio));
    return juce::Font(juce::FontOptions(height, juce::Font::bold));
}

juce::Font HardwarePanelLookAndFeel::getSmallPrintFont(float height) const
{
    if (smallPrintTypeface != nullptr)
        return juce::Font(juce::FontOptions(smallPrintTypeface).withHeight(height * theme.smallPrintTypeface.heightCorrectionRatio));
    return juce::Font(juce::FontOptions(height, juce::Font::bold));
}

juce::Font HardwarePanelLookAndFeel::getLabelFont(juce::Label& label)
{
    // Label::getFont() always returns whatever was last passed to setFont() -- including the
    // JUCE-internal default (FontOptions{15.0f}) if setFont() was never called at all. Comparing
    // against that sentinel is how JUCE itself distinguishes "no explicit font" from "explicit
    // font" here; getLabelFont() MUST return label.getFont() for the explicit case, or every
    // label's own setFont() (title, tag, field labels, the slider's value textbox) gets silently
    // clobbered back to whatever this function returns -- which is exactly the bug that shipped.
    static const juce::Font unset { juce::FontOptions{15.0f} };
    const auto labelFont = label.getFont();
    if (labelFont == unset)
        return getDisplayFont(11.0f).withExtraKerningFactor(0.09f);   // mockup's .knob-name default
    return labelFont;
}

juce::Font HardwarePanelLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return getDisplayFont((float) juce::jmin(16, buttonHeight - 6));
}

juce::Font HardwarePanelLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return getDisplayFont(11.5f);
}

juce::Colour HardwarePanelLookAndFeel::getLedOnColour() const { return ledOnColour; }
juce::Colour HardwarePanelLookAndFeel::getLedOffColour() const { return ledOffColour; }

juce::Label* HardwarePanelLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    // Matches the mockup's .knob-value (letter-spacing .03em) -- distinct from the 11px knob-name
    // size above, so this can't just come from getLabelFont().
    l->setFont(getDisplayFont(getSliderTextBoxFontHeight()).withExtraKerningFactor(0.03f));
    return l;
}

}
