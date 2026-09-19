#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ColourScheme.h"

class SamplePadLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SamplePadLookAndFeel()
    {
        applyPalette();
    }

    // Re-reads Colours2000::xxx into LookAndFeel's cached colour IDs. Call
    // this after Colours2000::setLightMode() so JUCE's built-in widgets
    // (sliders, buttons, labels) pick up the new palette on next repaint.
    void applyPalette()
    {
        setColour(juce::Slider::rotarySliderFillColourId, Colours2000::accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, Colours2000::border);
        setColour(juce::Slider::thumbColourId, Colours2000::accent);
        setColour(juce::Slider::textBoxTextColourId, Colours2000::text);
        setColour(juce::Slider::textBoxOutlineColourId, Colours2000::border);
        setColour(juce::Label::textColourId, Colours2000::textMuted);
        setColour(juce::TextButton::buttonColourId, Colours2000::voidBg);
        setColour(juce::TextButton::buttonOnColourId, Colours2000::accent);
        setColour(juce::TextButton::textColourOffId, Colours2000::textMuted);
        setColour(juce::TextButton::textColourOnId, Colours2000::accentText);
        setColour(juce::ComboBox::backgroundColourId, Colours2000::voidBg);
        setColour(juce::ComboBox::textColourId, Colours2000::text);
        setColour(juce::ComboBox::outlineColourId, Colours2000::border);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY();
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Track
        g.setColour(Colours2000::border);
        juce::Path track;
        track.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc
        g.setColour(Colours2000::accent);
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Cap
        g.setColour(Colours2000::panel);
        g.fillEllipse(centreX - radius * 0.62f, centreY - radius * 0.62f, radius * 1.24f, radius * 1.24f);

        // Pointer
        juce::Path pointer;
        const float pointerLength = radius * 0.55f;
        pointer.addRectangle(-1.2f, -pointerLength, 2.4f, pointerLength);
        g.setColour(Colours2000::accent);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    }
};
