// The Sizing field. A plain TextButton so it takes the transport strip's own look (the snap toggle's), with its
// toggle state as the "not Trim" light rather than a click toggle. Like the snap toggle it is a Tab stop but a
// click never takes focus: Cut/Copy route by real focus, and the field must not pull it out of the clip lanes. The
// Select key pressed again also steps through the modes (TimelinePanelShortcuts.cpp).
#include "ClipSizingButton.h"

namespace synth::ui {

namespace {
constexpr synth::ClipSizingMode kModes[] = {synth::ClipSizingMode::Trim, synth::ClipSizingMode::CarryContents,
                                            synth::ClipSizingMode::Stretch};

// The field is narrow, so it names the mode in one word; the menu and the screen-reader name use the full name.
const char* shortName(synth::ClipSizingMode mode) noexcept {
    switch (mode) {
    case synth::ClipSizingMode::Trim:
        return "Trim";
    case synth::ClipSizingMode::CarryContents:
        return "Carry";
    case synth::ClipSizingMode::Stretch:
        return "Stretch";
    }
    return "Trim";
}
} // namespace

ClipSizingButton::ClipSizingButton()
    : juce::TextButton("Sizing") {
    setComponentID("timelineClipSizing");
    setClickingTogglesState(false);
    setMouseClickGrabsKeyboardFocus(false);
    show(mode_);
}

void ClipSizingButton::show(synth::ClipSizingMode mode) {
    mode_ = mode;
    setButtonText(juce::String("Sizing: ") + shortName(mode));
    setToggleState(mode != synth::ClipSizingMode::Trim, juce::dontSendNotification);
    setTitle(juce::String("Clip sizing, ") + synth::clipSizingModeName(mode));
}

void ClipSizingButton::choose(synth::ClipSizingMode mode) {
    show(mode);
    if (props_ != nullptr)
        if (auto* settings = props_->getUserSettings()) {
            settings->setValue(kPropertyKey, synth::clipSizingModeKey(mode));
            props_->saveIfNeeded();
        }
    if (onModeChanged)
        onModeChanged(mode);
}

// An unknown or missing value keeps Trim, the behaviour every clip edge had before the setting existed.
void ClipSizingButton::setApplicationProperties(juce::ApplicationProperties* props) {
    props_ = props;
    auto* settings = props_ != nullptr ? props_->getUserSettings() : nullptr;
    const auto saved = settings != nullptr ? synth::clipSizingModeFromKey(settings->getValue(kPropertyKey))
                                           : std::optional<synth::ClipSizingMode>{};
    show(saved.value_or(mode_));
    if (onModeChanged)
        onModeChanged(mode_);
}

// Each mode with what it does to the notes; Stretch says it is MIDI only for now. The Option tip closes the menu.
juce::PopupMenu ClipSizingButton::buildMenu() const {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)std::size(kModes); ++i) {
        const auto mode = kModes[i];
        juce::String text =
            juce::String(synth::clipSizingModeName(mode)) + ": " + synth::clipSizingModeDescription(mode);
        if (mode == synth::ClipSizingMode::Stretch)
            text += " (MIDI clips; audio needs the time-stretch engine)";
        menu.addItem(kFirstModeItemId + i, text, true, mode == mode_);
    }
    menu.addSeparator();
    menu.addItem(-1, "Hold Option while dragging an edge to stretch once", false, false);
    return menu;
}

void ClipSizingButton::showMenu() {
    auto menu = buildMenu();
    if (showMenuHook) {
        showMenuHook(menu);
        return;
    }
    juce::Component::SafePointer<ClipSizingButton> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int result) {
        const int index = result - kFirstModeItemId;
        if (safeThis != nullptr && index >= 0 && index < (int)std::size(kModes))
            safeThis->choose(kModes[index]);
    });
}

void ClipSizingButton::clicked() { showMenu(); }

} // namespace synth::ui
