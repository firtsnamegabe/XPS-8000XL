#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PadComponent.h"
#include "BankTabsComponent.h"
#include "WaveformEditorComponent.h"
#include "EditParamsComponent.h"
#include "ChopParamsComponent.h"
#include "LayersPanelComponent.h"
#include "MasterPanelComponent.h"
#include "SamplePadLookAndFeel.h"
#include "ColourScheme.h"

class SamplePadEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         private juce::Timer
{
public:
    explicit SamplePadEditor(SamplePadProcessor&);
    ~SamplePadEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SamplePadProcessor& processorRef;
    SamplePadLookAndFeel lookAndFeel;

    BankTabsComponent bankTabs;
    juce::OwnedArray<PadComponent> pads;

    // Rescued (not destroyed) for the duration of a drag that originated
    // from one of the CURRENT bank's pads, in case that drag switches
    // banks mid-flight (BankTabsComponent's drag-hover-to-switch) -
    // rebuildPadsForCurrentBank() would otherwise destroy the very
    // PadComponent JUCE's drag machinery is using as the drag's source,
    // which silently cancels the whole operation partway through (this
    // was reported and confirmed: pad-to-pad drags across banks lost the
    // sample, while drags from the persistent WaveformEditorComponent -
    // never destroyed on a bank switch - worked fine). See
    // dragOperationStarted/Ended and rebuildPadsForCurrentBank().
    bool dragSourceIsCurrentBankPad = false;
    int dragSourcePadIndex = -1;
    std::unique_ptr<PadComponent> orphanedDragSourcePad;

    void dragOperationStarted(const juce::DragAndDropTarget::SourceDetails& details) override;
    void dragOperationEnded(const juce::DragAndDropTarget::SourceDetails& details) override;

    WaveformEditorComponent waveformEditor;
    EditParamsComponent editParams;
    ChopParamsComponent chopParams;
    LayersPanelComponent layersPanel;
    MasterPanelComponent masterPanel;
    juce::Viewport editParamsViewport;
    juce::Viewport chopParamsViewport;
    juce::Viewport layersPanelViewport;
    juce::Viewport masterPanelViewport;

    enum class Tab { edit, chop, layers, master };
    juce::TextButton editTabButton { "EDIT" };
    juce::TextButton chopTabButton { "CHOP" };
    juce::TextButton layersTabButton { "LAYERS" };
    juce::TextButton masterTabButton { "MASTER" };
    Tab currentTab = Tab::edit;

    juce::TextButton xoloThemeButton  { "XOLO" };  // dark mode
    juce::TextButton axoloThemeButton { "AXOLO" }; // light mode
    juce::TextButton kitButton { "KIT" };
    juce::TextButton killButton { "KILL" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label midiStatusLabel;
    juce::Label noteMapLabel;
    juce::TooltipWindow tooltipWindow { this }; // enables .setTooltip() on any child component

    int selectedBank = 0;
    int selectedPad = 0;
    bool wasRecording = false; // detects the recording->stopped transition for a one-shot status message

    void rebuildPadsForCurrentBank();
    void selectPad(int bankIndex, int padIndex);
    void showTab(Tab tab);
    void timerCallback() override; // clears "active" pad highlight after trigger

    void showKitMenu();
    void saveKitAs();
    void loadKit();
    void confirmNewKit();
    void refreshAfterKitLoad();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePadEditor)
};
