#include <algorithm>
#include <tuple>
#include <utility>
#include "SamplerEngine.h"

namespace
{
    juce::dsp::StateVariableTPTFilter<float>::Type toJuceFilterType(FilterType t)
    {
        switch (t)
        {
            case FilterType::lowPass:  return juce::dsp::StateVariableTPTFilter<float>::Type::lowpass;
            case FilterType::bandPass: return juce::dsp::StateVariableTPTFilter<float>::Type::bandpass;
            case FilterType::highPass: return juce::dsp::StateVariableTPTFilter<float>::Type::highpass;
        }
        return juce::dsp::StateVariableTPTFilter<float>::Type::lowpass;
    }

    constexpr float kChokeReleaseMs = 8.0f;   // fast fixed release used only for choke cutoffs
    constexpr float kParamSmoothMs = 20.0f;   // volume/pan/send ramp time
    constexpr float kFilterSmoothMs = 15.0f;  // filter cutoff/resonance ramp time (block-rate applied)
    constexpr double kMinGrainSizeSamples = 64.0;

    double grainSizeSamplesFor(float grainMs, double sampleRate)
    {
        const double clampedMs = juce::jlimit(10.0, 150.0, (double) grainMs);
        return juce::jmax(kMinGrainSizeSamples, sampleRate * clampedMs * 0.001);
    }

    // Linear-interpolated single-channel read, clamped to the buffer's
    // bounds (used by the granular pitch-shift grains, which can briefly
    // read slightly outside [0, sourceLength) near the very start/end of a
    // sample when the pitch ratio pushes a grain's internal read ahead of
    // or behind the source).
    float readInterpolatedClamped(const juce::AudioBuffer<float>& data, int channel, double pos, int sourceLength)
    {
        if (sourceLength <= 0)
            return 0.0f;
        const double clampedPos = juce::jlimit(0.0, (double) (sourceLength - 1), pos);
        const int idx0 = (int) clampedPos;
        const int idx1 = juce::jmin(idx0 + 1, sourceLength - 1);
        const float frac = (float) (clampedPos - (double) idx0);
        const float s0 = data.getSample(channel, idx0);
        const float s1 = data.getSample(channel, idx1);
        return s0 + frac * (s1 - s0);
    }

    // Hann window value for a grain of `size` samples at `age` samples in.
    float grainWindow(double age, double size)
    {
        if (size <= 0.0)
            return 0.0f;
        const double phase = juce::jlimit(0.0, 1.0, age / size);
        return (float) (0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * phase));
    }

    // Snapshot of every message-thread-owned Pad field, used to implement
    // copy/swap without needing Pad itself to be copyable (it holds atomics
    // and a spinlock-guarded clip, neither of which are copy-constructible).
    struct PadSnapshot
    {
        SampleClip::Ptr clip;
        juce::String displayName, sourceFilePath;
        std::vector<int> chopMarkers;
        int trimStart = 0, trimEnd = 0;
        float volumeDb = 0.0f, pitchSemis = 0.0f, pan = 0.0f, attackMs = 0.0f, releaseMs = 5.0f;
        FilterType filterType = FilterType::lowPass;
        float filterCutoffHz = 20000.0f, filterResonance = 0.7f;
        float reverbSend = 0.0f, delaySend = 0.0f;
        PlayMode playMode = PlayMode::oneShot;
        LoopMode loopMode = LoopMode::forward;
        bool reverse = false;
        int chokeGroup = 0;
    };

    PadSnapshot snapshotPad(const Pad& p)
    {
        PadSnapshot s;
        s.clip = p.getClipForAudioThread();
        s.displayName = p.displayName;
        s.sourceFilePath = p.sourceFilePath;
        s.chopMarkers = p.chopMarkers;
        s.trimStart = p.trimStart.load();
        s.trimEnd = p.trimEnd.load();
        s.volumeDb = p.volumeDb.load();
        s.pitchSemis = p.pitchSemis.load();
        s.pan = p.pan.load();
        s.attackMs = p.attackMs.load();
        s.releaseMs = p.releaseMs.load();
        s.filterType = p.filterType.load();
        s.filterCutoffHz = p.filterCutoffHz.load();
        s.filterResonance = p.filterResonance.load();
        s.reverbSend = p.reverbSend.load();
        s.delaySend = p.delaySend.load();
        s.playMode = p.playMode.load();
        s.loopMode = p.loopMode.load();
        s.reverse = p.reverse.load();
        s.chokeGroup = p.chokeGroup.load();
        return s;
    }

    void restorePad(Pad& p, const PadSnapshot& s)
    {
        p.setClip(s.clip);
        p.displayName = s.displayName;
        p.sourceFilePath = s.sourceFilePath;
        p.chopMarkers = s.chopMarkers;
        p.trimStart = s.trimStart;
        p.trimEnd = s.trimEnd;
        p.volumeDb = s.volumeDb;
        p.pitchSemis = s.pitchSemis;
        p.pan = s.pan;
        p.attackMs = s.attackMs;
        p.releaseMs = s.releaseMs;
        p.filterType = s.filterType;
        p.filterCutoffHz = s.filterCutoffHz;
        p.filterResonance = s.filterResonance;
        p.reverbSend = s.reverbSend;
        p.delaySend = s.delaySend;
        p.playMode = s.playMode;
        p.loopMode = s.loopMode;
        p.reverse = s.reverse;
        p.chokeGroup = s.chokeGroup;
    }
}

// ============================================================================
// SendBuses
// ============================================================================

void SendBuses::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    reverbBus.setSize((int) spec.numChannels, (int) spec.maximumBlockSize);
    delayBus.setSize((int) spec.numChannels, (int) spec.maximumBlockSize);

    reverb.prepare(spec);
    reverbParams.roomSize = 0.5f;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = 1.0f;
    reverbParams.dryLevel = 0.0f;
    reverbParams.width = 1.0f;
    reverb.setParameters(reverbParams);

    delayLine.prepare(spec);
    delayLine.setMaximumDelayInSamples(1 << 17);
    setDelayTimeSeconds(0.3f);
}

void SendBuses::reset()
{
    reverb.reset();
    delayLine.reset();
    reverbBus.clear();
    delayBus.clear();
}

void SendBuses::beginBlock(int numSamples)
{
    reverbBus.clear(0, numSamples);
    delayBus.clear(0, numSamples);
}

void SendBuses::addToReverb(int channel, int sampleIndex, float value)
{
    if (channel < reverbBus.getNumChannels())
        reverbBus.addSample(channel, sampleIndex, value);
}

void SendBuses::addToDelay(int channel, int sampleIndex, float value)
{
    if (channel < delayBus.getNumChannels())
        delayBus.addSample(channel, sampleIndex, value);
}

void SendBuses::processAndMixInto(juce::AudioBuffer<float>& output)
{
    const int numSamples = output.getNumSamples();
    const int numChannels = juce::jmin(output.getNumChannels(), reverbBus.getNumChannels());

    juce::dsp::AudioBlock<float> reverbBlock(reverbBus);
    auto reverbSub = reverbBlock.getSubBlock(0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> reverbCtx(reverbSub);
    reverb.process(reverbCtx);

    for (int ch = 0; ch < numChannels; ++ch)
        output.addFrom(ch, 0, reverbBus, ch, 0, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float in = delayBus.getSample(ch, i);
            const float delayed = delayLine.popSample(ch);
            delayLine.pushSample(ch, in + delayed * delayFeedback);
            output.addSample(ch, i, delayed);
        }
    }
}

void SendBuses::setReverbDecay(float decay01)
{
    reverbParams.roomSize = juce::jlimit(0.0f, 1.0f, decay01);
    reverb.setParameters(reverbParams);
}

void SendBuses::setDelayTimeSeconds(float seconds)
{
    const float samples = (float) juce::jlimit(0.001, 2.0, (double) seconds) * (float) sampleRate;
    delayLine.setDelay(samples);
}

void SendBuses::setDelayFeedback(float feedback01)
{
    delayFeedback = juce::jlimit(0.0f, 0.95f, feedback01);
}

// ============================================================================
// SamplerEngine
// ============================================================================

SamplerEngine::SamplerEngine()
{
    formatManager.registerBasicFormats();
}

void SamplerEngine::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    voiceSpec.sampleRate = newSampleRate;
    voiceSpec.maximumBlockSize = (juce::uint32) maxBlockSize;
    voiceSpec.numChannels = 2;

    for (auto& v : voices)
    {
        v.reset();
        v.filter.prepare(voiceSpec);
        v.smoothedVolumeGain.reset(newSampleRate, kParamSmoothMs * 0.001);
        v.smoothedPan.reset(newSampleRate, kParamSmoothMs * 0.001);
        v.smoothedCutoff.reset(newSampleRate, kFilterSmoothMs * 0.001);
        v.smoothedResonance.reset(newSampleRate, kFilterSmoothMs * 0.001);
        v.smoothedReverbSend.reset(newSampleRate, kParamSmoothMs * 0.001);
        v.smoothedDelaySend.reset(newSampleRate, kParamSmoothMs * 0.001);
    }

    sendBuses.prepare(voiceSpec);
    masterCompressor.prepare(voiceSpec);
    masterCompressor.reset();

    // The recording scratch buffer is now allocated lazily, in
    // startRecordingIntoPad() (also message-thread-only, so still never on
    // the audio thread) - at 5 minutes max, always pre-allocating it here
    // would reserve ~230MB per plugin instance whether or not you ever
    // actually record. Just drop any previous allocation and reset state;
    // the next recording (if any) sizes it on demand.
    {
        const juce::SpinLock::ScopedLockType sl(recordingLock);
        recordingScratchBuffer.setSize(0, 0);
    }
    recordWritePos = 0;
    recordingActive = false;
}

void SamplerEngine::releaseResources()
{
    for (auto& v : voices)
        v.reset();
    sendBuses.reset();

    // Safety net for heldMidiNotes (see its declaration comment): if a
    // host stops transport mid-note without sending a matching note-off,
    // a stale "held" entry would permanently block that note from ever
    // triggering again. releaseResources() runs whenever the host
    // stops/reconfigures playback, so this is a reliable place to clear
    // it regardless of whether the host behaved politely.
    heldMidiNotes.fill(false);
}

bool SamplerEngine::loadSampleIntoPad(int bankIndex, int padIndex, const juce::File& audioFile)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr)
        return false;

    auto newClip = new SampleClip();
    newClip->data.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&newClip->data, 0, (int) reader->lengthInSamples, 0, true, true);
    newClip->sourceSampleRate = reader->sampleRate;

    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    pad.setClip(newClip);

    pad.displayName = audioFile.getFileNameWithoutExtension();
    pad.sourceFilePath = audioFile.getFullPathName();
    pad.resetTrimToFull();
    pad.chopMarkers.clear();

    if (! pad.midiLearned.load())
        pad.midiNote = midiNoteOffset + bankIndex * kPadsPerBank + padIndex;

    return true;
}

bool SamplerEngine::addLayer(int bankIndex, int padIndex, const juce::File& audioFile, bool velocitySplit)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];

    ExtraLayer* target = nullptr;
    int loadedCount = 0; // main sample + already-loaded extra layers, before adding this one
    if (pad.isLoaded())
        ++loadedCount;
    for (auto& layer : pad.extraLayers)
    {
        if (layer.isLoaded())
            ++loadedCount;
        else if (target == nullptr)
            target = &layer;
    }
    if (target == nullptr)
        return false; // every extra-layer slot is already in use

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr)
        return false;

    auto newClip = new SampleClip();
    newClip->data.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&newClip->data, 0, (int) reader->lengthInSamples, 0, true, true);
    newClip->sourceSampleRate = reader->sampleRate;

    target->setClip(newClip);
    target->displayName = audioFile.getFileNameWithoutExtension();

    if (velocitySplit)
    {
        // Simple equal split across every now-loaded layer (main sample
        // counts as one), rather than a manual range-editing UI. This is
        // the only thing that ever changes an EXISTING layer's velocity
        // range automatically - always overwrites all of them to the new
        // even split.
        const int totalLayers = loadedCount + 1;
        const int span = 128 / totalLayers;
        int index = 0;
        if (pad.isLoaded())
        {
            pad.velocityLow = index * span;
            pad.velocityHigh = (index == totalLayers - 1) ? 127 : (index + 1) * span - 1;
            ++index;
        }
        for (auto& layer : pad.extraLayers)
        {
            if (! layer.isLoaded())
                continue;
            layer.velocityLow = index * span;
            layer.velocityHigh = (index == totalLayers - 1) ? 127 : (index + 1) * span - 1;
            ++index;
        }
    }
    else
    {
        target->velocityLow = 0;
        target->velocityHigh = 127; // full range - stacks with whatever else already matches
    }

    return true;
}

void SamplerEngine::clearExtraLayers(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    for (auto& layer : pad.extraLayers)
    {
        layer.setClip(nullptr);
        layer.displayName.clear();
        layer.velocityLow = 0;
        layer.velocityHigh = 127;
        layer.volumeDb = 0.0f;
    }
    pad.velocityLow = 0;
    pad.velocityHigh = 127; // restore the main sample to the full range too
}

void SamplerEngine::removeLayer(int bankIndex, int padIndex, int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= Pad::kMaxExtraLayers)
        return;

    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto& layer = pad.extraLayers[(size_t) layerIndex];
    layer.setClip(nullptr);
    layer.displayName.clear();
    layer.velocityLow = 0;
    layer.velocityHigh = 127;
    layer.volumeDb = 0.0f;
}

bool SamplerEngine::loadLayerSlot(int bankIndex, int padIndex, int layerIndex, const juce::File& audioFile)
{
    if (layerIndex < 0 || layerIndex >= Pad::kMaxExtraLayers)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr)
        return false;

    auto newClip = new SampleClip();
    newClip->data.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&newClip->data, 0, (int) reader->lengthInSamples, 0, true, true);
    newClip->sourceSampleRate = reader->sampleRate;

    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto& layer = pad.extraLayers[(size_t) layerIndex];
    layer.setClip(newClip);
    layer.displayName = audioFile.getFileNameWithoutExtension();
    // Velocity range deliberately untouched - see the declaration comment.
    return true;
}

bool SamplerEngine::loadSampleDataIntoPad(int bankIndex, int padIndex, juce::AudioBuffer<float>&& audioData,
                                           double sourceSampleRate, const juce::String& displayName)
{
    if (audioData.getNumChannels() <= 0 || audioData.getNumSamples() <= 0)
        return false;

    auto newClip = new SampleClip();
    newClip->data = std::move(audioData);
    newClip->sourceSampleRate = sourceSampleRate;

    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    pad.setClip(newClip);

    pad.displayName = displayName;
    pad.sourceFilePath.clear(); // embedded data has no current on-disk origin by default; caller may set it as informational metadata
    pad.resetTrimToFull();
    pad.chopMarkers.clear();

    if (! pad.midiLearned.load())
        pad.midiNote = midiNoteOffset + bankIndex * kPadsPerBank + padIndex;

    return true;
}

void SamplerEngine::startRecordingIntoPad(int bankIndex, int padIndex)
{
    const juce::SpinLock::ScopedLockType sl(recordingLock);

    // Allocate (only if not already the right size) here on the message
    // thread, before the audio thread can possibly see recordingActive
    // become true - never on the audio thread mid-recording.
    const int neededSamples = (int) (sampleRate * kMaxRecordingSeconds);
    if (recordingScratchBuffer.getNumSamples() < neededSamples || recordingScratchBuffer.getNumChannels() != 2)
    {
        recordingScratchBuffer.setSize(2, neededSamples, false, false, true);
        recordingScratchBuffer.clear();
    }

    recordBank = bankIndex;
    recordPad = padIndex;
    recordWritePos = 0;
    recordingPeakLevel = 0.0f;
    recordingActive = true; // set last, so the audio thread never sees a
                             // "recording" flag before bank/pad/writePos
                             // are actually valid
}

void SamplerEngine::captureRecordingInput(const juce::AudioBuffer<float>& input)
{
    if (! recordingActive.load())
        return;

    const float inputGain = juce::Decibels::decibelsToGain(masterInputGainDb.load());

    // Peak-with-decay level for the UI meter - doesn't need recordingLock,
    // it's an independent atomic that never touches the scratch buffer.
    // Measured AFTER input gain, so the meter reflects what's actually
    // about to be recorded, not the raw pre-gain signal.
    float blockPeak = 0.0f;
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        blockPeak = juce::jmax(blockPeak, input.getMagnitude(ch, 0, input.getNumSamples()) * inputGain);
    const float decayed = recordingPeakLevel.load() * 0.85f; // gives the meter a brief hold/fall rather than snapping to 0 between blocks
    recordingPeakLevel = juce::jmax(blockPeak, decayed);

    const juce::SpinLock::ScopedLockType sl(recordingLock);
    if (! recordingActive.load()) // re-check inside the lock: stopRecordingAndCommit may have raced us
        return;

    const int numSamples = input.getNumSamples();
    const int writePos = recordWritePos.load();
    const int maxLen = recordingScratchBuffer.getNumSamples();
    const int available = maxLen - writePos;

    if (available <= 0)
    {
        recordingActive = false; // scratch buffer full; auto-stop (whatever's captured can still be committed)
        return;
    }

    const int toCopy = juce::jmin(numSamples, available);
    const int inputChannels = input.getNumChannels();
    const int scratchChannels = recordingScratchBuffer.getNumChannels();
    const int matchedChannels = juce::jmin(inputChannels, scratchChannels);

    for (int ch = 0; ch < matchedChannels; ++ch)
        recordingScratchBuffer.copyFrom(ch, writePos, input.getReadPointer(ch), toCopy, inputGain);
    // Mono input (or fewer input channels than our stereo scratch buffer):
    // duplicate channel 0 into any remaining channels so a mono mic still
    // records into both, rather than leaving one silent.
    for (int ch = matchedChannels; ch < scratchChannels; ++ch)
        recordingScratchBuffer.copyFrom(ch, writePos, input.getReadPointer(0), toCopy, inputGain);

    recordWritePos = writePos + toCopy;

    if (toCopy < numSamples)
        recordingActive = false; // filled up mid-block; stop capturing further
}

void SamplerEngine::stopRecordingAndCommit()
{
    recordingActive = false; // stop the audio thread writing further before we touch the buffer
    recordingPeakLevel = 0.0f;

    int bank, pad, length;
    juce::AudioBuffer<float> captured;
    {
        const juce::SpinLock::ScopedLockType sl(recordingLock);
        bank = recordBank.load();
        pad = recordPad.load();
        length = recordWritePos.load();
        if (bank < 0 || pad < 0 || length <= 0)
        {
            recordBank = -1;
            recordPad = -1;
            recordWritePos = 0;
            return;
        }

        captured.setSize(recordingScratchBuffer.getNumChannels(), length);
        for (int ch = 0; ch < captured.getNumChannels(); ++ch)
            captured.copyFrom(ch, 0, recordingScratchBuffer, ch, 0, length);
    }

    loadSampleDataIntoPad(bank, pad, std::move(captured), sampleRate, "Recording");

    recordBank = -1;
    recordPad = -1;
    recordWritePos = 0;
}

void SamplerEngine::setMidiNoteOffset(int offset)
{
    midiNoteOffset = offset;
    reassignChromaticNotes();
}

void SamplerEngine::reassignChromaticNotes()
{
    for (int b = 0; b < kNumBanks; ++b)
        for (int p = 0; p < kPadsPerBank; ++p)
        {
            auto& pad = banks[(size_t) b][(size_t) p];
            if (! pad.midiLearned.load())
                pad.midiNote = midiNoteOffset + b * kPadsPerBank + p;
        }
}

bool SamplerEngine::noteToPad(int noteNumber, int& outBank, int& outPad) const
{
    for (int b = 0; b < kNumBanks; ++b)
        for (int p = 0; p < kPadsPerBank; ++p)
            if (banks[(size_t) b][(size_t) p].midiNote.load() == noteNumber)
            {
                outBank = b;
                outPad = p;
                return true;
            }
    return false;
}

void SamplerEngine::armMidiLearn(int bankIndex, int padIndex)
{
    midiLearnBank = bankIndex;
    midiLearnPad = padIndex;
    midiLearnArmed = true; // set last: audio thread only acts once this is true
}

void SamplerEngine::cancelMidiLearn()
{
    midiLearnArmed = false;
    midiLearnBank = -1;
    midiLearnPad = -1;
}

void SamplerEngine::requestTriggerFromUI(int bankIndex, int padIndex, float velocity01)
{
    const auto scope = triggerFifo.write(1);
    if (scope.blockSize1 > 0)
        triggerQueue[(size_t) scope.startIndex1] = { bankIndex, padIndex, velocity01, true };
    else if (scope.blockSize2 > 0)
        triggerQueue[(size_t) scope.startIndex2] = { bankIndex, padIndex, velocity01, true };
}

void SamplerEngine::requestReleaseFromUI(int bankIndex, int padIndex)
{
    const auto scope = triggerFifo.write(1);
    if (scope.blockSize1 > 0)
        triggerQueue[(size_t) scope.startIndex1] = { bankIndex, padIndex, 0.0f, false };
    else if (scope.blockSize2 > 0)
        triggerQueue[(size_t) scope.startIndex2] = { bankIndex, padIndex, 0.0f, false };
}

void SamplerEngine::requestKillAllFromUI()
{
    killAllRequested = true;
}

void SamplerEngine::drainTriggerQueue(juce::MidiBuffer& outMidi)
{
    const int numReady = triggerFifo.getNumReady();
    if (numReady <= 0)
        return;

    const auto scope = triggerFifo.read(numReady);

    auto handle = [this, &outMidi](int index)
    {
        const auto& req = triggerQueue[(size_t) index];
        auto& pad = banks[(size_t) req.bankIndex][(size_t) req.padIndex];
        const int note = juce::jlimit(0, 127, pad.midiNote.load());

        if (req.noteOn)
        {
            startVoice(req.bankIndex, req.padIndex, req.velocity);
            // Echo GUI-triggered notes out as real MIDI, so the host sees
            // an actual note event on the track and can record it -
            // previously, clicking a pad only produced sound, with
            // nothing appearing for the DAW to capture.
            const auto velocityByte = (juce::uint8) juce::jlimit(1, 127, (int) std::lround(req.velocity * 127.0f));
            outMidi.addEvent(juce::MidiMessage::noteOn(1, note, velocityByte), 0);
        }
        else
        {
            releasePad(req.bankIndex, req.padIndex);
            outMidi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        }
    };

    for (int i = 0; i < scope.blockSize1; ++i)
        handle(scope.startIndex1 + i);
    for (int i = 0; i < scope.blockSize2; ++i)
        handle(scope.startIndex2 + i);
}

int SamplerEngine::findFreeVoice()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[(size_t) i].active)
            return i;

    int quietest = 0;
    float quietestLevel = std::numeric_limits<float>::max();
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices[(size_t) i].envelopeLevel < quietestLevel)
        {
            quietestLevel = voices[(size_t) i].envelopeLevel;
            quietest = i;
        }
    }
    return quietest;
}

void SamplerEngine::chokeGroupIfNeeded(int bankIndex, int chokeGroup, int exceptVoiceIndex)
{
    if (chokeGroup == 0)
        return;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (i == exceptVoiceIndex)
            continue;

        auto& v = voices[(size_t) i];
        if (v.active && v.bankIndex == bankIndex && v.chokeGroup == chokeGroup)
        {
            // Fast fixed-time fade rather than an instant cut, to avoid a click.
            v.releasing = true;
            v.chokeFade = true;
        }
    }
}

void SamplerEngine::stopExistingVoicesForMonoRetrigger(int bankIndex, int padIndex, int exceptVoiceIndex)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (i == exceptVoiceIndex)
            continue;

        auto& v = voices[(size_t) i];
        if (v.active && v.bankIndex == bankIndex && v.padIndex == padIndex)
        {
            v.releasing = true;
            v.chokeFade = true; // reuse the same fast fixed release, avoids a click
        }
    }
}

void SamplerEngine::initializeVoiceForPlayback(Voice& v, Pad& pad, SampleClip::Ptr clip, float velocity01, float layerVolumeDb)
{
    v.reset();
    v.active = true;
    v.clip = clip;

    const int sourceLength = clip->data.getNumSamples();
    v.trimStart = juce::jlimit(0, sourceLength, pad.trimStart.load());
    const int storedTrimEnd = pad.trimEnd.load();
    v.trimEnd = storedTrimEnd > 0 ? juce::jlimit(v.trimStart, sourceLength, storedTrimEnd) : sourceLength;

    const bool reverse = pad.reverse.load();
    v.readPos = reverse ? (double) v.trimEnd : (double) v.trimStart;
    v.forwardDirection = ! reverse;

    // Grain start positions are anchored to the note's start; ages are
    // staggered by size/kNumGrains each so all 4 grains' Hann windows
    // overlap at a constant fixed phase spacing (the standard N-grain OLA
    // arrangement - see renderVoice's normalization comment for why 4
    // grains need dividing by 2 where 2 grains needed no scaling at all).
    const double grainSizeSamplesInit = grainSizeSamplesFor(pad.pitchGrainMs.load(), sampleRate);
    for (int g = 0; g < Voice::kNumGrains; ++g)
    {
        v.grainReadPos[g] = v.readPos;
        v.grainAgeSamples[g] = grainSizeSamplesInit * (double) g / (double) Voice::kNumGrains;
    }

    const float velSens = juce::jlimit(0.0f, 1.0f, pad.velocitySensitivity.load());
    const float clampedVelocity = juce::jlimit(0.0f, 1.0f, velocity01);
    v.velocityGain = 1.0f - velSens * (1.0f - clampedVelocity);

    const float attackMs = pad.attackMs.load();
    v.envelopeLevel = attackMs <= 0.0f ? 1.0f : 0.0f;

    const float offsetMs = juce::jmax(0.0f, pad.triggerOffsetMs.load());
    v.pendingDelaySamples = (int) (offsetMs * 0.001 * sampleRate);

    const double pitchRatio = std::pow(2.0, (double) pad.pitchSemis.load() / 12.0);
    v.playbackRatio = pitchRatio * (clip->sourceSampleRate / sampleRate);

    v.filter.setType(toJuceFilterType(pad.filterType.load()));
    const float cutoff = juce::jlimit(20.0f, 20000.0f, pad.filterCutoffHz.load());
    const float reso = juce::jlimit(0.1f, 10.0f, pad.filterResonance.load());
    v.filter.setCutoffFrequency(cutoff);
    v.filter.setResonance(reso);
    v.filter.reset();

    // layerVolumeDb, not pad.volumeDb directly - the MAIN sample's caller
    // passes pad.volumeDb.load() itself (so it behaves exactly as before),
    // but an extra layer passes ITS OWN volume instead, so stacked/layered
    // samples can be balanced against each other independently.
    const float volGain = juce::Decibels::decibelsToGain(layerVolumeDb);
    v.smoothedVolumeGain.setCurrentAndTargetValue(volGain);
    v.smoothedPan.setCurrentAndTargetValue(pad.pan.load());
    v.smoothedCutoff.setCurrentAndTargetValue(cutoff);
    v.smoothedResonance.setCurrentAndTargetValue(reso);
    v.smoothedReverbSend.setCurrentAndTargetValue(pad.reverbSend.load());
    v.smoothedDelaySend.setCurrentAndTargetValue(pad.delaySend.load());
}

void SamplerEngine::startVoice(int bankIndex, int padIndex, float velocity01)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];

    // Loop pads toggle on tap (tap to start the loop, tap again to stop
    // it) rather than sustaining while held. If
    // this pad is already looping, this trigger stops it instead of
    // layering a second overlapping copy.
    if (pad.playMode.load() == PlayMode::loop)
    {
        bool stoppedExisting = false;
        for (auto& v : voices)
        {
            if (v.active && v.bankIndex == bankIndex && v.padIndex == padIndex && ! v.releasing)
            {
                v.releasing = true;
                stoppedExisting = true;
            }
        }
        if (stoppedExisting)
            return;
    }

    // Collect every layer (the main sample + any extra velocity layers)
    // whose velocity range contains this hit. Almost always exactly one -
    // the ordinary single-sample-per-pad case, completely unaffected by any
    // of this. Can be more than one if layers' ranges deliberately overlap
    // (layering/stacking multiple sounds on one hit), or exactly one of
    // several if ranges are split (velocity switching between samples).
    const int velocity127 = juce::jlimit(0, 127, (int) std::lround(velocity01 * 127.0f));
    struct MatchedLayer { SampleClip::Ptr clip; int layerIndex; }; // layerIndex: -1 = main sample, 0..2 = extraLayers index
    juce::Array<MatchedLayer> matchedLayers;

    if (velocity127 >= pad.velocityLow.load() && velocity127 <= pad.velocityHigh.load())
    {
        auto mainClip = pad.getClipForAudioThread();
        if (mainClip != nullptr && mainClip->data.getNumSamples() > 0)
            matchedLayers.add({ mainClip, -1 });
    }
    for (int i = 0; i < Pad::kMaxExtraLayers; ++i)
    {
        auto& layer = pad.extraLayers[(size_t) i];
        if (velocity127 < layer.velocityLow.load() || velocity127 > layer.velocityHigh.load())
            continue;
        auto layerClip = layer.getClipForAudioThread();
        if (layerClip != nullptr && layerClip->data.getNumSamples() > 0)
            matchedLayers.add({ layerClip, i });
    }

    if (matchedLayers.isEmpty())
        return;

    // Choke/mono-retrigger happens ONCE per trigger event, before any of
    // this hit's own voices exist - not once per matched layer, which
    // would otherwise have layer 2 immediately choke/stop layer 1's voice
    // from the very same hit.
    const int chokeGroup = pad.chokeGroup.load();
    chokeGroupIfNeeded(bankIndex, chokeGroup, -1);
    if (! pad.polyMode.load())
        stopExistingVoicesForMonoRetrigger(bankIndex, padIndex, -1);

    for (auto& matched : matchedLayers)
    {
        const int voiceIndex = findFreeVoice();
        auto& v = voices[(size_t) voiceIndex];
        const float layerVolumeDb = (matched.layerIndex < 0) ? pad.volumeDb.load()
                                                              : pad.extraLayers[(size_t) matched.layerIndex].volumeDb.load();
        initializeVoiceForPlayback(v, pad, matched.clip, velocity01, layerVolumeDb);
        v.bankIndex = bankIndex;
        v.padIndex = padIndex;
        v.chokeGroup = chokeGroup;
        v.layerIndex = matched.layerIndex;
        v.active = true; // re-assert: the passes above only ever soft-release *other* voices
    }
}

void SamplerEngine::releasePad(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];

    // Only "hold" mode sustains-while-held and stops on release. One-shot ignores release entirely. Loop does NOT stop here -
    // it toggles on/off from a fresh trigger instead (see startVoice); a
    // mouse click's down+up fires almost instantly, so treating a plain
    // release as a stop-loop signal was killing loop playback a few
    // milliseconds after it started - loop pads never actually looped.
    if (pad.playMode.load() != PlayMode::hold)
        return;

    for (auto& v : voices)
        if (v.active && v.bankIndex == bankIndex && v.padIndex == padIndex && ! v.chokeFade)
            v.releasing = true;
}

void SamplerEngine::renderVoice(Voice& v, Pad& pad, juce::AudioBuffer<float>& output, int numSamples,
                                 bool anySoloActive, SendBuses& targetSendBuses)
{
    const int outChannels = output.getNumChannels();
    const int sourceChannels = v.clip->data.getNumChannels();
    const int sourceLength = v.clip->data.getNumSamples();
    if (sourceLength <= 0)
    {
        v.active = false;
        return;
    }

    const int trimStart = v.trimStart;
    const int trimEnd = v.trimEnd;
    const PlayMode playMode = pad.playMode.load();
    const LoopMode loopMode = pad.loopMode.load();

    const float releaseMsValue = v.chokeFade ? kChokeReleaseMs : pad.releaseMs.load();
    const float releaseDec = releaseMsValue > 0.0f
        ? (float) (1.0 / (releaseMsValue * 0.001 * sampleRate))
        : 1.0f;
    const float attackMs = pad.attackMs.load();
    const float attackInc = attackMs > 0.0f
        ? (float) (1.0 / (attackMs * 0.001 * sampleRate))
        : 1.0f;

    const PitchMode pitchMode = pad.pitchMode.load();
    const double grainSize = grainSizeSamplesFor(pad.pitchGrainMs.load(), sampleRate);
    // Hann-window overlap-add math: for M equally-spaced grains per period,
    // the windows sum to a constant M/2 (provable from the window's 3-term
    // Fourier series - only the DC term survives the M-way phase average).
    // M=2 (the original version) sums to exactly 1.0, needing no scaling;
    // M=4 sums to 2.0, so the combined output must be halved to keep the
    // same gain staging. This is exact, not a fudge factor.
    const float grainNormalize = 2.0f / (float) Voice::kNumGrains;

    // BPM time-stretch: reuses the exact same granular engine as
    // preserveLength pitch shift, just with the roles of the two rates
    // swapped. Pitch shift alone: grain-internal rate = pitchRatio (shifts
    // pitch), master rate = 1.0 (duration unchanged). Time-stretch alone:
    // grain-internal rate = pitchRatio (usually 1.0, so pitch is
    // untouched), master rate = stretchRatio (duration changes to match
    // project tempo). Both together: grain-internal rate still carries the
    // pitch shift, master rate still carries the stretch - they're
    // independent, so you can pitch-shift AND tempo-match the same pad.
    const int beatsInSample = pad.beatsInSample.load();
    double stretchRatio = 1.0;
    if (beatsInSample > 0 && v.clip != nullptr)
    {
        const int span = trimEnd - trimStart;
        const double naturalDurationSeconds = (double) span / v.clip->sourceSampleRate;
        if (naturalDurationSeconds > 0.0001)
        {
            const double nativeBpm = (double) beatsInSample * 60.0 / naturalDurationSeconds;
            const double targetBpm = juce::jmax(1.0, (double) projectBpm.load());
            // masterStep must be SMALLER than 1 to slow down (longer
            // duration) when the sample's native tempo is faster than the
            // target, and LARGER than 1 to speed up when native is slower
            // than target - i.e. targetBpm/nativeBpm, not nativeBpm/
            // targetBpm (that was inverted - it sped up when you asked to
            // slow down, and vice versa). Also folds in the source/output
            // sample-rate ratio, same as playbackRatio does in the plain
            // resample path, so this stays correct even when a sample's
            // native rate differs from the engine's.
            stretchRatio = (v.clip->sourceSampleRate / sampleRate) * (targetBpm / nativeBpm);
        }
    }
    const bool useGranular = (pitchMode == PitchMode::preserveLength) || (beatsInSample > 0);

    // Lo-fi character (classic hardware-sampler 12-bit crunch). Read once per
    // block like the other "set and forget" character controls (pitchMode,
    // grainSize) rather than per-sample - these aren't meant to be
    // automated in real time.
    const int lofiBits = juce::jlimit(4, 16, pad.bitDepth.load());
    const int lofiRateReduction = juce::jmax(1, pad.sampleRateReduction.load());
    const float lofiLevels = lofiBits < 16 ? std::pow(2.0f, (float) (lofiBits - 1)) : 0.0f;

    // Distortion: same block-rate-read treatment. tanh(x) is NOT close to
    // identity for normal signal levels (tanh(0.9) ~= 0.716), so this must
    // be explicitly bypassed at drive=0 rather than relying on driveGain=1
    // to be a no-op - every pad defaults to drive=0, and silently coloring
    // all of them would be a real regression, not a subtle one.
    const float distortionDrive = juce::jlimit(0.0f, 1.0f, pad.distortionDrive.load());
    const bool distortionActive = distortionDrive > 0.0f;
    const float driveGain = 1.0f + distortionDrive * 19.0f; // 1x .. 20x pre-gain into tanh

    // Mute/solo gate, checked live every block (not just at trigger time)
    // so muting/soloing an already-playing pad takes effect immediately.
    const float muteGain = (pad.muted.load() || (anySoloActive && ! pad.solo.load())) ? 0.0f : 1.0f;

    // Update smoothed targets once per block; filter coefficients and pan
    // are re-applied once per block too (cheaper than per-sample trig/coeff
    // recalculation for every active voice, and a block is short enough
    // that this doesn't introduce audible stepping).
    const float liveVolumeDb = (v.layerIndex < 0) ? pad.volumeDb.load()
                                                   : pad.extraLayers[(size_t) v.layerIndex].volumeDb.load();
    v.smoothedVolumeGain.setTargetValue(juce::Decibels::decibelsToGain(liveVolumeDb));
    v.smoothedReverbSend.setTargetValue(pad.reverbSend.load());
    v.smoothedDelaySend.setTargetValue(pad.delaySend.load());

    const float newCutoff = juce::jlimit(20.0f, 20000.0f, pad.filterCutoffHz.load());
    const float newReso = juce::jlimit(0.1f, 10.0f, pad.filterResonance.load());
    v.smoothedCutoff.setTargetValue(newCutoff);
    v.smoothedResonance.setTargetValue(newReso);
    v.filter.setType(toJuceFilterType(pad.filterType.load()));
    v.filter.setCutoffFrequency(v.smoothedCutoff.getNextValue());
    v.filter.setResonance(v.smoothedResonance.getNextValue());

    v.smoothedPan.setTargetValue(pad.pan.load());
    const float pan = v.smoothedPan.getNextValue();
    const float panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
    const float leftGain = std::cos(panAngle);
    const float rightGain = std::sin(panAngle);

    if (numSamples > 1)
    {
        v.smoothedCutoff.skip(numSamples - 1);
        v.smoothedResonance.skip(numSamples - 1);
        v.smoothedPan.skip(numSamples - 1);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (! v.active)
            break;

        if (v.pendingDelaySamples > 0)
        {
            --v.pendingDelaySamples;
            continue; // silent until the trigger offset elapses
        }

        if (v.releasing)
        {
            v.envelopeLevel -= releaseDec;
            if (v.envelopeLevel <= 0.0f)
            {
                v.active = false;
                break;
            }
        }
        else if (v.envelopeLevel < 1.0f)
        {
            v.envelopeLevel = juce::jmin(1.0f, v.envelopeLevel + attackInc);
        }

        // preserveLength: advance/re-anchor each grain BEFORE reading, so a
        // grain whose age just wrapped past grainSize reads from its fresh
        // anchor this same sample rather than one sample stale.
        float grainWin[Voice::kNumGrains] = {};
        if (useGranular)
        {
            for (int g = 0; g < Voice::kNumGrains; ++g)
            {
                if (v.grainAgeSamples[g] >= grainSize)
                {
                    v.grainAgeSamples[g] -= grainSize; // keep any overshoot for even spacing
                    v.grainReadPos[g] = v.readPos;      // re-anchor to the real-time master position
                }
                grainWin[g] = grainWindow(v.grainAgeSamples[g], grainSize);
            }
        }

        const float volGain = v.smoothedVolumeGain.getNextValue();
        const float reverbSendAmt = v.smoothedReverbSend.getNextValue();
        const float delaySendAmt = v.smoothedDelaySend.getNextValue();

        const float gain = v.envelopeLevel * v.velocityGain * volGain * muteGain;

        // Sample-rate reduction: decide ONCE per sample (not per channel,
        // so both channels hold/refresh in lockstep) whether this is a
        // "capture a fresh value" sample or a "repeat the held value"
        // sample.
        const bool lofiCapture = (lofiRateReduction <= 1) || (v.lofiCounter % lofiRateReduction == 0);
        ++v.lofiCounter;

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const int srcCh = juce::jmin(ch, sourceChannels - 1);
            float raw;

            if (useGranular)
            {
                float summed = 0.0f;
                for (int g = 0; g < Voice::kNumGrains; ++g)
                    summed += readInterpolatedClamped(v.clip->data, srcCh, v.grainReadPos[g], sourceLength) * grainWin[g];
                raw = summed * grainNormalize;
            }
            else
            {
                const int idx0 = juce::jlimit(0, sourceLength - 1, (int) v.readPos);
                const int idx1 = juce::jmin(idx0 + 1, sourceLength - 1);
                const float frac = (float) (v.readPos - std::floor(v.readPos));
                const float s0 = v.clip->data.getSample(srcCh, idx0);
                const float s1 = v.clip->data.getSample(srcCh, idx1);
                raw = s0 + frac * (s1 - s0);
            }

            if (lofiRateReduction > 1)
            {
                if (lofiCapture)
                    v.lofiHold[ch] = raw;
                raw = v.lofiHold[ch];
            }
            if (lofiBits < 16)
                raw = std::round(raw * lofiLevels) / lofiLevels;
            if (distortionActive)
                raw = std::tanh(raw * driveGain);

            const float filtered = v.filter.processSample(ch, raw);
            const float panGain = (outChannels >= 2) ? (ch == 0 ? leftGain : rightGain) : 1.0f;
            const float outVal = filtered * gain * panGain;

            output.addSample(ch, i, outVal);
            targetSendBuses.addToReverb(ch, i, outVal * reverbSendAmt);
            targetSendBuses.addToDelay(ch, i, outVal * delaySendAmt);
        }

        // Master position governs duration/loop bookkeeping. Real-time (1x)
        // for pure pitch shift; scaled by stretchRatio when BPM time-stretch
        // is active (both can apply at once - stretchRatio defaults to 1.0
        // when beatsInSample is 0, so it's a no-op unless actually used).
        // Falls back to the original pitch-scaled rate when granular isn't
        // engaged at all (the simple resample path, unchanged from before).
        const double masterStep = useGranular ? stretchRatio : v.playbackRatio;
        if (v.forwardDirection)
            v.readPos += masterStep;
        else
            v.readPos -= masterStep;

        if (useGranular)
        {
            const double grainStep = v.playbackRatio * (v.forwardDirection ? 1.0 : -1.0);
            for (int g = 0; g < Voice::kNumGrains; ++g)
            {
                v.grainReadPos[g] += grainStep;
                v.grainAgeSamples[g] += 1.0;
            }
        }

        if (v.forwardDirection && v.readPos >= (double) trimEnd)
        {
            if (playMode == PlayMode::loop)
            {
                if (loopMode == LoopMode::pingPong)
                {
                    v.forwardDirection = false;
                    v.readPos = (double) trimEnd - (v.readPos - (double) trimEnd);
                }
                else
                {
                    v.readPos = (double) trimStart + (v.readPos - (double) trimEnd);
                }
                for (int g = 0; g < Voice::kNumGrains; ++g) v.grainReadPos[g] = v.readPos; // re-anchor grains at the loop point
            }
            else
            {
                v.active = false;
                break;
            }
        }
        else if (! v.forwardDirection && v.readPos <= (double) trimStart)
        {
            if (playMode == PlayMode::loop && loopMode == LoopMode::pingPong)
            {
                v.forwardDirection = true;
                v.readPos = (double) trimStart + ((double) trimStart - v.readPos);
                for (int g = 0; g < Voice::kNumGrains; ++g) v.grainReadPos[g] = v.readPos;
            }
            else
            {
                v.active = false;
                break;
            }
        }
    }
}

void SamplerEngine::processBlock(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi)
{
    if (killAllRequested.exchange(false))
    {
        for (auto& v : voices)
            if (v.active)
            {
                v.releasing = true;
                v.chokeFade = true; // fast fixed release, same as choke - avoids a hard-cut click
            }
    }

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            // Wasn't handled at all before - a host/controller sending
            // this got silently ignored. Also doubles as another safety
            // net for heldMidiNotes getting stuck (see its declaration
            // comment): a host that stops transport mid-note often sends
            // this rather than individual note-offs.
            heldMidiNotes.fill(false);
            for (auto& v : voices)
                if (v.active)
                {
                    v.releasing = true;
                    v.chokeFade = true;
                }
            continue;
        }

        if (msg.isNoteOn())
        {
            if (midiLearnArmed.load())
            {
                const int lb = midiLearnBank.load();
                const int lp = midiLearnPad.load();
                auto& pad = banks[(size_t) lb][(size_t) lp];
                pad.midiNote = msg.getNoteNumber();
                pad.midiLearned = true;
                const int learnedNote = msg.getNoteNumber();
                cancelMidiLearn();

                if (onMidiLearned)
                    onMidiLearned(lb, lp, learnedNote);
            }
            else
            {
                const int note = msg.getNoteNumber();
                if (heldMidiNotes[(size_t) note])
                {
                    // Already sustaining - a duplicate/resent note-on for a
                    // note we haven't seen the matching note-off for yet.
                    // Ignore it rather than retriggering (see the comment
                    // on heldMidiNotes for why this exists).
                }
                else
                {
                    heldMidiNotes[(size_t) note] = true;
                    int bankIdx, padIdx;
                    if (noteToPad(note, bankIdx, padIdx))
                        startVoice(bankIdx, padIdx, msg.getFloatVelocity());
                }
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            heldMidiNotes[(size_t) note] = false;
            int bankIdx, padIdx;
            if (noteToPad(note, bankIdx, padIdx))
                releasePad(bankIdx, padIdx);
        }
    }

    // Drains GUI-triggered pad clicks AFTER the real-MIDI loop above, not
    // before - it adds echo note-on/off messages to `midi` itself (so the
    // host sees a click as a real note and can record it), and those must
    // never be re-read by the loop that just finished, or every GUI click
    // would double-trigger itself.
    drainTriggerQueue(midi);

    const int numSamples = output.getNumSamples();
    sendBuses.beginBlock(numSamples);

    const bool anySoloActive = isAnySoloActive(); // computed once per block, not per voice

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        auto& pad = banks[(size_t) v.bankIndex][(size_t) v.padIndex];
        renderVoice(v, pad, output, numSamples, anySoloActive, sendBuses);
    }

    sendBuses.processAndMixInto(output);

    // Master bus: compressor (if enabled) then output gain - both apply to
    // the fully mixed signal, after every voice and the shared reverb/
    // delay buses are already summed in. Parameters are read once per
    // block (control-rate), same treatment as every other "set and
    // forget" character control in this engine (filter cutoff, lofi, etc).
    if (compressorEnabled.load())
    {
        masterCompressor.setThreshold(compressorThresholdDb.load());
        masterCompressor.setRatio(juce::jmax(1.0f, compressorRatio.load()));
        masterCompressor.setAttack(juce::jmax(0.1f, compressorAttackMs.load()));
        masterCompressor.setRelease(juce::jmax(1.0f, compressorReleaseMs.load()));

        juce::dsp::AudioBlock<float> block(output);
        juce::dsp::ProcessContextReplacing<float> context(block);
        masterCompressor.process(context);
    }

    output.applyGain(juce::Decibels::decibelsToGain(masterOutputGainDb.load()));
}

bool SamplerEngine::isAnySoloActive() const
{
    for (int b = 0; b < kNumBanks; ++b)
        for (int p = 0; p < kPadsPerBank; ++p)
            if (banks[(size_t) b][(size_t) p].solo.load())
                return true;
    return false;
}

// ----------------------------------------------------------------------------
// Chop
// ----------------------------------------------------------------------------

void SamplerEngine::clearChopMarkers(int bankIndex, int padIndex)
{
    banks[(size_t) bankIndex][(size_t) padIndex].chopMarkers.clear();
}

void SamplerEngine::generateEqualChops(int bankIndex, int padIndex, int numChops)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    pad.chopMarkers.clear();

    auto clip = pad.getClipForAudioThread();
    if (numChops < 2 || clip == nullptr)
        return;

    const int start = pad.trimStart.load();
    const int storedEnd = pad.trimEnd.load();
    const int end = storedEnd > 0 ? storedEnd : clip->data.getNumSamples();
    const int span = end - start;
    if (span <= 0)
        return;

    for (int i = 1; i < numChops; ++i)
        pad.chopMarkers.push_back(start + (int) ((double) span * i / numChops));
}

void SamplerEngine::generateTransientChops(int bankIndex, int padIndex, float sensitivity01)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    pad.chopMarkers.clear();

    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr)
        return;

    const int start = pad.trimStart.load();
    const int storedEnd = pad.trimEnd.load();
    const int end = storedEnd > 0 ? storedEnd : clip->data.getNumSamples();
    if (end - start < 512)
        return;

    const int windowSize = juce::jmax(64, (int) (sampleRate * 0.01));
    const int minGapSamples = (int) (sampleRate * 0.06);
    const float threshold = juce::jmap(sensitivity01, 0.0f, 1.0f, 3.0f, 1.3f);

    std::vector<float> windowRms;
    for (int pos = start; pos < end; pos += windowSize)
    {
        const int len = juce::jmin(windowSize, end - pos);
        float sumSq = 0.0f;
        for (int ch = 0; ch < clip->data.getNumChannels(); ++ch)
        {
            const float* data = clip->data.getReadPointer(ch, pos);
            for (int i = 0; i < len; ++i)
                sumSq += data[i] * data[i];
        }
        windowRms.push_back(std::sqrt(sumSq / (float) juce::jmax(1, len * clip->data.getNumChannels())));
    }

    float runningAvg = windowRms.empty() ? 0.0f : windowRms.front();
    int lastMarkerPos = start - minGapSamples;

    for (size_t w = 0; w < windowRms.size(); ++w)
    {
        const int windowStartPos = start + (int) w * windowSize;
        const float level = windowRms[w];

        if (runningAvg > 1.0e-6f && level > runningAvg * threshold
            && (windowStartPos - lastMarkerPos) >= minGapSamples
            && windowStartPos > start)
        {
            pad.chopMarkers.push_back(windowStartPos);
            lastMarkerPos = windowStartPos;
        }

        runningAvg = runningAvg * 0.9f + level * 0.1f;
    }
}

void SamplerEngine::addLazyMarker(int bankIndex, int padIndex, int samplePosition)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = pad.getClipForAudioThread();
    const int start = pad.trimStart.load();
    const int storedEnd = pad.trimEnd.load();
    const int end = storedEnd > 0 ? storedEnd : (clip != nullptr ? clip->data.getNumSamples() : 0);
    const int clamped = juce::jlimit(start, end, samplePosition);

    if (std::find(pad.chopMarkers.begin(), pad.chopMarkers.end(), clamped) != pad.chopMarkers.end())
        return;

    pad.chopMarkers.push_back(clamped);
    std::sort(pad.chopMarkers.begin(), pad.chopMarkers.end());
}

int SamplerEngine::commitChopsToPads(int sourceBankIndex, int sourcePadIndex,
                                      bool keepOriginal, int choppedChokeGroup,
                                      bool forceOneShot, bool playThru)
{
    auto& sourceBank = banks[(size_t) sourceBankIndex];
    Pad& source = sourceBank[(size_t) sourcePadIndex];
    auto sourceClip = source.getClipForAudioThread();
    if (sourceClip == nullptr)
        return 0;

    std::vector<int> bounds;
    bounds.push_back(source.trimStart.load());
    const int storedEnd = source.trimEnd.load();
    const int trimEnd = storedEnd > 0 ? storedEnd : sourceClip->data.getNumSamples();
    for (int m : source.chopMarkers)
        if (m > source.trimStart.load() && m < trimEnd)
            bounds.push_back(m);
    bounds.push_back(trimEnd);

    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    if (bounds.size() < 2)
        return 0;

    // Free pad slots across ALL banks, not just the source bank: a sample
    // chopped into more than 16 pieces needs pads from bank B/C/D too, or
    // pieces past the first 16 are silently lost. Search starting at the
    // source bank and wrap forward through the others.
    std::vector<std::pair<int, int>> freeSlots;
    for (int k = 0; k < kNumBanks; ++k)
    {
        const int b = (sourceBankIndex + k) % kNumBanks;
        for (int p = 0; p < kPadsPerBank; ++p)
        {
            if (b == sourceBankIndex && p == sourcePadIndex)
                continue;
            if (! banks[(size_t) b][(size_t) p].isLoaded())
                freeSlots.push_back({ b, p });
        }
    }

    const int originalEnd = sourceClip->data.getNumSamples();
    const juce::String baseName = source.displayName;

    int chopsCreated = 0;
    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        const int regionStart = bounds[i];
        const int regionEnd = playThru ? originalEnd : bounds[i + 1];
        if (regionEnd <= regionStart)
            continue;

        int destBank, destPad;
        if (i == 0 && ! keepOriginal)
        {
            destBank = sourceBankIndex;
            destPad = sourcePadIndex;
        }
        else
        {
            if (freeSlots.empty())
                break; // all 64 pads full
            std::tie(destBank, destPad) = freeSlots.front();
            freeSlots.erase(freeSlots.begin());
        }

        Pad& dest = banks[(size_t) destBank][(size_t) destPad];
        const int numChannels = sourceClip->data.getNumChannels();
        const int length = regionEnd - regionStart;

        auto newClip = new SampleClip();
        newClip->data.setSize(numChannels, length);
        for (int ch = 0; ch < numChannels; ++ch)
            newClip->data.copyFrom(ch, 0, sourceClip->data, ch, regionStart, length);
        newClip->sourceSampleRate = sourceClip->sourceSampleRate;

        dest.setClip(newClip);
        dest.displayName = baseName + "_chop" + juce::String(chopsCreated + 1);
        dest.sourceFilePath.clear();
        dest.resetTrimToFull();
        dest.chopMarkers.clear();
        dest.chokeGroup = choppedChokeGroup;
        if (forceOneShot)
            dest.playMode = PlayMode::oneShot;
        if (! dest.midiLearned.load())
            dest.midiNote = midiNoteOffset + destBank * kPadsPerBank + destPad;

        ++chopsCreated;
    }

    return chopsCreated;
}

// ----------------------------------------------------------------------------
// Pad tools (message thread only)
// ----------------------------------------------------------------------------

void SamplerEngine::clearPad(int bankIndex, int padIndex)
{
    banks[(size_t) bankIndex][(size_t) padIndex].clear();
}

void SamplerEngine::copyPad(int fromBank, int fromPad, int toBank, int toPad)
{
    auto& src = banks[(size_t) fromBank][(size_t) fromPad];
    auto& dst = banks[(size_t) toBank][(size_t) toPad];
    auto snap = snapshotPad(src);
    restorePad(dst, snap);
}

void SamplerEngine::swapPads(int bankA, int padA, int bankB, int padB)
{
    auto& a = banks[(size_t) bankA][(size_t) padA];
    auto& b = banks[(size_t) bankB][(size_t) padB];
    auto snapA = snapshotPad(a);
    auto snapB = snapshotPad(b);
    restorePad(a, snapB);
    restorePad(b, snapA);
}

void SamplerEngine::normalizePad(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
        return;

    float peak = 0.0f;
    for (int ch = 0; ch < clip->data.getNumChannels(); ++ch)
        peak = juce::jmax(peak, clip->data.getMagnitude(ch, 0, clip->data.getNumSamples()));

    if (peak <= 1.0e-6f)
        return;

    const float targetPeak = juce::Decibels::decibelsToGain(-0.3f);
    const float scale = targetPeak / peak;

    auto newClip = new SampleClip();
    newClip->data.makeCopyOf(clip->data);
    newClip->sourceSampleRate = clip->sourceSampleRate;
    newClip->data.applyGain(scale);

    pad.setClip(newClip);
}

void SamplerEngine::trimSilence(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr)
        return;

    const int numSamples = clip->data.getNumSamples();
    const float threshold = juce::Decibels::decibelsToGain(-50.0f);

    int firstAboveThreshold = 0;
    for (; firstAboveThreshold < numSamples; ++firstAboveThreshold)
    {
        bool loud = false;
        for (int ch = 0; ch < clip->data.getNumChannels() && ! loud; ++ch)
            if (std::abs(clip->data.getSample(ch, firstAboveThreshold)) > threshold)
                loud = true;
        if (loud)
            break;
    }

    int lastAboveThreshold = numSamples - 1;
    for (; lastAboveThreshold > firstAboveThreshold; --lastAboveThreshold)
    {
        bool loud = false;
        for (int ch = 0; ch < clip->data.getNumChannels() && ! loud; ++ch)
            if (std::abs(clip->data.getSample(ch, lastAboveThreshold)) > threshold)
                loud = true;
        if (loud)
            break;
    }

    pad.trimStart = firstAboveThreshold;
    pad.trimEnd = juce::jmax(firstAboveThreshold + 1, lastAboveThreshold + 1);
}

void SamplerEngine::trimPadToSelection(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr)
        return;

    const int numSamples = clip->data.getNumSamples();
    const int start = juce::jlimit(0, numSamples, pad.trimStart.load());
    const int storedEnd = pad.trimEnd.load();
    const int end = storedEnd > 0 ? juce::jlimit(start, numSamples, storedEnd) : numSamples;

    if (end <= start || (start == 0 && end == numSamples))
        return; // nothing outside the current trim to discard

    auto newClip = new SampleClip();
    const int numChannels = clip->data.getNumChannels();
    newClip->data.setSize(numChannels, end - start);
    for (int ch = 0; ch < numChannels; ++ch)
        newClip->data.copyFrom(ch, 0, clip->data, ch, start, end - start);
    newClip->sourceSampleRate = clip->sourceSampleRate;

    pad.setClip(newClip);
    pad.resetTrimToFull();
    pad.chopMarkers.clear(); // marker positions were relative to the old, longer buffer
}

bool SamplerEngine::exportPadToFile(int bankIndex, int padIndex, const juce::File& destFile) const
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = pad.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
        return false;

    const int numSamples = clip->data.getNumSamples();
    const int start = juce::jlimit(0, numSamples, pad.trimStart.load());
    const int storedEnd = pad.trimEnd.load();
    const int end = storedEnd > 0 ? juce::jlimit(start, numSamples, storedEnd) : numSamples;
    if (end <= start)
        return false;

    destFile.deleteFile(); // overwrite cleanly if it already exists

    std::unique_ptr<juce::FileOutputStream> outStream(destFile.createOutputStream());
    if (outStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;
    // createWriterFor takes ownership of the stream ONLY on success (a
    // documented JUCE contract, easy to get backwards) - so outStream must
    // stay a unique_ptr (deleting it itself on the failure path) right up
    // until we've confirmed a writer was actually created, and only then
    // release() it to the writer.
    auto* rawWriter = wavFormat.createWriterFor(outStream.get(), clip->sourceSampleRate,
                                                 (unsigned int) clip->data.getNumChannels(), 32, {}, 0);
    if (rawWriter == nullptr)
        return false;

    outStream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(rawWriter);
    return writer->writeFromAudioSampleBuffer(clip->data, start, end - start);
}

void SamplerEngine::resetPadEffects(int bankIndex, int padIndex)
{
    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    if (! pad.isLoaded())
        return;

    pad.volumeDb = 0.0f;
    pad.pitchSemis = 0.0f;
    pad.pitchMode = PitchMode::resample;
    pad.pitchGrainMs = 50.0f;
    pad.pan = 0.0f;
    pad.attackMs = 0.0f;
    pad.releaseMs = 5.0f;
    pad.filterType = FilterType::lowPass;
    pad.filterCutoffHz = 20000.0f;
    pad.filterResonance = 0.7f;
    pad.reverbSend = 0.0f;
    pad.delaySend = 0.0f;
    pad.beatsInSample = 0;
    pad.bitDepth = 16;
    pad.sampleRateReduction = 1;
    pad.distortionDrive = 0.0f;
}

bool SamplerEngine::extractRegionBuffer(int bankIndex, int padIndex, int startSample, int endSample,
                                         juce::AudioBuffer<float>& outBuffer, double& outSourceSampleRate)
{
    auto& source = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = source.getClipForAudioThread();
    if (clip == nullptr)
        return false;

    const int length = clip->data.getNumSamples();
    const int s = juce::jlimit(0, length, startSample);
    const int e = juce::jlimit(s, length, endSample);
    if (e <= s)
        return false;

    const int numChannels = clip->data.getNumChannels();
    outBuffer.setSize(numChannels, e - s);
    for (int ch = 0; ch < numChannels; ++ch)
        outBuffer.copyFrom(ch, 0, clip->data, ch, s, e - s);

    outSourceSampleRate = clip->sourceSampleRate;
    return true;
}

bool SamplerEngine::extractRegionToPad(int sourceBankIndex, int sourcePadIndex, int startSample, int endSample,
                                        int destBankIndex, int destPadIndex)
{
    juce::AudioBuffer<float> region;
    double srcRate = 44100.0;
    if (! extractRegionBuffer(sourceBankIndex, sourcePadIndex, startSample, endSample, region, srcRate))
        return false;

    auto& source = banks[(size_t) sourceBankIndex][(size_t) sourcePadIndex];
    return loadSampleDataIntoPad(destBankIndex, destPadIndex, std::move(region), srcRate,
                                  source.displayName + "_chop");
}

bool SamplerEngine::addLayerFromRegion(int sourceBankIndex, int sourcePadIndex, int startSample, int endSample,
                                        int destBankIndex, int destPadIndex)
{
    juce::AudioBuffer<float> region;
    double srcRate = 44100.0;
    if (! extractRegionBuffer(sourceBankIndex, sourcePadIndex, startSample, endSample, region, srcRate))
        return false;

    auto& source = banks[(size_t) sourceBankIndex][(size_t) sourcePadIndex];
    return addLayerFromBuffer(destBankIndex, destPadIndex, std::move(region), srcRate,
                               source.displayName + "_chop");
}

bool SamplerEngine::renderPadToBuffer(int bankIndex, int padIndex, juce::AudioBuffer<float>& outBuffer)
{
    auto& source = banks[(size_t) bankIndex][(size_t) padIndex];
    auto clip = source.getClipForAudioThread();
    if (clip == nullptr || clip->data.getNumSamples() <= 0)
        return false;

    // A throwaway voice + dedicated send buses - deliberately NOT the live
    // `voices` array or the live `sendBuses` member, both of which are
    // audio-thread-owned during real playback. This method runs on the
    // message thread (a GUI action), so it gets its own scratch DSP state
    // instead of touching anything the audio thread might be using.
    Voice offlineVoice;
    offlineVoice.filter.prepare(voiceSpec);
    offlineVoice.smoothedVolumeGain.reset(sampleRate, kParamSmoothMs * 0.001);
    offlineVoice.smoothedPan.reset(sampleRate, kParamSmoothMs * 0.001);
    offlineVoice.smoothedCutoff.reset(sampleRate, kFilterSmoothMs * 0.001);
    offlineVoice.smoothedResonance.reset(sampleRate, kFilterSmoothMs * 0.001);
    offlineVoice.smoothedReverbSend.reset(sampleRate, kParamSmoothMs * 0.001);
    offlineVoice.smoothedDelaySend.reset(sampleRate, kParamSmoothMs * 0.001);

    initializeVoiceForPlayback(offlineVoice, source, clip, 1.0f, source.volumeDb.load()); // full velocity, no offset delay for a clean bounce; always uses the pad's own volume
    offlineVoice.pendingDelaySamples = 0;

    SendBuses offlineSendBuses;
    offlineSendBuses.prepare(voiceSpec);

    const int maxSamples = (int) (sampleRate * kMaxResampleSeconds);
    juce::AudioBuffer<float> scratch(2, maxSamples);
    scratch.clear();

    constexpr int kChunkSize = 512;
    juce::AudioBuffer<float> chunk(2, kChunkSize);
    int written = 0;

    while (offlineVoice.active && written < maxSamples)
    {
        const int thisChunk = juce::jmin(kChunkSize, maxSamples - written);
        chunk.setSize(2, thisChunk, false, false, true);
        chunk.clear();

        offlineSendBuses.beginBlock(thisChunk);
        renderVoice(offlineVoice, source, chunk, thisChunk, false, offlineSendBuses);
        offlineSendBuses.processAndMixInto(chunk);

        for (int ch = 0; ch < 2; ++ch)
            scratch.copyFrom(ch, written, chunk, ch, 0, thisChunk);
        written += thisChunk;
    }

    if (written <= 0)
        return false;

    outBuffer.setSize(2, written);
    for (int ch = 0; ch < 2; ++ch)
        outBuffer.copyFrom(ch, 0, scratch, ch, 0, written);
    return true;
}

bool SamplerEngine::resamplePad(int sourceBankIndex, int sourcePadIndex, int destBankIndex, int destPadIndex)
{
    juce::AudioBuffer<float> rendered;
    if (! renderPadToBuffer(sourceBankIndex, sourcePadIndex, rendered))
        return false;

    // Dest pad gets fresh, unprocessed audio and its OWN default
    // parameters - loadSampleDataIntoPad already resets trim/chop markers;
    // it deliberately does not touch filter/pitch/envelope/etc, which
    // default-construct to "no processing" on a from-scratch Pad, and this
    // is always writing into what should be a clean target.
    auto& source = banks[(size_t) sourceBankIndex][(size_t) sourcePadIndex];
    return loadSampleDataIntoPad(destBankIndex, destPadIndex, std::move(rendered), sampleRate,
                                  source.displayName + " (resampled)");
}

bool SamplerEngine::addLayerFromPad(int sourceBankIndex, int sourcePadIndex, int destBankIndex, int destPadIndex)
{
    juce::AudioBuffer<float> rendered;
    if (! renderPadToBuffer(sourceBankIndex, sourcePadIndex, rendered))
        return false;

    auto& source = banks[(size_t) sourceBankIndex][(size_t) sourcePadIndex];
    return addLayerFromBuffer(destBankIndex, destPadIndex, std::move(rendered), sampleRate,
                               source.displayName + " (resampled)");
}

bool SamplerEngine::addLayerFromBuffer(int bankIndex, int padIndex, juce::AudioBuffer<float>&& audioData,
                                        double sourceSampleRate, const juce::String& displayName)
{
    if (audioData.getNumChannels() <= 0 || audioData.getNumSamples() <= 0)
        return false;

    auto& pad = banks[(size_t) bankIndex][(size_t) padIndex];
    ExtraLayer* target = nullptr;
    for (auto& layer : pad.extraLayers)
        if (! layer.isLoaded())
        {
            target = &layer;
            break;
        }
    if (target == nullptr)
        return false; // every extra-layer slot already in use

    auto newClip = new SampleClip();
    newClip->data = std::move(audioData);
    newClip->sourceSampleRate = sourceSampleRate;

    target->setClip(newClip);
    target->displayName = displayName;
    target->velocityLow = 0;
    target->velocityHigh = 127; // full range - stacks with whatever else already matches
    return true;
}

float SamplerEngine::getPlayheadFraction(int bankIndex, int padIndex) const
{
    for (auto& v : voices)
    {
        if (v.active && v.bankIndex == bankIndex && v.padIndex == padIndex)
        {
            const int span = juce::jmax(1, v.trimEnd - v.trimStart);
            return juce::jlimit(0.0f, 1.0f, (float) ((v.readPos - v.trimStart) / span));
        }
    }
    return -1.0f;
}

bool SamplerEngine::isPadPlaying(int bankIndex, int padIndex) const
{
    for (auto& v : voices)
        if (v.active && v.bankIndex == bankIndex && v.padIndex == padIndex)
            return true;
    return false;
}
