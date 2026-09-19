#include "MasterPanelComponent.h"

MasterPanelComponent::MasterPanelComponent(SamplerEngine& engineRef) : engine(engineRef)
{
    inputGainKnob.setTooltip("Gain applied to live audio while Record Sample is armed - "
                              "gain-stage before recording, not an effect on playback.");
    outputGainKnob.setTooltip("Gain applied to the final mixed output, after the compressor.");

    for (auto* knob : { &inputGainKnob, &outputGainKnob, &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
        addAndMakeVisible(knob);

    inputGainKnob.slider.onValueChange = [this]
    { engine.setMasterInputGainDb((float) inputGainKnob.slider.getValue()); };
    outputGainKnob.slider.onValueChange = [this]
    { engine.setMasterOutputGainDb((float) outputGainKnob.slider.getValue()); };

    compressorEnableButton.setTooltip("Master-bus compressor ('glue' compression on the whole mix, "
                                       "not per-pad) - applied after every pad and the shared reverb/delay.");
    compressorEnableButton.onClick = [this]
    {
        engine.setCompressorEnabled(compressorEnableButton.getToggleState());
        setCompressorKnobsEnabled(compressorEnableButton.getToggleState());
    };
    addAndMakeVisible(compressorEnableButton);

    thresholdKnob.setTooltip("Level above which compression kicks in.");
    ratioKnob.setTooltip("How strongly the signal is compressed once it crosses the threshold - "
                          "4:1 is a moderate, common starting point.");
    attackKnob.setTooltip("How quickly the compressor reacts once the signal crosses the threshold.");
    releaseKnob.setTooltip("How quickly the compressor lets go once the signal drops back below the threshold.");

    thresholdKnob.slider.onValueChange = [this] { engine.setCompressorThresholdDb((float) thresholdKnob.slider.getValue()); };
    ratioKnob.slider.onValueChange = [this] { engine.setCompressorRatio((float) ratioKnob.slider.getValue()); };
    attackKnob.slider.onValueChange = [this] { engine.setCompressorAttackMs((float) attackKnob.slider.getValue()); };
    releaseKnob.slider.onValueChange = [this] { engine.setCompressorReleaseMs((float) releaseKnob.slider.getValue()); };

    refreshFromEngine();
}

void MasterPanelComponent::setCompressorKnobsEnabled(bool enabled)
{
    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
        knob->slider.setEnabled(enabled);
}

void MasterPanelComponent::refreshFromEngine()
{
    inputGainKnob.slider.setValue(engine.getMasterInputGainDb(), juce::dontSendNotification);
    outputGainKnob.slider.setValue(engine.getMasterOutputGainDb(), juce::dontSendNotification);

    const bool enabled = engine.isCompressorEnabled();
    compressorEnableButton.setToggleState(enabled, juce::dontSendNotification);
    thresholdKnob.slider.setValue(engine.getCompressorThresholdDb(), juce::dontSendNotification);
    ratioKnob.slider.setValue(engine.getCompressorRatio(), juce::dontSendNotification);
    attackKnob.slider.setValue(engine.getCompressorAttackMs(), juce::dontSendNotification);
    releaseKnob.slider.setValue(engine.getCompressorReleaseMs(), juce::dontSendNotification);
    setCompressorKnobsEnabled(enabled);
}

int MasterPanelComponent::getPreferredContentHeight() const
{
    // One full-width row for gain (label+knobs), one for the compressor
    // (label+enable row, then a knob row) - mirrors resized() exactly:
    // resized() never removes a trailing gap after the last knob row, so
    // this doesn't add one either.
    return (kLabelH + kKnobH + kGap) + (kLabelH + 26 + kGap + kKnobH);
}

void MasterPanelComponent::paint(juce::Graphics& g)
{
    auto drawSectionLabel = [&g](juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour(Colours2000::textMuted);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(text, r, juce::Justification::centredLeft);
    };

    drawSectionLabel(gainLabelArea, "MASTER GAIN");
    drawSectionLabel(compLabelArea, "COMPRESSOR (MASTER BUS)");
}

void MasterPanelComponent::resized()
{
    auto area = getLocalBounds();

    auto layoutKnobRow = [&](juce::Rectangle<int>& row, std::initializer_list<PadKnob*> knobs)
    {
        for (auto* k : knobs)
        {
            k->setBounds(row.removeFromLeft(kKnobW).withHeight(kKnobH));
            row.removeFromLeft(kGap);
        }
    };

    gainLabelArea = area.removeFromTop(kLabelH);
    auto gainRow = area.removeFromTop(kKnobH);
    layoutKnobRow(gainRow, { &inputGainKnob, &outputGainKnob });
    area.removeFromTop(kGap);

    compLabelArea = area.removeFromTop(kLabelH);
    auto enableRow = area.removeFromTop(26);
    compressorEnableButton.setBounds(enableRow.removeFromLeft(90));
    area.removeFromTop(kGap);

    auto compKnobRow = area.removeFromTop(kKnobH);
    layoutKnobRow(compKnobRow, { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob });
}
