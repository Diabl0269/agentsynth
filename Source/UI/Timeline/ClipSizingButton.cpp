// The Sizing field. A TextButton so it takes the transport strip's own background, border and focus ring (the snap
// toggle's, through AppLookAndFeel::drawButtonBackground), but it paints the mode's glyph and a menu chevron instead of
// its text. The glyph is the state: the fill is the same in every mode, so no mode looks switched off, and the width is
// fixed, so stepping the mode never shifts the strip. The button text keeps the mode's short name for test mode only.
// Like the snap toggle it is a Tab stop but a click never takes focus: Cut/Copy route by real focus, and the field
// must not pull it out of the clip lanes. The Select key pressed again also steps through the modes
// (TimelinePanelShortcuts.cpp).
#include "ClipSizingButton.h"

#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr synth::ClipSizingMode kModes[] = {synth::ClipSizingMode::Trim, synth::ClipSizingMode::CarryContents,
                                            synth::ClipSizingMode::Stretch};
constexpr float kGlyphSize = 16.0f;
constexpr float kGlyphLeft = 4.0f;
constexpr float kChevronFromRight = 9.0f;

// The short name test mode reads as the button text; the menu, tooltip and screen-reader name use the full name.
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

// Which glyph the field paints, for test mode.
const char* glyphName(synth::theme::Glyph glyph) noexcept {
    switch (glyph) {
    case synth::theme::Glyph::SizingCarry:
        return "carry";
    case synth::theme::Glyph::SizingStretch:
        return "stretch";
    default:
        return "trim";
    }
}
} // namespace

ClipSizingButton::ClipSizingButton()
    : juce::TextButton("Sizing") {
    setComponentID("timelineClipSizing");
    setClickingTogglesState(false);
    setMouseClickGrabsKeyboardFocus(false);
    // Test mode reads the mode, the glyph painted and the width from a 1x1 probe, so this header stays free of the
    // test-mode interface (it is included widely).
    stateProbe_ = makeLaneStateProbe("timelineClipSizingState", [this](juce::DynamicObject& into) {
        into.setProperty("sizingMode", synth::clipSizingModeKey(mode_));
        into.setProperty("glyph", glyphName(glyphFor(mode_)));
        into.setProperty("fieldWidth", getWidth());
    });
    addAndMakeVisible(*stateProbe_);
    show(mode_);
}

synth::theme::Glyph ClipSizingButton::glyphFor(synth::ClipSizingMode mode) noexcept {
    switch (mode) {
    case synth::ClipSizingMode::Trim:
        return synth::theme::Glyph::SizingTrim;
    case synth::ClipSizingMode::CarryContents:
        return synth::theme::Glyph::SizingCarry;
    case synth::ClipSizingMode::Stretch:
        return synth::theme::Glyph::SizingStretch;
    }
    return synth::theme::Glyph::SizingTrim;
}

juce::Rectangle<float> ClipSizingButton::glyphBounds() const noexcept {
    const float side = std::min(kGlyphSize, (float)getHeight());
    return {kGlyphLeft, ((float)getHeight() - side) * 0.5f, side, side};
}

void ClipSizingButton::show(synth::ClipSizingMode mode) {
    mode_ = mode;
    setButtonText(shortName(mode));
    setTitle(juce::String("Clip sizing, ") + synth::clipSizingModeName(mode));
    refreshTooltip();
    repaint();
}

// The tooltip leads with the full mode name, since the field itself shows only a glyph.
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

// The plain button fill in every mode (never the lit one), then the glyph and the chevron in the theme's primary ink,
// as the tool buttons draw theirs. Headless (no AppLookAndFeel) falls back to the button's own text colour.
void ClipSizingButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto ink = lf != nullptr ? lf->getTheme().colors.textPrimary : findColour(juce::TextButton::textColourOffId);
    getLookAndFeel().drawButtonBackground(g, *this, findColour(juce::TextButton::buttonColourId), over, down);
    synth::theme::paintGlyph(g, glyphFor(mode_), glyphBounds(), ink);
    synth::theme::paintComboChevron(g, {(float)getWidth() - kChevronFromRight, (float)getHeight() * 0.5f}, ink);
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

} // namespace synth::ui
