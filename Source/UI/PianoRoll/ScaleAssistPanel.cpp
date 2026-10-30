#include "UI/PianoRoll/ScaleAssistPanel.h"

#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"

// The parts of ScaleAssistPanel that need the full KeyPicker / KeyAuditioner, so the panel's header (included by
// many files) does not pull the picker's header in with it.

namespace synth::ui {

ScaleAssistPanel::ScaleAssistPanel()
    : auditioner_(std::make_unique<KeyAuditioner>()) {
    setComponentID("scaleAssistPanel");
    // Its own focus region (Tab: timeline -> routing pane -> scale pane): the root takes focus, Up/Down step
    // through the controls and Escape comes back. A click never moves keyboard focus onto the root.
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Scale assist");
    buildKeyControls();
    buildCustomScaleEditor();
    buildPitchVisibilityControl();
    buildRandomGenerationControls();
    // Every control above lives in `scaleContent_`, the scrolled child of `scrollViewport_`.
    // The viewport shows its vertical scrollbar (and clips its child) only while the content's
    // natural height exceeds the panel's own height, so when the panel IS tall enough the whole
    // sidebar is visible and there is nothing to scroll. A too-short panel (a cramped timeline)
    // gets the scrollbar and the user scrolls within the sidebar instead of having its tail
    // controls squished off the bottom. See resized()/contentNaturalHeight().
    addAndMakeVisible(scrollViewport_);
    scrollViewport_.setComponentID("scaleAssistScrollViewport");
    scrollViewport_.setTitle("Scale assist options");
    scrollViewport_.setViewedComponent(&scaleContent_, /*deleteComponentWhenNoLongerNeeded=*/false);
    scrollViewport_.setScrollBarsShown(/*vertical=*/true, /*horizontal=*/false);
    showCustomEditor(false);
    paneKeys_.attachToDescendants();
    // The custom scale block is hidden here, so attachToDescendants() skipped its controls.
    for (auto& toggle : customPitchToggles_)
        paneKeys_.attach(toggle);
    paneKeys_.attach(customScaleNameEditor_);
    paneKeys_.attach(saveCustomScaleButton_);
}

ScaleAssistPanel::~ScaleAssistPanel() {
    if (auto* picker = getOpenKeyPicker()) {
        picker->onPick = nullptr;
        picker->onFollow = nullptr;
    }
}

void ScaleAssistPanel::setPropertiesFile(juce::PropertiesFile* props) {
    propertiesFile_ = props;
    auditioner_->setSettings(props);
    if (propertiesFile_ != nullptr)
        userScales_ = synth::parseUserScales(propertiesFile_->getValue(kUserScalesPropertyKey, {}));
}

void ScaleAssistPanel::openKeyPicker() {
    if (openPicker_ != nullptr)
        return;
    KeyPicker::Options options;
    options.current = following_ ? std::nullopt : selectedScale_;
    options.showFollowRow = true;
    options.following = following_;
    options.followDetail = songKey_.has_value() ? synth::keyDisplayName(*songKey_) : juce::String("No key");
    options.showNoKey = false; // an own key is always a key; "no key" is following a song with none
    options.userScales = userScales_;
    options.what = "Key";
    juce::Component::SafePointer<ScaleAssistPanel> self(this);
    auto picker = std::make_unique<KeyPicker>(std::move(options), [self](std::optional<synth::MusicalScale> key) {
        if (self != nullptr && key.has_value())
            self->pickOwnKey(*key);
    });
    picker->onFollow = [self] {
        if (self != nullptr && self->onFollowSongKey)
            self->onFollowSongKey();
    };
    auditioner_->wire(*picker);
    openPicker_ = picker.get();
    const auto area = keyField_.getScreenBounds();
    if (pickerLauncher)
        pickerLauncher(std::move(picker), area);
    else
        juce::CallOutBox::launchAsynchronously(std::move(picker), area, nullptr);
}

KeyPicker* ScaleAssistPanel::getOpenKeyPicker() const noexcept {
    return dynamic_cast<KeyPicker*>(openPicker_.getComponent());
}

} // namespace synth::ui
