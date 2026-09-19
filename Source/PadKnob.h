#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// One rotary knob + label. Originally lived inside EditParamsComponent.h;
// pulled out to its own header once MasterPanelComponent needed the same
// widget - depending on another component's header just to reuse a
// generic knob would have been a strange, unnecessary coupling.
struct PadKnob : public juce::Component
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;

    PadKnob(const juce::String& text, double minVal, double maxVal, double step)
    {
        slider.setRange(minVal, maxVal, step);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(10.0f)));
        addAndMakeVisible(label);
    }

    // Forwards to the slider - PadKnob itself is a plain Component, not a
    // SettableTooltipClient, so calling .setTooltip() directly on a PadKnob
    // instance wouldn't otherwise compile.
    void setTooltip(const juce::String& tip) { slider.setTooltip(tip); }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromBottom(14));
        slider.setBounds(area);
    }
};
