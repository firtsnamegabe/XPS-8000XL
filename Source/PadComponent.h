#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"

class SamplePadEditor; // fwd decl

// A single clickable/drag-target pad in the 4x4 grid.
class PadComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropTarget,
                      public juce::SettableTooltipClient
{
public:
    PadComponent(SamplerEngine& engineRef, int bankIdx, int padIdx);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    void setActiveVisual(bool isActive)
    {
        if (activeVisual == isActive)
            return; // avoid a needless repaint every timer tick when nothing changed
        activeVisual = isActive;
        repaint();
    }

    void setSelectedVisual(bool isSelected)
    {
        if (selectedVisual == isSelected)
            return;
        selectedVisual = isSelected;
        repaint();
    }

    void setRecordingVisual(bool isRecording)
    {
        if (recordingVisual == isRecording)
            return;
        recordingVisual = isRecording;
        repaint();
    }

    // 0..1 input level while this pad is the one being recorded into -
    // repaints on every call (unlike the other setters here) since a level
    // meter needs to visibly move continuously, not just on state changes.
    void setRecordingLevel(float level01)
    {
        recordingLevel = level01;
        if (recordingVisual)
            repaint();
    }

    // Internal pad-to-pad drag (copy onto empty pad, swap onto loaded pad).
    // The description string is "SAMPLEPAD_INTERNAL:<bank>:<pad>".
    static juce::String makeDragDescription(int bank, int pad);
    static bool parseDragDescription(const juce::String& desc, int& bank, int& pad);
    static bool parseRegionDragDescription(const juce::String& desc, int& bank, int& pad, int& startSample, int& endSample);
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

    std::function<void(int bank, int pad)> onPadSelected;
    std::function<void()> onPadChanged; // fired after clear/normalize/trim/copy/swap, for GUI refresh

private:
    SamplerEngine& engine;
    int bankIndex;
    int padIndex;
    bool activeVisual = false;
    bool selectedVisual = false;
    bool recordingVisual = false;
    float recordingLevel = 0.0f;
    bool dragHover = false;
    juce::Point<int> mouseDownPos;
    bool dragArmed = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void showContextMenu();
    void openSampleDialog();
    void openLayerDialog(bool velocitySplit);
    void openExportDialog();

    Pad& getPad() { return engine.getBank(bankIndex)[(size_t) padIndex]; }
};
