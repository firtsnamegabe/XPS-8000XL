#include <algorithm>
#include <cmath>
#include "WaveformEditorComponent.h"

WaveformEditorComponent::WaveformEditorComponent(SamplerEngine& engineRef) : engine(engineRef)
{
    startTimerHz(30);
    hScrollBar.setAutoHide(false);
    hScrollBar.addListener(this);
    hScrollBar.setVisible(false);
    addAndMakeVisible(hScrollBar);
}

void WaveformEditorComponent::setPad(int newBank, int newPad)
{
    bankIndex = newBank;
    padIndex = newPad;
    // A different pad's zoom/scroll state carrying over would be
    // confusing (you'd land on a random zoomed-in region of a sample
    // you've just switched to), so every pad starts fully zoomed out.
    zoomFactor = 1.0;
    scrollOffsetSamples = 0.0;
    hScrollBar.setVisible(false);
    repaint();
}

void WaveformEditorComponent::timerCallback()
{
    if (engine.isPadPlaying(bankIndex, padIndex))
        repaint();
}

int WaveformEditorComponent::xToSample(int x, int totalSamples) const
{
    if (totalSamples <= 0)
        return 0;
    const float frac = juce::jlimit(0.0f, 1.0f, (float) x / (float) juce::jmax(1, getWidth()));
    return (int) (scrollOffsetSamples + (double) frac * (double) visibleSampleSpan(totalSamples));
}

int WaveformEditorComponent::sampleToX(int sample, int totalSamples) const
{
    if (totalSamples <= 0)
        return 0;
    const double span = (double) visibleSampleSpan(totalSamples);
    const double frac = ((double) sample - scrollOffsetSamples) / span;
    return (int) (frac * (double) getWidth());
}

void WaveformEditorComponent::setZoom(double newZoom, int anchorSample, int totalSamples)
{
    const double oldSpan = (double) visibleSampleSpan(totalSamples);
    const double anchorFrac = oldSpan > 0.0 ? ((double) anchorSample - scrollOffsetSamples) / oldSpan : 0.5;

    zoomFactor = juce::jlimit(1.0, kMaxZoom, newZoom);

    const double newSpan = (double) visibleSampleSpan(totalSamples);
    scrollOffsetSamples = (double) anchorSample - anchorFrac * newSpan;
    scrollOffsetSamples = juce::jlimit(0.0, juce::jmax(0.0, (double) totalSamples - newSpan), scrollOffsetSamples);

    updateScrollBar(totalSamples);
    repaint();
}

void WaveformEditorComponent::updateScrollBar(int numSamples)
{
    const bool zoomed = zoomFactor > 1.0;
    hScrollBar.setVisible(zoomed);
    if (! zoomed)
        return;

    hScrollBar.setRangeLimits(0.0, (double) numSamples, juce::dontSendNotification);
    hScrollBar.setCurrentRange(scrollOffsetSamples, (double) visibleSampleSpan(numSamples), juce::dontSendNotification);
}

void WaveformEditorComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar != &hScrollBar)
        return;
    scrollOffsetSamples = newRangeStart;
    repaint();
}

void WaveformEditorComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto& pad = getPad();
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
        return;

    const int numSamples = clip->data.getNumSamples();
    const int anchorSample = xToSample(e.getPosition().getX(), numSamples);

    // deltaY > 0 is "scroll up"/away from the user on most mice - treat
    // that as zoom IN, matching the usual convention in DAWs/image viewers.
    const double zoomStep = 1.0 + juce::jlimit(0.05f, 0.5f, std::abs(wheel.deltaY)) * 4.0;
    const double newZoom = wheel.deltaY > 0.0f ? zoomFactor * zoomStep : zoomFactor / zoomStep;
    setZoom(newZoom, anchorSample, numSamples);
}

void WaveformEditorComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    zoomFactor = 1.0;
    scrollOffsetSamples = 0.0;
    hScrollBar.setVisible(false);
    repaint();
}

juce::String WaveformEditorComponent::makeRegionDragDescription(int bank, int pad, int startSample, int endSample)
{
    return "SAMPLEPAD_REGION:" + juce::String(bank) + ":" + juce::String(pad)
         + ":" + juce::String(startSample) + ":" + juce::String(endSample);
}

void WaveformEditorComponent::findSegmentAt(Pad& pad, int numSamples, int samplePos, int& outStart, int& outEnd) const
{
    const int trimStart = pad.trimStart.load();
    const int trimEnd = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;

    std::vector<int> bounds;
    bounds.push_back(trimStart);
    for (int m : pad.chopMarkers)
        if (m > trimStart && m < trimEnd)
            bounds.push_back(m);
    bounds.push_back(trimEnd);
    std::sort(bounds.begin(), bounds.end());

    outStart = bounds.front();
    outEnd = bounds.back();
    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        if (samplePos >= bounds[i] && samplePos <= bounds[i + 1])
        {
            outStart = bounds[i];
            outEnd = bounds[i + 1];
            return;
        }
    }
}

int WaveformEditorComponent::snapToZeroCrossing(int samplePos, const SampleClip& clip, int numSamples) const
{
    // Search window is fixed in samples rather than ms so it stays cheap
    // regardless of sample rate; ~4-5ms at typical rates, small enough that
    // the result still feels like "here", not "somewhere nearby".
    constexpr int searchWindow = 200;
    const int lo = juce::jmax(0, samplePos - searchWindow);
    const int hi = juce::jmin(numSamples - 2, samplePos + searchWindow);
    if (hi <= lo)
        return samplePos;

    int bestPos = samplePos;
    int bestDist = searchWindow + 1;

    for (int i = lo; i <= hi; ++i)
    {
        const float a = clip.data.getSample(0, i);
        const float b = clip.data.getSample(0, i + 1);
        if ((a >= 0.0f) != (b >= 0.0f)) // sign change = a zero crossing between i and i+1
        {
            const int dist = std::abs(i - samplePos);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestPos = i;
            }
        }
    }
    return bestPos;
}

int WaveformEditorComponent::snapToGrid(int samplePos, int numSamples)
{
    auto& pad = getPad();
    const int beats = pad.beatsInSample.load();
    if (beats <= 0)
        return samplePos; // nothing to snap to without a beat count set

    const int trimStart = pad.trimStart.load();
    const int trimEnd = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;
    const int span = trimEnd - trimStart;
    if (span <= 0)
        return samplePos;

    constexpr int subdivisionsPerBeat = 4; // 16th-note grid
    const double gridStep = (double) span / (double) (beats * subdivisionsPerBeat);
    if (gridStep < 1.0)
        return samplePos;

    const double relative = (double) samplePos - (double) trimStart;
    const double gridIndex = std::round(relative / gridStep);
    return trimStart + (int) std::round(gridIndex * gridStep);
}

int WaveformEditorComponent::applySnap(int samplePos, const SampleClip& clip, int numSamples)
{
    switch (snapMode)
    {
        case SnapMode::zeroCrossing: return snapToZeroCrossing(samplePos, clip, numSamples);
        case SnapMode::grid:         return snapToGrid(samplePos, numSamples);
        case SnapMode::off:
        default:                     return samplePos;
    }
}

void WaveformEditorComponent::rebuildThumbnailIfNeeded(const SampleClip& clip, int rangeStart, int rangeEnd)
{
    const int width = getWidth();
    if (thumbnail.forClip == &clip && thumbnail.forWidth == width
        && thumbnail.forRangeStart == rangeStart && thumbnail.forRangeEnd == rangeEnd)
        return; // already cached for this clip + this width + this visible range

    thumbnail.forClip = &clip;
    thumbnail.forWidth = width;
    thumbnail.forRangeStart = rangeStart;
    thumbnail.forRangeEnd = rangeEnd;
    thumbnail.columns.clear();
    thumbnail.columns.resize((size_t) juce::jmax(1, width));

    const int span = juce::jmax(1, rangeEnd - rangeStart);
    for (int x = 0; x < width; ++x)
    {
        const int startSample = rangeStart + (int) ((double) x / width * span);
        const int endSample = rangeStart + (int) ((double) (x + 1) / width * span);
        const int clampedStart = juce::jlimit(0, clip.data.getNumSamples() - 1, startSample);
        const int clampedLen = juce::jlimit(1, clip.data.getNumSamples() - clampedStart, endSample - startSample);
        float minV = 0.0f, maxV = 0.0f;
        for (int ch = 0; ch < clip.data.getNumChannels(); ++ch)
        {
            auto r = clip.data.findMinMax(ch, clampedStart, clampedLen);
            minV = juce::jmin(minV, r.getStart());
            maxV = juce::jmax(maxV, r.getEnd());
        }
        thumbnail.columns[(size_t) x] = { minV, maxV };
    }
}

void WaveformEditorComponent::rebuildLayerThumbnailIfNeeded(int layerIndex, const SampleClip& clip)
{
    auto& cache = layerThumbnails[(size_t) layerIndex];
    const int width = getWidth();
    if (cache.forClip == &clip && cache.forWidth == width)
        return;

    cache.forClip = &clip;
    cache.forWidth = width;
    cache.columns.clear();
    cache.columns.resize((size_t) juce::jmax(1, width));

    const int numSamples = clip.data.getNumSamples();
    for (int x = 0; x < width; ++x)
    {
        const int startSample = (int) ((double) x / width * numSamples);
        const int endSample = (int) ((double) (x + 1) / width * numSamples);
        float minV = 0.0f, maxV = 0.0f;
        for (int ch = 0; ch < clip.data.getNumChannels(); ++ch)
        {
            auto r = clip.data.findMinMax(ch, startSample, juce::jmax(1, endSample - startSample));
            minV = juce::jmin(minV, r.getStart());
            maxV = juce::jmax(maxV, r.getEnd());
        }
        cache.columns[(size_t) x] = { minV, maxV };
    }
}

void WaveformEditorComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours2000::inset);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(Colours2000::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    auto& pad = getPad();
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
    {
        g.setColour(Colours2000::insetText.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Drop a sample on a pad", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int width = getWidth();
    const int height = getHeight();
    const int numSamples = clip->data.getNumSamples();
    const int visibleStart = (int) scrollOffsetSamples;
    const int visibleEnd = juce::jmin(numSamples, visibleStart + visibleSampleSpan(numSamples));
    rebuildThumbnailIfNeeded(*clip, visibleStart, visibleEnd);

    const float midY = (float) height / 2.0f;

    // Extra layers' waveforms, drawn UNDERNEATH the main one (translucent,
    // colour-matched to the velocity map) - each at its own full length,
    // not time-aligned with the main sample's zoom/scroll, since a layer
    // is independent audio that may be a completely different duration.
    // This is "there's audio here and roughly what shape it has", not a
    // precise editing view - only the main waveform is ever interactive.
    if (pad.hasExtraLayers())
    {
        static const juce::Colour layerColours[Pad::kMaxExtraLayers] = {
            juce::Colours::orange, juce::Colours::hotpink, juce::Colours::cyan
        };
        for (int i = 0; i < Pad::kMaxExtraLayers; ++i)
        {
            auto& layer = pad.extraLayers[(size_t) i];
            auto layerClip = layer.getClipForAudioThread();
            if (layerClip == nullptr || layerClip->data.getNumSamples() <= 0)
                continue;

            rebuildLayerThumbnailIfNeeded(i, *layerClip);
            g.setColour(layerColours[i].withAlpha(0.25f));
            auto& cols = layerThumbnails[(size_t) i].columns;
            for (int x = 0; x < width && x < (int) cols.size(); ++x)
            {
                const auto& r = cols[(size_t) x];
                g.drawLine((float) x, midY - r.getEnd() * midY, (float) x, midY - r.getStart() * midY, 1.0f);
            }
        }
    }

    g.setColour(Colours2000::insetText.withAlpha(0.8f));
    for (int x = 0; x < width && x < (int) thumbnail.columns.size(); ++x)
    {
        const auto& r = thumbnail.columns[(size_t) x];
        g.drawLine((float) x, midY - r.getEnd() * midY, (float) x, midY - r.getStart() * midY, 1.0f);
    }

    const int trimEnd = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;
    const int leftX = sampleToX(pad.trimStart.load(), numSamples);
    const int rightX = sampleToX(trimEnd, numSamples);

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    if (leftX > 0)
        g.fillRect(0, 0, juce::jlimit(0, width, leftX), height);
    if (rightX < width)
        g.fillRect(juce::jlimit(0, width, rightX), 0, width - juce::jlimit(0, width, rightX), height);

    g.setColour(Colours2000::accent);
    g.fillRect((float) leftX - 1.0f, 0.0f, 2.0f, (float) height);
    g.fillRect((float) rightX - 1.0f, 0.0f, 2.0f, (float) height);

    // Faint beat marks, shown only when this pad actually has BPM
    // time-stretch configured (Pad::beatsInSample > 0) - a visual aid for
    // beat-matching against the stretched result. One line per beat,
    // slightly bolder every 4th (bar lines) for orientation. Drawn UNDER
    // the chop markers, not on top - this is a reference grid, not
    // something to interact with (no hit-testing against these).
    {
        const int trimStartVal = pad.trimStart.load();
        const int beats = pad.beatsInSample.load();
        const int beatSpan = trimEnd - trimStartVal;
        if (beats > 0 && beatSpan > 0)
        {
            const double beatStep = (double) beatSpan / (double) beats;
            for (int b = 0; b <= beats; ++b)
            {
                const int beatSample = trimStartVal + (int) std::round((double) b * beatStep);
                const int bx = sampleToX(beatSample, numSamples);
                if (bx < -2 || bx > width + 2)
                    continue;
                const bool barLine = (b % 4 == 0);
                g.setColour(Colours2000::accent.withAlpha(barLine ? 0.35f : 0.15f));
                g.fillRect((float) bx, 0.0f, barLine ? 3.0f : 2.0f, (float) height);
            }
        }
    }

    g.setColour(Colours2000::insetText);
    for (int marker : pad.chopMarkers)
    {
        const int mx = sampleToX(marker, numSamples);
        if (mx < -kHandleSizePx || mx > width + kHandleSizePx)
            continue; // well off-screen when zoomed in - skip drawing, not just clipping
        g.fillRect((float) mx, 0.0f, kMarkerLineWidthPx, (float) height);
        // Enlarged from the original 7x7 - markers were fiddly to grab.
        g.fillRect((float) mx - (float) kHandleSizePx * 0.5f, 0.0f, (float) kHandleSizePx, (float) kHandleSizePx);
    }

    // While a segment is armed for an external drag (see mouseDrag), show
    // which region is about to be carried out to a pad.
    if (dragTarget == DragTarget::segment && dragArmed && pendingSegmentEnd > pendingSegmentStart)
    {
        const int segX0 = sampleToX(pendingSegmentStart, numSamples);
        const int segX1 = sampleToX(pendingSegmentEnd, numSamples);
        g.setColour(Colours2000::accent.withAlpha(0.25f));
        g.fillRect(segX0, 0, juce::jmax(1, segX1 - segX0), height);
    }

    const float playheadFrac = engine.getPlayheadFraction(bankIndex, padIndex);
    if (playheadFrac >= 0.0f)
    {
        const float px = (float) leftX + playheadFrac * (float) (rightX - leftX);
        g.setColour(Colours2000::red);
        g.fillRect(px - 1.0f, 0.0f, 2.0f, (float) height);
    }

    // Small SNAP toggle, top-right corner - cycles OFF -> ZERO -> GRID on
    // click (see mouseDown). GRID only does anything once BEATS is set on
    // this pad; shown regardless so it's discoverable either way.
    const juce::String snapText = snapMode == SnapMode::zeroCrossing ? "SNAP: ZERO"
                                 : snapMode == SnapMode::grid         ? "SNAP: GRID"
                                                                       : "SNAP: OFF";
    g.setColour(snapMode == SnapMode::off ? Colours2000::insetText.withAlpha(0.6f) : Colours2000::accent);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.fillRoundedRectangle(snapButtonBounds.toFloat(), 4.0f);
    g.setColour(Colours2000::inset);
    g.drawText(snapText, snapButtonBounds, juce::Justification::centred);

    if (zoomFactor > 1.0)
    {
        // Top-left, deliberately not near the bottom - the horizontal
        // scrollbar lives in the bottom 10px whenever zoomed in too, and
        // this and that being in the same region at the same time is
        // exactly the kind of overlap worth avoiding on sight rather than
        // after a screenshot catches it.
        g.setColour(Colours2000::insetText.withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(juce::String(zoomFactor, 1) + "x  (double-click to reset)",
                   bounds.reduced(6.0f).removeFromTop(14.0f).removeFromLeft(180.0f).toNearestInt(),
                   juce::Justification::topLeft);
    }

    // Velocity-layer map: the only visual indication (besides the small
    // "L" badge on the pad tile) of which velocity ranges map to which
    // layer. Placed bottom-center, clear of the scrollbar's bottom-10px
    // zone (which only appears when zoomed in anyway) rather than fighting
    // the zoom-text/SNAP-button corners at the top for space. Invisible
    // for a plain single-sample pad - hasExtraLayers() is false, so this
    // whole block is skipped and costs nothing for the common case.
    if (pad.hasExtraLayers())
    {
        static const juce::Colour layerColours[1 + Pad::kMaxExtraLayers] = {
            Colours2000::accent, juce::Colours::orange, juce::Colours::hotpink, juce::Colours::cyan
        };

        const float mapW = 140.0f, mapH = 10.0f;
        const juce::Rectangle<float> mapArea((float) width * 0.5f - mapW * 0.5f, (float) height - 22.0f, mapW, mapH);

        g.setColour(Colours2000::insetText.withAlpha(0.25f));
        g.fillRoundedRectangle(mapArea, 3.0f);

        auto drawSegment = [&](int lo, int hi, juce::Colour colour)
        {
            const float x0 = mapArea.getX() + mapArea.getWidth() * ((float) lo / 127.0f);
            const float x1 = mapArea.getX() + mapArea.getWidth() * ((float) hi / 127.0f);
            g.setColour(colour);
            g.fillRoundedRectangle(x0, mapArea.getY(), juce::jmax(1.0f, x1 - x0), mapArea.getHeight(), 2.0f);
        };

        if (pad.isLoaded())
            drawSegment(pad.velocityLow.load(), pad.velocityHigh.load(), layerColours[0]);
        for (int i = 0; i < Pad::kMaxExtraLayers; ++i)
        {
            auto& layer = pad.extraLayers[(size_t) i];
            if (layer.isLoaded())
                drawSegment(layer.velocityLow.load(), layer.velocityHigh.load(), layerColours[i + 1]);
        }

        g.setColour(Colours2000::insetText.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(8.0f)));
        g.drawText("VEL 0", mapArea.translated(0.0f, -10.0f).withWidth(30.0f), juce::Justification::centredLeft);
        g.drawText("127", mapArea.translated(0.0f, -10.0f).withX(mapArea.getRight() - 24.0f).withWidth(24.0f),
                   juce::Justification::centredRight);
    }
}

void WaveformEditorComponent::resized()
{
    hScrollBar.setBounds(getLocalBounds().removeFromBottom(10).reduced(2, 0));
    snapButtonBounds = getLocalBounds().reduced(6).removeFromTop(16).removeFromRight(72);
}

void WaveformEditorComponent::mouseDown(const juce::MouseEvent& e)
{
    if (snapButtonBounds.contains(e.getPosition()))
    {
        snapMode = snapMode == SnapMode::off ? SnapMode::zeroCrossing
                 : snapMode == SnapMode::zeroCrossing ? SnapMode::grid
                                                       : SnapMode::off;
        repaint();
        return;
    }

    auto& pad = getPad();
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
        return;

    const int numSamples = clip->data.getNumSamples();
    const int trimEnd = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;
    const int leftX = sampleToX(pad.trimStart.load(), numSamples);
    const int rightX = sampleToX(trimEnd, numSamples);
    const int x = e.getPosition().getX();

    if (std::abs(x - leftX) <= kMarkerGrabPx)
    {
        dragTarget = DragTarget::leftTrim;
        return;
    }
    if (std::abs(x - rightX) <= kMarkerGrabPx)
    {
        dragTarget = DragTarget::rightTrim;
        return;
    }

    // Existing chop marker under the click: right-click removes it,
    // left-click starts sliding it. Checked before "add a new marker" so
    // you can actually edit markers you've already placed rather than only
    // ever adding more.
    if (chopEditingEnabled)
    {
        for (int i = 0; i < (int) pad.chopMarkers.size(); ++i)
        {
            const int markerX = sampleToX(pad.chopMarkers[(size_t) i], numSamples);
            if (std::abs(x - markerX) > kMarkerGrabPx)
                continue;

            if (e.mods.isPopupMenu())
            {
                pad.chopMarkers.erase(pad.chopMarkers.begin() + i);
                if (onMarkersChanged)
                    onMarkersChanged();
                repaint();
            }
            else
            {
                dragTarget = DragTarget::marker;
                draggedMarkerIndex = i;
            }
            return;
        }
    }

    // Inside a chop segment, not on a handle or marker: don't act yet -
    // mouseUp decides "add a marker" (a plain click) vs mouseDrag deciding
    // "drag this segment out to a pad" (the pointer actually moved), so a
    // single gesture unambiguously means one or the other.
    if (chopEditingEnabled && ! e.mods.isPopupMenu() && ! lazyModeActive)
    {
        mouseDownPos = e.getPosition();
        dragArmed = false;
        findSegmentAt(pad, numSamples, xToSample(x, numSamples), pendingSegmentStart, pendingSegmentEnd);
        dragTarget = DragTarget::segment;
        return;
    }

    dragTarget = DragTarget::none;

    if (chopEditingEnabled && ! e.mods.isPopupMenu())
    {
        // Lazy mode: drop the marker at the CURRENT PLAYHEAD, not the
        // click position - a "place markers during playback" behaviour
        // common to sample-chopping workflows. If nothing is playing, start a preview.
        // (Lazy mode never becomes a segment-drag - a tap here is always
        // "place a marker", since dragging while previewing wouldn't make
        // sense as a pad-export gesture.)
        const float frac = engine.getPlayheadFraction(bankIndex, padIndex);
        if (frac >= 0.0f)
        {
            const int start = pad.trimStart.load();
            const int end = trimEnd;
            const int samplePos = applySnap(start + (int) (frac * (end - start)), *clip, numSamples);
            engine.addLazyMarker(bankIndex, padIndex, samplePos);
        }
        else
        {
            engine.requestTriggerFromUI(bankIndex, padIndex, 1.0f);
        }

        if (onMarkersChanged)
            onMarkersChanged();
        repaint();
    }
}

void WaveformEditorComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::none)
        return;

    auto& pad = getPad();
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr)
        return;

    const int numSamples = clip->data.getNumSamples();
    const int x = juce::jlimit(0, getWidth(), e.getPosition().getX());
    const int sample = xToSample(x, numSamples);

    if (dragTarget == DragTarget::leftTrim)
    {
        const int currentEnd = pad.trimEnd.load();
        const int maxStart = (currentEnd > 0 ? currentEnd : numSamples) - 1;
        pad.trimStart = juce::jlimit(0, juce::jmax(0, maxStart), sample);
        repaint();
    }
    else if (dragTarget == DragTarget::rightTrim)
    {
        pad.trimEnd = juce::jlimit(pad.trimStart.load() + 1, numSamples, sample);
        repaint();
    }
    else if (dragTarget == DragTarget::marker && draggedMarkerIndex >= 0
             && draggedMarkerIndex < (int) pad.chopMarkers.size())
    {
        // Clamp between neighboring markers (and the trim bounds) so a
        // slide can't cross another marker or invert region order - the
        // vector stays sorted without needing a re-sort after every drag.
        // Snap is applied BEFORE clamping, so a snapped position that
        // would cross a neighbor still gets pulled back to a legal spot.
        const int trimStart = pad.trimStart.load();
        const int trimEndVal = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;

        const int lowerBound = (draggedMarkerIndex > 0)
            ? pad.chopMarkers[(size_t) draggedMarkerIndex - 1] + kMinMarkerGapSamples
            : trimStart + kMinMarkerGapSamples;
        const int upperBound = (draggedMarkerIndex + 1 < (int) pad.chopMarkers.size())
            ? pad.chopMarkers[(size_t) draggedMarkerIndex + 1] - kMinMarkerGapSamples
            : trimEndVal - kMinMarkerGapSamples;

        if (lowerBound < upperBound)
        {
            const int snapped = applySnap(sample, *clip, numSamples);
            pad.chopMarkers[(size_t) draggedMarkerIndex] = juce::jlimit(lowerBound, upperBound, snapped);
        }
        repaint();
    }
    else if (dragTarget == DragTarget::segment && ! dragArmed)
    {
        if (e.getPosition().getDistanceFrom(mouseDownPos) < kDragThresholdPx)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            dragArmed = true;
            container->startDragging(
                makeRegionDragDescription(bankIndex, padIndex, pendingSegmentStart, pendingSegmentEnd), this);
        }
        repaint();
    }
}

void WaveformEditorComponent::mouseUp(const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::marker && onMarkersChanged)
        onMarkersChanged();

    if (dragTarget == DragTarget::segment && ! dragArmed)
    {
        // The pointer never moved far enough to count as a drag - treat it
        // as the original "tap to add a marker here" gesture.
        auto& pad = getPad();
        auto clip = pad.getClipForAudioThread();
        if (clip != nullptr && clip->data.getNumSamples() > 0)
        {
            const int numSamples = clip->data.getNumSamples();
            const int samplePos = applySnap(xToSample(e.getPosition().getX(), numSamples), *clip, numSamples);
            engine.addLazyMarker(bankIndex, padIndex, samplePos);
            if (onMarkersChanged)
                onMarkersChanged();
        }
    }

    dragTarget = DragTarget::none;
    draggedMarkerIndex = -1;
    dragArmed = false;
    pendingSegmentStart = pendingSegmentEnd = -1;
    repaint();
}

void WaveformEditorComponent::mouseMove(const juce::MouseEvent& e)
{
    auto& pad = getPad();
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const int numSamples = clip->data.getNumSamples();
    const int trimEnd = pad.trimEnd.load() > 0 ? pad.trimEnd.load() : numSamples;
    const int leftX = sampleToX(pad.trimStart.load(), numSamples);
    const int rightX = sampleToX(trimEnd, numSamples);
    const int x = e.getPosition().getX();

    bool overGrabbable = std::abs(x - leftX) <= kMarkerGrabPx || std::abs(x - rightX) <= kMarkerGrabPx;
    if (! overGrabbable && chopEditingEnabled)
    {
        for (int marker : pad.chopMarkers)
        {
            if (std::abs(x - sampleToX(marker, numSamples)) <= kMarkerGrabPx)
            {
                overGrabbable = true;
                break;
            }
        }
    }

    setMouseCursor(overGrabbable ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformEditorComponent::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
