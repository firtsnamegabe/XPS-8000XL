#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"

class BankTabsComponent : public juce::Component,
                           public juce::DragAndDropTarget
{
public:
    explicit BankTabsComponent(SamplerEngine& engineRef);

    void resized() override;

    // Re-syncs the highlighted tab with the engine's current bank. Needed
    // after any programmatic bank change (e.g. New Kit resets to bank
    // A) since the highlight otherwise only updates from a tab click.
    void updateTabStates();

    // Dragging a pad (or a chop region) over a different bank's tab
    // switches to that bank, so you can keep dragging and drop it onto a
    // pad there - without this, there was no way to drag a sample into a
    // bank other than the one currently showing.
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

    std::function<void()> onBankChanged;

private:
    SamplerEngine& engine;
    juce::OwnedArray<juce::TextButton> tabs;

    // Shared by itemDragEnter/itemDragMove: switches to whichever tab the
    // drag is currently positioned over, if any and if it isn't already
    // the current bank. Checked on both entry and continued movement,
    // since entering the tab STRIP once doesn't tell us which SPECIFIC
    // tab the pointer ends up over as it moves across them.
    void updateBankForDragPosition(juce::Point<int> localPosition);
};
