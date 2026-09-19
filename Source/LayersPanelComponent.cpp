#include "LayersPanelComponent.h"

LayersPanelComponent::LayersPanelComponent(SamplerEngine& engineRef) : engine(engineRef)
{
    for (int row = 0; row < kNumRows; ++row)
    {
        auto* r = rows.add(new RowControls());

        r->nameLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
        r->nameLabel.setColour(juce::Label::textColourId, Colours2000::padText);
        addAndMakeVisible(r->nameLabel);

        r->volumeSlider.setRange(-60.0, 12.0, 0.1);
        r->volumeSlider.setValue(0.0, juce::dontSendNotification);
        r->volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        r->volumeSlider.setTooltip(row == 0
            ? "Volume for the main sample."
            : "This layer's own volume, independent of the main sample - lets stacked/layered "
              "samples be balanced against each other.");
        r->volumeSlider.onValueChange = [this, row]
        {
            auto& p = getPad();
            const float db = (float) rows[row]->volumeSlider.getValue();
            if (row == 0) p.volumeDb = db;
            else p.extraLayers[(size_t) (row - 1)].volumeDb = db;
            if (onLayersChanged) onLayersChanged();
        };
        addAndMakeVisible(r->volumeSlider);

        r->rangeSlider.setRange(0.0, 127.0, 1.0);
        r->rangeSlider.setMinAndMaxValues(0.0, 127.0, juce::dontSendNotification);
        r->rangeSlider.setTooltip("Velocity range for this layer - non-overlapping ranges switch between "
                                   "layers by hit force, overlapping/full ranges stack them together.");
        r->rangeSlider.onValueChange = [this, row]
        {
            auto& p = getPad();
            auto& slider = rows[row]->rangeSlider;
            const int lo = (int) slider.getMinValue();
            const int hi = (int) slider.getMaxValue();
            if (row == 0) { p.velocityLow = lo; p.velocityHigh = hi; }
            else { auto& layer = p.extraLayers[(size_t) (row - 1)]; layer.velocityLow = lo; layer.velocityHigh = hi; }
            if (onLayersChanged) onLayersChanged();
        };
        addAndMakeVisible(r->rangeSlider);

        r->loadButton.setTooltip(row == 0 ? "Load/replace the main sample."
                                           : "Load a sample into this layer slot.");
        r->loadButton.onClick = [this, row] { loadIntoRow(row); };
        addAndMakeVisible(r->loadButton);

        if (row > 0)
        {
            r->removeButton.setTooltip("Clear this layer slot.");
            r->removeButton.onClick = [this, row]
            {
                engine.removeLayer(bankIndex, padIndex, row - 1);
                refreshFromPad();
                if (onLayersChanged) onLayersChanged();
            };
            addAndMakeVisible(r->removeButton);
        }
    }
}

void LayersPanelComponent::setPad(int newBank, int newPad)
{
    bankIndex = newBank;
    padIndex = newPad;
    refreshFromPad();
}

juce::Colour LayersPanelComponent::colourForRow(int row) const
{
    static const juce::Colour colours[kNumRows] = {
        Colours2000::accent, juce::Colours::orange, juce::Colours::hotpink, juce::Colours::cyan
    };
    return colours[row];
}

void LayersPanelComponent::refreshFromPad()
{
    auto& pad = getPad();
    for (int row = 0; row < kNumRows; ++row)
    {
        auto* r = rows[row];
        const bool isMain = row == 0;
        const bool loaded = isMain ? pad.isLoaded() : pad.extraLayers[(size_t) (row - 1)].isLoaded();
        const juce::String name = isMain ? pad.displayName : pad.extraLayers[(size_t) (row - 1)].displayName;
        const int lo = isMain ? pad.velocityLow.load() : pad.extraLayers[(size_t) (row - 1)].velocityLow.load();
        const int hi = isMain ? pad.velocityHigh.load() : pad.extraLayers[(size_t) (row - 1)].velocityHigh.load();
        const float vol = isMain ? pad.volumeDb.load() : pad.extraLayers[(size_t) (row - 1)].volumeDb.load();

        r->nameLabel.setText((isMain ? juce::String("MAIN: ") : juce::String("LAYER " + juce::String(row) + ": "))
                              + (loaded ? name : juce::String("(empty)")),
                              juce::dontSendNotification);
        r->volumeSlider.setValue(vol, juce::dontSendNotification);
        r->volumeSlider.setEnabled(loaded);
        r->rangeSlider.setMinAndMaxValues((double) lo, (double) hi, juce::dontSendNotification);
        r->rangeSlider.setEnabled(loaded);
        r->loadButton.setButtonText(loaded ? "Replace..." : "Load...");
        if (row > 0)
            r->removeButton.setEnabled(loaded);
    }
    repaint();
}

void LayersPanelComponent::loadIntoRow(int row)
{
    fileChooser = std::make_unique<juce::FileChooser>(
        row == 0 ? "Load Sample" : "Load Layer Sample",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, row](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (row == 0)
            engine.loadSampleIntoPad(bankIndex, padIndex, file);
        else
            engine.loadLayerSlot(bankIndex, padIndex, row - 1, file);

        refreshFromPad();
        if (onLayersChanged)
            onLayersChanged();
    });
}

int LayersPanelComponent::getPreferredContentHeight() const
{
    return kNumRows * kRowHeight + 16;
}

void LayersPanelComponent::paint(juce::Graphics& g)
{
    g.setColour(Colours2000::textMuted);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("VELOCITY LAYERS", 0, 0, getWidth(), 14, juce::Justification::centredLeft);

    for (int row = 0; row < kNumRows; ++row)
    {
        g.setColour(colourForRow(row));
        g.fillRoundedRectangle(swatchBounds[row].toFloat(), 3.0f);

        if (row > 0)
        {
            g.setColour(Colours2000::border);
            const int y = swatchBounds[row].getY() - 6;
            g.drawLine(0.0f, (float) y, (float) getWidth(), (float) y, 1.0f);
        }
    }
}

void LayersPanelComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(16); // room for the "VELOCITY LAYERS" title drawn in paint()

    for (int row = 0; row < kNumRows; ++row)
    {
        auto rowArea = area.removeFromTop(kRowHeight).reduced(0, 4);
        auto* r = rows[row];

        swatchBounds[row] = rowArea.removeFromLeft(10).withSizeKeepingCentre(10, 10);
        rowArea.removeFromLeft(8);

        auto topRow = rowArea.removeFromTop(18);
        r->nameLabel.setBounds(topRow);

        rowArea.removeFromTop(4);
        auto volRow = rowArea.removeFromTop(20);
        r->volumeSlider.setBounds(volRow);

        rowArea.removeFromTop(4);
        auto bottomRow = rowArea.removeFromTop(24);
        if (row > 0)
        {
            r->removeButton.setBounds(bottomRow.removeFromRight(70));
            bottomRow.removeFromRight(6);
        }
        r->loadButton.setBounds(bottomRow.removeFromRight(80));
        bottomRow.removeFromRight(8);
        r->rangeSlider.setBounds(bottomRow);
    }
}
