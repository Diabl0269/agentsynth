#include "UI/Chrome/ColourPickerFields.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/PopupHost.h"

namespace synth::ui {

ColourPickerPopup::ColourPickerPopup(juce::Colour initial, juce::PropertiesFile* props,
                                     std::function<void(juce::Colour)> onPreview,
                                     std::function<void(juce::Colour)> onCommit)
    : props_(props)
    , favourites_(loadFavouriteColours(props))
    , onPreview_(std::move(onPreview))
    , onCommit_(std::move(onCommit))
    , lastColour_(initial)
    , initial_(initial) {
    setComponentID("colourPickerPopup");

    addAndMakeVisible(selector_);
    selector_.setCurrentColour(initial, juce::dontSendNotification);
    selector_.addChangeListener(this);
    nameSelectorSliders();

    hsb_ = std::make_unique<Hsb>();
    saturationBrightness_ = std::make_unique<SaturationBrightnessField>();
    hueBar_ = std::make_unique<HueBar>();
    addAndMakeVisible(*saturationBrightness_);
    addAndMakeVisible(*hueBar_);
    saturationBrightness_->setExplicitFocusOrder(1);
    hueBar_->setExplicitFocusOrder(2);
    saturationBrightness_->onChange = [this](const Hsb& hsb) { applyFieldEdit(hsb); };
    hueBar_->onChange = [this](const Hsb& hsb) { applyFieldEdit(hsb); };
    followColour(initial);

    addAndMakeVisible(addFavouriteButton_);
    addFavouriteButton_.setComponentID("colourPickerAddFavourite");
    addFavouriteButton_.setButtonText(juce::CharPointer_UTF8("\xE2\x98\x85")); // filled star
    addFavouriteButton_.setTitle("Add to favourites");
    addFavouriteButton_.setTooltip("Add the current colour to favourites");
    addFavouriteButton_.onClick = [this] {
        addFavourite(favourites_, lastColour_);
        saveFavouriteColours(props_, favourites_);
        rebuildFavouriteButtons();
    };

    rebuildFavouriteButtons();
    setSize(280, kFieldsHeight + 112 + kFavouritesAreaHeight);
}

ColourPickerPopup::~ColourPickerPopup() {
    selector_.removeChangeListener(this);
    commitOnce(); // the callout closing (or this component being torn down any other way)
                  // is the ONE close event — fire the deferred commit here if it never fired.
    if (onClosed)
        onClosed();
}

void ColourPickerPopup::resized() {
    auto bounds = getLocalBounds();
    auto favArea = bounds.removeFromBottom(kFavouritesAreaHeight);
    auto fields = bounds.removeFromTop(kFieldsHeight).reduced(kFieldsMargin, kFieldsMargin);
    hueBar_->setBounds(fields.removeFromRight(kHueBarWidth));
    fields.removeFromRight(kFieldsMargin);
    saturationBrightness_->setBounds(fields);
    // The selector lays itself out for a taller box than it needs when it has no colour square: it keeps the
    // tall height so its colour swatch and sliders get their full rows, and the part past the bottom is clipped.
    selector_.setBounds(0, kFieldsHeight, getWidth(), kSelectorHeight);

    addFavouriteButton_.setBounds(favArea.removeFromLeft(kSwatchSize).reduced(2));
    layoutFavouriteButtons(favArea);
}

void ColourPickerPopup::followColour(juce::Colour colour) {
    if (colour != hsb_->toColour()) {
        const auto next = Hsb::fromColour(colour);
        const bool hueless = next.saturation < 0.002f || next.brightness < 0.002f;
        *hsb_ = next;
        if (hueless)
            hsb_->hue = hueKept_;
    }
    hueKept_ = hsb_->hue;
    saturationBrightness_->setHsb(*hsb_);
    hueBar_->setHsb(*hsb_);
}

void ColourPickerPopup::applyFieldEdit(const Hsb& hsb) {
    *hsb_ = hsb;
    hueKept_ = hsb_->hue;
    saturationBrightness_->setHsb(*hsb_);
    hueBar_->setHsb(*hsb_);
    const auto colour = hsb_->toColour();
    selector_.setCurrentColour(colour, juce::dontSendNotification);
    previewNow(colour);
}

void ColourPickerPopup::show(juce::Rectangle<int> screenArea, juce::Colour initial, juce::PropertiesFile* props,
                             std::function<void(juce::Colour)> onPreview, std::function<void(juce::Colour)> onCommit) {
    auto popup = std::make_unique<ColourPickerPopup>(initial, props, std::move(onPreview), std::move(onCommit));
    launchPopover(std::move(popup), screenArea, nullptr);
}

} // namespace synth::ui
