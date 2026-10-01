#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Settings window's tab strip and panels. Differs from a stock juce::TabbedComponent in how the
// keyboard drives it: a focused tab button opens its tab on Space as well as Return, and opening a
// tab leaves keyboard focus on the tab button (as a native tab strip does) instead of moving it into
// the tab's content, so the next Tab goes to the tab's first control.
class SettingsTabs : public juce::TabbedComponent {
public:
    explicit SettingsTabs(juce::TabbedButtonBar::Orientation orientation)
        : juce::TabbedComponent(orientation) {}

    // Gives keyboard focus to the button of the tab that is currently open. A no-op until the tabs
    // are on screen.
    void focusCurrentTabButton() {
        if (auto* button = getTabbedButtonBar().getTabButton(getCurrentTabIndex()))
            button->grabKeyboardFocus();
    }

protected:
    juce::TabBarButton* createTabButton(const juce::String& tabName, int /*tabIndex*/) override {
        return new KeyboardTabButton(tabName, getTabbedButtonBar());
    }

    // Opening a tab brings its panel to the front, which hands keyboard focus to the panel. Take it
    // back to the tab button: the panel's own controls are reached with Tab.
    void currentTabChanged(int newCurrentTabIndex, const juce::String& /*newCurrentTabName*/) override {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        auto* panel = getTabContentComponent(newCurrentTabIndex);
        if (focused != nullptr && panel != nullptr && panel->isParentOf(focused))
            focusCurrentTabButton();
    }

private:
    class KeyboardTabButton : public juce::TabBarButton {
    public:
        using juce::TabBarButton::TabBarButton;

        bool keyPressed(const juce::KeyPress& key) override {
            if (isEnabled() && (key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey))) {
                getTabbedButtonBar().setCurrentTabIndex(getIndex());
                return true;
            }
            return juce::TabBarButton::keyPressed(key);
        }
    };
};
