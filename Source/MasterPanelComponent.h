#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"
#include "PadKnob.h"

// Fourth tab, alongside EDIT/CHOP/LAYERS: global (not per-pad) controls -
// input gain (applies to live audio while Record Sample is armed), output
// gain, and a master-bus compressor (glue compression on the whole mix,
// not per-voice - see SamplerEngine's comment on why).
class MasterPanelComponent : public juce::Component
{
public:
    explicit MasterPanelComponent(SamplerEngine& engineRef);

    void refreshFromEngine();
    void paint(juce::Graphics& g) override;
    void resized() override;

    int getPreferredContentHeight() const;

private:
    SamplerEngine& engine;

    static constexpr int kKnobW = 58, kKnobH = 64, kGap = 8, kLabelH = 16;

    PadKnob inputGainKnob  { "IN GAIN",  -60.0, 12.0, 0.1 };
    PadKnob outputGainKnob { "OUT GAIN", -60.0, 12.0, 0.1 };

    juce::ToggleButton compressorEnableButton { "ENABLE" };
    PadKnob thresholdKnob { "THRESH", -60.0, 0.0,   0.1 };
    PadKnob ratioKnob     { "RATIO",   1.0,  20.0,  0.1 };
    PadKnob attackKnob    { "ATTACK",  0.1,  200.0, 0.1 };
    PadKnob releaseKnob   { "RELEASE", 1.0,  1000.0, 1.0 };

    juce::Rectangle<int> gainLabelArea, compLabelArea;

    void setCompressorKnobsEnabled(bool enabled);
};
