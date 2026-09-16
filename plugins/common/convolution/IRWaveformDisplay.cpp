#include "IRWaveformDisplay.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

IRWaveformDisplay::IRWaveformDisplay()
{
    setOpaque(true);
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
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll(findColour(backgroundColourId));

    if (snapshot == nullptr || snapshot->source.empty())
        return;

    const auto midY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;

    // Envelopes are absolute peaks, so each point becomes a symmetric vertical span about the
    // centre line - the familiar mirrored waveform, without needing signed min/max pairs.
    auto drawEnvelope = [&](const std::vector<float>& envelope, juce::Colour colour)
    {
        if (envelope.empty())
            return;

        // Both envelopes share the SOURCE's time axis: the shaped one has proportionally fewer
        // points and therefore stops partway across, which is exactly how a Length cut should read.
        const auto pointWidth = bounds.getWidth() / (float) snapshot->source.size();

        juce::RectangleList<float> bars;
        for (size_t i = 0; i < envelope.size(); ++i)
        {
            const auto magnitude = juce::jlimit(0.0f, 1.0f, envelope[i]);
            const auto barHeight = std::max(magnitude * halfHeight, 0.5f);
            bars.addWithoutMerging({ bounds.getX() + (float) i * pointWidth,
                                     midY - barHeight,
                                     std::max(pointWidth, 1.0f),
                                     barHeight * 2.0f });
        }

        g.setColour(colour);
        g.fillRectList(bars);
    };

    drawEnvelope(snapshot->source, findColour(sourceWaveformColourId));
    drawEnvelope(snapshot->shaped, findColour(shapedWaveformColourId));

    g.setColour(findColour(gridColourId));
    g.drawHorizontalLine((int) midY, bounds.getX(), bounds.getRight());

    // Pre-delay is drawn against the IR's own time axis so the marker means "the reverb starts
    // here", in the same units as everything else on screen.
    if (preDelayMs > 0.0f && snapshot->sourceSeconds > 0.0)
    {
        const auto position = (float) ((preDelayMs * 0.001) / snapshot->sourceSeconds);

        if (position < 1.0f)
        {
            const auto x = bounds.getX() + position * bounds.getWidth();
            g.setColour(findColour(preDelayMarkerColourId));
            g.drawVerticalLine((int) x, bounds.getY(), bounds.getBottom());
        }
    }
}

} // namespace wildjag::conv
