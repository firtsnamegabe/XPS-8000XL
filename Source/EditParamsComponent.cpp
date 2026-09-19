#include "EditParamsComponent.h"

EditParamsComponent::EditParamsComponent(SamplerEngine& engineRef) : engine(engineRef)
{
    for (auto* k : { &volKnob, &pitchKnob, &panKnob, &attackKnob, &releaseKnob,
                      &cutoffKnob, &resoKnob, &reverbSendKnob, &reverbDecayKnob,
                      &delaySendKnob, &delayTimeKnob, &delayFdbkKnob,
                      &velSensKnob, &offsetKnob, &pitchGrainKnob, &beatsKnob, &bpmKnob,
                      &bitDepthKnob, &crushKnob, &driveKnob })
        addAndMakeVisible(k);

    for (auto* b : { &lpButton, &bpButton, &hpButton, &oneShotToggle, &loopToggle,
                      &reverseToggle, &pingPongToggle, &midiLearnButton, &polyToggle,
                      &varispeedButton, &timeLockButton })
        addAndMakeVisible(b);

    for (int i = 1; i <= kNumChokeGroups; ++i)
    {
        auto* chip = chokeChips.add(new juce::TextButton(juce::String(i)));
        addAndMakeVisible(chip);
    }

    noteReadout.setFont(juce::Font(juce::FontOptions(11.0f)));
    noteReadout.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(noteReadout);

    for (auto* l : { &reverbNameLabel, &delayNameLabel })
    {
        l->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, Colours2000::accent);
        l->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l);
    }

    // Tooltips for the controls whose behaviour isn't obvious from the
    // label alone - skipped for VOL/PAN/PITCH/ATTACK/RELEASE etc., which
    // are self-explanatory.
    varispeedButton.setTooltip("VARISPEED: pitch and duration change together, like a tape deck's speed knob "
                                "(the original, cheap behavior).");
    timeLockButton.setTooltip("TIME LOCK: pitch shifts without changing playback length.");
    pitchGrainKnob.setTooltip("Grain size for Pitch Mode TIME LOCK and BPM time-stretch. Shorter = more "
                               "percussive/grainy, longer = smoother/more smeared. For drum loops/transient "
                               "material specifically, try much shorter (10-20ms) - stretching tempo "
                               "inherently means re-reading overlapping chunks of source audio, and longer "
                               "grains make that overlap more audible as a repeating 'jump'. Smooth/sustained "
                               "sounds (pads, vocals) can use longer grains without that artifact.");
    beatsKnob.setTooltip("How many beats this sample contains. Set to 0 to disable BPM time-stretch for this pad.");
    bpmKnob.setTooltip("Project tempo (applies to ALL pads with BEATS set, not just this one). "
                        "Auto-synced from your DAW's tempo in VST3.");
    polyToggle.setTooltip("POLY: overlapping hits layer freely. MONO: a new hit stops the pad's previous voice first.");
    velSensKnob.setTooltip("0 = pad always plays at full volume regardless of hit force. 1 = fully velocity-responsive.");
    offsetKnob.setTooltip("Delays playback start after the hit, in milliseconds.");
    midiLearnButton.setTooltip("Click, then hit a note on your MIDI controller to map it to this pad.");
    loopToggle.setTooltip("Tap to start the loop, tap the pad again to stop it (does not sustain while held).");
    oneShotToggle.setTooltip("Plays through to the end regardless of how long the pad is held.");
    lpButton.setTooltip("Low-pass: cuts frequencies above the cutoff.");
    bpButton.setTooltip("Band-pass: cuts frequencies above AND below the cutoff.");
    hpButton.setTooltip("High-pass: cuts frequencies below the cutoff.");
    bitDepthKnob.setTooltip("Bit-depth crunch, like classic hardware-sampler lo-fi sound. 16 = off/full quality, "
                             "lower = grittier and more quantized.");
    crushKnob.setTooltip("Sample-rate reduction. 1 = off, higher = more aliased/stair-stepped "
                          "(holds each sample longer, like an old sampler running at a lower rate).");
    driveKnob.setTooltip("Distortion/saturation. 0 = off, higher = more clipping and a hotter output level - "
                          "like driving a distortion pedal harder, not just adding grit at a fixed volume.");
    reverbSendKnob.setTooltip("How much of this pad goes to the shared Xolo Hall reverb.");
    reverbDecayKnob.setTooltip("Reverb decay time - shared by every pad sending to it.");
    delaySendKnob.setTooltip("How much of this pad goes to the shared Xolo Echo delay.");
    delayTimeKnob.setTooltip("Delay time - shared by every pad sending to it.");
    delayFdbkKnob.setTooltip("Delay feedback (repeats) - shared by every pad sending to it.");
    for (int i = 0; i < chokeChips.size(); ++i)
        chokeChips[i]->setTooltip("Choke group " + juce::String(i + 1)
                                   + ": triggering any pad in the same group on this bank cuts the others off.");

    bindCallbacks();
    refreshFromPad();
}

void EditParamsComponent::bindCallbacks()
{
    volKnob.slider.onValueChange     = [this] { getPad().volumeDb = (float) volKnob.slider.getValue(); };
    pitchKnob.slider.onValueChange   = [this] { getPad().pitchSemis = (float) pitchKnob.slider.getValue(); };
    panKnob.slider.onValueChange     = [this] { getPad().pan = (float) panKnob.slider.getValue(); };
    attackKnob.slider.onValueChange  = [this] { getPad().attackMs = (float) attackKnob.slider.getValue(); };
    releaseKnob.slider.onValueChange = [this] { getPad().releaseMs = (float) releaseKnob.slider.getValue(); };

    cutoffKnob.slider.onValueChange = [this] { getPad().filterCutoffHz = (float) cutoffKnob.slider.getValue(); };
    resoKnob.slider.onValueChange   = [this] { getPad().filterResonance = (float) resoKnob.slider.getValue(); };

    lpButton.onClick = [this] { getPad().filterType = FilterType::lowPass; updateFilterButtons(); };
    bpButton.onClick = [this] { getPad().filterType = FilterType::bandPass; updateFilterButtons(); };
    hpButton.onClick = [this] { getPad().filterType = FilterType::highPass; updateFilterButtons(); };

    // Reverb/delay SEND is per-pad; DECAY/TIME/FDBK shape the one shared
    // effect bus every pad sends into (like an aux-return in a mixer).
    reverbSendKnob.slider.onValueChange = [this] { getPad().reverbSend = (float) reverbSendKnob.slider.getValue(); };
    reverbDecayKnob.slider.onValueChange = [this]
    {
        engine.getSendBuses().setReverbDecay((float) reverbDecayKnob.slider.getValue());
    };
    delaySendKnob.slider.onValueChange = [this] { getPad().delaySend = (float) delaySendKnob.slider.getValue(); };
    delayTimeKnob.slider.onValueChange = [this]
    {
        engine.getSendBuses().setDelayTimeSeconds((float) delayTimeKnob.slider.getValue());
    };
    delayFdbkKnob.slider.onValueChange = [this]
    {
        engine.getSendBuses().setDelayFeedback((float) delayFdbkKnob.slider.getValue());
    };

    for (int i = 0; i < chokeChips.size(); ++i)
    {
        chokeChips[i]->onClick = [this, i]
        {
            auto& pad = getPad();
            pad.chokeGroup = (pad.chokeGroup == i + 1) ? 0 : (i + 1); // click again to clear
            updateChokeChips();
        };
    }

    oneShotToggle.onClick = [this] { getPad().playMode = PlayMode::oneShot; updatePlaybackButtons(); };
    loopToggle.onClick = [this]
    {
        auto& pad = getPad();
        pad.playMode = (pad.playMode == PlayMode::loop) ? PlayMode::hold : PlayMode::loop;
        updatePlaybackButtons();
    };
    reverseToggle.onClick = [this] { getPad().reverse = ! getPad().reverse; updatePlaybackButtons(); };
    pingPongToggle.onClick = [this]
    {
        auto& pad = getPad();
        pad.loopMode = (pad.loopMode == LoopMode::pingPong) ? LoopMode::forward : LoopMode::pingPong;
        updatePlaybackButtons();
    };

    midiLearnButton.onClick = [this]
    {
        if (onMidiLearnRequested)
            onMidiLearnRequested();
    };

    polyToggle.onClick = [this]
    {
        auto& pad = getPad();
        pad.polyMode = ! pad.polyMode.load();
        updatePolyToggle();
    };

    varispeedButton.onClick = [this] { getPad().pitchMode = PitchMode::resample; updatePitchModeToggle(); };
    timeLockButton.onClick  = [this] { getPad().pitchMode = PitchMode::preserveLength; updatePitchModeToggle(); };
    pitchGrainKnob.slider.onValueChange = [this] { getPad().pitchGrainMs = (float) pitchGrainKnob.slider.getValue(); };
    beatsKnob.slider.onValueChange = [this] { getPad().beatsInSample = (int) beatsKnob.slider.getValue(); };
    bpmKnob.slider.onValueChange = [this] { engine.setProjectBpm((float) bpmKnob.slider.getValue()); };
    bitDepthKnob.slider.onValueChange = [this] { getPad().bitDepth = (int) bitDepthKnob.slider.getValue(); };
    crushKnob.slider.onValueChange = [this] { getPad().sampleRateReduction = (int) crushKnob.slider.getValue(); };
    driveKnob.slider.onValueChange = [this] { getPad().distortionDrive = (float) driveKnob.slider.getValue(); };
    velSensKnob.slider.onValueChange = [this] { getPad().velocitySensitivity = (float) velSensKnob.slider.getValue(); };
    offsetKnob.slider.onValueChange  = [this] { getPad().triggerOffsetMs = (float) offsetKnob.slider.getValue(); };
}

void EditParamsComponent::setPad(int newBank, int newPad)
{
    bankIndex = newBank;
    padIndex = newPad;
    refreshFromPad();
}

void EditParamsComponent::refreshFromPad()
{
    auto& pad = getPad();
    volKnob.slider.setValue(pad.volumeDb, juce::dontSendNotification);
    pitchKnob.slider.setValue(pad.pitchSemis, juce::dontSendNotification);
    panKnob.slider.setValue(pad.pan, juce::dontSendNotification);
    attackKnob.slider.setValue(pad.attackMs, juce::dontSendNotification);
    releaseKnob.slider.setValue(pad.releaseMs, juce::dontSendNotification);
    cutoffKnob.slider.setValue(pad.filterCutoffHz, juce::dontSendNotification);
    resoKnob.slider.setValue(pad.filterResonance, juce::dontSendNotification);
    reverbSendKnob.slider.setValue(pad.reverbSend, juce::dontSendNotification);
    delaySendKnob.slider.setValue(pad.delaySend, juce::dontSendNotification);
    velSensKnob.slider.setValue(pad.velocitySensitivity, juce::dontSendNotification);
    offsetKnob.slider.setValue(pad.triggerOffsetMs, juce::dontSendNotification);
    pitchGrainKnob.slider.setValue(pad.pitchGrainMs, juce::dontSendNotification);
    beatsKnob.slider.setValue(pad.beatsInSample.load(), juce::dontSendNotification);
    bpmKnob.slider.setValue(engine.getProjectBpm(), juce::dontSendNotification);
    bitDepthKnob.slider.setValue(pad.bitDepth.load(), juce::dontSendNotification);
    crushKnob.slider.setValue(pad.sampleRateReduction.load(), juce::dontSendNotification);
    driveKnob.slider.setValue(pad.distortionDrive.load(), juce::dontSendNotification);

    noteReadout.setText("NOTE " + juce::String(pad.midiNote), juce::dontSendNotification);

    updateFilterButtons();
    updatePlaybackButtons();
    updateChokeChips();
    updatePolyToggle();
    updatePitchModeToggle();
    updateMidiLearnButton();
}

void EditParamsComponent::updateMidiLearnButton()
{
    const bool armed = engine.isMidiLearnArmed();
    midiLearnButton.setButtonText(armed ? "CANCEL LEARN" : "MIDI LEARN");
    midiLearnButton.setToggleState(armed, juce::dontSendNotification);
}

void EditParamsComponent::updatePitchModeToggle()
{
    auto& pad = getPad();
    const bool preserveLength = (pad.pitchMode.load() == PitchMode::preserveLength);
    varispeedButton.setToggleState(! preserveLength, juce::dontSendNotification);
    timeLockButton.setToggleState(preserveLength, juce::dontSendNotification);
}

void EditParamsComponent::updatePolyToggle()
{
    auto& pad = getPad();
    const bool poly = pad.polyMode.load();
    polyToggle.setButtonText(poly ? "POLY" : "MONO");
    polyToggle.setToggleState(poly, juce::dontSendNotification);
}

void EditParamsComponent::updateFilterButtons()
{
    auto& pad = getPad();
    lpButton.setToggleState(pad.filterType == FilterType::lowPass, juce::dontSendNotification);
    bpButton.setToggleState(pad.filterType == FilterType::bandPass, juce::dontSendNotification);
    hpButton.setToggleState(pad.filterType == FilterType::highPass, juce::dontSendNotification);
}

void EditParamsComponent::updatePlaybackButtons()
{
    auto& pad = getPad();
    oneShotToggle.setToggleState(pad.playMode == PlayMode::oneShot, juce::dontSendNotification);
    loopToggle.setToggleState(pad.playMode == PlayMode::loop, juce::dontSendNotification);
    reverseToggle.setToggleState(pad.reverse, juce::dontSendNotification);
    pingPongToggle.setToggleState(pad.loopMode == LoopMode::pingPong, juce::dontSendNotification);
}

void EditParamsComponent::updateChokeChips()
{
    auto& pad = getPad();
    for (int i = 0; i < chokeChips.size(); ++i)
        chokeChips[i]->setToggleState(pad.chokeGroup == i + 1, juce::dontSendNotification);
}

void EditParamsComponent::paint(juce::Graphics& g)
{
    auto drawSectionLabel = [&g](juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour(Colours2000::textMuted);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(text, r, juce::Justification::centredLeft);
    };

    drawSectionLabel(ampLabelArea, "AMP");
    drawSectionLabel(pitchTimeLabelArea, "PITCH & TIME STRETCH (BPM SYNC)");
    drawSectionLabel(filterLabelArea, "FILTER");
    drawSectionLabel(lofiLabelArea, "LOFI & DRIVE");
    drawSectionLabel(fxLabelArea, "SEND FX");
    drawSectionLabel(chokeLabelArea, "CHOKE GROUP");
    drawSectionLabel(playbackLabelArea, "PLAYBACK");
    drawSectionLabel(playLabelArea, "PLAY");
}

int EditParamsComponent::getPreferredContentHeight() const
{
    // Two full-width rows (AMP, PITCH & TIME STRETCH), then the taller of
    // the two columns below them. Mirrors resized()'s layout exactly - see
    // the comment there before changing either without the other.
    const int fullWidthRows = (kLabelH + kKnobH + kGap) * 2;

    const int columnA = kLabelH + 28 + 4 + kKnobH + kGap                 // FILTER
                       + kLabelH + kKnobH + kGap                         // LOFI
                       + kLabelH + 14 + kKnobH + 4 + 14 + kKnobH + kGap; // SEND FX
    const int columnB = kLabelH + 30 + kGap                              // CHOKE GROUP
                       + kLabelH + 30 + 4 + 30 + kGap                    // PLAYBACK (2 rows)
                       + kLabelH + kKnobH + kGap                         // PLAY
                       + 26;                                             // MIDI LEARN

    return fullWidthRows + juce::jmax(columnA, columnB) + kGap * 2; // + top/bottom breathing room
}

void EditParamsComponent::resized()
{
    auto area = getLocalBounds();

    auto layoutKnobRow = [&](juce::Rectangle<int> row, std::initializer_list<PadKnob*> knobs)
    {
        auto r = row;
        for (auto* k : knobs)
        {
            k->setBounds(r.removeFromLeft(kKnobW).withHeight(kKnobH));
            r.removeFromLeft(kGap);
        }
    };

    // --- Full-width: AMP ---------------------------------------------
    ampLabelArea = area.removeFromTop(kLabelH);
    layoutKnobRow(area.removeFromTop(kKnobH), { &volKnob, &panKnob, &attackKnob, &releaseKnob });
    area.removeFromTop(kGap);

    // --- Full-width: PITCH & TIME STRETCH ------------------------------
    // Grouped together deliberately: BPM time-stretch and Pitch Mode share
    // the same underlying granular engine (see SamplerEngine::renderVoice),
    // so it makes sense for them to live in one place rather than being
    // split across the panel.
    pitchTimeLabelArea = area.removeFromTop(kLabelH);
    auto pitchTimeRow = area.removeFromTop(kKnobH);
    // NOTE: layoutKnobRow takes its rect BY VALUE, so it only actually
    // shrinks the caller's rectangle when called with a throwaway
    // temporary (e.g. `area.removeFromTop(...)`) or as the LAST thing done
    // with that rect. Calling it mid-sequence on a named rect you then
    // keep using - like pitchTimeRow here - silently shrinks a copy and
    // leaves the original untouched, which is exactly what put the PITCH
    // knob and the VARISPEED/TIME LOCK buttons on top of each other. Fixed
    // by positioning pitchKnob with a direct removeFromLeft() call instead
    // of routing it through the lambda.
    pitchKnob.setBounds(pitchTimeRow.removeFromLeft(kKnobW).withHeight(kKnobH));
    pitchTimeRow.removeFromLeft(kGap);
    varispeedButton.setBounds(pitchTimeRow.removeFromLeft(78).withHeight(28));
    pitchTimeRow.removeFromLeft(4);
    timeLockButton.setBounds(pitchTimeRow.removeFromLeft(78).withHeight(28));
    pitchTimeRow.removeFromLeft(kGap);
    layoutKnobRow(pitchTimeRow, { &pitchGrainKnob, &beatsKnob, &bpmKnob }); // last use of pitchTimeRow - safe
    area.removeFromTop(kGap);

    // --- Two columns for everything else -------------------------------
    const int columnGap = 16;
    const int columnWidth = (area.getWidth() - columnGap) / 2;
    auto columnA = area.removeFromLeft(columnWidth);
    area.removeFromLeft(columnGap);
    auto columnB = area;

    // Column A: FILTER, SEND FX
    filterLabelArea = columnA.removeFromTop(kLabelH);
    auto filterTypeRow = columnA.removeFromTop(28);
    for (auto* btn : { &lpButton, &bpButton, &hpButton })
    {
        btn->setBounds(filterTypeRow.removeFromLeft(32));
        filterTypeRow.removeFromLeft(4);
    }
    columnA.removeFromTop(4);
    layoutKnobRow(columnA.removeFromTop(kKnobH), { &cutoffKnob, &resoKnob });
    columnA.removeFromTop(kGap);

    lofiLabelArea = columnA.removeFromTop(kLabelH);
    layoutKnobRow(columnA.removeFromTop(kKnobH), { &bitDepthKnob, &crushKnob, &driveKnob });
    columnA.removeFromTop(kGap);

    fxLabelArea = columnA.removeFromTop(kLabelH);
    reverbNameLabel.setBounds(columnA.removeFromTop(14));
    layoutKnobRow(columnA.removeFromTop(kKnobH), { &reverbSendKnob, &reverbDecayKnob });
    columnA.removeFromTop(4);
    delayNameLabel.setBounds(columnA.removeFromTop(14));
    layoutKnobRow(columnA.removeFromTop(kKnobH), { &delaySendKnob, &delayTimeKnob, &delayFdbkKnob });

    // Column B: CHOKE GROUP, PLAYBACK, PLAY, MIDI LEARN
    chokeLabelArea = columnB.removeFromTop(kLabelH);
    auto chokeRow = columnB.removeFromTop(30);
    for (auto* chip : chokeChips)
    {
        chip->setBounds(chokeRow.removeFromLeft(30));
        chokeRow.removeFromLeft(6);
    }
    columnB.removeFromTop(kGap);

    playbackLabelArea = columnB.removeFromTop(kLabelH);
    auto playbackRow1 = columnB.removeFromTop(30);
    oneShotToggle.setBounds(playbackRow1.removeFromLeft(80));
    playbackRow1.removeFromLeft(6);
    loopToggle.setBounds(playbackRow1.removeFromLeft(80));
    columnB.removeFromTop(4);
    auto playbackRow2 = columnB.removeFromTop(30);
    reverseToggle.setBounds(playbackRow2.removeFromLeft(80));
    playbackRow2.removeFromLeft(6);
    pingPongToggle.setBounds(playbackRow2.removeFromLeft(80));
    columnB.removeFromTop(kGap);

    playLabelArea = columnB.removeFromTop(kLabelH);
    auto playRow = columnB.removeFromTop(kKnobH);
    polyToggle.setBounds(playRow.removeFromLeft(64).withHeight(28));
    playRow.removeFromLeft(kGap);
    layoutKnobRow(playRow, { &velSensKnob, &offsetKnob });
    columnB.removeFromTop(kGap);

    auto learnRow = columnB.removeFromTop(26);
    midiLearnButton.setBounds(learnRow.removeFromLeft(110));
    noteReadout.setBounds(learnRow);
}
