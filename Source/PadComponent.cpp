#include "PadComponent.h"

PadComponent::PadComponent(SamplerEngine& engineRef, int bankIdx, int padIdx)
    : engine(engineRef), bankIndex(bankIdx), padIndex(padIdx)
{
    setInterceptsMouseClicks(true, false);
    setTooltip("Click: trigger & select. Right-click: menu (load/record/mute/solo/trim/reset/clear). "
               "Drag onto an empty pad: resample (bakes in effects). Drag onto a loaded pad: swap. "
               "Drop an audio file: load.");
}

juce::String PadComponent::makeDragDescription(int bank, int pad)
{
    return "SAMPLEPAD_INTERNAL:" + juce::String(bank) + ":" + juce::String(pad);
}

bool PadComponent::parseDragDescription(const juce::String& desc, int& bank, int& pad)
{
    if (! desc.startsWith("SAMPLEPAD_INTERNAL:"))
        return false;

    auto rest = desc.fromFirstOccurrenceOf("SAMPLEPAD_INTERNAL:", false, false);
    auto bankStr = rest.upToFirstOccurrenceOf(":", false, false);
    auto padStr = rest.fromFirstOccurrenceOf(":", false, false);
    if (bankStr.isEmpty() || padStr.isEmpty())
        return false;

    bank = bankStr.getIntValue();
    pad = padStr.getIntValue();
    return true;
}

bool PadComponent::parseRegionDragDescription(const juce::String& desc, int& bank, int& pad,
                                               int& startSample, int& endSample)
{
    if (! desc.startsWith("SAMPLEPAD_REGION:"))
        return false;

    auto rest = desc.fromFirstOccurrenceOf("SAMPLEPAD_REGION:", false, false);
    juce::StringArray parts;
    parts.addTokens(rest, ":", "");
    if (parts.size() != 4)
        return false;

    bank = parts[0].getIntValue();
    pad = parts[1].getIntValue();
    startSample = parts[2].getIntValue();
    endSample = parts[3].getIntValue();
    return true;
}

void PadComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto& pad = getPad();

    const bool loaded = pad.isLoaded();
    const bool muted = pad.muted.load();
    const bool soloed = pad.solo.load();
    auto base = loaded ? Colours2000::pad : Colours2000::padEmpty;
    if (muted)
        base = base.darker(0.4f); // visibly dimmed so a muted pad reads at a glance
    if (activeVisual)
        base = Colours2000::accent.withAlpha(0.55f); // whole pad lights up while playing, not just the border glow
    if (recordingVisual)
        base = Colours2000::red.withAlpha(0.55f); // dominant red tint - recording is a rarer, more critical state, wins over just-playing
    if (dragHover)
        base = base.brighter(0.15f);

    g.setColour(base);
    g.fillRoundedRectangle(bounds, 8.0f);

    if (recordingVisual)
    {
        // Local copy, deliberately NOT mutating `bounds` itself -
        // `bounds` is reused below for the border, choke dot, pad number,
        // and more; removeFromTop() on the shared rect would shrink it for
        // everything drawn after this block.
        auto recArea = bounds;
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("REC", recArea.removeFromTop(recArea.getHeight() * 0.55f), juce::Justification::centred);

        // Live input level meter - a thin bar under the REC text, filled
        // proportional to recordingLevel. Green/amber/red like any level
        // meter, since "is it clipping" needs to be obvious at a glance
        // without reading a number.
        auto meterArea = recArea.reduced(bounds.getWidth() * 0.15f, 0.0f).removeFromTop(8.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(meterArea, 2.0f);

        const float level = juce::jlimit(0.0f, 1.0f, recordingLevel);
        if (level > 0.01f)
        {
            auto fillArea = meterArea.withWidth(meterArea.getWidth() * level);
            const juce::Colour meterColour = level > 0.95f ? juce::Colours::red
                                            : level > 0.75f ? juce::Colours::orange
                                                             : juce::Colours::limegreen;
            g.setColour(meterColour);
            g.fillRoundedRectangle(fillArea, 2.0f);
        }
    }

    juce::Colour borderColour = activeVisual ? Colours2000::accent
                                : selectedVisual ? Colours2000::red
                                : Colours2000::padDark;
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, (activeVisual || selectedVisual) ? 2.0f : 1.0f);

    if (activeVisual)
    {
        g.setColour(Colours2000::accent.withAlpha(0.35f));
        g.drawRoundedRectangle(bounds.reduced(-2.0f), 9.0f, 3.0f);
    }

    if (muted || soloed)
    {
        g.setColour(muted ? Colours2000::red : Colours2000::accent);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(muted ? "M" : "S",
                   bounds.reduced(6.0f).removeFromBottom(14.0f).removeFromRight(14.0f),
                   juce::Justification::centred);
    }

    if (pad.chokeGroup > 0)
    {
        static const juce::Colour chokeColours[6] = {
            Colours2000::red, Colours2000::accent, juce::Colours::orange,
            juce::Colours::mediumpurple, juce::Colours::gold, juce::Colours::seagreen
        };
        g.setColour(chokeColours[(pad.chokeGroup - 1) % 6]);
        g.fillEllipse(bounds.getWidth() - 12.0f, 6.0f, 7.0f, 7.0f);
    }

    if (pad.hasExtraLayers())
    {
        g.setColour(Colours2000::accent);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("L", juce::Rectangle<float>(bounds.getWidth() - 26.0f, 4.0f, 12.0f, 12.0f),
                   juce::Justification::centred);
    }

    g.setColour(Colours2000::padTextMuted);
    g.setFont(juce::Font(juce::FontOptions(11.0f)).withTypefaceStyle("Regular"));
    g.drawText(juce::String(padIndex + 1).paddedLeft('0', 2),
               bounds.reduced(8.0f).removeFromTop(14.0f),
               juce::Justification::topLeft);

    if (loaded)
    {
        // Carved from ONE sequentially-shrinking rect so the waveform strip
        // and the name label can never land on overlapping pixel ranges
        // (they previously came from two independently-reduced copies of
        // `bounds` with mismatched offsets, which did overlap).
        auto contentArea = bounds.reduced(6.0f);
        auto waveArea = contentArea.removeFromBottom(20.0f);
        contentArea.removeFromBottom(2.0f);
        auto nameArea = contentArea.removeFromBottom(14.0f);

        g.setColour(Colours2000::accentDim.withAlpha(0.7f));
        juce::Random rnd(padIndex + bankIndex * 16);
        const int bars = 14;
        const float barW = waveArea.getWidth() / (float) bars;
        for (int i = 0; i < bars; ++i)
        {
            const float h = juce::jmap(rnd.nextFloat(), 0.3f, 1.0f) * waveArea.getHeight();
            g.fillRect(waveArea.getX() + i * barW, waveArea.getBottom() - h, barW * 0.6f, h);
        }

        g.setColour(Colours2000::padText);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(pad.displayName, nameArea, juce::Justification::bottomLeft);
    }
    else
    {
        g.setColour(Colours2000::padTextMuted);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("--", bounds.reduced(8.0f), juce::Justification::bottomLeft);
    }
}

void PadComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    // Tapping the pad that's currently being recorded stops and commits
    // the recording instead of triggering playback - "tap the pad again to
    // stop recording" is the standard sample-based groovebox convention.
    if (engine.isRecording() && engine.getRecordingBank() == bankIndex && engine.getRecordingPad() == padIndex)
    {
        engine.stopRecordingAndCommit();
        if (onPadChanged)
            onPadChanged();
        repaint();
        return;
    }

    mouseDownPos = e.getPosition();
    dragArmed = false;

    engine.requestTriggerFromUI(bankIndex, padIndex, 1.0f);
    setActiveVisual(true);
    if (onPadSelected)
        onPadSelected(bankIndex, padIndex);
}

void PadComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragArmed || e.mods.isPopupMenu())
        return;

    if (! getPad().isLoaded())
        return;

    if (e.getPosition().getDistanceFrom(mouseDownPos) < 10)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        dragArmed = true;
        container->startDragging(makeDragDescription(bankIndex, padIndex), this);
    }
}

void PadComponent::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    engine.requestReleaseFromUI(bankIndex, padIndex);
    setActiveVisual(false);
    dragArmed = false;
}

void PadComponent::showContextMenu()
{
    juce::PopupMenu menu;
    auto& pad = getPad();
    const bool loaded = pad.isLoaded();
    const bool muted = pad.muted.load();
    const bool soloed = pad.solo.load();

    menu.addItem("Load Sample...", [this] { openSampleDialog(); });
    menu.addItem("Export Sample...", loaded, false, [this] { openExportDialog(); });
    menu.addItem("Record Sample...", ! engine.isRecording(), false, [this]
    {
        engine.startRecordingIntoPad(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    // Explicit alternative to "tap the pad again to stop" - that gesture
    // isn't obvious from the menu alone, and someone who opened this menu
    // instead of tapping the pad clearly hasn't found it yet.
    const bool thisPadIsRecording = engine.isRecording() && engine.getRecordingBank() == bankIndex
                                   && engine.getRecordingPad() == padIndex;
    menu.addItem("Stop Recording", thisPadIsRecording, false, [this]
    {
        engine.stopRecordingAndCommit();
        if (onPadChanged) onPadChanged();
        repaint();
    });

    // Velocity layers: optional and tucked in a submenu specifically so a
    // pad with a single sample (the common case) looks exactly like it
    // always has in this menu - this doesn't get in the way unless you go
    // looking for it.
    juce::PopupMenu layersMenu;
    layersMenu.addItem("Add Layer (stacked - plays together on every hit)...", loaded, false,
                        [this] { openLayerDialog(false); });
    layersMenu.addItem("Add Velocity Layer (splits soft/hard hits)...", loaded, false,
                        [this] { openLayerDialog(true); });
    layersMenu.addSeparator();
    layersMenu.addItem("Clear Extra Layers", getPad().hasExtraLayers(), false, [this]
    {
        engine.clearExtraLayers(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addSubMenu(getPad().hasExtraLayers() ? "Layers (active)" : "Layers", layersMenu, loaded);

    menu.addSeparator();
    menu.addItem(muted ? "Unmute Pad" : "Mute Pad", loaded, false, [this]
    {
        auto& p = getPad();
        p.muted = ! p.muted.load();
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addItem(soloed ? "Unsolo Pad" : "Solo Pad", loaded, false, [this]
    {
        auto& p = getPad();
        p.solo = ! p.solo.load();
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addSeparator();
    menu.addItem("Normalize", loaded, false, [this]
    {
        engine.normalizePad(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addItem("Trim Silence", loaded, false, [this]
    {
        engine.trimSilence(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addItem("Trim Sample", loaded, false, [this]
    {
        engine.trimPadToSelection(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addItem("Reset Effects", loaded, false, [this]
    {
        engine.resetPadEffects(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });
    menu.addSeparator();
    menu.addItem("Clear Pad", loaded, false, [this]
    {
        engine.clearPad(bankIndex, padIndex);
        if (onPadChanged) onPadChanged();
        repaint();
    });

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void PadComponent::openSampleDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Sample",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        engine.loadSampleIntoPad(bankIndex, padIndex, file);
        if (onPadChanged)
            onPadChanged();
        repaint();
    });
}

void PadComponent::openLayerDialog(bool velocitySplit)
{
    fileChooser = std::make_unique<juce::FileChooser>(
        velocitySplit ? "Add Velocity Layer" : "Add Layer",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, velocitySplit](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        engine.addLayer(bankIndex, padIndex, file, velocitySplit);
        if (onPadChanged)
            onPadChanged();
        repaint();
    });
}

void PadComponent::openExportDialog()
{
    auto& pad = getPad();
    const juce::String suggestedName = pad.displayName.isNotEmpty() ? pad.displayName : "Sample";

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export Sample",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(suggestedName + ".wav"),
        "*.wav");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                      | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (! file.hasFileExtension("wav"))
            file = file.withFileExtension("wav");

        engine.exportPadToFile(bankIndex, padIndex, file);
    });
}

bool PadComponent::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    int srcBank, srcPad, startSample, endSample;
    const auto desc = details.description.toString();
    return parseDragDescription(desc, srcBank, srcPad)
        || parseRegionDragDescription(desc, srcBank, srcPad, startSample, endSample);
}

void PadComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto desc = details.description.toString();

    int regionBank, regionPad, startSample, endSample;
    if (parseRegionDragDescription(desc, regionBank, regionPad, startSample, endSample))
    {
        if (getPad().isLoaded())
        {
            // Already something here - ask rather than silently overwrite.
            // "Layer" extracts the same region audio but adds it as an
            // extra layer instead of replacing what's on this pad.
            juce::PopupMenu menu;
            menu.addItem("Layer", [this, regionBank, regionPad, startSample, endSample]
            {
                engine.addLayerFromRegion(regionBank, regionPad, startSample, endSample, bankIndex, padIndex);
                if (onPadChanged) onPadChanged();
                repaint();
            });
            menu.addItem("Replace", [this, regionBank, regionPad, startSample, endSample]
            {
                engine.extractRegionToPad(regionBank, regionPad, startSample, endSample, bankIndex, padIndex);
                if (onPadChanged) onPadChanged();
                repaint();
            });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
        }
        else
        {
            // Empty pad - nothing to layer with, so no prompt needed.
            engine.extractRegionToPad(regionBank, regionPad, startSample, endSample, bankIndex, padIndex);
            if (onPadChanged)
                onPadChanged();
            repaint();
        }
        return;
    }

    int srcBank, srcPad;
    if (! parseDragDescription(desc, srcBank, srcPad))
        return;

    if (srcBank == bankIndex && srcPad == padIndex)
        return; // dropped onto itself

    if (getPad().isLoaded())
    {
        // "Layer" renders the source pad's actual processed sound (same
        // as Replace/resample would) but adds it as an extra layer here
        // instead of swapping the two pads' contents.
        juce::PopupMenu menu;
        menu.addItem("Layer", [this, srcBank, srcPad]
        {
            engine.addLayerFromPad(srcBank, srcPad, bankIndex, padIndex);
            if (onPadChanged) onPadChanged();
            repaint();
        });
        menu.addItem("Replace (swap)", [this, srcBank, srcPad]
        {
            engine.swapPads(srcBank, srcPad, bankIndex, padIndex);
            if (onPadChanged) onPadChanged();
            repaint();
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }
    else
    {
        // Dropping onto an EMPTY pad resamples rather than plainly copying:
        // the destination gets the source's actual processed sound (filter,
        // pitch shift, time-stretch, sends - everything baked into real
        // audio) with its own settings left at defaults, not a parametric
        // copy of the source's settings. This is what makes "process a
        // loop, then chop the resampled copy" produce chops that all
        // actually sound like the processed original - chopping normally
        // reads raw stored audio, which never reflects live processing.
        // Nothing to layer with on an empty pad, so no prompt needed.
        engine.resamplePad(srcBank, srcPad, bankIndex, padIndex);
        if (onPadChanged)
            onPadChanged();
        repaint();
    }
}

bool PadComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    }
    return false;
}

void PadComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void PadComponent::fileDragExit(const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void PadComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (files.isEmpty())
    {
        repaint();
        return;
    }

    const juce::File file(files[0]);

    if (getPad().isLoaded())
    {
        // Already something here - ask rather than silently overwrite.
        // "Layer" adds the dropped file as an extra layer (stacked, full
        // velocity range - the simplest default; velocity-splitting is
        // still available via right-click or the LAYERS tab afterward).
        juce::PopupMenu menu;
        menu.addItem("Layer", [this, file]
        {
            engine.addLayer(bankIndex, padIndex, file, false);
            if (onPadChanged) onPadChanged();
            repaint();
        });
        menu.addItem("Replace", [this, file]
        {
            engine.loadSampleIntoPad(bankIndex, padIndex, file);
            if (onPadChanged) onPadChanged();
            repaint();
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }
    else
    {
        // Empty pad - nothing to layer with, so no prompt needed.
        engine.loadSampleIntoPad(bankIndex, padIndex, file);
        if (onPadChanged)
            onPadChanged();
        repaint();
    }
}
