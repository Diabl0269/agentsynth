// The Timeline bar's clip sizing control: a ToolbarButton like the Add Track and Snap buttons beside it, so it takes
// the bar's chip, caption, hover and focus ring. The icon is the state: the chip looks the same in every mode (never
// lit), so no mode looks switched off, and the width is fixed from the widest caption, so stepping the mode never
// shifts the strip. A small notch in the chip's lower-right corner marks that a click opens a menu. Like the snap
// toggle it is a Tab stop but a click never takes focus: Cut/Copy route by real focus, and the control must not pull it
// out of the clip lanes. The Select key pressed again also steps through the modes (TimelinePanelShortcuts.cpp). The
// screen reader reads the title "Clip sizing" and the mode as its description.
#include "ClipSizingButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include <algorithm>

namespace synth::ui {

namespace {
// The menu mark: a right triangle whose legs run this far along the chip's right and bottom edges, mixed this far from
// the accent toward the primary text, and dimmed while the button is disabled.
constexpr float kNotchLeg = 5.0f;
constexpr float kNotchTextMix = 0.7f;
constexpr float kNotchDisabledAlpha = 0.4f;

constexpr synth::ClipSizingMode kModes[] = {synth::ClipSizingMode::Trim, synth::ClipSizingMode::CarryContents,
                                            synth::ClipSizingMode::Stretch};
const char* iconName(synth::theme::Icon icon) noexcept {
    switch (icon) {
    case synth::theme::Icon::ClipSizingCarry:
        return "carry";
    case synth::theme::Icon::ClipSizingStretch:
        return "stretch";
    default:
        return "trim";
    }
}
} // namespace

ClipSizingButton::ClipSizingButton()
    : ToolbarButton("Clip sizing") {
    setComponentID("timelineClipSizing");
    setLayout(ToolbarButtonLayout::Inline);
    setClickingTogglesState(false);
    setMouseClickGrabsKeyboardFocus(false);
    // Test mode reads the mode, the icon painted, the caption and the width from a 1x1 probe, so this header stays
    // free of the test-mode interface (it is included widely).
    stateProbe_ = makeLaneStateProbe("timelineClipSizingState", [this](juce::DynamicObject& into) {
        into.setProperty("sizingMode", synth::clipSizingModeKey(mode_));
        into.setProperty("glyph", iconName(iconFor(mode_)));
        into.setProperty("caption", getButtonText());
        into.setProperty("fieldWidth", getWidth());
    });
    addAndMakeVisible(*stateProbe_);
    show(mode_);
}

synth::theme::Icon ClipSizingButton::iconFor(synth::ClipSizingMode mode) noexcept {
    switch (mode) {
    case synth::ClipSizingMode::Trim:
        return synth::theme::Icon::ClipSizingTrim;
    case synth::ClipSizingMode::CarryContents:
        return synth::theme::Icon::ClipSizingCarry;
    case synth::ClipSizingMode::Stretch:
        return synth::theme::Icon::ClipSizingStretch;
    }
    return synth::theme::Icon::ClipSizingTrim;
}

// The caption is the short name: the menu, tooltip and screen-reader description use the full mode name.
juce::String ClipSizingButton::captionFor(synth::ClipSizingMode mode) {
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

void ClipSizingButton::show(synth::ClipSizingMode mode) {
    mode_ = mode;
    setIcon(iconFor(mode), ToolbarGroup::Edit);
    setButtonText(captionShown_ ? captionFor(mode) : juce::String());
    setTitle("Clip sizing");
    setDescription(synth::clipSizingModeName(mode));
    refreshTooltip();
    repaint();
}

void ClipSizingButton::setCaptionShown(bool shown) {
    captionShown_ = shown;
    show(mode_);
}

// The widest caption any mode can show, so the width never depends on the mode; just the chip with labels off.
int ClipSizingButton::getFixedWidth() const {
    if (!captionShown_)
        return getPreferredWidthFor({});
    int widest = 0;
    for (const auto mode : kModes)
        widest = std::max(widest, getPreferredWidthFor(captionFor(mode)));
    return widest;
}

// The tooltip leads with the full mode name, since the button shows only the short caption or none.
void ClipSizingButton::refreshTooltip() {
    setTooltip(formatShortcutHint(juce::String("Clip sizing: ") + synth::clipSizingModeName(mode_) +
                                      ". What dragging a clip edge does to its notes; Option-drag stretches once."
                                      " With Select active, the Select key steps it",
                                  stepKey_));
}

void ClipSizingButton::setStepKeyHint(const juce::String& key) {
    stepKey_ = key;
    refreshTooltip();
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

// Each mode's full name with one short clause on what it does to a clip's contents, and the Option tip last. The menu
// is as wide as its longest row, so every row stays one short line (TimelineClipSizingFieldTest caps the width).
juce::PopupMenu ClipSizingButton::buildMenu() const {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)std::size(kModes); ++i) {
        const auto mode = kModes[i];
        juce::String text =
            juce::String(synth::clipSizingModeName(mode)) + ": " + synth::clipSizingModeDescription(mode);
        menu.addItem(kFirstModeItemId + i, text, true, mode == mode_);
    }
    menu.addSeparator();
    menu.addItem(-1, "Option-drag an edge to stretch once", false, false);
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

// The notch is part of the chip: it is clipped to the chip's rounded border and sits in its lower-right corner, outside
// the glyph's box, so the glyph keeps its full size and centred place like the other modes' and labels off look alike.
// It follows the chip's press squash.
void ClipSizingButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    ToolbarButton::paintButton(g, highlighted, down);
    const auto& theme = synth::theme::themeOf(*this);
    const auto chip = synth::theme::toolbarChipBounds(*this);
    const auto squash = getChipSquash();
    const auto squashTransform = juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(), chip.getCentreY());

    juce::Path chipPath;
    chipPath.addRoundedRectangle(chip, toolbarChipSpec(getLayout()).chipRadius);
    chipPath.applyTransform(squashTransform);
    juce::Path notch;
    notch.addTriangle(chip.getRight() - kNotchLeg, chip.getBottom(), chip.getRight(), chip.getBottom(), chip.getRight(),
                      chip.getBottom() - kNotchLeg);
    notch.applyTransform(squashTransform);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(chipPath);
    const auto colour = theme.colors.accent.interpolatedWith(theme.colors.textPrimary, kNotchTextMix);
    g.setColour(isEnabled() ? colour : colour.withMultipliedAlpha(kNotchDisabledAlpha));
    g.fillPath(notch);
}

} // namespace synth::ui
