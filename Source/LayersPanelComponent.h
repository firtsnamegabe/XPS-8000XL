#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"

// Dedicated tab exposing every layering-related control in one place: each
// layer's velocity range (directly adjustable, not just auto-split), which
// sample is loaded into it, and the ability to remove one layer at a time.
// Previously layers were only reachable via a right-click submenu plus a
// small read-only velocity map on the waveform - this makes the whole
// feature visible and editable, not just "on or off via a menu".
class LayersPanelComponent : public juce::Component
{
public:
    explicit LayersPanelComponent(SamplerEngine& engineRef);

    void setPad(int bankIndex, int padIndex);
    void paint(juce::Graphics& g) override;
    void resized() override;

    int getPreferredContentHeight() const;

    std::function<void()> onLayersChanged; // fired after any load/remove/range edit

private:
    SamplerEngine& engine;
    int bankIndex = 0;
    int padIndex = 0;

    static constexpr int kNumRows = 1 + Pad::kMaxExtraLayers; // main + extras
    static constexpr int kRowHeight = 78;

    struct RowControls
    {
        juce::Label nameLabel;
        juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider rangeSlider { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
        juce::TextButton loadButton { "Load..." };
        juce::TextButton removeButton { "Remove" };
    };
    juce::OwnedArray<RowControls> rows;

    juce::Rectangle<int> swatchBounds[kNumRows]; // painted, not a child component

    std::unique_ptr<juce::FileChooser> fileChooser;

    Pad& getPad() { return engine.getBank(bankIndex)[(size_t) padIndex]; }
    juce::Colour colourForRow(int row) const;
    void refreshFromPad();
    void loadIntoRow(int row);
};
