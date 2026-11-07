#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/PopupHost.h"

namespace synth::ui {

void ColourPickerPopup::show(juce::Rectangle<int> screenArea, juce::Colour initial, juce::PropertiesFile* props,
                             std::function<void(juce::Colour)> onPreview, std::function<void(juce::Colour)> onCommit) {
    auto popup = std::make_unique<ColourPickerPopup>(initial, props, std::move(onPreview), std::move(onCommit));
    launchPopover(std::move(popup), screenArea, nullptr);
}

} // namespace synth::ui
