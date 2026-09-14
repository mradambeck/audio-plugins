#include "ConcreteScreen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace
{
    // ---- Palette, from ~/code/lcd-mockup/src/index.css's :root and LcdScreen.module.css ----
    const juce::Colour lcdInk { 0xffc4cef9 };          // --text
    const juce::Colour lcdBacklightLeft { 0xff1231de }; // --bg (.lcdScreen's gradient start)
    const juce::Colour lcdBacklightRight { 0xff0555eb };

    constexpr float screenWidth = 500.0f;
    constexpr float screenHeight = 250.0f;
    constexpr float screenPaddingX = 20.0f;
    constexpr float screenPaddingY = 14.0f;

    // ---- Boot sequence timing - see lcd-mockup's LcdScreen.tsx/BootScreen.tsx ----
    constexpr double bootContentDelayMs = 500.0;   // LcdScreen.tsx's boot-content fade-in delay
    constexpr double bootContentFadeMs = 150.0;
    constexpr double bootTotalDurationMs = 3600.0; // LcdScreen.tsx's BOOT_DURATION_MS
    constexpr double revealStepIntervalMs = 55.0;  // BootScreen.tsx's REVEAL_STEP_INTERVAL_MS

    const juce::String bootTitle = "Wild Jag Audio";
    const juce::String bootTagline = "Booting v0.1.0";

    // LcdScreen.tsx's power-on flicker: animate={{opacity:[0,1,0.15,1,0.4,1]}},
    // transition={{duration:0.6, times:[0,0.08,0.18,0.3,0.45,1], ease:"linear"}} - a piecewise-
    // linear interpolation between these (time-fraction, opacity) keyframes.
    const std::array<float, 6> flickerTimes { 0.0f, 0.08f, 0.18f, 0.3f, 0.45f, 1.0f };
    const std::array<float, 6> flickerOpacities { 0.0f, 1.0f, 0.15f, 1.0f, 0.4f, 1.0f };
    constexpr double flickerDurationMs = 600.0;

    // ---- The ghost logo - identical string/algorithm to BootScreen.tsx's GHOST/isSweepChar/
    // sweepDiagonals/pseudoRandom, translated from the TSX rather than re-derived. ----
    const juce::StringArray& ghostRows()
    {
        static const juce::StringArray rows {
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\x81\xe2\xa0\x80\xe2\xa0\x80\xe2\xa3\xa0\xe2\xa3\xb6\xe2\xa3\xbf\xe2\xa3\xb7\xe2\xa3\xb6\xe2\xa3\x84\xe2\xa0\x80\xe2\xa0\x80\xe2\xa0\x81")),
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\x88\xe2\xa0\x80\xe2\xa2\xb0\xe2\xa1\xbf\xe2\xa0\x9b\xe2\xa2\xbf\xe2\xa1\xbf\xe2\xa0\xbb\xe2\xa3\xbf\xe2\xa1\x86\xe2\xa0\x80\xe2\xa1\x80")),
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\x80\xe2\xa0\xa0\xe2\xa3\xbe\xe2\xa1\x87\xe2\xa0\x80\xe2\xa2\xb8\xe2\xa1\x87\xe2\xa0\x80\xe2\xa2\xb8\xe2\xa3\xa7\xe2\xa0\x80\xe2\xa0\x80")),
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\x80\xe2\xa0\x80\xe2\xa3\xbf\xe2\xa3\xb7\xe2\xa3\xa4\xe2\xa3\xbf\xe2\xa3\xb7\xe2\xa3\xa4\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa0\x80\xe2\xa0\x80")),
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\xa0\xe2\xa0\x80\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa3\xbf\xe2\xa0\x80\xe2\xa1\x80")),
            juce::String (juce::CharPointer_UTF8 ("\xe2\xa0\x84\xe2\xa1\x80\xe2\xa0\x99\xe2\xa0\x9f\xe2\xa2\xbf\xe2\xa3\xbf\xe2\xa1\xbf\xe2\xa0\xbf\xe2\xa0\xbf\xe2\xa0\x8b\xe2\xa0\x80\xe2\xa0\x80")),
        };
        return rows;
    }

    constexpr juce::juce_wchar blankChar = 0x2800; // BRAILLE PATTERN BLANK - ⠀
    const std::array<juce::juce_wchar, 3> dustChars { 0x2840, 0x2804, 0x2808 }; // ⡀ ⠄ ⠈

    bool isDustChar (juce::juce_wchar c) noexcept
    {
        return c == dustChars[0] || c == dustChars[1] || c == dustChars[2];
    }

    bool isSweepChar (juce::juce_wchar c) noexcept { return c != blankChar && ! isDustChar (c); }

    // Every (row,col) diagonal (row+col) that contains at least one sweep character, ascending -
    // one "reveal step" per unique diagonal, sweeping top-left to bottom-right. Computed once.
    const std::vector<int>& sweepDiagonals()
    {
        static const std::vector<int> diagonals = []
        {
            std::set<int> found;
            const auto& rows = ghostRows();
            for (int r = 0; r < rows.size(); ++r)
                for (int c = 0; c < rows[r].length(); ++c)
                    if (isSweepChar (rows[r][c]))
                        found.insert (r + c);
            return std::vector<int> (found.begin(), found.end());
        }();
        return diagonals;
    }

    int diagonalStep (int row, int col)
    {
        const auto& diagonals = sweepDiagonals();
        const auto it = std::find (diagonals.begin(), diagonals.end(), row + col);
        return it == diagonals.end() ? -1 : (int) std::distance (diagonals.begin(), it);
    }

    // Identical formula to BootScreen.tsx's pseudoRandom() - deterministic float in [0,1) from an
    // integer seed, used only to desync each dust glyph's blink rate from the others.
    float pseudoRandom (int seed) noexcept
    {
        const float x = std::sin ((float) seed * 12.9898f) * 43758.5453f;
        return x - std::floor (x);
    }

    // steps(1) at a CSS `@keyframes blink { 50% { opacity: 0; } }` - on for the first half of each
    // cycle, off for the second half, snapping (not fading) at the midpoint.
    bool blinkIsOn (double elapsedMs, double periodMs, double phaseOffsetMs) noexcept
    {
        const double t = std::fmod (elapsedMs - phaseOffsetMs, periodMs);
        const double positiveT = t < 0.0 ? t + periodMs : t;
        return positiveT < periodMs * 0.5;
    }

    // ---- Sample page - see WaveSurfer.tsx/.module.css/SelectableField.module.css ----
    // MainContent's own width is whatever paintSamplePage()'s `content` rect already is
    // (screenWidth - 2*screenPaddingX = 460) - not re-declared here since nothing needs it as a
    // standalone constant.
    constexpr float mainContentHeight = 140.0f;
    constexpr float mainContentBorder = 3.0f;
    constexpr float footerHeight = 28.0f;
    constexpr float footerBorder = 3.0f;
    // WaveSurfer.module.css's own authored widths (its comment: fixed, not fr/%) sum to 454px,
    // wider than playerArea's actual available width - see paintSamplePage's lastColumnWidth for
    // how that's reconciled. First three columns of each row keep these values exactly; only the
    // last (Loop/Play) is recomputed to fit.
    constexpr float gridColumnGap = 10.0f;
    const std::array<float, 4> topRowColumns { 52.0f, 80.0f, 120.0f, 172.0f };
    const std::array<float, 4> bottomRowColumns { 106.0f, 106.0f, 106.0f, 106.0f };

    // The mockup's own row width runs flush to the edge of the available space (Play's right
    // edge sits right against the pane border, no margin) - a deliberate departure from that
    // here, not a mockup-matching value: leave a visible gap on the right instead.
    constexpr float gridRowRightMargin = 12.0f;

    juce::String formatMinsSecs (double seconds)
    {
        const auto totalCentiseconds = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 100.0);
        const auto mins = totalCentiseconds / 6000;
        const auto secs = (totalCentiseconds % 6000) / 100;
        const auto centis = totalCentiseconds % 100;
        return juce::String (mins) + ":" + juce::String (secs).paddedLeft ('0', 2) + "."
               + juce::String (centis).paddedLeft ('0', 2);
    }

    juce::String formatMegabytes (juce::int64 bytes)
    {
        return juce::String (bytes / (1024.0 * 1024.0), 2) + " MB";
    }
}

ConcreteScreen::ConcreteScreen (ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn)
    : processor (processorIn), lookAndFeel (lookAndFeelIn)
{
    setSize ((int) screenWidth, (int) screenHeight);
    constructedAtMs = juce::Time::getMillisecondCounter();
    processor.processorStateBroadcaster.addChangeListener (this);
    startTimerHz (60);
}

ConcreteScreen::~ConcreteScreen()
{
    processor.processorStateBroadcaster.removeChangeListener (this);
}

double ConcreteScreen::elapsedBootMs() const noexcept
{
    return (double) (juce::Time::getMillisecondCounter() - constructedAtMs);
}

void ConcreteScreen::timerCallback()
{
    if (! isBooted && elapsedBootMs() >= bootTotalDurationMs)
        isBooted = true;
    repaint();
}

void ConcreteScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A background bake (Capture-page parameter, not built yet) may have changed the working
    // buffer's length/content - repaint so the waveform/duration stay honest. See this class's
    // header comment on why the flicker/busy treatment itself is scoped to isLoading only, not to
    // isBakeInProgress() generally.
    repaint();
}

float ConcreteScreen::lcdFlickerAlpha (double elapsedMs) const noexcept
{
    if (elapsedMs >= flickerDurationMs)
        return flickerOpacities.back();
    const float t = (float) (elapsedMs / flickerDurationMs);
    for (size_t i = 1; i < flickerTimes.size(); ++i)
    {
        if (t <= flickerTimes[i])
        {
            const float span = flickerTimes[i] - flickerTimes[i - 1];
            const float local = span > 0.0f ? (t - flickerTimes[i - 1]) / span : 1.0f;
            return juce::jmap (local, flickerOpacities[i - 1], flickerOpacities[i]);
        }
    }
    return flickerOpacities.back();
}

void ConcreteScreen::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // .lcdScreen's own gradient + glass recess - drawn full-bleed, then the whole rect's alpha is
    // modulated by the power-on flicker while booting (matching LcdScreen.tsx's motion.section).
    const auto flickerAlpha = isBooted ? 1.0f : lcdFlickerAlpha (elapsedBootMs());

    juce::Graphics::ScopedSaveState saveState (g);
    g.beginTransparencyLayer (flickerAlpha);

    juce::ColourGradient backlight (lcdBacklightLeft, bounds.getX(), bounds.getY(),
                                     lcdBacklightRight, bounds.getRight(), bounds.getY(), false);
    g.setGradientFill (backlight);
    g.fillRect (bounds);

    // Approximates box-shadow: inset 0 0 10px 2px rgba(0,0,0,0.5) - JUCE has no native inner
    // shadow. A small blur+spread inset shadow like this one only darkens a thin rim actually
    // touching the edges (roughly blur+spread =~ 12px, a bit more with typical blur falloff) and
    // leaves the rest of the box - the large majority of it - completely untouched; a single
    // radial gradient reaching toward the centre (the first attempt here) darkened far more of
    // the box than the real effect does and read as "way off" against the mockup. Four edge-
    // hugging linear fades (one per side) approximate the real rectangular falloff much more
    // closely than a radial gradient can; overlapping at the corners naturally reads darker there
    // too, same as the real shadow.
    constexpr float recessDepth = 16.0f;
    const auto recessColour = juce::Colours::black.withAlpha (0.5f);

    juce::ColourGradient topFade (recessColour, 0, bounds.getY(), juce::Colours::transparentBlack, 0, bounds.getY() + recessDepth, false);
    g.setGradientFill (topFade);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getY(), bounds.getWidth(), recessDepth));

    juce::ColourGradient bottomFade (recessColour, 0, bounds.getBottom(), juce::Colours::transparentBlack, 0, bounds.getBottom() - recessDepth, false);
    g.setGradientFill (bottomFade);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getBottom() - recessDepth, bounds.getWidth(), recessDepth));

    juce::ColourGradient leftFade (recessColour, bounds.getX(), 0, juce::Colours::transparentBlack, bounds.getX() + recessDepth, 0, false);
    g.setGradientFill (leftFade);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getY(), recessDepth, bounds.getHeight()));

    juce::ColourGradient rightFade (recessColour, bounds.getRight(), 0, juce::Colours::transparentBlack, bounds.getRight() - recessDepth, 0, false);
    g.setGradientFill (rightFade);
    g.fillRect (juce::Rectangle<float> (bounds.getRight() - recessDepth, bounds.getY(), recessDepth, bounds.getHeight()));

    const auto content = bounds.reduced (screenPaddingX, screenPaddingY);
    if (! isBooted)
    {
        paintBootScreen (g, content);
    }
    else
    {
        switch (currentPage)
        {
            case ScreenPage::Sample:  paintSamplePage (g, content); break;
            case ScreenPage::Machine: paintMachinePage (g, content); break;
            case ScreenPage::Filter:  paintFilterPage (g, content); break;
            case ScreenPage::Capture: paintCapturePage (g, content); break;
        }
    }

    g.endTransparencyLayer();
}

void ConcreteScreen::paintBootScreen (juce::Graphics& g, juce::Rectangle<float> content)
{
    const auto elapsed = elapsedBootMs();
    if (elapsed < bootContentDelayMs)
        return; // boot content stays fully invisible until the flicker settles

    const auto contentAlpha = juce::jlimit (0.0f, 1.0f,
        (float) ((elapsed - bootContentDelayMs) / bootContentFadeMs));

    juce::Graphics::ScopedSaveState saveState (g);
    g.beginTransparencyLayer (contentAlpha);

    // .BootScreen: flex column, space-evenly, centered, text-align:center. Dividing `content`
    // into three equal thirds (title/ghost/tagline) doesn't work - the ghost block (~130px tall)
    // is far taller than a 1/3 share of the ~222px content area, so it overflowed its band and
    // visually crowded both neighbors regardless of what gap the divider math intended. Measured
    // directly off a real mockup screenshot instead (see this class's own verification notes):
    // ghost sits centered in `content`, and title/tagline sit a fixed ~17px gap above/below it.
    const auto titleFont = lookAndFeel.getLcdFont (30.0f).withExtraKerningFactor (0.133f);
    const auto ghostFont = lookAndFeel.getLcdFont (20.0f);
    const auto taglineFont = lookAndFeel.getLcdFont (13.0f).withExtraKerningFactor (0.154f);

    constexpr float ghostRowPitch = 20.0f * 1.15f; // .ghost's line-height:1.15
    const auto ghostCharWidth = juce::GlyphArrangement::getStringWidth (ghostFont, "X");
    const auto ghostBlockWidth = ghostCharWidth * 12.0f;
    const auto ghostBlockHeight = ghostRowPitch * (float) ghostRows().size();
    constexpr float bootEdgeGapPx = 17.0f;

    const auto ghostBlockRect = content.withSizeKeepingCentre (ghostBlockWidth, ghostBlockHeight);
    const juce::Rectangle<float> titleBand (content.getX(), content.getY(),
                                             content.getWidth(), ghostBlockRect.getY() - bootEdgeGapPx - content.getY());
    const juce::Rectangle<float> taglineBand (content.getX(), ghostBlockRect.getBottom() + bootEdgeGapPx,
                                               content.getWidth(), content.getBottom() - (ghostBlockRect.getBottom() + bootEdgeGapPx));

    g.setColour (lcdInk);
    g.setFont (titleFont);
    g.drawText (bootTitle, titleBand, juce::Justification::centred);

    const auto ghostOrigin = ghostBlockRect.getTopLeft();
    const auto ghostStepCount = (int) std::floor (juce::jmax (0.0, elapsed - bootContentDelayMs) / revealStepIntervalMs);
    g.setFont (ghostFont);
    const auto& rows = ghostRows();
    for (int r = 0; r < rows.size(); ++r)
    {
        const auto& row = rows[r];
        for (int c = 0; c < row.length(); ++c)
        {
            const juce::juce_wchar ch = row[c];
            if (ch == blankChar)
                continue;

            const juce::Rectangle<float> cell (ghostOrigin.x + (float) c * ghostCharWidth,
                                                ghostOrigin.y + (float) r * ghostRowPitch,
                                                ghostCharWidth, ghostRowPitch);
            if (isSweepChar (ch))
            {
                if (diagonalStep (r, c) < ghostStepCount)
                {
                    g.setColour (lcdInk);
                    g.drawText (juce::String::charToString (ch), cell, juce::Justification::centred);
                }
            }
            else // dust
            {
                const auto seed = r * 100 + c;
                const double periodMs = (0.8 + pseudoRandom (seed) * 1.4) * 1000.0;
                const double delayMs = pseudoRandom (seed + 100) * 1500.0;
                if (blinkIsOn (elapsed - bootContentDelayMs, periodMs, delayMs))
                {
                    g.setColour (lcdInk);
                    g.drawText (juce::String::charToString (ch), cell, juce::Justification::centred);
                }
            }
        }
    }

    const auto tagElapsed = elapsed - bootContentDelayMs;
    const auto visibleChars = juce::jlimit (0, bootTagline.length(),
        (int) std::floor (tagElapsed / revealStepIntervalMs));
    g.setFont (taglineFont);
    const auto taglineWidth = juce::GlyphArrangement::getStringWidth (taglineFont, bootTagline)
                               + juce::GlyphArrangement::getStringWidth (taglineFont, "_") + 2.0f;
    auto taglineX = taglineBand.getCentreX() - taglineWidth / 2.0f;
    const auto taglineY = taglineBand.getCentreY() - taglineFont.getHeight() / 2.0f;
    for (int i = 0; i < bootTagline.length(); ++i)
    {
        const auto chStr = bootTagline.substring (i, i + 1);
        const auto w = juce::GlyphArrangement::getStringWidth (taglineFont, chStr);
        if (i < visibleChars)
        {
            // .tagline's own text-transform:uppercase (BootScreen.module.css) - the stored string
            // stays mixed-case (matching BootScreen.tsx's own TAGLINE constant) so width/step math
            // above is unaffected; only the drawn glyph is transformed, same split as the CSS.
            g.setColour (lcdInk);
            g.drawText (chStr.toUpperCase(), juce::Rectangle<float> (taglineX, taglineY, w, taglineFont.getHeight()),
                        juce::Justification::centred);
        }
        taglineX += w;
    }
    if (visibleChars >= bootTagline.length())
    {
        // Cursor's own blink: animation: blink 1s steps(1) infinite - starts fresh the instant
        // typing finishes, so its phase is anchored to that moment, not to elapsedBootMs() zero.
        const auto cursorElapsed = tagElapsed - (double) bootTagline.length() * revealStepIntervalMs;
        if (blinkIsOn (cursorElapsed, 1000.0, 0.0))
        {
            g.setColour (lcdInk);
            g.drawText ("_", juce::Rectangle<float> (taglineX + 2.0f, taglineY, 12.0f, taglineFont.getHeight()),
                        juce::Justification::centredLeft);
        }
    }

    g.endTransparencyLayer();
}

void ConcreteScreen::paintHeader (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (lcdInk);
    g.setFont (lookAndFeel.getLcdFont (16.0f));
    g.drawText ("Wild Jag Audio", area, juce::Justification::centredLeft);
    g.drawText ("Concrete v" + juce::String (JucePlugin_VersionString), area, juce::Justification::centredRight);
}

void ConcreteScreen::paintFooter (juce::Graphics& g, juce::Rectangle<float> area)
{
    // `area` is Footer's full content-box footprint INCLUDING its own bottom border (see
    // paintSamplePage's own comment on why MainContent/Footer add their border outside their
    // declared height rather than inset within it - no box-sizing:border-box reset in the
    // mockup). border-top:0 - MainContent's own bottom border (drawn immediately above, with no
    // gap) already reads as the line between them, so only left/right/bottom are drawn here, and
    // tab content is confined to the area ABOVE the bottom border strip.
    g.setColour (lcdInk);
    g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), footerBorder, area.getHeight()));
    g.fillRect (juce::Rectangle<float> (area.getRight() - footerBorder, area.getY(), footerBorder, area.getHeight()));
    g.fillRect (juce::Rectangle<float> (area.getX(), area.getBottom() - footerBorder, area.getWidth(), footerBorder));

    const auto tabArea = area.withTrimmedBottom (footerBorder);
    const auto pageNames = { "Sample", "Machine", "Filter", "Capture" };
    const auto tabWidth = tabArea.getWidth() / (float) pageNames.size();
    // NOT 16px, on purpose - FooterBtn.module.css sets font-family but never an explicit
    // font-size, and unlike every other button in this codebase (SelectableField, .playButton -
    // both explicitly set 12px) it has no font-size/font:inherit rule. Browsers don't inherit
    // font-size onto <button> elements by default, so this one actually renders at Chrome's
    // default button size (~13px), not the 16px it would get by inheriting .lcdScreen's own
    // font-size the way every other piece of screen text does. Confirmed by measuring "Machine"/
    // "Capture" (mock: 53px wide) against "Wild Jag Audio" in the same screenshot (mock: 132px
    // for 14 monospace characters, i.e. ~9.4px/char vs the footer's ~7.6px/char) - matching the
    // mockup's actual rendered footer, not the 16px the surrounding CSS implies but never reaches.
    // Worth fixing in the mockup itself (an explicit font-size on .FooterBtn) since this reads as
    // an oversight rather than a deliberate choice - flagging rather than "fixing" unilaterally.
    g.setFont (lookAndFeel.getLcdFont (13.0f));

    int index = 0;
    for (const auto* name : pageNames)
    {
        const juce::Rectangle<float> tab (tabArea.getX() + tabWidth * (float) index, tabArea.getY(), tabWidth, tabArea.getHeight());
        footerTabBounds[(size_t) index] = tab;
        const bool selected = index == (int) currentPage; // ScreenPage's own declaration order matches pageNames
        if (selected)
        {
            g.setColour (lcdInk);
            g.fillRect (tab);
            g.setColour (lcdBacklightLeft);
        }
        else
        {
            g.setColour (lcdInk);
        }
        g.drawText (name, tab, juce::Justification::centred);
        if (index > 0)
        {
            g.setColour (lcdInk);
            g.fillRect (juce::Rectangle<float> (tab.getX(), tab.getY(), footerBorder, tab.getHeight()));
        }
        ++index;
    }
}

void ConcreteScreen::paintLoadingOverlay (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& message)
{
    // Reuses the boot sequence's own flicker feel (same visual language, see this class's header
    // comment) rather than a bespoke spinner - a few uneven on/off beats while loadSampleAsync()
    // (or, on the Capture page, a real background bake) is in flight. Anchored to elapsedBootMs()
    // directly rather than to when the in-progress state started, since (unlike the boot sequence
    // itself) there's no fixed duration to phase the flicker against - it just needs to keep
    // pulsing for as long as the real operation takes.
    const auto alpha = lcdFlickerAlpha (std::fmod (elapsedBootMs(), flickerDurationMs * 2.0));
    g.setColour (lcdInk.withAlpha (juce::jlimit (0.15f, 1.0f, alpha)));
    g.setFont (lookAndFeel.getLcdFont (14.0f));
    g.drawText (message, area, juce::Justification::centred);
}

juce::Rectangle<float> ConcreteScreen::paintPageChrome (juce::Graphics& g, juce::Rectangle<float> content)
{
    fieldHitAreas.clear();

    auto area = content;
    // Measured off a real mockup screenshot (juce::Font's own getHeight() undershoots the actual
    // rendered line-box here - see this class's verification notes) rather than derived from CSS
    // line-height theory, which came out visibly short and left the whole MainContent/Footer
    // block sitting too high.
    constexpr float headerHeight = 27.0f;
    paintHeader (g, area.removeFromTop (headerHeight));
    area.removeFromTop (6.0f); // MainContent's margin-top

    // The mockup has no box-sizing:border-box reset, so .MainContent's declared height:140px is
    // its CONTENT box - the 3px border is drawn OUTSIDE that and adds to the total footprint
    // (146px), rather than being inset within 140px the way this used to draw it. Confirmed by
    // measuring a real screenshot: MainContent+Footer's combined footprint is 177px
    // (146 + 31, see paintFooter), not 168 (140 + 28) - an under-allocation that was the other
    // main contributor to the block sitting too high overall.
    auto mainContentTotal = area.removeFromTop (mainContentHeight + 2.0f * mainContentBorder);
    g.setColour (lcdInk);
    g.drawRect (mainContentTotal, mainContentBorder);
    auto inner = mainContentTotal.reduced (mainContentBorder);

    // MainContent and Footer share a border line (no gap) - Footer's own footprint is its 28px
    // content height plus its own 3px BOTTOM border (border-top:0, added outside per the
    // content-box note above). Painted here, before the page-specific content that fills `inner`
    // below, since the two never overlap and every page (Sample/Machine/Filter/Capture) shares
    // this exact same header/MainContent-border/footer shell - only what's inside `inner` differs.
    paintFooter (g, area.removeFromTop (footerHeight + footerBorder));
    return inner;
}

void ConcreteScreen::drawField (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& id,
                                 const juce::String& label, const juce::String& value)
{
    const bool selected = selectedFieldId == id;
    if (selected)
    {
        g.setColour (lcdInk);
        g.fillRect (cell);
        g.setColour (lcdBacklightLeft);
    }
    else
    {
        g.setColour (lcdInk);
    }
    g.setFont (lookAndFeel.getLcdFont (12.0f));
    g.drawText (label + ": " + value, cell.reduced (2.0f, 0.0f), juce::Justification::centredLeft);
    fieldHitAreas.push_back ({ id, cell, [this, id] { selectedFieldId = id; repaint(); } });
}

void ConcreteScreen::paintStatusLine (juce::Graphics& g, juce::Rectangle<float> area)
{
    // StatusLine.tsx: "Machine: <name>" - repeats the selected machine on every non-Sample page,
    // since the physical Machine selector sits far from the screen. getCurrentValueAsText() reads
    // the live AudioParameterChoice directly, so this can never drift out of sync with what the
    // Machine selector itself shows, unlike hand-indexing into getConcreteMachines() separately.
    const auto* machineParam = processor.apvts.getParameter (ConcreteAudioProcessor::machineParamID);
    g.setColour (lcdInk);
    g.setFont (lookAndFeel.getLcdFont (14.0f));
    g.drawText ("Machine: " + machineParam->getCurrentValueAsText(), area, juce::Justification::centred);
}

void ConcreteScreen::paintFieldGrid (juce::Graphics& g, juce::Rectangle<float>& area, const std::vector<std::vector<GridField>>& rows)
{
    // Machine/Filter/Capture.module.css: grid-template-columns: 219px 219px, column-gap:16px,
    // line-height:1.3 at 12px, no row-gap. Column 1 keeps its full authored width; column 2 is
    // shrunk to fit plus a margin, the same "only the last column gives up space" fix (and for the
    // same reason) as Sample page's own field grid - see paintSamplePage's lastColumnWidth.
    constexpr float col1Width = 219.0f;
    constexpr float rowHeight = 12.0f * 1.3f;
    const auto col2Width = juce::jmax (20.0f, area.getWidth() - gridRowRightMargin - col1Width - gridColumnGap);

    g.setFont (lookAndFeel.getLcdFont (12.0f));
    for (const auto& row : rows)
    {
        auto rowArea = area.removeFromTop (rowHeight);
        if (row.size() >= 1)
            drawField (g, juce::Rectangle<float> (rowArea.getX(), rowArea.getY(), col1Width, rowHeight),
                       row[0].id, row[0].label, row[0].value);
        if (row.size() >= 2)
            drawField (g, juce::Rectangle<float> (rowArea.getX() + col1Width + gridColumnGap, rowArea.getY(), col2Width, rowHeight),
                       row[1].id, row[1].label, row[1].value);
    }
}

void ConcreteScreen::paintSamplePage (juce::Graphics& g, juce::Rectangle<float> content)
{
    auto inner = paintPageChrome (g, content);

    if (isDraggingFileOver)
    {
        g.setColour (lcdInk.withAlpha (0.1f));
        g.fillRect (inner);
    }

    const auto sampleSet = processor.getCurrentSampleSet();
    const bool hasSample = sampleSet != nullptr && ! sampleSet->zones.empty()
                            && sampleSet->zones[0].sourceBuffer != nullptr;

    if (isLoading)
    {
        paintLoadingOverlay (g, inner, "Loading...");
    }
    else if (! hasSample)
    {
        g.setColour (lcdInk);
        // Empty state's own 2px DASHED border (WaveSurfer.module.css's .dropzone), inset slightly
        // from MainContent's own solid border so both remain visible, matching the mockup's nested
        // dashed-inside-solid look.
        const auto dashArea = inner.reduced (2.0f);
        float dashLengths[] { 4.0f, 3.0f };
        juce::Path dashPath;
        dashPath.addRectangle (dashArea);
        juce::PathStrokeType (2.0f).createDashedStroke (dashPath, dashPath, dashLengths, 2);
        g.strokePath (dashPath, juce::PathStrokeType (2.0f));

        g.setColour (lcdInk.withAlpha (0.7f));
        g.setFont (lookAndFeel.getLcdFont (16.0f));
        g.drawText ("Drop an audio file here", inner, juce::Justification::centred);
    }
    else
    {
        const auto& zone = sampleSet->zones[0];

        // .dropzone is `display:flex; align-items:center` with `.player{width:100%}` - the loaded
        // content is full-width but vertically CENTERED within the box, not top-packed. 8px is
        // .dropzone's own padding on every side; contentHeight is the fixed sum of every row drawn
        // below (fileName line, waveform, two grid rows, the margins between them).
        constexpr float contentHeight = 14.0f * 1.2f + 4.0f + 40.0f + 4.0f + 12.0f * 1.3f + 4.0f + 12.0f * 1.3f;
        const auto availableHeight = inner.getHeight() - 16.0f;
        const auto topInset = 8.0f + juce::jmax (0.0f, (availableHeight - contentHeight) / 2.0f);
        auto playerArea = inner.withTrimmedLeft (8.0f).withTrimmedRight (8.0f).withTrimmedTop (topInset);

        const auto fileName = zone.sourcePath.isNotEmpty()
                                   ? juce::File (zone.sourcePath).getFileName()
                                   : juce::String ("(embedded)");
        const auto sizeText = formatMegabytes ((juce::int64) (zone.sourceBuffer->getNumSamples()
                                                               * (juce::int64) zone.sourceBuffer->getNumChannels()
                                                               * (int) sizeof (float)));

        g.setColour (lcdInk);
        g.setFont (lookAndFeel.getLcdFont (14.0f));
        auto fileNameLine = playerArea.removeFromTop (14.0f * 1.2f);
        g.drawText ("Sample: " + fileName + " - " + sizeText, fileNameLine, juce::Justification::centredLeft);
        playerArea.removeFromTop (4.0f);

        constexpr float waveformHeight = 40.0f;
        auto waveformArea = playerArea.removeFromTop (waveformHeight);
        {
            // WavesurferPlayer's own barWidth={3} barGap={1} (WaveSurfer.tsx) - chunky discrete
            // bars on a 4px pitch, not a continuous per-pixel min/max trace (which is what the OLD
            // Phase 1 ConcreteWaveformDisplay this replaced drew, and what this code used to copy).
            constexpr float barWidth = 3.0f;
            constexpr float barGap = 1.0f;
            constexpr float barPitch = barWidth + barGap;

            const auto& buffer = *zone.sourceBuffer;
            const auto numSamples = buffer.getNumSamples();
            const auto midY = waveformArea.getCentreY();
            const auto halfHeight = waveformArea.getHeight() * 0.5f;
            const auto left = waveformArea.getX();
            const auto width = waveformArea.getWidth();
            const auto numBars = (int) (width / barPitch);

            g.setColour (lcdInk);
            for (int bar = 0; bar < numBars; ++bar)
            {
                const auto barX = (float) bar * barPitch;
                const auto rangeStart = (int) ((double) barX / (double) width * numSamples);
                const auto rangeEnd = juce::jmax (rangeStart + 1,
                    (int) ((double) (barX + barWidth) / (double) width * numSamples));
                float peak = 0.0f;
                for (int i = rangeStart; i < juce::jmin (rangeEnd, numSamples); ++i)
                    peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));
                const auto barHeight = juce::jmax (1.0f, peak * halfHeight * 2.0f);
                g.fillRect (juce::Rectangle<float> (left + barX, midY - barHeight * 0.5f, barWidth, barHeight));
            }

            // Loop region overlay - static for this slice (drag-to-resize is deferred, see
            // ui-plan.md's loop-marker scope and this class's header comment). Fixed at the
            // mockup's own default placement (10%-90% of duration) rather than wired to
            // zone.loopStart/loopEnd, since there's no editing UI yet to have moved them from
            // there.
            if (zone.loopEnabled)
            {
                const auto regionStart = waveformArea.getX() + width * 0.1f;
                const auto regionEnd = waveformArea.getX() + width * 0.9f;
                g.setColour (juce::Colour (0xff0555eb).withAlpha (0.25f));
                g.fillRect (juce::Rectangle<float> (regionStart, waveformArea.getY(), regionEnd - regionStart, waveformArea.getHeight()));
            }
        }
        playerArea.removeFromTop (4.0f);

        const auto durationSeconds = zone.sourceSampleRate > 0.0
                                          ? (double) zone.sourceBuffer->getNumSamples() / zone.sourceSampleRate
                                          : 0.0;

        // CSS Grid's default justify-content:start packs the defined columns at the row's own
        // left edge and leaves any leftover width unused on the right - it does NOT center them.
        // withSizeKeepingCentre() used to be used here, which (since gridRowWidth is wider than
        // playerArea's actual ~438px) added ~8px on EACH side, shifting the row's left edge past
        // playerArea's own left margin and flush against the border - exactly the "fields bumping
        // the edge instead of aligning with the waveform" bug. Fixing that by anchoring X at
        // playerArea's left edge (unchanged from that fix) isn't enough on its own though - the
        // row's declared 454px width is ALSO wider than playerArea's ~438px, so anchoring left
        // instead pushed the overflow onto the right edge instead (Play spilling past the pane's
        // border). Uniformly scaling every column down to fit was the first attempt at a fix, but
        // it shrank the duration field enough to start ellipsizing "0:21.57" - a regression - to
        // buy back the same few pixels of margin that only the LAST column (Loop/Play) actually
        // needs to give up. Shrinking only the last column - explicitly requested for Play besides
        // - avoids that: everything else keeps its original, already-comfortable width, and the
        // last column absorbs both the overflow fix and the requested right margin by itself.
        const auto lastColumnWidth = [&] (const std::array<float, 4>& columns)
        {
            const auto othersWidth = columns[0] + columns[1] + columns[2] + 3.0f * gridColumnGap;
            return juce::jmax (20.0f, playerArea.getWidth() - gridRowRightMargin - othersWidth);
        };

        auto topRow = playerArea.removeFromTop (12.0f * 1.3f);
        {
            auto x = topRow.getX();
            auto cell = [&] (float w) { auto r = juce::Rectangle<float> (x, topRow.getY(), w, topRow.getHeight()); x += w + gridColumnGap; return r; };

            const auto durationCell = cell (topRowColumns[0]);
            g.setColour (lcdInk);
            g.setFont (lookAndFeel.getLcdFont (12.0f));
            g.drawText (formatMinsSecs (durationSeconds), durationCell, juce::Justification::centredLeft);

            drawField (g, cell (topRowColumns[1]), "root", "Root",
                       juce::MidiMessage::getMidiNoteName (zone.rootNote, true, true, 3));
            drawField (g, cell (topRowColumns[2]), "oneShot", "One-Shot", zone.oneShot ? "On" : "Off");
            drawField (g, cell (lastColumnWidth (topRowColumns)), "loop", "Loop",
                       zone.loopEnabled ? (formatMinsSecs (durationSeconds * 0.1) + "-" + formatMinsSecs (durationSeconds * 0.9))
                                        : "Off");
        }

        playerArea.removeFromTop (4.0f);
        auto bottomRow = playerArea.removeFromTop (12.0f * 1.3f);
        {
            auto x = bottomRow.getX();
            auto cell = [&] (float w) { auto r = juce::Rectangle<float> (x, bottomRow.getY(), w, bottomRow.getHeight()); x += w + gridColumnGap; return r; };

            const auto* coarseParam = processor.apvts.getParameter (ConcreteAudioProcessor::coarseTuneParamID);
            const auto* fineParam = processor.apvts.getParameter (ConcreteAudioProcessor::fineTuneParamID);
            drawField (g, cell (bottomRowColumns[0]), "coarse", "Coarse",
                       juce::String (coarseParam->convertFrom0to1 (coarseParam->getValue()), 2));
            drawField (g, cell (bottomRowColumns[1]), "fine", "Fine",
                       juce::String (fineParam->convertFrom0to1 (fineParam->getValue()), 1));
            drawField (g, cell (bottomRowColumns[2]), "volume", "Volume",
                       juce::String ((int) std::lround (localVolumePercent)) + "%");

            const auto playCell = cell (lastColumnWidth (bottomRowColumns));
            g.setColour (lcdInk);
            g.drawRect (playCell, 1.0f);
            g.setFont (lookAndFeel.getLcdFont (12.0f));
            g.drawText ("Play", playCell, juce::Justification::centred);
        }
    }
}

void ConcreteScreen::paintMachinePage (juce::Graphics& g, juce::Rectangle<float> content)
{
    auto inner = paintPageChrome (g, content);
    auto area = inner.reduced (8.0f); // .Machine{padding:8px; box-sizing:border-box}

    paintStatusLine (g, area.removeFromTop (14.0f * 1.2f));
    area.removeFromTop (6.0f); // .grid's margin-top

    auto& apvts = processor.apvts;
    const auto* pitchEngineParam = apvts.getParameter (ConcreteAudioProcessor::pitchEngineModeParamID);
    const auto* quantModeParam = apvts.getParameter (ConcreteAudioProcessor::quantizerModeParamID);
    const auto* baseRateParam = apvts.getParameter (ConcreteAudioProcessor::baseRateParamID);
    const auto* voiceCountParam = apvts.getParameter (ConcreteAudioProcessor::voiceCountParamID);
    const auto* bitDepthParam = apvts.getParameter (ConcreteAudioProcessor::bitDepthParamID);
    const auto* ampEnvParam = apvts.getParameter (ConcreteAudioProcessor::ampEnvelopeModeParamID);

    const std::vector<std::vector<GridField>> rows {
        { { "pitchEngine", "Pitch Engine", pitchEngineParam->getCurrentValueAsText() },
          { "companding", "Companding", quantModeParam->getCurrentValueAsText() } },
        { { "baseRate", "Base Rate",
            juce::String (juce::roundToInt (baseRateParam->convertFrom0to1 (baseRateParam->getValue()))) + " Hz" },
          { "voices", "Voices",
            juce::String (juce::roundToInt (voiceCountParam->convertFrom0to1 (voiceCountParam->getValue()))) } },
        { { "bitDepth", "Bit Depth",
            juce::String (juce::roundToInt (bitDepthParam->convertFrom0to1 (bitDepthParam->getValue()))) + "-bit" },
          { "volume", "Volume", juce::String ((int) std::lround (localVolumePercent)) + "%" } },
    };
    paintFieldGrid (g, area, rows);

    // Amp Envelope is its own centered row below the grid - Machine.tsx's one lone unpaired row.
    area.removeFromTop (6.0f); // .envRow's margin-top
    const auto envRow = area.removeFromTop (12.0f * 1.3f);
    const auto envCell = envRow.withSizeKeepingCentre (juce::jmin (219.0f, envRow.getWidth()), envRow.getHeight());
    g.setFont (lookAndFeel.getLcdFont (12.0f));
    drawField (g, envCell, "ampEnvelope", "Amp Envelope", ampEnvParam->getCurrentValueAsText());
}

void ConcreteScreen::paintFilterPage (juce::Graphics& g, juce::Rectangle<float> content)
{
    auto inner = paintPageChrome (g, content);
    auto area = inner.reduced (8.0f);

    paintStatusLine (g, area.removeFromTop (14.0f * 1.2f));
    area.removeFromTop (6.0f);

    auto& apvts = processor.apvts;
    const auto* filterModelParam = apvts.getParameter (ConcreteAudioProcessor::filterModelParamID);
    const auto* cutoffParam = apvts.getParameter (ConcreteAudioProcessor::filterCutoffParamID);
    const auto* resonanceParam = apvts.getParameter (ConcreteAudioProcessor::filterResonanceParamID);
    const auto* envAmountParam = apvts.getParameter (ConcreteAudioProcessor::filterEnvAmountParamID);
    const auto* keyTrackParam = apvts.getParameter (ConcreteAudioProcessor::filterKeyTrackParamID);

    // Resonance's row has only one column filled - grid auto-placement leaves the second blank,
    // same as Filter.tsx's own comment on why no special-casing is needed there either.
    const std::vector<std::vector<GridField>> rows {
        { { "filterModel", "Filter Model", filterModelParam->getCurrentValueAsText() },
          { "envAmount", "Env Amount",
            juce::String (envAmountParam->convertFrom0to1 (envAmountParam->getValue()), 2) } },
        { { "cutoff", "Cutoff",
            juce::String (juce::roundToInt (cutoffParam->convertFrom0to1 (cutoffParam->getValue()))) + " Hz" },
          { "keyTrack", "Key Tracking",
            juce::String (keyTrackParam->convertFrom0to1 (keyTrackParam->getValue()), 2) } },
        { { "resonance", "Resonance",
            juce::String (resonanceParam->convertFrom0to1 (resonanceParam->getValue()), 3) } },
    };
    paintFieldGrid (g, area, rows);
}

void ConcreteScreen::paintCapturePage (juce::Graphics& g, juce::Rectangle<float> content)
{
    auto inner = paintPageChrome (g, content);
    auto area = inner.reduced (8.0f);

    paintStatusLine (g, area.removeFromTop (14.0f * 1.2f));
    area.removeFromTop (6.0f);

    auto& apvts = processor.apvts;
    const auto* transposeParam = apvts.getParameter (ConcreteAudioProcessor::captureTransposeParamID);
    const auto* driveParam = apvts.getParameter (ConcreteAudioProcessor::captureDriveParamID);
    const auto* iterationsParam = apvts.getParameter (ConcreteAudioProcessor::captureIterationsParamID);
    const auto* bypassParam = apvts.getParameter (ConcreteAudioProcessor::captureBypassParamID);
    const auto* pitchCompParam = apvts.getParameter (ConcreteAudioProcessor::captureAutoCompensateParamID);

    const std::vector<std::vector<GridField>> rows {
        { { "transpose", "Transpose",
            "+" + juce::String (juce::roundToInt (transposeParam->convertFrom0to1 (transposeParam->getValue()))) + "st" },
          { "bypass", "Bypass", bypassParam->getValue() >= 0.5f ? "On" : "Off" } },
        { { "drive", "Drive",
            juce::String (driveParam->convertFrom0to1 (driveParam->getValue()), 1) + " dB" },
          { "pitchCompensate", "Pitch Compensate", pitchCompParam->getValue() >= 0.5f ? "On" : "Off" } },
        { { "iterations", "Iterations",
            juce::String (juce::roundToInt (iterationsParam->convertFrom0to1 (iterationsParam->getValue()))) },
          { "saveSample", "Save Sample",
            processor.getEmbedSamplesOverride() ? "On (large)" : "Off (ref)" } },
    };
    paintFieldGrid (g, area, rows);

    // Bake progress - real state now (isBakeInProgress()/processorStateBroadcaster), not
    // Capture.tsx's own simulated 1400ms fake timer (see useCaptureStatus.tsx: "Mirrors
    // processor-plumbing gap #1... the real plugin has no public bake-in-progress signal yet
    // either" - it does now). There's no real FRACTION available (ConcreteCapturePass::apply()
    // doesn't report partial progress), so this is an honest indeterminate "Capturing..." state
    // reusing the same flicker language as the async-load overlay, rather than faking a percentage
    // that could desync from how long the real bake actually takes.
    area.removeFromTop (6.0f); // .bakeRow's margin-top
    const auto bakeRow = area.removeFromTop (18.0f);
    if (processor.isBakeInProgress())
    {
        const auto barArea = bakeRow.withSizeKeepingCentre (220.0f, 18.0f);
        g.setColour (lcdInk);
        g.drawRect (barArea, 1.0f);
        paintLoadingOverlay (g, barArea.reduced (1.0f), "Capturing...");
    }
    else
    {
        const auto buttonArea = bakeRow.withSizeKeepingCentre (
            juce::GlyphArrangement::getStringWidth (lookAndFeel.getLcdFont (12.0f), "Resample Now") + 20.0f, 18.0f);
        g.setColour (lcdInk);
        g.drawRect (buttonArea, 1.0f);
        g.setFont (lookAndFeel.getLcdFont (12.0f));
        g.drawText ("Resample Now", buttonArea, juce::Justification::centred);
        fieldHitAreas.push_back ({ "resampleNow", buttonArea, [this] { processor.triggerBake(); repaint(); } });
    }
}

void ConcreteScreen::mouseDown (const juce::MouseEvent& event)
{
    const auto pos = event.position;
    for (const auto& hit : fieldHitAreas)
    {
        if (hit.bounds.contains (pos))
        {
            if (hit.onTap)
                hit.onTap();
            return;
        }
    }

    if (! isBooted)
        return;

    static const std::array<ScreenPage, 4> tabPages { ScreenPage::Sample, ScreenPage::Machine, ScreenPage::Filter, ScreenPage::Capture };
    for (size_t i = 0; i < footerTabBounds.size(); ++i)
    {
        if (footerTabBounds[i].contains (pos))
        {
            setCurrentPage (tabPages[i]);
            return;
        }
    }
}

void ConcreteScreen::setCurrentPage (ScreenPage page)
{
    if (page == currentPage)
        return;
    currentPage = page;
    selectedFieldId = firstFieldIdForPage (page);
    repaint();
}

juce::String ConcreteScreen::firstFieldIdForPage (ScreenPage page)
{
    // Matches useEditableFields.tsx's own `grid[0][0]` per view.
    switch (page)
    {
        case ScreenPage::Sample:  return "root";
        case ScreenPage::Machine: return "pitchEngine";
        case ScreenPage::Filter:  return "filterModel";
        case ScreenPage::Capture: return "transpose";
    }
    return "root";
}

void ConcreteScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY > 0.0f)
        adjustSelectedField (1);
    else if (wheel.deltaY < 0.0f)
        adjustSelectedField (-1);
}

void ConcreteScreen::adjustSelectedField (int delta)
{
    auto& apvts = processor.apvts;

    // Continuous/int params - step then clamp, same convention regardless of the underlying
    // AudioParameterFloat/Int subtype (RangedAudioParameter's normalized-value interface doesn't
    // distinguish them).
    auto adjustParam = [&apvts] (const char* paramID, float step, float lo, float hi, int direction)
    {
        if (auto* param = apvts.getParameter (paramID))
        {
            const auto current = param->convertFrom0to1 (param->getValue());
            const auto next = juce::jlimit (lo, hi, current + step * (float) direction);
            param->setValueNotifyingHost (param->convertTo0to1 (next));
        }
    };
    // Choice params - cycle the raw index, wrapping (matching useMachineParams.tsx's own cycle()).
    auto cycleChoice = [&apvts] (const char* paramID, int direction)
    {
        if (auto* param = apvts.getParameter (paramID))
        {
            const auto numChoices = param->getNumSteps();
            const auto current = (int) std::lround (param->convertFrom0to1 (param->getValue()));
            const auto next = ((current + direction) % numChoices + numChoices) % numChoices;
            param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
        }
    };
    // Bool params - any wheel tick toggles, same as Sample page's One-Shot/Loop.
    auto toggleBool = [&apvts] (const char* paramID)
    {
        if (auto* param = apvts.getParameter (paramID))
            param->setValueNotifyingHost (param->getValue() >= 0.5f ? 0.0f : 1.0f);
    };

    if (selectedFieldId == "root" || selectedFieldId == "oneShot" || selectedFieldId == "loop")
    {
        const auto sampleSet = processor.getCurrentSampleSet();
        if (sampleSet != nullptr && ! sampleSet->zones.empty())
        {
            const auto& zone = sampleSet->zones[0];
            if (selectedFieldId == "root")
                processor.setRootNoteForZone (0, juce::jlimit (0, 127, zone.rootNote + delta));
            else if (selectedFieldId == "oneShot")
                processor.setOneShotForZone (0, ! zone.oneShot);
            else
                processor.setLoopEnabledForZone (0, ! zone.loopEnabled);
        }
    }
    else if (selectedFieldId == "coarse")
        adjustParam (ConcreteAudioProcessor::coarseTuneParamID, 1.0f, -24.0f, 24.0f, delta);
    else if (selectedFieldId == "fine")
        adjustParam (ConcreteAudioProcessor::fineTuneParamID, 1.0f, -50.0f, 50.0f, delta);
    else if (selectedFieldId == "volume")
        localVolumePercent = juce::jlimit (0.0f, 120.0f, localVolumePercent + (float) delta);
    // Machine page
    else if (selectedFieldId == "pitchEngine")
        cycleChoice (ConcreteAudioProcessor::pitchEngineModeParamID, delta);
    else if (selectedFieldId == "companding")
        cycleChoice (ConcreteAudioProcessor::quantizerModeParamID, delta);
    else if (selectedFieldId == "baseRate")
        adjustParam (ConcreteAudioProcessor::baseRateParamID, 200.0f, 4000.0f, 100000.0f, delta);
    else if (selectedFieldId == "voices")
        adjustParam (ConcreteAudioProcessor::voiceCountParamID, 1.0f, 1.0f, 18.0f, delta);
    else if (selectedFieldId == "bitDepth")
        adjustParam (ConcreteAudioProcessor::bitDepthParamID, 1.0f, 1.0f, 16.0f, delta);
    else if (selectedFieldId == "ampEnvelope")
        cycleChoice (ConcreteAudioProcessor::ampEnvelopeModeParamID, delta);
    // Filter page
    else if (selectedFieldId == "filterModel")
        cycleChoice (ConcreteAudioProcessor::filterModelParamID, delta);
    else if (selectedFieldId == "cutoff")
    {
        if (auto* param = apvts.getParameter (ConcreteAudioProcessor::filterCutoffParamID))
        {
            // Roughly a semitone (6%) per step, matching the mockup's own log-scaled feel for
            // this control (useEditableFields.tsx's cycleCutoff) far better than a fixed Hz step
            // would across a 20-20000Hz range.
            const auto current = param->convertFrom0to1 (param->getValue());
            const auto next = juce::jlimit (20.0f, 20000.0f, current * std::pow (1.06f, (float) delta));
            param->setValueNotifyingHost (param->convertTo0to1 (next));
        }
    }
    else if (selectedFieldId == "resonance")
        adjustParam (ConcreteAudioProcessor::filterResonanceParamID, 0.01f, 0.0f, 1.0f, delta);
    else if (selectedFieldId == "envAmount")
        adjustParam (ConcreteAudioProcessor::filterEnvAmountParamID, 0.1f, -8.0f, 8.0f, delta);
    else if (selectedFieldId == "keyTrack")
        adjustParam (ConcreteAudioProcessor::filterKeyTrackParamID, 0.05f, 0.0f, 1.0f, delta);
    // Capture page
    else if (selectedFieldId == "transpose")
        adjustParam (ConcreteAudioProcessor::captureTransposeParamID, 1.0f, 0.0f, 24.0f, delta);
    else if (selectedFieldId == "drive")
        adjustParam (ConcreteAudioProcessor::captureDriveParamID, 1.0f, 0.0f, 24.0f, delta);
    else if (selectedFieldId == "iterations")
        adjustParam (ConcreteAudioProcessor::captureIterationsParamID, 1.0f, 1.0f, 4.0f, delta);
    else if (selectedFieldId == "bypass")
        toggleBool (ConcreteAudioProcessor::captureBypassParamID);
    else if (selectedFieldId == "pitchCompensate")
        toggleBool (ConcreteAudioProcessor::captureAutoCompensateParamID);
    else if (selectedFieldId == "saveSample")
        processor.setEmbedSamplesOverride (! processor.getEmbedSamplesOverride());

    repaint();
}

bool ConcreteScreen::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg"))
            return true;
    return false;
}

void ConcreteScreen::fileDragEnter (const juce::StringArray&, int, int)
{
    isDraggingFileOver = true;
    repaint();
}

void ConcreteScreen::fileDragExit (const juce::StringArray&)
{
    isDraggingFileOver = false;
    repaint();
}

void ConcreteScreen::filesDropped (const juce::StringArray& files, int, int)
{
    isDraggingFileOver = false;
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.hasFileExtension ("wav;aif;aiff;flac;ogg"))
        {
            loadFile (file);
            break;
        }
    }
}

void ConcreteScreen::loadFile (const juce::File& file)
{
    // Centralized here (rather than duplicated at PluginEditor's Load button and top-level drop
    // target) since this is the one place that owns what the screen shows WHILE a load is in
    // flight - see PluginProcessor.h's loadSampleAsync() for why the completion callback captures
    // no reference back to the processor, and juce::Component::SafePointer below for the matching
    // guard on this component's own side.
    isLoading = true;
    repaint();

    juce::Component::SafePointer<ConcreteScreen> safeThis (this);
    processor.loadSampleAsync (file, [safeThis] (bool /*ok*/)
    {
        if (auto* self = safeThis.getComponent())
        {
            self->isLoading = false;
            self->selectedFieldId = "root"; // matches useSample.tsx's loadFile() reset
            self->repaint();
        }
    });
}
