#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <juce_dsp/juce_dsp.h>
#include "Pad.h"

constexpr int kNumBanks = 4;
constexpr int kPadsPerBank = 16;
constexpr int kMaxVoices = 32;
constexpr int kNumChokeGroups = 6;
constexpr int kTriggerQueueSize = 256;

using Bank = std::array<Pad, kPadsPerBank>;

// A single playing instance of a pad (a triggered sample).
struct Voice
{
    bool active = false;
    int bankIndex = -1;
    int padIndex = -1;
    int chokeGroup = 0;

    // Which layer this voice is playing: -1 = the pad's main sample, 0..2
    // = an index into Pad::extraLayers. Needed so renderVoice's live
    // volume-knob-while-sustaining update (see its use below) reads the
    // CORRECT layer's volume each block, not always the main sample's.
    int layerIndex = -1;

    // The voice holds its own reference to the clip it's playing, taken at
    // trigger time. If the pad is reloaded with new audio mid-playback, this
    // voice keeps playing the old clip safely rather than reading a buffer
    // that's being resized on another thread.
    SampleClip::Ptr clip;

    double readPos = 0.0;       // fractional read position, in source samples
    double playbackRatio = 1.0; // pitch-driven read speed
    bool releasing = false;
    bool chokeFade = false;     // true when releasing due to a choke (fast, fixed release)
    float envelopeLevel = 0.0f;
    float velocityGain = 1.0f;
    bool forwardDirection = true; // for ping-pong loop

    // Cached trim bounds, snapshotted at trigger time from the pad's atomics
    // (trim can still be edited live via the smoothed values below, but the
    // loop/one-shot boundary check uses this stable snapshot per note).
    int trimStart = 0;
    int trimEnd = 0;

    // Trigger Offset: counts down (in samples) before playback actually
    // starts; the voice is active but silent while this is > 0.
    int pendingDelaySamples = 0;

    // Granular pitch-shift state, used only when the pad's PitchMode is
    // preserveLength. Four overlapping grains (25% hop / 75% overlap) read
    // the source at the pitch-shifted rate while their start positions are
    // periodically re-anchored to `readPos`, which itself advances at
    // real-time (1x) speed in this mode instead of the pitch-scaled rate -
    // that's what keeps the note's overall duration independent of pitch.
    // Went from 2 to 4 grains to roughly halve the residual periodic
    // amplitude-modulation artifact (Hann windows are exactly constant-sum
    // at both 50% and 75% overlap, so this is a real improvement, not just
    // more CPU for nothing - see SamplerEngine::renderVoice's normalization
    // comment for the math). Only reduces one specific artifact, though:
    // it does not fix the more fundamental "grainy/smeared" character
    // inherent to small-window time-domain granular pitch shifting at
    // large pitch ratios - that's what the grain-size knob is for.
    static constexpr int kNumGrains = 4;
    double grainReadPos[kNumGrains] = {};
    double grainAgeSamples[kNumGrains] = {};

    // Sample-rate reduction (lo-fi decimation): holds the last "captured"
    // value per channel until the next capture point, creating the classic
    // stair-stepped/aliased lo-fi character. Bit-depth reduction (the other
    // half of the lo-fi effect) is stateless quantization, so it needs no
    // per-voice state here.
    float lofiHold[2] = {};
    int lofiCounter = 0;

    // Per-voice filter state.
    juce::dsp::StateVariableTPTFilter<float> filter;

    // Smoothed per-block parameters, so live knob moves don't zipper/click.
    juce::SmoothedValue<float> smoothedVolumeGain;
    juce::SmoothedValue<float> smoothedPan;
    juce::SmoothedValue<float> smoothedCutoff;
    juce::SmoothedValue<float> smoothedResonance;
    juce::SmoothedValue<float> smoothedReverbSend;
    juce::SmoothedValue<float> smoothedDelaySend;

    void reset()
    {
        active = false;
        bankIndex = -1;
        padIndex = -1;
        chokeGroup = 0;
        layerIndex = -1;
        clip = nullptr;
        readPos = 0.0;
        playbackRatio = 1.0;
        releasing = false;
        chokeFade = false;
        envelopeLevel = 0.0f;
        velocityGain = 1.0f;
        forwardDirection = true;
        filter.reset();
        for (int g = 0; g < kNumGrains; ++g)
        {
            grainReadPos[g] = 0.0;
            grainAgeSamples[g] = 0.0;
        }
        lofiHold[0] = lofiHold[1] = 0.0f;
        lofiCounter = 0;
    }
};

// Shared send-effects buses: one reverb + one delay line, fed by every
// voice's per-pad send amount and mixed back into the main output.
class SendBuses
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    void beginBlock(int numSamples);
    void addToReverb(int channel, int sampleIndex, float value);
    void addToDelay(int channel, int sampleIndex, float value);
    void processAndMixInto(juce::AudioBuffer<float>& output);

    void setReverbDecay(float decay01);
    void setDelayTimeSeconds(float seconds);
    void setDelayFeedback(float feedback01);

private:
    juce::AudioBuffer<float> reverbBus, delayBus;
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters reverbParams;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 1 << 17 };
    float delayFeedback = 0.35f;
    double sampleRate = 44100.0;
};

class SamplerEngine
{
public:
    SamplerEngine();

    void prepare(double sampleRate, int maxBlockSize);
    void releaseResources();

    // Audio-thread entry point. Drains queued GUI triggers, consumes MIDI,
    // renders voices.
    void processBlock(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi);

    // --- GUI-thread trigger requests ---------------------------------------
    // GUI pad clicks must NOT call triggerPad/releasePad directly - the
    // voices array is only ever mutated on the audio thread. These push a
    // request onto a lock-free queue that processBlock drains at the start
    // of each call.
    void requestTriggerFromUI(int bankIndex, int padIndex, float velocity01);
    void requestReleaseFromUI(int bankIndex, int padIndex);

    // Emergency stop: soft-releases every currently-active voice across
    // every bank/pad. GUI-safe like the trigger requests above - just sets
    // a flag; the actual voice-array mutation happens on the audio thread
    // at the top of the next processBlock().
    void requestKillAllFromUI();

    bool loadSampleIntoPad(int bankIndex, int padIndex, const juce::File& audioFile);

    // --- Velocity layers -------------------------------------------------
    // See ExtraLayer's comment in Pad.h for the scope this deliberately
    // covers (and doesn't). velocitySplit=true auto-narrows every existing
    // layer's range to make room for the new one (velocity switching);
    // false leaves every layer's range as-is, defaulting the new one to the
    // full range (layering/stacking - it plays alongside whatever else
    // matches). Returns false if the pad already has kMaxExtraLayers loaded
    // or the file fails to load.
    bool addLayer(int bankIndex, int padIndex, const juce::File& audioFile, bool velocitySplit);
    void clearExtraLayers(int bankIndex, int padIndex);

    // Clears one specific extra layer slot (0..kMaxExtraLayers-1), leaving
    // the others untouched - unlike clearExtraLayers(), which wipes all of
    // them.
    void removeLayer(int bankIndex, int padIndex, int layerIndex);

    // Loads directly into a SPECIFIC slot (0..kMaxExtraLayers-1), whether
    // currently empty or replacing what's there - unlike addLayer(), which
    // always picks the next empty slot. Used by the Layers panel, where
    // each row is a fixed slot index rather than "wherever there's room".
    // Deliberately does not touch that slot's velocity range either way -
    // a replace shouldn't undo a range the user already set up.
    bool loadLayerSlot(int bankIndex, int padIndex, int layerIndex, const juce::File& audioFile);

    // Adds already-decoded audio as the next empty extra layer (full
    // velocity range - stacks with whatever else already matches). Shared
    // building block for the "layer" side of drag-and-drop: dropping a
    // whole pad or a chop region onto an already-loaded pad, when you
    // choose "Layer" instead of the default replace/resample/swap
    // behavior, renders/extracts the same audio addLayerFromPad()/
    // addLayerFromRegion() below would produce a full replacement from,
    // and adds it as a layer here instead.
    bool addLayerFromBuffer(int bankIndex, int padIndex, juce::AudioBuffer<float>&& audioData,
                             double sourceSampleRate, const juce::String& displayName);

    // "Layer" counterpart to resamplePad()/extractRegionToPad(): same
    // rendering/extraction, but the result is added as an extra layer on
    // the destination instead of replacing its main sample.
    bool addLayerFromPad(int sourceBankIndex, int sourcePadIndex, int destBankIndex, int destPadIndex);
    bool addLayerFromRegion(int sourceBankIndex, int sourcePadIndex, int startSample, int endSample,
                             int destBankIndex, int destPadIndex);

    // Loads already-decoded audio data directly (used when restoring a Kit
    // file, whose samples are embedded rather than referenced by path).
    // Takes ownership of audioData via move.
    bool loadSampleDataIntoPad(int bankIndex, int padIndex, juce::AudioBuffer<float>&& audioData,
                                double sourceSampleRate, const juce::String& displayName);

    // --- Direct audio recording ---------------------------------------
    // Records live audio input (routed to the plugin's audio input bus, or
    // Standalone's selected input device) directly into a pad - a standard
    // Sample Record mode. Start/stop are called from
    // the GUI/message thread; captureRecordingInput is called every block
    // from the audio thread (PluginProcessor::processBlock, before the
    // output buffer is cleared). recordingLock (see private section)
    // guards the handoff, the same SpinLock pattern already used for
    // Pad's sample-clip swaps.
    void startRecordingIntoPad(int bankIndex, int padIndex);
    void stopRecordingAndCommit(); // safe to call even if nothing is recording
    void captureRecordingInput(const juce::AudioBuffer<float>& input);

    bool isRecording() const noexcept { return recordingActive.load(); }
    int getRecordingBank() const noexcept { return recordBank.load(); }
    int getRecordingPad() const noexcept { return recordPad.load(); }

    // --- Master (not per-pad) -------------------------------------------
    // Input gain applies to live audio as it's captured for recording
    // (captureRecordingInput above). Output gain and the compressor apply
    // once to the final mixed output, at the end of processBlock - after
    // every voice and the shared reverb/delay buses are already summed.
    // The compressor is master-bus ("glue") compression on the whole mix,
    // not per-pad - lower risk (one instance, not one per voice) and the
    // more conventional use of compression on a drum sampler.
    void setMasterInputGainDb(float db) { masterInputGainDb = db; }
    float getMasterInputGainDb() const noexcept { return masterInputGainDb.load(); }
    void setMasterOutputGainDb(float db) { masterOutputGainDb = db; }
    float getMasterOutputGainDb() const noexcept { return masterOutputGainDb.load(); }

    void setCompressorEnabled(bool enabled) { compressorEnabled = enabled; }
    bool isCompressorEnabled() const noexcept { return compressorEnabled.load(); }
    void setCompressorThresholdDb(float db) { compressorThresholdDb = db; }
    float getCompressorThresholdDb() const noexcept { return compressorThresholdDb.load(); }
    void setCompressorRatio(float ratio) { compressorRatio = ratio; }
    float getCompressorRatio() const noexcept { return compressorRatio.load(); }
    void setCompressorAttackMs(float ms) { compressorAttackMs = ms; }
    float getCompressorAttackMs() const noexcept { return compressorAttackMs.load(); }
    void setCompressorReleaseMs(float ms) { compressorReleaseMs = ms; }
    float getCompressorReleaseMs() const noexcept { return compressorReleaseMs.load(); }
    double getRecordingSeconds() const noexcept { return (double) recordWritePos.load() / juce::jmax(1.0, sampleRate); }

    // Live input level while recording is armed, 0..1 (peak with decay,
    // not RMS - meant for "is it clipping" at a glance, not precise
    // metering). 0 whenever not recording.
    float getRecordingPeakLevel() const noexcept { return recordingPeakLevel.load(); }

    Bank& getBank(int index) { return banks[(size_t) index]; }
    const Bank& getBank(int index) const { return banks[(size_t) index]; }

    int getCurrentBank() const noexcept { return currentBank; }
    void setCurrentBank(int index) { currentBank = juce::jlimit(0, kNumBanks - 1, index); }

    void setMidiNoteOffset(int offset);
    int getMidiNoteOffset() const noexcept { return midiNoteOffset; }

    // Project tempo used by per-pad BPM time-stretch (Pad::beatsInSample).
    // Auto-updated from the host's tempo each block when running as a
    // plugin with a playhead that reports one (see PluginProcessor::
    // processBlock); stays at whatever it was last set to otherwise
    // (Standalone has no host tempo, so this is the only way to set it).
    void setProjectBpm(float bpm) { projectBpm = juce::jlimit(20.0f, 400.0f, bpm); }
    float getProjectBpm() const noexcept { return projectBpm.load(); }

    bool noteToPad(int noteNumber, int& outBank, int& outPad) const;

    // --- MIDI Learn ---------------------------------------------------------
    void armMidiLearn(int bankIndex, int padIndex);
    void cancelMidiLearn();
    bool isMidiLearnArmed() const noexcept { return midiLearnArmed.load(); }
    std::function<void(int bank, int pad, int note)> onMidiLearned;

    // --- Chop ---------------------------------------------------------
    void generateEqualChops(int bankIndex, int padIndex, int numChops);
    void generateTransientChops(int bankIndex, int padIndex, float sensitivity01);
    void addLazyMarker(int bankIndex, int padIndex, int samplePosition);
    void clearChopMarkers(int bankIndex, int padIndex);

    int commitChopsToPads(int sourceBankIndex, int sourcePadIndex,
                           bool keepOriginal, int choppedChokeGroup,
                           bool forceOneShot, bool playThru);

    // --- Pad tools (message-thread only) -----------------------------------
    void clearPad(int bankIndex, int padIndex);
    void copyPad(int fromBank, int fromPad, int toBank, int toPad);
    void swapPads(int bankA, int padA, int bankB, int padB);
    void normalizePad(int bankIndex, int padIndex);
    void trimSilence(int bankIndex, int padIndex);

    // Destructively discards audio outside the current trim points, then
    // resets trim to the new full length ("Trim Sample" - non-destructive
    // trim just moves playback bounds, this actually shrinks the stored
    // audio). Chop markers are cleared, since
    // their positions were relative to the old, longer buffer.
    void trimPadToSelection(int bankIndex, int padIndex);

    // Writes the pad's currently TRIMMED audio (matching what actually
    // plays, not the full stored buffer if trim has shrunk the playback
    // range) out as a real 32-bit float WAV file - unlike Kit files (which
    // embed raw PCM in XML for internal round-tripping), this needs to be
    // an actual standards-compliant file other apps can open.
    bool exportPadToFile(int bankIndex, int padIndex, const juce::File& destFile) const;

    // Resets a pad's processing parameters (volume, pitch/pitch mode/grain,
    // filter, envelope, reverb/delay sends, BPM stretch) to their defaults.
    // Deliberately leaves the loaded sample, trim, choke group, MIDI
    // mapping, mute/solo, and play mode untouched - those are structural/
    // performance settings, not "effects".
    void resetPadEffects(int bankIndex, int padIndex);

    // Extracts [startSample, endSample) of the source pad's raw audio (its
    // own sample-rate units) and loads it as fresh audio onto the dest pad,
    // always overwriting whatever's there. Used for dragging a single chop
    // region straight onto a specific pad, as an alternative to COMMIT
    // (which auto-fills the next free pads in bank order).
    bool extractRegionToPad(int sourceBankIndex, int sourcePadIndex, int startSample, int endSample,
                             int destBankIndex, int destPadIndex);

    // Renders the source pad's *actual playback* - filter, pitch shift,
    // time-stretch, envelope, reverb/delay sends, all of it - into a fresh
    // audio buffer and loads that as new, unprocessed audio onto the dest
    // pad (whose own parameters are left at their defaults, not copied).
    // The point: chopping normally reads a pad's raw stored audio, so any
    // live processing (filter/pitch/stretch) is invisible to the chopper -
    // this "prints" that processing into real samples first, so every
    // resulting chop actually sounds like the processed original. Message-
    // thread only; runs its own offline voice + dedicated send buses so it
    // never touches the live, audio-thread-owned voices/sendBuses.
    // Capped at kMaxResampleSeconds so a looping source can't run forever.
    bool resamplePad(int sourceBankIndex, int sourcePadIndex, int destBankIndex, int destPadIndex);

    float getPlayheadFraction(int bankIndex, int padIndex) const;
    bool isPadPlaying(int bankIndex, int padIndex) const;
    bool isAnySoloActive() const;

    SendBuses& getSendBuses() { return sendBuses; }

private:
    std::array<Bank, kNumBanks> banks;
    std::array<Voice, kMaxVoices> voices;
    std::atomic<int> currentBank { 0 };
    int midiNoteOffset = 36; // C1, only touched from message thread (state load, offset UI)
    std::atomic<float> projectBpm { 120.0f }; // read by the audio thread (renderVoice), written by GUI or host-tempo sync

    std::atomic<float> masterInputGainDb { 0.0f };
    std::atomic<float> masterOutputGainDb { 0.0f };
    std::atomic<bool> compressorEnabled { false };
    std::atomic<float> compressorThresholdDb { -12.0f };
    std::atomic<float> compressorRatio { 4.0f };
    std::atomic<float> compressorAttackMs { 10.0f };
    std::atomic<float> compressorReleaseMs { 100.0f };
    juce::dsp::Compressor<float> masterCompressor; // master-bus only, not per-voice - see the public accessors' comment

    std::atomic<bool> midiLearnArmed { false };
    std::atomic<int> midiLearnBank { -1 };
    std::atomic<int> midiLearnPad { -1 };

    // Guards against a host/controller re-sending note-on for a note that's
    // already sustaining (no matching note-off seen yet) - some DAWs do
    // this during piano-roll playback (e.g. around automation or loop
    // points), and without this, each resend retriggers the pad, sounding
    // like the note is firing repeatedly during what should be one
    // sustained hit. Audio-thread-only, no atomics needed - only ever
    // touched from inside processBlock's MIDI handling.
    std::array<bool, 128> heldMidiNotes {};

    double sampleRate = 44100.0;
    juce::dsp::ProcessSpec voiceSpec {};

    juce::AudioFormatManager formatManager;
    SendBuses sendBuses;

    // Direct-recording state. Start/stop touched from the message thread;
    // captureRecordingInput touched from the audio thread. recordingLock
    // guards the actual buffer/write-position access (both sides), same
    // SpinLock handoff pattern as Pad::clipLock.
    std::atomic<bool> recordingActive { false };
    std::atomic<int> recordBank { -1 };
    std::atomic<int> recordPad { -1 };
    std::atomic<int> recordWritePos { 0 };
    std::atomic<float> recordingPeakLevel { 0.0f };
    juce::AudioBuffer<float> recordingScratchBuffer; // pre-allocated in prepare(), fixed max length - no audio-thread allocation
    juce::SpinLock recordingLock;
    static constexpr double kMaxRecordingSeconds = 300.0; // 5 min - allocated lazily on first recording, not reserved permanently
    static constexpr double kMaxResampleSeconds = 300.0;  // 5 min safety cap for resamplePad, e.g. a looping source

    // Lock-free SPSC queue: GUI thread (producer) -> audio thread (consumer).
    struct PendingTrigger { int bankIndex; int padIndex; float velocity; bool noteOn; };
    juce::AbstractFifo triggerFifo { kTriggerQueueSize };
    std::array<PendingTrigger, kTriggerQueueSize> triggerQueue;
    void drainTriggerQueue(juce::MidiBuffer& outMidi);

    std::atomic<bool> killAllRequested { false };

    void reassignChromaticNotes();

    void chokeGroupIfNeeded(int bankIndex, int chokeGroup, int exceptVoiceIndex);
    void stopExistingVoicesForMonoRetrigger(int bankIndex, int padIndex, int exceptVoiceIndex);
    int findFreeVoice();
    void initializeVoiceForPlayback(Voice& v, Pad& pad, SampleClip::Ptr clip, float velocity01, float layerVolumeDb);

    // Shared by resamplePad()/addLayerFromPad() and extractRegionToPad()/
    // addLayerFromRegion() respectively - same rendering/extraction logic,
    // just handed back as a buffer instead of already being loaded
    // somewhere, so the caller decides whether it becomes a replacement
    // or an extra layer.
    bool renderPadToBuffer(int bankIndex, int padIndex, juce::AudioBuffer<float>& outBuffer);
    bool extractRegionBuffer(int bankIndex, int padIndex, int startSample, int endSample,
                              juce::AudioBuffer<float>& outBuffer, double& outSourceSampleRate);
    void startVoice(int bankIndex, int padIndex, float velocity01);
    void releasePad(int bankIndex, int padIndex);
    void renderVoice(Voice& v, Pad& pad, juce::AudioBuffer<float>& output, int numSamples,
                      bool anySoloActive, SendBuses& targetSendBuses);
};
