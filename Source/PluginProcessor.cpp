#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ColourScheme.h"

SamplePadProcessor::SamplePadProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void SamplePadProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);
}

void SamplePadProcessor::releaseResources()
{
    engine.releaseResources();
}

bool SamplePadProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Some hosts won't route audio into an instrument-flagged plugin at
    // all - that's fine, direct recording just won't receive anything
    // until they do (or, in Standalone, until an input device is picked
    // in Audio/MIDI Settings). Allow either no input bus or stereo.
    if (! layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SamplePadProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Auto-sync project BPM from the host's tempo when one is available
    // (VST3 in a DAW). Standalone has no playhead/tempo, so this simply
    // does nothing there - the BPM stays at whatever the user set in the UI.
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                engine.setProjectBpm((float) *bpm);
        }
    }

    // Capture live audio input for direct-recording BEFORE it's cleared
    // below - since the plugin has matching input/output bus channel
    // counts, `buffer` holds live input audio on entry to this call.
    engine.captureRecordingInput(buffer);

    buffer.clear();
    engine.processBlock(buffer, midiMessages);
}

juce::AudioProcessorEditor* SamplePadProcessor::createEditor()
{
    return new SamplePadEditor(*this);
}

namespace
{
    juce::String filterTypeToString(FilterType t)
    {
        switch (t)
        {
            case FilterType::lowPass:  return "lp";
            case FilterType::bandPass: return "bp";
            case FilterType::highPass: return "hp";
        }
        return "lp";
    }

    FilterType filterTypeFromString(const juce::String& s)
    {
        if (s == "bp") return FilterType::bandPass;
        if (s == "hp") return FilterType::highPass;
        return FilterType::lowPass;
    }

    juce::String playModeToString(PlayMode m)
    {
        switch (m)
        {
            case PlayMode::oneShot: return "oneshot";
            case PlayMode::hold:    return "hold";
            case PlayMode::loop:    return "loop";
        }
        return "oneshot";
    }

    PlayMode playModeFromString(const juce::String& s)
    {
        if (s == "hold") return PlayMode::hold;
        if (s == "loop") return PlayMode::loop;
        return PlayMode::oneShot;
    }

    juce::String pitchModeToString(PitchMode m)
    {
        return m == PitchMode::preserveLength ? "preserveLength" : "resample";
    }

    PitchMode pitchModeFromString(const juce::String& s)
    {
        return s == "preserveLength" ? PitchMode::preserveLength : PitchMode::resample;
    }

    // --- Embedded audio encode/decode ---------------------------------
    // A Kit embeds the actual sample audio (interleaved raw float32 PCM,
    // base64-encoded) rather than just a file path, so it's genuinely
    // portable - loads correctly on another machine or after the original
    // file has moved or been deleted. This trades off file size (base64
    // inflates by ~33%, and float32 PCM itself isn't compressed) for that
    // portability; a kit with several long samples can be a large file.

    juce::String encodeClipAudio(const SampleClip& clip)
    {
        const int numChannels = clip.data.getNumChannels();
        const int numSamples = clip.data.getNumSamples();
        juce::MemoryBlock block((size_t) numChannels * (size_t) numSamples * sizeof(float));
        auto* dest = static_cast<float*>(block.getData());

        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
                dest[(size_t) i * (size_t) numChannels + (size_t) ch] = clip.data.getSample(ch, i);

        return juce::Base64::toBase64(block.getData(), block.getSize());
    }

    bool decodeClipAudio(const juce::String& base64, int numChannels, int numSamples,
                          juce::AudioBuffer<float>& outBuffer)
    {
        if (numChannels <= 0 || numSamples <= 0 || base64.isEmpty())
            return false;

        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream mo(block, false);
            if (! juce::Base64::convertFromBase64(mo, base64))
                return false;
        } // mo's destructor flushes into block

        const size_t expectedBytes = (size_t) numChannels * (size_t) numSamples * sizeof(float);
        if (block.getSize() < expectedBytes)
            return false;

        const auto* src = static_cast<const float*>(block.getData());
        outBuffer.setSize(numChannels, numSamples);
        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
                outBuffer.setSample(ch, i, src[(size_t) i * (size_t) numChannels + (size_t) ch]);

        return true;
    }
}

juce::ValueTree SamplePadProcessor::buildStateTree() const
{
    juce::ValueTree root("XPS8000XLKit");
    root.setProperty("midiNoteOffset", engine.getMidiNoteOffset(), nullptr);
    root.setProperty("projectBpm", engine.getProjectBpm(), nullptr);
    root.setProperty("masterInputGainDb", engine.getMasterInputGainDb(), nullptr);
    root.setProperty("masterOutputGainDb", engine.getMasterOutputGainDb(), nullptr);
    root.setProperty("compressorEnabled", engine.isCompressorEnabled(), nullptr);
    root.setProperty("compressorThresholdDb", engine.getCompressorThresholdDb(), nullptr);
    root.setProperty("compressorRatio", engine.getCompressorRatio(), nullptr);
    root.setProperty("compressorAttackMs", engine.getCompressorAttackMs(), nullptr);
    root.setProperty("compressorReleaseMs", engine.getCompressorReleaseMs(), nullptr);
    root.setProperty("lightMode", Colours2000::isLightMode, nullptr);

    for (int b = 0; b < kNumBanks; ++b)
    {
        juce::ValueTree bankTree("Bank");
        bankTree.setProperty("index", b, nullptr);

        for (int p = 0; p < kPadsPerBank; ++p)
        {
            auto& pad = engine.getBank(b)[(size_t) p];
            auto clip = pad.getClipForAudioThread(); // safe from any thread, not just audio
            if (clip == nullptr)
                continue;

            // Every atomic field is explicitly .load()'d: var's constructor
            // is itself a user-defined conversion, and only one implicit
            // user-defined conversion (atomic<T> -> T) is allowed per
            // argument, so atomic<T> can't convert to var in one step.
            juce::ValueTree padTree("Pad");
            padTree.setProperty("index", p, nullptr);
            padTree.setProperty("numChannels", clip->data.getNumChannels(), nullptr);
            padTree.setProperty("numSamples", clip->data.getNumSamples(), nullptr);
            padTree.setProperty("sourceSampleRate", clip->sourceSampleRate, nullptr);
            padTree.setProperty("sampleDataBase64", encodeClipAudio(*clip), nullptr);
            padTree.setProperty("originalFilePath", pad.sourceFilePath, nullptr); // informational only, not required on load
            padTree.setProperty("displayName", pad.displayName, nullptr);
            padTree.setProperty("trimStart", pad.trimStart.load(), nullptr);
            padTree.setProperty("trimEnd", pad.trimEnd.load(), nullptr);
            padTree.setProperty("volumeDb", pad.volumeDb.load(), nullptr);
            padTree.setProperty("pitchSemis", pad.pitchSemis.load(), nullptr);
            padTree.setProperty("pan", pad.pan.load(), nullptr);
            padTree.setProperty("attackMs", pad.attackMs.load(), nullptr);
            padTree.setProperty("releaseMs", pad.releaseMs.load(), nullptr);
            padTree.setProperty("filterType", filterTypeToString(pad.filterType.load()), nullptr);
            padTree.setProperty("filterCutoffHz", pad.filterCutoffHz.load(), nullptr);
            padTree.setProperty("filterResonance", pad.filterResonance.load(), nullptr);
            padTree.setProperty("reverbSend", pad.reverbSend.load(), nullptr);
            padTree.setProperty("delaySend", pad.delaySend.load(), nullptr);
            padTree.setProperty("playMode", playModeToString(pad.playMode.load()), nullptr);
            padTree.setProperty("pitchMode", pitchModeToString(pad.pitchMode.load()), nullptr);
            padTree.setProperty("pitchGrainMs", pad.pitchGrainMs.load(), nullptr);
            padTree.setProperty("beatsInSample", pad.beatsInSample.load(), nullptr);
            padTree.setProperty("bitDepth", pad.bitDepth.load(), nullptr);
            padTree.setProperty("sampleRateReduction", pad.sampleRateReduction.load(), nullptr);
            padTree.setProperty("distortionDrive", pad.distortionDrive.load(), nullptr);
            padTree.setProperty("velocityLow", pad.velocityLow.load(), nullptr);
            padTree.setProperty("velocityHigh", pad.velocityHigh.load(), nullptr);

            for (auto& layer : pad.extraLayers)
            {
                auto layerClip = layer.getClipForAudioThread();
                if (layerClip == nullptr)
                    continue;

                juce::ValueTree layerTree("Layer");
                layerTree.setProperty("numChannels", layerClip->data.getNumChannels(), nullptr);
                layerTree.setProperty("numSamples", layerClip->data.getNumSamples(), nullptr);
                layerTree.setProperty("sourceSampleRate", layerClip->sourceSampleRate, nullptr);
                layerTree.setProperty("sampleDataBase64", encodeClipAudio(*layerClip), nullptr);
                layerTree.setProperty("displayName", layer.displayName, nullptr);
                layerTree.setProperty("velocityLow", layer.velocityLow.load(), nullptr);
                layerTree.setProperty("velocityHigh", layer.velocityHigh.load(), nullptr);
                layerTree.setProperty("volumeDb", layer.volumeDb.load(), nullptr);
                padTree.addChild(layerTree, -1, nullptr);
            }
            padTree.setProperty("pingPong", pad.loopMode.load() == LoopMode::pingPong, nullptr);
            padTree.setProperty("reverse", pad.reverse.load(), nullptr);
            padTree.setProperty("chokeGroup", pad.chokeGroup.load(), nullptr);
            padTree.setProperty("muted", pad.muted.load(), nullptr);
            padTree.setProperty("solo", pad.solo.load(), nullptr);
            padTree.setProperty("midiNote", pad.midiNote.load(), nullptr);
            padTree.setProperty("midiLearned", pad.midiLearned.load(), nullptr);
            padTree.setProperty("polyMode", pad.polyMode.load(), nullptr);
            padTree.setProperty("velocitySensitivity", pad.velocitySensitivity.load(), nullptr);
            padTree.setProperty("triggerOffsetMs", pad.triggerOffsetMs.load(), nullptr);

            bankTree.addChild(padTree, -1, nullptr);
        }

        root.addChild(bankTree, -1, nullptr);
    }

    return root;
}

void SamplePadProcessor::applyStateTree(const juce::ValueTree& root)
{
    if (! root.isValid())
        return;

    engine.setMidiNoteOffset((int) root.getProperty("midiNoteOffset", 36));
    engine.setProjectBpm((float) (double) root.getProperty("projectBpm", 120.0));
    engine.setMasterInputGainDb((float) (double) root.getProperty("masterInputGainDb", 0.0));
    engine.setMasterOutputGainDb((float) (double) root.getProperty("masterOutputGainDb", 0.0));
    engine.setCompressorEnabled((bool) root.getProperty("compressorEnabled", false));
    engine.setCompressorThresholdDb((float) (double) root.getProperty("compressorThresholdDb", -12.0));
    engine.setCompressorRatio((float) (double) root.getProperty("compressorRatio", 4.0));
    engine.setCompressorAttackMs((float) (double) root.getProperty("compressorAttackMs", 10.0));
    engine.setCompressorReleaseMs((float) (double) root.getProperty("compressorReleaseMs", 100.0));
    Colours2000::setLightMode((bool) root.getProperty("lightMode", false));

    for (auto bankTree : root)
    {
        if (! bankTree.hasType("Bank"))
            continue;
        const int b = (int) bankTree.getProperty("index", -1);
        if (b < 0 || b >= kNumBanks)
            continue;

        for (auto padTree : bankTree)
        {
            if (! padTree.hasType("Pad"))
                continue;
            const int p = (int) padTree.getProperty("index", -1);
            if (p < 0 || p >= kPadsPerBank)
                continue;

            const int numChannels = (int) padTree.getProperty("numChannels", 0);
            const int numSamples = (int) padTree.getProperty("numSamples", 0);
            const double sourceSampleRate = (double) padTree.getProperty("sourceSampleRate", 44100.0);
            const juce::String base64 = padTree.getProperty("sampleDataBase64", "").toString();

            juce::AudioBuffer<float> decoded;
            if (! decodeClipAudio(base64, numChannels, numSamples, decoded))
                continue; // no/corrupt embedded audio for this pad; skip rather than crash

            const juce::String displayName = padTree.getProperty("displayName", "Sample").toString();
            if (! engine.loadSampleDataIntoPad(b, p, std::move(decoded), sourceSampleRate, displayName))
                continue;

            auto& pad = engine.getBank(b)[(size_t) p];
            pad.sourceFilePath = padTree.getProperty("originalFilePath", "").toString(); // informational only
            pad.trimStart = (int) padTree.getProperty("trimStart", pad.trimStart.load());
            pad.trimEnd = (int) padTree.getProperty("trimEnd", pad.trimEnd.load());
            pad.volumeDb = (float) (double) padTree.getProperty("volumeDb", 0.0);
            pad.pitchSemis = (float) (double) padTree.getProperty("pitchSemis", 0.0);
            pad.pan = (float) (double) padTree.getProperty("pan", 0.0);
            pad.attackMs = (float) (double) padTree.getProperty("attackMs", 0.0);
            pad.releaseMs = (float) (double) padTree.getProperty("releaseMs", 5.0);
            pad.filterType = filterTypeFromString(padTree.getProperty("filterType", "lp").toString());
            pad.filterCutoffHz = (float) (double) padTree.getProperty("filterCutoffHz", 20000.0);
            pad.filterResonance = (float) (double) padTree.getProperty("filterResonance", 0.7);
            pad.reverbSend = (float) (double) padTree.getProperty("reverbSend", 0.0);
            pad.delaySend = (float) (double) padTree.getProperty("delaySend", 0.0);
            pad.playMode = playModeFromString(padTree.getProperty("playMode", "oneshot").toString());
            pad.pitchMode = pitchModeFromString(padTree.getProperty("pitchMode", "resample").toString());
            pad.pitchGrainMs = (float) (double) padTree.getProperty("pitchGrainMs", 50.0);
            pad.beatsInSample = (int) padTree.getProperty("beatsInSample", 0);
            pad.bitDepth = (int) padTree.getProperty("bitDepth", 16);
            pad.sampleRateReduction = (int) padTree.getProperty("sampleRateReduction", 1);
            pad.distortionDrive = (float) (double) padTree.getProperty("distortionDrive", 0.0);
            pad.loopMode = (bool) padTree.getProperty("pingPong", false) ? LoopMode::pingPong : LoopMode::forward;
            pad.reverse = (bool) padTree.getProperty("reverse", false);
            pad.chokeGroup = (int) padTree.getProperty("chokeGroup", 0);
            pad.muted = (bool) padTree.getProperty("muted", false);
            pad.solo = (bool) padTree.getProperty("solo", false);
            pad.midiLearned = (bool) padTree.getProperty("midiLearned", false);
            pad.polyMode = (bool) padTree.getProperty("polyMode", true);
            pad.velocitySensitivity = (float) (double) padTree.getProperty("velocitySensitivity", 1.0);
            pad.triggerOffsetMs = (float) (double) padTree.getProperty("triggerOffsetMs", 0.0);
            pad.velocityLow = (int) padTree.getProperty("velocityLow", 0);
            pad.velocityHigh = (int) padTree.getProperty("velocityHigh", 127);
            if (pad.midiLearned.load())
                pad.midiNote = (int) padTree.getProperty("midiNote", pad.midiNote.load());

            int layerSlot = 0;
            for (auto layerTree : padTree)
            {
                if (! layerTree.hasType("Layer") || layerSlot >= Pad::kMaxExtraLayers)
                    continue;

                const int lNumChannels = (int) layerTree.getProperty("numChannels", 0);
                const int lNumSamples = (int) layerTree.getProperty("numSamples", 0);
                const double lSampleRate = (double) layerTree.getProperty("sourceSampleRate", 44100.0);
                const juce::String lBase64 = layerTree.getProperty("sampleDataBase64", "").toString();

                juce::AudioBuffer<float> lDecoded;
                if (! decodeClipAudio(lBase64, lNumChannels, lNumSamples, lDecoded))
                    continue; // no/corrupt embedded audio for this layer; skip rather than crash

                auto newClip = new SampleClip();
                newClip->data = std::move(lDecoded);
                newClip->sourceSampleRate = lSampleRate;

                auto& layer = pad.extraLayers[(size_t) layerSlot];
                layer.setClip(newClip);
                layer.displayName = layerTree.getProperty("displayName", "Layer").toString();
                layer.velocityLow = (int) layerTree.getProperty("velocityLow", 0);
                layer.velocityHigh = (int) layerTree.getProperty("velocityHigh", 127);
                layer.volumeDb = (float) (double) layerTree.getProperty("volumeDb", 0.0);
                ++layerSlot;
            }
        }
    }
}

void SamplePadProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = buildStateTree().createXml())
        copyXmlToBinary(*xml, destData);
}

void SamplePadProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr)
        return;

    applyStateTree(juce::ValueTree::fromXml(*xml));
}

bool SamplePadProcessor::saveKitToFile(const juce::File& file) const
{
    auto xml = buildStateTree().createXml();
    if (xml == nullptr)
        return false;

    return xml->writeTo(file);
}

bool SamplePadProcessor::loadKitFromFile(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;

    auto root = juce::ValueTree::fromXml(*xml);
    if (! root.isValid())
        return false;

    // Clear every pad first: a kit file may legitimately have fewer loaded
    // pads than the current in-memory state (e.g. loading a smaller kit
    // over a fuller one), and applyStateTree only ever *sets* pads it
    // finds in the tree - it never clears ones that are absent.
    newKit();
    applyStateTree(root);
    return true;
}

void SamplePadProcessor::newKit()
{
    for (int b = 0; b < kNumBanks; ++b)
        for (int p = 0; p < kPadsPerBank; ++p)
            engine.getBank(b)[(size_t) p].clear();

    engine.setMidiNoteOffset(36);
    engine.setCurrentBank(0);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SamplePadProcessor();
}
