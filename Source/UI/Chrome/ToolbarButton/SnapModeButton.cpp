// The Snap mode picker: a ToolbarButton in the clip sizing control's pattern (see ClipSizingButton.cpp). The icon is
// the state and the chip never lights, so no mode looks switched off. A click never takes focus (the curve editor
// keeps its keys) but the picker is a Tab stop, right after the Snap button; Enter and Space open the menu. It stays
// usable while Snap is off, so the mode can be chosen first. The screen reader reads the title "Snap mode" and the
// mode as its description.
#include "SnapModeButton.h"

#include "ToolbarMenuNotch.h"
#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Layout/LaneStateProbe.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <iterator>

namespace synth::ui {

namespace {
const char* iconName(synth::theme::Icon icon) noexcept {
    switch (icon) {
    case synth::theme::Icon::SnapModeTime:
        return "time";
    case synth::theme::Icon::SnapModeLevel:
        return "level";
    default:
        return "both";
    }
}
} // namespace

SnapModeButton::~SnapModeButton() = default;

SnapModeButton::SnapModeButton(const juce::String& componentId, const juce::String& propertyKey)
    : ToolbarButton(componentId)
    , propertyKey_(propertyKey) {
    setComponentID(componentId);
    setLayout(ToolbarButtonLayout::Inline);
    setClickingTogglesState(false);
    setMouseClickGrabsKeyboardFocus(false);
    setWantsKeyboardFocus(true);
    // Test mode reads the mode, the icon painted and whether it is usable from a 1x1 probe, so this header stays free
    // of the test-mode interface.
    stateProbe_ = makeLaneStateProbe(componentId + "State", [this](juce::DynamicObject& into) {
        into.setProperty("snapMode", snapModeKey(mode_));
        into.setProperty("glyph", iconName(iconFor(mode_)));
        into.setProperty("enabled", isEnabled());
    });
    addAndMakeVisible(*stateProbe_);
    gesture_ = std::make_unique<SideMenuGesture>();
    gesture_->chipBounds = [this] { return synth::theme::toolbarChipBounds(*this).toNearestInt(); };
    gesture_->onOpen = [this] { showMenu(); };
    gesture_->onPressSwallowed = [this] { setState(buttonNormal); };
    show(mode_);
}

synth::theme::Icon SnapModeButton::iconFor(SnapMode mode) noexcept {
    switch (mode) {
    case SnapMode::TimeOnly:
        return synth::theme::Icon::SnapModeTime;
    case SnapMode::LevelOnly:
        return synth::theme::Icon::SnapModeLevel;
    case SnapMode::TimeAndLevel:
        break;
    }
    return synth::theme::Icon::SnapModeBoth;
}

void SnapModeButton::show(SnapMode mode) {
    mode_ = mode;
    setIcon(iconFor(mode), ToolbarGroup::Edit);
    setTitle("Snap mode");
    setDescription(snapModeName(mode));
    refreshTooltip();
    repaint();
}

int SnapModeButton::getFixedWidth() const { return getPreferredWidthFor({}); }

void SnapModeButton::refreshTooltip() {
    const auto text = "Snap mode: " + snapModeName(mode_) +
                      ". Which way Snap works: across (time), up and down (level) or both. The Snap key turns Snap "
                      "on or off and keeps the mode";
    setTooltip(decorate_ ? decorate_(text) : text);
}

void SnapModeButton::setTooltipDecorator(std::function<juce::String(const juce::String&)> decorate) {
    decorate_ = std::move(decorate);
    refreshTooltip();
}

void SnapModeButton::choose(SnapMode mode) {
    show(mode);
    if (props_ != nullptr)
        if (auto* settings = props_->getUserSettings()) {
            settings->setValue(propertyKey_, snapModeKey(mode));
            props_->saveIfNeeded();
        }
    if (onModeChanged)
        onModeChanged(mode);
}

// An unknown or missing value keeps Time and level, what Snap did before the picker existed.
void SnapModeButton::setApplicationProperties(juce::ApplicationProperties* props) {
    props_ = props;
    auto* settings = props_ != nullptr ? props_->getUserSettings() : nullptr;
    show(settings != nullptr ? snapModeFromKey(settings->getValue(propertyKey_)) : mode_);
    if (onModeChanged)
        onModeChanged(mode_);
}

juce::PopupMenu SnapModeButton::buildMenu() const {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)std::size(kSnapModes); ++i)
        menu.addItem(kFirstModeItemId + i, snapModeName(kSnapModes[i]), true, kSnapModes[i] == mode_);
    return menu;
}

void SnapModeButton::showMenu() {
    auto menu = buildMenu();
    if (showMenuHook) {
        showMenuHook(menu);
        return;
    }
    juce::Component::SafePointer<SnapModeButton> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int result) {
        const int index = result - kFirstModeItemId;
        if (safeThis != nullptr && index >= 0 && index < (int)std::size(kSnapModes))
            safeThis->choose(kSnapModes[index]);
    });
}

void SnapModeButton::clicked() { showMenu(); }

void SnapModeButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    ToolbarButton::paintButton(g, highlighted, down);
    paintToolbarMenuNotch(g, *this);
}

// A plain click still opens the menu (clicked); a right-click, a corner press and a held press open it too and never
// pick a mode.
void SnapModeButton::mouseDown(const juce::MouseEvent& e) {
    if (!gesture_->mouseDown(e))
        ToolbarButton::mouseDown(e);
}

void SnapModeButton::mouseDrag(const juce::MouseEvent& e) {
    if (!gesture_->mouseDrag(e))
        ToolbarButton::mouseDrag(e);
}

void SnapModeButton::mouseUp(const juce::MouseEvent& e) {
    if (!gesture_->mouseUp(e))
        ToolbarButton::mouseUp(e);
}

void SnapModeButton::mouseExit(const juce::MouseEvent& e) {
    gesture_->mouseExit();
    ToolbarButton::mouseExit(e);
}

} // namespace synth::ui
