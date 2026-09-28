#include "IRWaveformDisplay.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

namespace
{
    // Matches the combo's own chrome (drawComboBox's 5px radius / 1px outline), so the two inset
    // elements in the IMPULSE section read as the same kind of recessed window.
    constexpr float cornerSize = 5.0f;
    constexpr float verticalMargin = 6.0f;
}

IRWaveformDisplay::IRWaveformDisplay()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

void IRWaveformDisplay::update(std::shared_ptr<const WaveformSnapshot> newSnapshot, float newPreDelayMs)
{
    const auto snapshotChanged = newSnapshot != snapshot;
    const auto preDelayChanged = ! juce::approximatelyEqual(newPreDelayMs, preDelayMs);

    if (! snapshotChanged && ! preDelayChanged)
        return;

    snapshot = std::move(newSnapshot);
    preDelayMs = newPreDelayMs;
    repaint();
}

void IRWaveformDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setGradientFill(juce::ColourGradient(findColour(backgroundColourId).brighter(0.06f),
                                            bounds.getX(), bounds.getY(),
                                            findColour(backgroundColourId), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, cornerSize);

    if (snapshot != nullptr && ! snapshot->source.empty())
    {
        juce::Path clip;
        clip.addRoundedRectangle(bounds, cornerSize);
        g.saveState();
        g.reduceClipRegion(clip);

        const auto midY = bounds.getCentreY();
        const auto halfHeight = bounds.getHeight() * 0.5f - verticalMargin;

        // Envelopes are absolute peaks, so each point becomes a symmetric vertical span about the
        // centre line - the familiar mirrored waveform, without needing signed min/max pairs. Drawn
        // as ONE closed, filled Path (top edge left-to-right, then bottom edge back right-to-left)
        // rather than ~1000 individual adjacent hairline-width rectangles - the previous
        // RectangleList/fillRectList approach could show seams between rectangles (each one gets
        // its own antialiased edge, and adjacent sub-pixel-width rects don't always cover each
        // other's edge fringe) that came and went with backing-store/scale-factor changes, e.g. the
        // waveform looking solid on first paint and hairline-gapped after the window's peer
        // finalises its scale following any interaction. A single filled polygon has no adjacent
        // edges to show a seam between, so it can't exhibit that regardless of root cause.
        auto drawEnvelope = [&](const std::vector<float>& envelope, juce::Colour colour)
        {
            if (envelope.empty())
                return;

            // Both envelopes share the SOURCE's time axis: the shaped one has proportionally fewer
            // points and so stops partway across, which is exactly how a Length cut should read.
            const auto pointWidth = bounds.getWidth() / (float) snapshot->source.size();

            auto xAt = [&](size_t i) { return bounds.getX() + (float) i * pointWidth; };
            auto barHeightAt = [&](size_t i)
            {
                const auto magnitude = juce::jlimit(0.0f, 1.0f, envelope[i]);
                return std::max(magnitude * halfHeight, 0.5f);
            };

            juce::Path path;
            path.startNewSubPath(xAt(0), midY - barHeightAt(0));
            for (size_t i = 1; i < envelope.size(); ++i)
                path.lineTo(xAt(i), midY - barHeightAt(i));
            for (size_t i = envelope.size(); i-- > 0;)
                path.lineTo(xAt(i), midY + barHeightAt(i));
            path.closeSubPath();

            g.setColour(colour);
            g.fillPath(path);
        };

        drawEnvelope(snapshot->source, findColour(sourceWaveformColourId));
        drawEnvelope(snapshot->shaped, findColour(shapedWaveformColourId));

        g.setColour(findColour(gridColourId));
        g.drawHorizontalLine((int) midY, bounds.getX(), bounds.getRight());

        // Pre-delay is drawn against the IR's own time axis, so the marker means "the reverb starts
        // here" in the same units as everything else on screen. Dashed, so it reads as an
        // annotation over the waveform rather than as part of it.
        if (preDelayMs > 0.0f && snapshot->sourceSeconds > 0.0)
        {
            const auto position = (float) ((preDelayMs * 0.001) / snapshot->sourceSeconds);

            if (position < 1.0f)
            {
                const auto x = bounds.getX() + position * bounds.getWidth();
                const float dashes[] { 3.0f, 3.0f };
                g.setColour(findColour(preDelayMarkerColourId));
                g.drawDashedLine({ x, bounds.getY(), x, bounds.getBottom() }, dashes, 2, 1.0f);
            }
        }

        g.restoreState();
    }

    g.setColour(findColour(outlineColourId));
    g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
}

} // namespace wildjag::conv
