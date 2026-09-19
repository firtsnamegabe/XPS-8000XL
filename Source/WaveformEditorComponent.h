#pragma once
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"

class WaveformEditorComponent : public juce::Component,
                                 private juce::Timer,
                                 private juce::ScrollBar::Listener
{
public:
    explicit WaveformEditorComponent(SamplerEngine& engineRef);

    void setPad(int bankIndex, int padIndex);
    void setChopEditingEnabled(bool enabled) { chopEditingEnabled = enabled; repaint(); }
    void setLazyMode(bool isLazy) { lazyModeActive = isLazy; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void()> onMarkersChanged;

private:
    SamplerEngine& engine;
    int bankIndex = 0;
    int padIndex = 0;
    bool chopEditingEnabled = false;
    bool lazyModeActive = false;

    enum class DragTarget { none, leftTrim, rightTrim, marker, segment };
    DragTarget dragTarget = DragTarget::none;
    int draggedMarkerIndex = -1;

    // Segment click-vs-drag: mouseDown records where a chop-segment press
    // started and which segment it's in, but doesn't act yet - mouseUp
    // treats it as "add a marker here" if the pointer never moved far
    // enough to count as a drag; mouseDrag turns it into an external
    // pad-to-pad drag (see makeRegionDragDescription) once it does.
    juce::Point<int> mouseDownPos;
    bool dragArmed = false;
    int pendingSegmentStart = -1, pendingSegmentEnd = -1;

    static constexpr int kMinMarkerGapSamples = 64; // keeps dragged/adjacent markers from crossing/colliding
    static constexpr int kDragThresholdPx = 10;
    static constexpr int kMarkerGrabPx = 16; // enlarged from 8 -> 12 -> 16 (markers were fiddly to grab)
    static constexpr int kHandleSizePx = 20; // visual grab-handle size at the top of each marker (was 7, then 10)
    static constexpr float kMarkerLineWidthPx = 2.0f; // was 1.0f - twice as wide, easier to spot/click

    // --- Zoom / scroll ---------------------------------------------------
    // 1.0 = whole (trimmed) sample visible, matches the original fixed
    // behaviour exactly. Higher = zoomed in; scrollOffsetSamples is the
    // sample index at the left edge of the view. Both reset to 1.0/0 on
    // setPad() (a different pad's zoom state carrying over would be
    // confusing) and on double-click (a quick way back to "fit" without
    // hunting for a reset control).
    double zoomFactor = 1.0;
    double scrollOffsetSamples = 0.0;
    static constexpr double kMaxZoom = 64.0;
    juce::ScrollBar hScrollBar { false };
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void updateScrollBar(int numSamples);
    void setZoom(double newZoom, int anchorSample, int totalSamples); // anchorSample stays under the same x while zooming

    // --- Snap --------------------------------------------------------
    // Applies to manually placing/dragging chop markers only (not COMMIT's
    // EQUAL/TRANSIENT slicing, which computes its own positions). Zero-
    // crossing avoids audible clicks at slice boundaries; grid needs
    // Pad::beatsInSample set (nothing to snap to otherwise, so it's a
    // no-op without that).
    enum class SnapMode { off, zeroCrossing, grid };
    SnapMode snapMode = SnapMode::off;
    juce::Rectangle<int> snapButtonBounds; // hit-test area, set in resized()
    int applySnap(int samplePos, const SampleClip& clip, int numSamples);
    int snapToZeroCrossing(int samplePos, const SampleClip& clip, int numSamples) const;
    int snapToGrid(int samplePos, int numSamples);

    // Drag description format shared with PadComponent, which parses it on
    // drop: "SAMPLEPAD_REGION:<bank>:<pad>:<startSample>:<endSample>".
    static juce::String makeRegionDragDescription(int bank, int pad, int startSample, int endSample);

    Pad& getPad() { return engine.getBank(bankIndex)[(size_t) padIndex]; }

    int xToSample(int x, int totalSamples) const;
    int sampleToX(int sample, int totalSamples) const;
    int visibleSampleSpan(int totalSamples) const { return juce::jmax(1, (int) (totalSamples / zoomFactor)); }

    // Finds the chop-segment (between adjacent markers, or a marker and a
    // trim boundary) containing `samplePos`.
    void findSegmentAt(Pad& pad, int numSamples, int samplePos, int& outStart, int& outEnd) const;

    // Cached min/max-per-pixel-column thumbnail, so paint() doesn't rescan
    // the whole sample every repaint (only when the clip, pad selection,
    // component width, or the visible zoom/scroll range actually changes).
    struct ThumbnailCache
    {
        const SampleClip* forClip = nullptr;
        int forWidth = -1;
        int forRangeStart = -1;
        int forRangeEnd = -1;
        std::vector<juce::Range<float>> columns; // one min/max range per pixel column, over [forRangeStart, forRangeEnd)
    } thumbnail;

    void rebuildThumbnailIfNeeded(const SampleClip& clip, int rangeStart, int rangeEnd);

    // Simpler per-layer cache (no zoom/scroll to track - extra layers
    // always render at their own full length, not time-aligned with the
    // main sample's zoom/pan state, since they're independent audio that
    // may be a completely different length). Drawn UNDER the main
    // waveform, translucent, in the same colour as that layer's segment
    // in the velocity map - just enough to see "there's audio here and
    // roughly what shape it has", not a precise editing view.
    struct LayerThumbnailCache
    {
        const SampleClip* forClip = nullptr;
        int forWidth = -1;
        std::vector<juce::Range<float>> columns;
    };
    std::array<LayerThumbnailCache, Pad::kMaxExtraLayers> layerThumbnails;
    void rebuildLayerThumbnailIfNeeded(int layerIndex, const SampleClip& clip);

    void timerCallback() override;
};
