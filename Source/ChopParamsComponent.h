#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SamplerEngine.h"
#include "ColourScheme.h"
#include "WaveformEditorComponent.h"

class ChopParamsComponent : public juce::Component
{
public:
    ChopParamsComponent(SamplerEngine& engineRef, WaveformEditorComponent& waveformRef);

    void setPad(int bankIndex, int padIndex);
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Fixed estimate matching resized()'s row heights with margin - this
    // panel is short enough that it should rarely need to scroll, but it's
    // still wrapped in the same Viewport pattern as EditParamsComponent for
    // consistency (and as a safety net if that ever changes).
    int getPreferredContentHeight() const { return 220; }

    std::function<void()> onPadsChangedAcrossBanks; // fired after COMMIT, in case chops landed in other banks

private:
    SamplerEngine& engine;
    WaveformEditorComponent& waveform;
    int bankIndex = 0;
    int padIndex = 0;
    ChopMode mode = ChopMode::equal;

    juce::TextButton transientsButton { "TRANSIENTS" };
    juce::TextButton equalButton      { "EQUAL" };
    juce::TextButton lazyButton       { "LAZY" };

    juce::Slider countOrSensitivitySlider;
    juce::Label countOrSensitivityLabel;

    juce::TextButton keepOriginalToggle { "KEEP ORIGINAL" };
    juce::TextButton oneShotToggle      { "ONESHOT" };
    juce::TextButton playThruToggle     { "PLAYTHRU" };
    juce::OwnedArray<juce::TextButton> chokeChips;
    int selectedChokeGroup = 0;

    juce::TextButton clearButton  { "CLEAR MARKERS" };
    juce::TextButton commitButton { "COMMIT CHOPS" };
    juce::Label resultLabel;

    void updateModeButtons();
    void regenerateFromSliderIfNeeded();
    void applyMode();
};
