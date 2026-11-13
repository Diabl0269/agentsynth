#include "UI/Settings/SettingsTabs.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include "UI/Layout/TabStripKeys.h"
#include "UI/Layout/TooltipHelpHandler.h"

// Concern: the Settings tab strip as one keyboard stop -- its focusable leaf, the keys that switch
// tabs from it, the ring around the open tab, and where focus goes when a tab opens (see the header).

SettingsTabs::SettingsTabs(juce::TabbedButtonBar::Orientation orientation)
    : juce::TabbedComponent(orientation) {
    addAndMakeVisible(stripFocus_);
}

SettingsTabs::StripFocus::StripFocus(SettingsTabs& owner)
    : owner_(owner) {
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(false, false);
    setTitle("Settings tabs");
    setTooltip("Settings tabs: Left and Right switch tab, Return or Tab goes into the tab");
}

void SettingsTabs::StripFocus::paint(juce::Graphics& g) {
    if (!owner_.isStripFocused())
        return;
    if (auto* button = owner_.getTabbedButtonBar().getTabButton(owner_.getCurrentTabIndex()))
        synth::ui::paintFocusRingAlways(g, button->getBounds().toFloat(), *this, 3.0f);
}

// Role group with the open tab as its value, so focus landing on the strip reads "Settings tabs,
// Preferences"; the tab buttons are child elements. The tooltip (the strip's keys) is its help text.
std::unique_ptr<juce::AccessibilityHandler> SettingsTabs::StripFocus::createAccessibilityHandler() {
    return std::make_unique<synth::ui::TooltipHelpHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{
            std::make_unique<synth::ui::ReadOnlyTextValue>([this] { return owner_.getCurrentTabName(); })});
}

void SettingsTabs::resized() {
    juce::TabbedComponent::resized();
    stripFocus_.setBounds(getTabbedButtonBar().getBounds());
}

juce::TabBarButton* SettingsTabs::createTabButton(const juce::String& tabName, int /*tabIndex*/) {
    auto* button = new juce::TabBarButton(tabName, getTabbedButtonBar());
    button->setWantsKeyboardFocus(false); // the strip's leaf is the one stop
    return button;
}

void SettingsTabs::setStripShownFocusedForTest(bool focused) {
    shownFocusedForTest_ = focused;
    stripFocus_.repaint();
}

void SettingsTabs::focusTabStrip() { stripFocus_.grabKeyboardFocus(); }

// Plain Left/Right/Home/End open a tab and keep focus on the leaf; Return and Space enter the open
// tab. Everything else, Tab and every modified key included, is left for the window.
bool SettingsTabs::handleStripKey(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (const auto target = synth::ui::tabStripKeyTarget(key, getCurrentTabIndex(), getNumTabs())) {
        setCurrentTabIndex(*target);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        focusOpenTabContent();
        return true;
    }
    return false;
}

bool SettingsTabs::focusOpenTabContent() {
    auto* content = getTabContentComponent(getCurrentTabIndex());
    if (content == nullptr)
        return false;
    juce::KeyboardFocusTraverser traverser;
    auto* first = traverser.getDefaultComponent(content);
    if (first == nullptr)
        return false;
    if (contentFocusHook_)
        contentFocusHook_(*first);
    else
        first->grabKeyboardFocus();
    return true;
}

// Opening a tab brings its panel to the front, which hands keyboard focus to the panel. Take it
// back to the strip: the panel's own controls are reached with Tab or Return.
void SettingsTabs::currentTabChanged(int newCurrentTabIndex, const juce::String& /*newCurrentTabName*/) {
    stripFocus_.repaint();
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    auto* panel = getTabContentComponent(newCurrentTabIndex);
    if (focused != nullptr && panel != nullptr && panel->isParentOf(focused))
        focusTabStrip();
}
