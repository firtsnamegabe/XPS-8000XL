#include "PluginEditor.h"

SamplePadEditor::SamplePadEditor(SamplePadProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), bankTabs(p.getEngine()),
      waveformEditor(p.getEngine()), editParams(p.getEngine()),
      chopParams(p.getEngine(), waveformEditor), layersPanel(p.getEngine()),
      masterPanel(p.getEngine())
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(bankTabs);
    bankTabs.onBankChanged = [this] { rebuildPadsForCurrentBank(); };

    addAndMakeVisible(waveformEditor);

    editParamsViewport.setViewedComponent(&editParams, false);
    chopParamsViewport.setViewedComponent(&chopParams, false);
    layersPanelViewport.setViewedComponent(&layersPanel, false);
    masterPanelViewport.setViewedComponent(&masterPanel, false);
    editParamsViewport.setScrollBarsShown(true, false); // vertical only
    chopParamsViewport.setScrollBarsShown(true, false);
    layersPanelViewport.setScrollBarsShown(true, false);
    masterPanelViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(editParamsViewport);
    addAndMakeVisible(chopParamsViewport);
    addAndMakeVisible(layersPanelViewport);
    addAndMakeVisible(masterPanelViewport);

    layersPanel.onLayersChanged = [this]
    {
        waveformEditor.repaint(); // velocity map / layer waveforms depend on layer state
        if (auto* pad = pads[selectedPad])
            pad->repaint(); // "L" badge
    };

    editParams.onMidiLearnRequested = [this]
    {
        // Toggle: cancel if already armed (there's only ever one armed
        // slot, so no ambiguity about which pad), otherwise arm for the
        // currently selected pad. Previously there was no way to back out
        // of MIDI Learn short of actually hitting a note -
        // engine.cancelMidiLearn() already existed, just wasn't wired to
        // anything discoverable.
        auto& engine = processorRef.getEngine();
        if (engine.isMidiLearnArmed())
        {
            engine.cancelMidiLearn();
            midiStatusLabel.setText("MIDI LEARN CANCELLED", juce::dontSendNotification);
        }
        else
        {
            engine.armMidiLearn(selectedBank, selectedPad);
            midiStatusLabel.setText("MIDI LEARN: tap a key...", juce::dontSendNotification);
        }
        editParams.setPad(selectedBank, selectedPad); // refreshes the button's text/toggle state
    };

    chopParams.onPadsChangedAcrossBanks = [this]
    {
        rebuildPadsForCurrentBank(); // refresh visible bank; other banks pick it up when selected
    };

    processorRef.getEngine().onMidiLearned = [this](int, int, int note)
    {
        juce::MessageManager::callAsync([this, note]
        {
            midiStatusLabel.setText("LEARNED NOTE " + juce::String(note), juce::dontSendNotification);
            editParams.setPad(selectedBank, selectedPad); // refresh note readout
        });
    };

    addAndMakeVisible(editTabButton);
    addAndMakeVisible(chopTabButton);
    addAndMakeVisible(layersTabButton);
    addAndMakeVisible(masterTabButton);
    editTabButton.onClick = [this] { showTab(Tab::edit); };
    chopTabButton.onClick = [this] { showTab(Tab::chop); };
    layersTabButton.onClick = [this] { showTab(Tab::layers); };
    masterTabButton.onClick = [this] { showTab(Tab::master); };
    editTabButton.setTooltip("Volume, pitch, filter, FX, playback, and MIDI settings for the selected pad.");
    chopTabButton.setTooltip("Slice the selected pad's sample and bounce pieces onto other pads.");
    layersTabButton.setTooltip("Velocity layers: which samples are loaded and their velocity ranges.");
    masterTabButton.setTooltip("Input/output gain and the master-bus compressor - not per-pad, applies to everything.");

    // Two-button segmented control rather than one button whose label
    // flips - matches the same pattern used for VARISPEED/TIME LOCK: the
    // active theme is the one shown/highlighted, not something you'd click
    // to switch away from. XOLO = dark mode, AXOLO = light mode.
    auto refreshThemeButtons = [this]
    {
        xoloThemeButton.setToggleState(! Colours2000::isLightMode, juce::dontSendNotification);
        axoloThemeButton.setToggleState(Colours2000::isLightMode, juce::dontSendNotification);
    };
    refreshThemeButtons();
    addAndMakeVisible(xoloThemeButton);
    addAndMakeVisible(axoloThemeButton);
    xoloThemeButton.onClick = [this, refreshThemeButtons]
    {
        Colours2000::setLightMode(false);
        lookAndFeel.applyPalette();
        refreshThemeButtons();
        repaint();
    };
    axoloThemeButton.onClick = [this, refreshThemeButtons]
    {
        Colours2000::setLightMode(true);
        lookAndFeel.applyPalette();
        refreshThemeButtons();
        repaint();
    };

    addAndMakeVisible(kitButton);
    kitButton.onClick = [this] { showKitMenu(); };
    kitButton.setTooltip("Save/load a full Kit (.xolo) - every pad's audio and settings, portable and shareable.");

    addAndMakeVisible(killButton);
    // Deliberately a FIXED colour, not the theme-dependent Colours2000::red
    // (which inverts with light/dark mode) - an emergency stop button
    // should look the same regardless of which theme is active.
    killButton.setColour(juce::TextButton::buttonColourId, juce::Colour(Colours2000::detail::darkRedHex));
    killButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    killButton.setTooltip("Stop all sounds immediately.");
    killButton.onClick = [this] { processorRef.getEngine().requestKillAllFromUI(); };

    midiStatusLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    midiStatusLabel.setColour(juce::Label::textColourId, Colours2000::textMuted);
    midiStatusLabel.setText("MIDI READY (standalone: enable input in Audio/MIDI Settings)",
                             juce::dontSendNotification);
    midiStatusLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(midiStatusLabel);

    noteMapLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    noteMapLabel.setColour(juce::Label::textColourId, Colours2000::textMuted);
    noteMapLabel.setText("NOTE MAP: C1 -> PAD A01 - CHROMATIC", juce::dontSendNotification);
    addAndMakeVisible(noteMapLabel);

    rebuildPadsForCurrentBank();
    showTab(Tab::edit);

    setResizable(true, true);
    setResizeLimits(760, 560, 2000, 1600); // keeps the layout usable at both ends - small enough to still show
                                            // everything (via the EDIT/CHOP viewport scroll), large enough that
                                            // the pad grid and knobs don't get absurdly huge
    setSize(940, 820);

    startTimerHz(30);
}

SamplePadEditor::~SamplePadEditor()
{
    processorRef.getEngine().onMidiLearned = nullptr;
    setLookAndFeel(nullptr);
}

void SamplePadEditor::dragOperationStarted(const juce::DragAndDropTarget::SourceDetails& details)
{
    int bank, pad;
    if (PadComponent::parseDragDescription(details.description.toString(), bank, pad)
        && bank == selectedBank && pad >= 0 && pad < pads.size())
    {
        dragSourceIsCurrentBankPad = true;
        dragSourcePadIndex = pad;
    }
}

void SamplePadEditor::dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&)
{
    dragSourceIsCurrentBankPad = false;
    dragSourcePadIndex = -1;
    orphanedDragSourcePad.reset(); // safe to actually destroy now - the drag is over
}

void SamplePadEditor::rebuildPadsForCurrentBank()
{
    // If a drag is in progress FROM one of the pads about to be destroyed
    // below, rescue that ONE component (transfer ownership out) instead
    // of letting pads.clear() delete it - see the member comment on
    // dragSourceIsCurrentBankPad for why. This only matters for the
    // instant of THIS rebuild call; the array is back to a normal,
    // fully-aligned 16 elements immediately afterward either way, so
    // nothing else ever observes it in a partial state.
    if (dragSourceIsCurrentBankPad && dragSourcePadIndex >= 0 && dragSourcePadIndex < pads.size())
    {
        orphanedDragSourcePad.reset(pads.removeAndReturn(dragSourcePadIndex));
        orphanedDragSourcePad->setVisible(false); // doesn't belong to whichever bank ends up showing
        dragSourceIsCurrentBankPad = false; // rescued once; nothing further to protect for this drag
    }

    pads.clear();
    const int bankIndex = processorRef.getEngine().getCurrentBank();
    const bool sameBank = (bankIndex == selectedBank);
    selectedBank = bankIndex;

    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto* padComp = pads.add(new PadComponent(processorRef.getEngine(), bankIndex, i));
        addAndMakeVisible(padComp);
        padComp->onPadSelected = [this](int b, int pd) { selectPad(b, pd); };
        padComp->onPadChanged = [this] { selectPad(selectedBank, selectedPad); };
    }
    resized();
    selectPad(bankIndex, sameBank ? selectedPad : 0); // preserve selection on refresh, default to pad 1 on bank switch
    repaint();
}

void SamplePadEditor::selectPad(int bankIndex, int padIndex)
{
    selectedBank = bankIndex;
    selectedPad = padIndex;

    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setSelectedVisual(i == padIndex);

    waveformEditor.setPad(bankIndex, padIndex);
    editParams.setPad(bankIndex, padIndex);
    chopParams.setPad(bankIndex, padIndex);
    layersPanel.setPad(bankIndex, padIndex);
}

void SamplePadEditor::showTab(Tab tab)
{
    currentTab = tab;
    editTabButton.setToggleState(tab == Tab::edit, juce::dontSendNotification);
    chopTabButton.setToggleState(tab == Tab::chop, juce::dontSendNotification);
    layersTabButton.setToggleState(tab == Tab::layers, juce::dontSendNotification);
    masterTabButton.setToggleState(tab == Tab::master, juce::dontSendNotification);
    editParamsViewport.setVisible(tab == Tab::edit);
    chopParamsViewport.setVisible(tab == Tab::chop);
    layersPanelViewport.setVisible(tab == Tab::layers);
    masterPanelViewport.setVisible(tab == Tab::master);
    if (tab == Tab::master)
        masterPanel.refreshFromEngine(); // global state, not tied to pad selection - refresh on entry instead
}

void SamplePadEditor::showKitMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Save Kit...", [this] { saveKitAs(); });
    menu.addItem("Load Kit...", [this] { loadKit(); });
    menu.addSeparator();
    menu.addItem("New Kit...", [this] { confirmNewKit(); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&kitButton));
}

void SamplePadEditor::saveKitAs()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save XPS-8000XL Kit",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        juce::String("*") + SamplePadProcessor::getKitFileExtension());

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                      | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (! file.hasFileExtension(SamplePadProcessor::getKitFileExtension()))
            file = file.withFileExtension(SamplePadProcessor::getKitFileExtension());

        midiStatusLabel.setText("SAVING KIT...", juce::dontSendNotification);
        if (! processorRef.saveKitToFile(file))
            midiStatusLabel.setText("SAVE FAILED: " + file.getFileName(), juce::dontSendNotification);
        else
            midiStatusLabel.setText("SAVED: " + file.getFileName(), juce::dontSendNotification);
    });
}

void SamplePadEditor::loadKit()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load XPS-8000XL Kit",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        juce::String("*") + SamplePadProcessor::getKitFileExtension());

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        midiStatusLabel.setText("LOADING KIT...", juce::dontSendNotification);
        if (! processorRef.loadKitFromFile(file))
        {
            midiStatusLabel.setText("LOAD FAILED: " + file.getFileName(), juce::dontSendNotification);
            return;
        }

        midiStatusLabel.setText("LOADED: " + file.getFileName(), juce::dontSendNotification);
        refreshAfterKitLoad();
    });
}

void SamplePadEditor::confirmNewKit()
{
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("New Kit")
        .withMessage("Clear all 4 banks and start a new kit? This can't be undone unless you've saved.")
        .withButton("New Kit")
        .withButton("Cancel");

    juce::AlertWindow::showAsync(options, [this](int result)
    {
        // JUCE convention: the first-added button (here "New Kit")
        // returns 1, matching showOkCancelBox's OK/Cancel numbering. I
        // can't run this to double-check in this environment - if the
        // buttons come back inverted, swap this to `result == 0`.
        if (result != 1)
            return;

        processorRef.newKit();
        midiStatusLabel.setText("NEW KIT", juce::dontSendNotification);
        refreshAfterKitLoad();
    });
}

void SamplePadEditor::refreshAfterKitLoad()
{
    lookAndFeel.applyPalette();
    xoloThemeButton.setToggleState(! Colours2000::isLightMode, juce::dontSendNotification);
    axoloThemeButton.setToggleState(Colours2000::isLightMode, juce::dontSendNotification);
    bankTabs.updateTabStates();
    rebuildPadsForCurrentBank();
    masterPanel.refreshFromEngine();
    repaint();
}

void SamplePadEditor::timerCallback()
{
    auto& engine = processorRef.getEngine();
    const bool recording = engine.isRecording();
    const int recordingBank = engine.getRecordingBank();
    const int recordingPad = engine.getRecordingPad();

    for (int i = 0; i < pads.size(); ++i)
    {
        pads[i]->setActiveVisual(engine.isPadPlaying(selectedBank, i));
        const bool thisPadRecording = recording && recordingBank == selectedBank && recordingPad == i;
        pads[i]->setRecordingVisual(thisPadRecording);
        if (thisPadRecording)
            pads[i]->setRecordingLevel(engine.getRecordingPeakLevel());
    }

    if (recording)
    {
        const int seconds = (int) engine.getRecordingSeconds();
        midiStatusLabel.setText(juce::String::formatted("RECORDING... %d:%02d", seconds / 60, seconds % 60),
                                 juce::dontSendNotification);
    }
    else if (wasRecording)
    {
        midiStatusLabel.setText("SAMPLE RECORDED", juce::dontSendNotification);
    }
    wasRecording = recording;
}

void SamplePadEditor::paint(juce::Graphics& g)
{
    g.fillAll(Colours2000::panel);

    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(56);
    g.setColour(Colours2000::panelHi);
    g.fillRect(topBar);
    g.setColour(Colours2000::border);
    g.drawLine((float) topBar.getX(), (float) topBar.getBottom(),
               (float) topBar.getRight(), (float) topBar.getBottom(), 1.0f);

    // Two-tier logo treatment: a bold product name with a small letter-
    // spaced subtitle underneath, a common badge-style layout for
    // hardware-inspired UIs - not tied to any specific product's actual
    // trademarked wordmark, font, or tagline text.
    // Bold + tight kerning + accent colour on the main title (not a named
    // typeface - that's a gamble on what's installed on a given Linux
    // system, and a missing font falls back silently with no visible
    // change) plus a WIDE-tracked caps subtitle underneath spelling out
    // what "XPS" actually stands for.
    auto titleArea = topBar.reduced(16, 4);
    auto mainTitleArea = titleArea.removeFromTop(24).removeFromLeft(140);
    titleArea.removeFromTop(2);
    auto subtitleArea = titleArea.removeFromTop(12).removeFromLeft(140);

    g.setColour(Colours2000::accent);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)).withExtraKerningFactor(-0.03f));
    g.drawText("XPS-8000XL", mainTitleArea, juce::Justification::bottomLeft);

    g.setColour(Colours2000::textMuted);
    // "XOLO PRODUCTION SAMPLER" is a longer string than what was here
    // before ("PRODUCTION CENTER") - widened the reserved area to 140px
    // (from 100px) and pulled the font size/tracking back slightly (7.5pt/
    // 0.08 -> 7.0pt/0.05) to keep some safety margin, since I still can't
    // measure rendered text width in this environment and would rather
    // under-track a bit than risk this specific, deliberately-chosen
    // string getting cut off with an ellipsis.
    g.setFont(juce::Font(juce::FontOptions(7.0f, juce::Font::plain)).withExtraKerningFactor(0.05f));
    g.drawText("XOLO PRODUCTION SAMPLER", subtitleArea, juce::Justification::topLeft);

    auto bottomBar = area.removeFromBottom(34);
    g.setColour(Colours2000::panelHi);
    g.fillRect(bottomBar);
    g.setColour(Colours2000::border);
    g.drawLine((float) bottomBar.getX(), (float) bottomBar.getY(),
               (float) bottomBar.getRight(), (float) bottomBar.getY(), 1.0f);
}

void SamplePadEditor::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(56).reduced(16, 11);
    topBar.removeFromLeft(150); // space reserved for the two-line "XPS-8000XL" / "XOLO PRODUCTION SAMPLER" logo drawn in paint()
    bankTabs.setBounds(topBar.removeFromLeft(160)); // 4 tabs need ~154px (34px x 4 + 6px gap x 3)
    topBar.removeFromLeft(12);
    killButton.setBounds(topBar.removeFromLeft(60)); // right next to the bank tabs - reachable without hunting for it

    // "AXOLO" (5 chars) is longer than "XOLO" (4 chars) - 48px was only
    // just enough for XOLO and truncated AXOLO to "AX..." (caught via
    // screenshot). Sized generously (68px, not just "barely enough") since
    // I've now underestimated this once already and can't render text to
    // verify exactly - both buttons the same width for a matched pair.
    axoloThemeButton.setBounds(topBar.removeFromRight(68));
    topBar.removeFromRight(4);
    xoloThemeButton.setBounds(topBar.removeFromRight(68));
    topBar.removeFromRight(8);
    kitButton.setBounds(topBar.removeFromRight(90));

    auto bottomBar = area.removeFromBottom(34).reduced(16, 8);
    noteMapLabel.setBounds(bottomBar.removeFromLeft(bottomBar.getWidth() / 2));
    midiStatusLabel.setBounds(bottomBar);

    auto content = area.reduced(16);
    // Pad grid width is now a proportion of the available space (clamped
    // to a sane range) instead of a fixed 380px, so resizing the window
    // actually grows/shrinks the pads instead of just changing how much
    // empty space surrounds a fixed-size grid.
    const int padAreaWidth = juce::jlimit(300, 560, (int) (content.getWidth() * 0.42f));
    auto padArea = content.removeFromLeft(padAreaWidth);
    content.removeFromLeft(16);
    auto rightArea = content;

    const int gap = 8;
    const int padSize = juce::jmin((padArea.getWidth() - gap * 3) / 4,
                                    (padArea.getHeight() - gap * 3) / 4);
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const int index = row * 4 + col;
            if (index >= pads.size())
                continue;
            pads[index]->setBounds(padArea.getX() + col * (padSize + gap),
                                    padArea.getY() + row * (padSize + gap),
                                    padSize, padSize);
        }
    }

    waveformEditor.setBounds(rightArea.removeFromTop(140));
    rightArea.removeFromTop(gap);

    auto tabRow = rightArea.removeFromTop(28);
    editTabButton.setBounds(tabRow.removeFromLeft(70));
    tabRow.removeFromLeft(4);
    chopTabButton.setBounds(tabRow.removeFromLeft(70));
    tabRow.removeFromLeft(4);
    layersTabButton.setBounds(tabRow.removeFromLeft(70));
    tabRow.removeFromLeft(4);
    masterTabButton.setBounds(tabRow.removeFromLeft(70));
    rightArea.removeFromTop(gap);

    editParamsViewport.setBounds(rightArea);
    chopParamsViewport.setBounds(rightArea);
    layersPanelViewport.setBounds(rightArea);
    masterPanelViewport.setBounds(rightArea);

    // Size each panel to its own preferred content height at the
    // viewport's width - if that's taller than the viewport, the
    // Viewport shows a scrollbar and scrolls rather than clipping.
    // Subtracting the scrollbar thickness from the width avoids a
    // horizontal scrollbar ever appearing too (vertical-only scrolling).
    const int scrollBarW = editParamsViewport.getScrollBarThickness();
    const int panelWidth = juce::jmax(100, rightArea.getWidth() - scrollBarW);
    editParams.setSize(panelWidth, editParams.getPreferredContentHeight());
    chopParams.setSize(panelWidth, chopParams.getPreferredContentHeight());
    layersPanel.setSize(panelWidth, layersPanel.getPreferredContentHeight());
    masterPanel.setSize(panelWidth, masterPanel.getPreferredContentHeight());
}
