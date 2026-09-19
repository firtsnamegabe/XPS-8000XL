#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"
#include "PadKnob.h"

class EditParamsComponent : public juce::Component
{
public:
    explicit EditParamsComponent(SamplerEngine& engineRef);

    void setPad(int bankIndex, int padIndex);
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Full height this panel actually needs to lay out without clipping,
    // at the given width - may exceed the viewport it's shown in, which is
    // the point: PluginEditor sizes this component to its preferred
    // height and puts it inside a juce::Viewport, so anything that
    // doesn't fit just scrolls instead of being cut off.
    int getPreferredContentHeight() const;

    std::function<void()> onMidiLearnRequested;

private:
    SamplerEngine& engine;
    int bankIndex = 0;
    int padIndex = 0;

    // Shared layout constants (used by both resized() and
    // getPreferredContentHeight(), so they can never drift apart).
    static constexpr int kKnobW = 58, kKnobH = 64, kGap = 8, kLabelH = 16;

    PadKnob volKnob     { "VOL",     -60.0, 6.0,  0.1 };
    PadKnob panKnob     { "PAN",     -1.0,  1.0,  0.01 };
    PadKnob attackKnob  { "ATTACK",  0.0,   2000.0, 1.0 };
    PadKnob releaseKnob { "RELEASE", 1.0,   4000.0, 1.0 };

    PadKnob pitchKnob   { "PITCH",   -24.0, 24.0, 0.1 };
    // Two-button segmented control rather than one toggle whose label
    // changes - both options are visible at once instead of needing a
    // click to discover the other state. Named VARISPEED (the actual
    // audio-engineering term for "pitch and speed change together", like
    // adjusting a tape deck's speed) rather than "RESAMPLE" - the latter
    // collided badly with the unrelated Resample-to-pad drag feature.
    juce::TextButton varispeedButton { "VARISPEED" };
    juce::TextButton timeLockButton  { "TIME LOCK" };
    PadKnob pitchGrainKnob { "GRAIN", 10.0, 150.0, 1.0 };
    PadKnob beatsKnob { "BEATS", 0.0, 32.0, 1.0 };
    PadKnob bpmKnob   { "BPM",   40.0, 240.0, 0.1 };

    juce::TextButton lpButton { "LP" }, bpButton { "BP" }, hpButton { "HP" };
    PadKnob cutoffKnob { "CUTOFF", 20.0, 20000.0, 1.0 };
    PadKnob resoKnob   { "RESO",   0.1,  10.0,    0.01 };

    // Lo-fi character (classic hardware-sampler bit-depth/sample-rate crunch).
    // BITS 16 = off; CRUSH 1 = off (higher = more aliased/stair-stepped).
    PadKnob bitDepthKnob { "BITS",  4.0, 16.0, 1.0 };
    PadKnob crushKnob    { "CRUSH", 1.0, 20.0, 1.0 };
    PadKnob driveKnob    { "DRIVE", 0.0, 1.0, 0.01 };

    PadKnob reverbSendKnob  { "SEND",  0.0, 1.0, 0.01 };
    PadKnob reverbDecayKnob { "DECAY", 0.0, 1.0, 0.01 };
    juce::Label reverbNameLabel { {}, "XOLO HALL" };
    PadKnob delaySendKnob   { "SEND",  0.0, 1.0, 0.01 };
    PadKnob delayTimeKnob   { "TIME",  0.02, 1.5, 0.01 };
    PadKnob delayFdbkKnob   { "FDBK",  0.0, 0.9, 0.01 };
    juce::Label delayNameLabel { {}, "XOLO ECHO" };

    juce::OwnedArray<juce::TextButton> chokeChips;

    juce::TextButton oneShotToggle { "ONE SHOT" };
    juce::TextButton loopToggle    { "LOOP" };
    juce::TextButton reverseToggle { "REVERSE" };
    juce::TextButton pingPongToggle { "PING PONG" };

    juce::TextButton midiLearnButton { "MIDI LEARN" };
    juce::Label noteReadout;

    juce::TextButton polyToggle { "POLY" };
    PadKnob velSensKnob { "VEL SENS", 0.0, 1.0, 0.01 };
    PadKnob offsetKnob  { "OFFSET",   0.0, 500.0, 1.0 };

    juce::Rectangle<int> ampLabelArea, pitchTimeLabelArea, filterLabelArea, lofiLabelArea, fxLabelArea,
                          chokeLabelArea, playbackLabelArea, playLabelArea;

    Pad& getPad() { return engine.getBank(bankIndex)[(size_t) padIndex]; }

    void refreshFromPad();
    void bindCallbacks();
    void updateFilterButtons();
    void updatePlaybackButtons();
    void updateChokeChips();
    void updatePolyToggle();
    void updatePitchModeToggle();
    void updateMidiLearnButton();
};
