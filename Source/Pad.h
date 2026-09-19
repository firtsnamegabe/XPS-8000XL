#pragma once
#include <atomic>
#include <array>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

enum class PlayMode
{
    oneShot,
    hold,   // gate: plays while note held
    loop
};

enum class LoopMode
{
    forward,
    pingPong
};

enum class FilterType
{
    lowPass,
    bandPass,
    highPass
};

enum class PitchMode
{
    // Pitch and playback speed/duration change together (simple resample -
    // the original, default behaviour).
    resample,
    // Pitch changes independently of playback duration, via a two-grain
    // overlap-add granular read (see SamplerEngine::renderVoice). This is
    // the most complex DSP in the project and hasn't been heard by anyone -
    // built by careful reading, not by testing, since this environment has
    // no audio output. Expect it to need real-world tuning (grain size in
    // particular) once you can actually listen to it.
    preserveLength
};

enum class ChopMode
{
    transients,
    equal,
    lazy
};

// Sample audio + its native sample rate, ref-counted so a playing Voice can
// hold its own reference: if the pad is reloaded with a new sample mid-
// playback, the old clip stays alive (and audible) until that voice
// finishes, instead of the audio thread reading a buffer that's being
// resized out from under it on the message thread.
struct SampleClip : public juce::ReferenceCountedObject
{
    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    using Ptr = juce::ReferenceCountedObjectPtr<SampleClip>;
};

// A single extra velocity-layer sample. Deliberately minimal: a clip, a
// velocity range, and its own volume (added after real use showed stacked/
// layered samples need independent balancing - the one thing that's
// genuinely awkward to share at the pad level). Everything else (trim,
// filter, pitch, envelope, sends, choke, play mode...) still stays at the
// PAD level and applies uniformly no matter which layer's audio ends up
// playing - layers are alternate *audio sources* with independent level,
// not alternate full pad configs. That's a deliberate scope decision to
// keep this from becoming a much bigger feature (each layer would
// otherwise need its own trim/chop/etc, and the whole panel would need
// per-layer switching UI) - and it's also what keeps a pad with no extra
// layers behaving in every way identically to before this existed.
struct ExtraLayer
{
    SampleClip::Ptr getClipForAudioThread() const
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        return clip;
    }

    void setClip(SampleClip::Ptr newClip)
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        clip = newClip;
    }

    bool isLoaded() const noexcept { return getClipForAudioThread() != nullptr; }

    juce::String displayName;
    std::atomic<int> velocityLow  { 0 };
    std::atomic<int> velocityHigh { 127 };
    std::atomic<float> volumeDb   { 0.0f };

private:
    SampleClip::Ptr clip;
    mutable juce::SpinLock clipLock;
};

// One sample pad: 16 of these live in a Bank.
//
// Threading contract: fields below are std::atomic because they're written
// from the message/GUI thread (knob drags, chop edits, MIDI Learn) and read
// every block by the audio thread. The sample buffer itself is swapped via
// `clip` under `clipLock` rather than being atomic directly - see
// getClipForAudioThread()/setClip(). Non-atomic fields (displayName,
// sourceFilePath, chopMarkers) are message/GUI-thread-only: the audio
// thread never touches them, only clip data + the atomics below.
struct Pad
{
    SampleClip::Ptr getClipForAudioThread() const
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        return clip;
    }

    void setClip(SampleClip::Ptr newClip)
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        clip = newClip;
    }

    bool isLoaded() const noexcept
    {
        auto c = getClipForAudioThread();
        return c != nullptr && c->data.getNumSamples() > 0;
    }

    int getNumSamples() const noexcept
    {
        auto c = getClipForAudioThread();
        return c != nullptr ? c->data.getNumSamples() : 0;
    }

    juce::String displayName;
    juce::String sourceFilePath;

    // Trim region, in source-sample-rate samples.
    std::atomic<int> trimStart { 0 };
    std::atomic<int> trimEnd   { 0 }; // 0 == "end of sample", resolved on load

    // Amp
    std::atomic<float> volumeDb   { 0.0f };  // -inf..+6
    std::atomic<float> pitchSemis { 0.0f };  // -24..+24
    std::atomic<PitchMode> pitchMode { PitchMode::resample };
    std::atomic<float> pitchGrainMs { 50.0f }; // preserveLength grain size; shorter=more percussive/grainy, longer=smoother/more smeared
    std::atomic<float> pan        { 0.0f };  // -1..1
    std::atomic<float> attackMs   { 0.0f };
    std::atomic<float> releaseMs  { 5.0f };  // small default to avoid clicks

    // Filter
    std::atomic<FilterType> filterType   { FilterType::lowPass };
    std::atomic<float> filterCutoffHz    { 20000.0f };
    std::atomic<float> filterResonance   { 0.7f };

    // Sends (0..1, post-fader send to shared FX buses)
    std::atomic<float> reverbSend { 0.0f };
    std::atomic<float> delaySend  { 0.0f };

    // Playback behaviour
    std::atomic<PlayMode> playMode { PlayMode::oneShot };
    std::atomic<LoopMode> loopMode  { LoopMode::forward };
    std::atomic<bool> reverse       { false };

    // Choke: 0 == no choke group, 1-6 == group index
    std::atomic<int> chokeGroup { 0 };

    // Time stretch: 0 = off (no BPM sync). When > 0, the engine computes
    // this sample's native BPM from (beatsInSample / trimmed duration) and
    // stretches playback to match SamplerEngine::getProjectBpm() - pitch
    // is preserved via the same granular engine used for Pitch Mode (see
    // SamplerEngine::renderVoice). Independent of Pitch Mode: you can
    // tempo-match AND pitch-shift the same pad at once if you want both.
    std::atomic<int> beatsInSample { 0 };

    // Mute/Solo are live gates checked every block, not just at trigger
    // time - muting or soloing a pad silences/reveals it immediately even
    // if it's already mid-playback (matches how real hardware mute works).
    // Multiple pads can be soloed simultaneously (standard mixer
    // convention) - this is our own addition beyond a simple Mute +
    // Unmute All.
    std::atomic<bool> muted { false };
    std::atomic<bool> solo  { false };

    // Lo-fi character, like classic hardware-sampler 12-bit sampling. bitDepth: 16 =
    // off/full quality, down to 4 = heavy crunch. sampleRateReduction: 1 =
    // off, higher = more aliased/stair-stepped (holds each captured sample
    // for N output samples - a "sample and hold" decimator, the same
    // technique real bitcrusher effects use).
    std::atomic<int> bitDepth { 16 };
    std::atomic<int> sampleRateReduction { 1 };

    // Distortion (DRIVE knob): 0 = off, up to 1 = heavily saturated. Simple
    // tanh waveshaping, not normalized against its own gain - like a real
    // drive pedal, more drive means both more clipping AND a hotter output
    // level, not just "the same volume but crunchier".
    std::atomic<float> distortionDrive { 0.0f };

    // Velocity range for the MAIN sample (the one loaded via drag-and-drop/
    // Load Sample/Record - "layer 0"). Defaults to the full range, so a pad
    // with no extra layers is triggered at any velocity exactly as before.
    // Only meaningful once extraLayers has at least one loaded layer.
    std::atomic<int> velocityLow  { 0 };
    std::atomic<int> velocityHigh { 127 };

    // Optional extra velocity layers - see ExtraLayer's comment. A trigger
    // plays EVERY layer (main + any extras) whose velocity range contains
    // the hit velocity: non-overlapping ranges give velocity switching
    // (soft hit = one sample, hard hit = another), overlapping/full ranges
    // give layering/stacking (multiple sounds fire together on one hit).
    static constexpr int kMaxExtraLayers = 3;
    std::array<ExtraLayer, kMaxExtraLayers> extraLayers;

    bool hasExtraLayers() const noexcept
    {
        for (auto& layer : extraLayers)
            if (layer.isLoaded())
                return true;
        return false;
    }

    // Poly: overlapping hits layer freely (default). Mono: a new hit
    // soft-stops any voice already playing this exact pad first (a
    // common Polyphony setting on sample-based groove boxes).
    std::atomic<bool> polyMode { true };

    // 0 = always full-velocity regardless of hit force, 1 = fully
    // velocity-responsive. A common Vel Sens convention, just normalized
    // to 0..1 instead of 0..127.
    std::atomic<float> velocitySensitivity { 1.0f };

    // Delay, in ms, between a trigger and the sample actually starting
    // (a common per-pad trigger Offset control).
    std::atomic<float> triggerOffsetMs { 0.0f };

    // MIDI. midiNote always reflects the note that currently triggers this
    // pad (chromatic-computed by default). midiLearned flags that it was
    // set explicitly via MIDI Learn rather than the chromatic formula, so
    // engine-wide offset changes won't silently override it.
    std::atomic<int> midiNote { -1 };
    std::atomic<bool> midiLearned { false };

    // Chop markers (sample positions, source-sample-rate). GUI/message
    // thread only - never read by the audio thread.
    std::vector<int> chopMarkers;

    void resetTrimToFull()
    {
        trimStart = 0;
        trimEnd = getNumSamples();
    }

    void clear()
    {
        setClip(nullptr);
        displayName.clear();
        sourceFilePath.clear();
        chopMarkers.clear();
        trimStart = 0;
        trimEnd = 0;
        volumeDb = 0.0f;
        pitchSemis = 0.0f;
        pan = 0.0f;
        chokeGroup = 0;
        muted = false;
        solo = false;
        bitDepth = 16;
        sampleRateReduction = 1;
        distortionDrive = 0.0f;
        velocityLow = 0;
        velocityHigh = 127;
        for (auto& layer : extraLayers)
        {
            layer.setClip(nullptr);
            layer.displayName.clear();
            layer.velocityLow = 0;
            layer.velocityHigh = 127;
            layer.volumeDb = 0.0f;
        }
    }

private:
    SampleClip::Ptr clip;
    mutable juce::SpinLock clipLock;
};
