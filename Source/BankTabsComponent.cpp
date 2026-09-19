#include "BankTabsComponent.h"
#include "PadComponent.h"

BankTabsComponent::BankTabsComponent(SamplerEngine& engineRef) : engine(engineRef)
{
    static const char* names[kNumBanks] = { "A", "B", "C", "D" };

    for (int i = 0; i < kNumBanks; ++i)
    {
        auto* btn = tabs.add(new juce::TextButton(names[i]));
        addAndMakeVisible(btn);

        btn->onClick = [this, i]
        {
            engine.setCurrentBank(i);
            updateTabStates();
            if (onBankChanged)
                onBankChanged();
        };
    }

    updateTabStates();
}

void BankTabsComponent::updateTabStates()
{
    for (int i = 0; i < tabs.size(); ++i)
    {
        const bool active = (i == engine.getCurrentBank());
        auto* btn = tabs[i];
        btn->setColour(juce::TextButton::buttonColourId,
                        active ? Colours2000::accent : Colours2000::voidBg);
        btn->setColour(juce::TextButton::textColourOffId,
                        active ? Colours2000::accentText : Colours2000::textMuted);
    }
}

void BankTabsComponent::resized()
{
    auto area = getLocalBounds();
    const int tabWidth = 34;
    const int gap = 6;
    for (auto* btn : tabs)
    {
        btn->setBounds(area.removeFromLeft(tabWidth));
        area.removeFromLeft(gap);
    }
}

bool BankTabsComponent::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    int bank, pad, startSample, endSample;
    const auto desc = details.description.toString();
    return PadComponent::parseDragDescription(desc, bank, pad)
        || PadComponent::parseRegionDragDescription(desc, bank, pad, startSample, endSample);
}

void BankTabsComponent::updateBankForDragPosition(juce::Point<int> localPosition)
{
    for (int i = 0; i < tabs.size(); ++i)
    {
        if (tabs[i]->getBounds().contains(localPosition) && i != engine.getCurrentBank())
        {
            engine.setCurrentBank(i);
            updateTabStates();
            if (onBankChanged)
                onBankChanged();
            return;
        }
    }
}

void BankTabsComponent::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details)
{
    updateBankForDragPosition(details.localPosition);
}

void BankTabsComponent::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    updateBankForDragPosition(details.localPosition);
}

void BankTabsComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails&)
{
    // Deliberately does nothing - dropping directly on the tab strip
    // itself isn't a valid target (there's no specific pad to load into).
    // Hovering here only switches which bank is showing; the actual drop
    // still needs to land on a pad in the now-visible bank.
}
