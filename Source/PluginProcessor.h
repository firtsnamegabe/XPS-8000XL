#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"

class SamplePadProcessor : public juce::AudioProcessor
{
public:
    SamplePadProcessor();
    ~SamplePadProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock; // un-hide the double-precision overload
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "XPS-8000XL"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Exposed to the editor.
    SamplerEngine& getEngine() { return engine; }

    // --- Kit save/load ---------------------------------------------------
    // A "Kit" (.xpskit) is every bank/pad fully self-contained: the actual
    // sample audio is embedded (base64-encoded raw PCM), not just a file
    // path, so a kit loads correctly on a different machine or after the
    // original sample files have moved or been deleted - the point of a
    // kit is to be a shareable, portable preset, like a real hardware
    // sampler's kit. getStateInformation/setStateInformation (the DAW's
    // own automatic session recall, called whenever the host saves/loads
    // its project - required for the plugin to behave correctly in any
    // host, so this isn't optional to implement) now uses this exact same
    // fully-embedded format instead of a separate lighter-weight one, so
    // there's only one save format anywhere in the plugin.
    bool saveKitToFile(const juce::File& file) const;
    bool loadKitFromFile(const juce::File& file);

    // Clears every pad in every bank and resets MIDI mapping to default -
    // a fresh empty kit, same idea as a "New Project" action.
    void newKit();

    static const char* getKitFileExtension() { return ".xolo"; }

private:
    SamplerEngine engine;

    juce::ValueTree buildStateTree() const;
    void applyStateTree(const juce::ValueTree& root);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePadProcessor)
};
