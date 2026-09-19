#include "ChopParamsComponent.h"

ChopParamsComponent::ChopParamsComponent(SamplerEngine& engineRef, WaveformEditorComponent& waveformRef)
    : engine(engineRef), waveform(waveformRef)
{
    for (auto* b : { &transientsButton, &equalButton, &lazyButton, &keepOriginalToggle,
                      &oneShotToggle, &playThruToggle, &clearButton, &commitButton })
        addAndMakeVisible(b);

    for (int i = 1; i <= kNumChokeGroups; ++i)
    {
        auto* chip = chokeChips.add(new juce::TextButton(juce::String(i)));
        addAndMakeVisible(chip);
        chip->onClick = [this, i]
        {
            selectedChokeGroup = (selectedChokeGroup == i) ? 0 : i;
            for (int c = 0; c < chokeChips.size(); ++c)
                chokeChips[c]->setToggleState(selectedChokeGroup == c + 1, juce::dontSendNotification);
        };
    }

    countOrSensitivitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    countOrSensitivitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(countOrSensitivitySlider);

    countOrSensitivityLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(countOrSensitivityLabel);

    keepOriginalToggle.setClickingTogglesState(true);
    oneShotToggle.setClickingTogglesState(true);
    oneShotToggle.setToggleState(true, juce::dontSendNotification);
    playThruToggle.setClickingTogglesState(true);

    resultLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    resultLabel.setColour(juce::Label::textColourId, Colours2000::textMuted);
    addAndMakeVisible(resultLabel);

    transientsButton.onClick = [this] { mode = ChopMode::transients; applyMode(); };
    equalButton.onClick      = [this] { mode = ChopMode::equal; applyMode(); };
    lazyButton.onClick       = [this] { mode = ChopMode::lazy; applyMode(); };

    countOrSensitivitySlider.onValueChange = [this] { regenerateFromSliderIfNeeded(); };

    clearButton.onClick = [this]
    {
        engine.clearChopMarkers(bankIndex, padIndex);
        repaint();
    };

    commitButton.onClick = [this]
    {
        const int created = engine.commitChopsToPads(bankIndex, padIndex,
                                                       keepOriginalToggle.getToggleState(),
                                                       selectedChokeGroup,
                                                       oneShotToggle.getToggleState(),
                                                       playThruToggle.getToggleState());
        juce::String msg;
        if (created > 0)
        {
            msg = juce::String(created) + " chops bounced";
            if (created > kPadsPerBank - 1)
                msg += " (spilled into other banks - check A/B/C/D)";
        }
        else
        {
            msg = "No free pads / no markers";
        }
        resultLabel.setText(msg, juce::dontSendNotification);

        if (onPadsChangedAcrossBanks)
            onPadsChangedAcrossBanks();
    };

    applyMode();
}

void ChopParamsComponent::applyMode()
{
    updateModeButtons();

    const bool lazy = (mode == ChopMode::lazy);
    waveform.setLazyMode(lazy);
    waveform.setChopEditingEnabled(true);

    if (mode == ChopMode::equal)
    {
        countOrSensitivityLabel.setText("CHOPS", juce::dontSendNotification);
        countOrSensitivitySlider.setRange(2, 32, 1);
        countOrSensitivitySlider.setValue(4, juce::dontSendNotification);
        countOrSensitivitySlider.setVisible(true);
        regenerateFromSliderIfNeeded();
    }
    else if (mode == ChopMode::transients)
    {
        countOrSensitivityLabel.setText("SENSITIVITY", juce::dontSendNotification);
        countOrSensitivitySlider.setRange(0.0, 1.0, 0.01);
        countOrSensitivitySlider.setValue(0.5, juce::dontSendNotification);
        countOrSensitivitySlider.setVisible(true);
        regenerateFromSliderIfNeeded();
    }
    else // lazy: markers are placed by tapping the waveform during playback
    {
        countOrSensitivityLabel.setText("TAP WAVEFORM WHILE PLAYING", juce::dontSendNotification);
        countOrSensitivitySlider.setVisible(false);
    }
}

void ChopParamsComponent::regenerateFromSliderIfNeeded()
{
    if (mode == ChopMode::equal)
        engine.generateEqualChops(bankIndex, padIndex, (int) countOrSensitivitySlider.getValue());
    else if (mode == ChopMode::transients)
        engine.generateTransientChops(bankIndex, padIndex, (float) countOrSensitivitySlider.getValue());

    waveform.repaint();
}

void ChopParamsComponent::updateModeButtons()
{
    transientsButton.setToggleState(mode == ChopMode::transients, juce::dontSendNotification);
    equalButton.setToggleState(mode == ChopMode::equal, juce::dontSendNotification);
    lazyButton.setToggleState(mode == ChopMode::lazy, juce::dontSendNotification);
}

void ChopParamsComponent::setPad(int newBank, int newPad)
{
    bankIndex = newBank;
    padIndex = newPad;
    resultLabel.setText({}, juce::dontSendNotification);
    if (mode != ChopMode::lazy)
        regenerateFromSliderIfNeeded();
}

void ChopParamsComponent::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void ChopParamsComponent::resized()
{
    auto area = getLocalBounds();
    const int gap = 8;

    auto modeRow = area.removeFromTop(32);
    for (auto* b : { &transientsButton, &equalButton, &lazyButton })
    {
        b->setBounds(modeRow.removeFromLeft((getWidth() - 2 * gap) / 3));
        modeRow.removeFromLeft(gap);
    }
    area.removeFromTop(gap);

    auto sliderRow = area.removeFromTop(28);
    countOrSensitivityLabel.setBounds(sliderRow.removeFromLeft(150));
    countOrSensitivitySlider.setBounds(sliderRow);
    area.removeFromTop(gap * 2);

    auto togglesRow = area.removeFromTop(28);
    for (auto* b : { &keepOriginalToggle, &oneShotToggle, &playThruToggle })
    {
        b->setBounds(togglesRow.removeFromLeft(110));
        togglesRow.removeFromLeft(gap);
    }
    area.removeFromTop(gap);

    auto chokeRow = area.removeFromTop(30);
    for (auto* chip : chokeChips)
    {
        chip->setBounds(chokeRow.removeFromLeft(30));
        chokeRow.removeFromLeft(6);
    }
    area.removeFromTop(gap * 2);

    auto actionRow = area.removeFromTop(30);
    clearButton.setBounds(actionRow.removeFromLeft(120));
    actionRow.removeFromLeft(gap);
    commitButton.setBounds(actionRow.removeFromLeft(120));
    actionRow.removeFromLeft(gap);
    resultLabel.setBounds(actionRow);
}
