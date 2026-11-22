// ModuleComponentFilterPicture.cpp -- the Filter card's picture: the response view on top of the card, with its Show
// Spectrum and Show Response icon toggles on its top-right corner. Hiding the picture folds it to a thin strip that
// keeps the icons (so it can be shown again), fading with the card's block fade and height. The picture is card
// chrome, not a layout item: its choice is the module's CardViewState (ModuleComponentCardView.cpp).
// docs/layout/module-card-layout.md#the-filter-picture.
#include "ModuleComponent.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardWidgets/CardIconToggle.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"

namespace {
constexpr int kPictureHeight = synth::cardbody::kFilterPictureHeight;
constexpr int kStripHeight = synth::cardbody::kFilterPictureStripHeight;
constexpr int kIconInset = 3; // the chips' distance from the picture's top and right edges
constexpr int kIconGap = 4;
} // namespace

void ModuleComponent::createFilterPicture() {
    auto* filter = dynamic_cast<FilterModule*>(module);
    if (filter == nullptr)
        return;
    freqResponseComponent = std::make_unique<FrequencyResponseComponent>(*filter);
    addAndMakeVisible(freqResponseComponent.get()); // shown by default; restoreCardView() hides it for a card that was

    freqResponseToggle = std::make_unique<synth::ui::CardIconToggle>(synth::ui::CardIconToggle::Glyph::response,
                                                                     synth::cardbody::kShowResponseText,
                                                                     synth::ui::actiontext::kShowResponseTip);
    freqResponseToggle->setToggleState(true, juce::dontSendNotification);
    freqResponseToggle->onClick = [this] { setResponseShown(freqResponseToggle->getToggleState()); };
    addAndMakeVisible(freqResponseToggle.get());

    spectrumToggle = std::make_unique<synth::ui::CardIconToggle>(synth::ui::CardIconToggle::Glyph::spectrum,
                                                                 synth::ui::actiontext::kShowSpectrum,
                                                                 synth::ui::actiontext::kShowSpectrumFilterTip);
    spectrumToggle->setToggleState(false, juce::dontSendNotification);
    spectrumToggle->onClick = [this] { setSpectrumShown(spectrumToggle->getToggleState()); };
    addAndMakeVisible(spectrumToggle.get()); // fades with the picture
    attachBlockFade(responseFade_, {freqResponseComponent.get(), spectrumToggle.get()});
    responseFade_.get()->snapTo(true); // a new card starts with the picture shown, no fade-in
    // The icons sit over the picture, so they paint after it.
    spectrumToggle->toFront(false);
    freqResponseToggle->toFront(false);
}

int ModuleComponent::layoutFilterPicture(int y, int contentX, int contentW, bool apply) {
    if (freqResponseComponent == nullptr)
        return y;
    const int height = kStripHeight + blockHeight(responseFade_, kPictureHeight - kStripHeight);
    if (apply) {
        freqResponseComponent->setBounds(contentX, y, contentW, height);
        // Response sits in the corner whether the picture is shown or folded; Spectrum beside it, only while shown.
        constexpr int kSize = synth::ui::CardIconToggle::kSize;
        const int right = contentX + contentW - kIconInset;
        freqResponseToggle->setBounds(right - kSize, y + kIconInset, kSize, kSize);
        spectrumToggle->setBounds(right - 2 * kSize - kIconGap, y + kIconInset, kSize, kSize);
    }
    return y + height;
}
