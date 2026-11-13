#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// The Settings window's tab strip and panels. Differs from a stock juce::TabbedComponent in how the
// keyboard drives it: the strip is ONE Tab stop (a focusable leaf laid over the tab bar, the bottom
// dock's pattern), not one per tab button. While the leaf has focus Left / Right / Home / End open
// the neighbouring / first / last tab at once (nothing wraps), Return or Space moves focus into the
// open tab's first control, and Tab does the same by normal traversal; Shift+Tab from that control
// comes back to the leaf, which is ahead of every panel in the Tab order. The open tab's button wears
// the accent ring while the leaf has focus.
class SettingsTabs : public juce::TabbedComponent {
public:
    explicit SettingsTabs(juce::TabbedButtonBar::Orientation orientation);

    // The strip's single Tab stop and focus target.
    juce::Component& getStripFocus() noexcept { return stripFocus_; }
    // Gives keyboard focus to the strip. A no-op until the tabs are on screen.
    void focusTabStrip();
    // Moves keyboard focus into the open tab's first control (what Return and Space do on the strip).
    // False when the open tab has no control that takes focus.
    bool focusOpenTabContent();
    // Handles a key delivered to the focused strip; false leaves the key for the window (Cmd+1..9, Escape, Tab).
    bool handleStripKey(const juce::KeyPress& key);
    // Test seam: replaces the focus grab of focusOpenTabContent, since a headless window cannot hold focus.
    void setContentFocusHookForTest(std::function<void(juce::Component&)> hook) { contentFocusHook_ = std::move(hook); }
    // Test seam: draws the strip's ring as if its leaf held keyboard focus, which a headless window cannot.
    void setStripShownFocusedForTest(bool focused);
    // Test seam: the strip's screen-reader handler, which getAccessibilityHandler() only builds on screen.
    std::unique_ptr<juce::AccessibilityHandler> createStripAccessibilityHandlerForTest() {
        return stripFocus_.createAccessibilityHandler();
    }

    void resized() override;

protected:
    juce::TabBarButton* createTabButton(const juce::String& tabName, int tabIndex) override;
    void currentTabChanged(int newCurrentTabIndex, const juce::String& newCurrentTabName) override;

private:
    // Transparent, over the tab bar, never hit by the mouse: it takes the keys and draws the ring.
    class StripFocus
        : public juce::Component
        , public juce::SettableTooltipClient {
    public:
        explicit StripFocus(SettingsTabs& owner);
        bool keyPressed(const juce::KeyPress& key) override { return owner_.handleStripKey(key); }
        void focusGained(FocusChangeType) override { repaint(); }
        void focusLost(FocusChangeType) override { repaint(); }
        void paint(juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        SettingsTabs& owner_;
    };

    bool isStripFocused() const { return shownFocusedForTest_ || stripFocus_.hasKeyboardFocus(false); }

    StripFocus stripFocus_{*this};
    bool shownFocusedForTest_ = false;
    std::function<void(juce::Component&)> contentFocusHook_;
};
