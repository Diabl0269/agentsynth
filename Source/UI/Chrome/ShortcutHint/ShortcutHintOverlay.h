#pragma once

#include "ShortcutHintLayout.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** One bottom-dock tab as the overlay needs to see it (BottomDockComponent::getStripTabs()). */
struct DockTabHint {
    juce::TextButton* button{nullptr};
    juce::String actionId;
    juce::String name;
};

/** What the overlay needs to know about the bottom dock, read fresh each time the hints appear. */
struct DockHintInfo {
    bool open{true};
    juce::Component* dock{nullptr};      // the dock's own bounds cap bubbles from buttons inside it
    juce::Component* statusBar{nullptr}; // the hidden-dock row sits above it
    juce::Component* toggle{nullptr};    // the panel toggle button; its pill leads the hidden-dock row
    juce::String toggleActionId;
    juce::String toggleLabel;
    std::vector<DockTabHint> tabs; // offered in the strip, in tab order (detached tabs excluded)
};

// ShortcutHintOverlay: hold Cmd (or, on macOS, Ctrl; or Option/Alt) on its own for ~500 ms and a key-cap
// bubble appears on every visible registered button; Ctrl and Option show only the buttons whose live
// binding uses that key. Release and they fade out. Any other key, a mouse click or a loss of
// window focus cancels at once. A full-window child of the host that paints only while showing and
// never takes a click (docs/control/shortcuts.md#shortcut-hints).
//
// Each bubble grows out of the button it labels (or, for the hidden-panel pills, rises into place):
// one tween value `t` scales, moves and fades every bubble together (hint::animatedBubbleBounds).
// Nothing runs at rest: the 500 ms delay is a one-shot Timer armed only while Cmd is down alone,
// and the tween uses an AnimationDriver that exists only while it is in flight. Cmd is noticed
// by sample(), fed from the host's existing 10 Hz poll and from modifierKeysChanged().
class ShortcutHintOverlay
    : public juce::Component
    , public juce::KeyListener
    , private juce::Timer
    , private juce::ChangeListener
    , private juce::FocusChangeListener
    , private juce::ComponentListener {
public:
    ShortcutHintOverlay(juce::Component& host, ShortcutManager& shortcuts);
    ~ShortcutHintOverlay() override;

    static constexpr double kShowDelayMs = 500.0;
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;

    /** Registers a button that triggers `actionId`; the key shown is always read from the
     *  ShortcutManager. A registered component that is hidden, clipped or covered gets no hint.
     *  `fallbackActionId` is shown instead while `actionId` is unbound (one button, two actions). */
    void addTarget(juce::Component& component, const juce::String& actionId, const juce::String& fallbackActionId = {});
    /** Registers a button that a fixed, positional key opens (the Settings tabs' Cmd+1..9): the bubble shows
     *  `key` as given, not a ShortcutManager binding, so a rebind cannot change it. */
    void addFixedKeyTarget(juce::Component& component, const juce::KeyPress& key);
    /** Like addTarget, for a painted region (a header chip) of `owner`; `areaInOwner` is read fresh each time. */
    void addAreaTarget(juce::Component& owner, std::function<juce::Rectangle<int>()> areaInOwner,
                       const juce::String& actionId);
    /** Supplies the bottom dock's tabs and toggle; called each time the hints appear. */
    void setDockSource(std::function<DockHintInfo()> source) { dockSource_ = std::move(source); }

    /** The host's poll and modifierKeysChanged() both land here with the live modifier state. */
    void sample(const juce::ModifierKeys& mods);
    void modifierKeysChanged(const juce::ModifierKeys& mods) override { sample(mods); }
    using juce::Component::keyPressed;
    /** Any real key press cancels at once. Never consumes the key. */
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    /** Window focus loss (a null focused component) cancels at once. */
    void globalFocusChanged(juce::Component* focused) override;

    bool areHintsShowing() const noexcept { return state_ == State::Showing || state_ == State::FadingOut; }
    bool isPending() const noexcept { return state_ == State::Pending; }
    /** The tween value t (0 hidden .. 1 settled); it is also the opacity. */
    float getOpacity() const noexcept { return opacity_; }

    struct Entry {
        juce::Rectangle<int> bounds; // the whole bubble (cap, or pill for the hidden-dock row)
        juce::Rectangle<int> cap;    // the key cap itself
        juce::String keyText;
        juce::String label; // pill text; empty for a plain bubble
        bool isPill{false};
        juce::Point<float> origin; // where the bubble grows out of at t = 0 (hint::bubbleOrigin)
        bool compact{false};       // the smaller in-tab cap
    };
    const std::vector<Entry>& getEntries() const noexcept { return entries_; }

    /** Test seam: swap the millisecond clock (default: juce::Time::getMillisecondCounterHiRes). */
    void setClockForTest(std::function<double()> clock) { clock_ = std::move(clock); }

    void paint(juce::Graphics& g) override;
    bool hitTest(int, int) override { return false; }

private:
    enum class State { Idle, Pending, Showing, FadingOut };
    struct Target {
        juce::Component::SafePointer<juce::Component> component;
        juce::String actionId;
        std::function<juce::Rectangle<int>()> area; // empty: the whole component
        juce::KeyPress fixedKey;                    // valid: shown instead of actionId's binding
        juce::String fallbackActionId;              // shown while actionId is unbound
    };
    // The one modifier whose hold drives the hints; None = no hint modifier, or more than one.
    enum class HintModifier { None, Cmd, Ctrl, Alt };

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void mouseDown(const juce::MouseEvent&) override;
    // A modifier change makes JUCE send a fake mouse move, which reaches this global listener with the
    // live modifiers -- so a press or release is noticed at once, wherever the pointer is.
    void mouseMove(const juce::MouseEvent& e) override { sample(e.mods); }
    void componentMovedOrResized(juce::Component&, bool, bool) override;

    static HintModifier hintModifierOf(const juce::ModifierKeys& mods) noexcept;
    static bool isAnyHintModifierDown(const juce::ModifierKeys& mods) noexcept;
    bool bindingShownInMode(const juce::KeyPress& binding) const noexcept;
    void cancelNow();
    void showHints();
    void beginFadeOut();
    void resumeFadeIn();
    void setOpacity(float value);
    void hideNow();
    void applyEntryTween(juce::Graphics& g, const Entry& entry) const;
    bool isHintable(const juce::Component& c, juce::Point<int> pointInC) const;
    bool isHintable(const juce::Component& c) const { return isHintable(c, c.getLocalBounds().getCentre()); }

    void rebuildEntries();
    void addBubbleEntries(const DockHintInfo& dock);
    void addTabEntries(const DockHintInfo& dock);
    void addHiddenDockRow(const DockHintInfo& dock);
    int textWidth(const juce::Font& font, const juce::String& text) const;
    int capWidth(const juce::String& text, bool compact = false) const;
    juce::String keyTextFor(const juce::String& actionId) const;
    juce::String keyTextForBinding(const juce::KeyPress& binding) const;
    juce::String keyTextForTarget(const Target& target) const;
    juce::Rectangle<int> boundsInOverlay(const juce::Component& c) const;

    juce::Component& host_;
    ShortcutManager& shortcuts_;
    std::vector<Target> targets_;
    std::function<DockHintInfo()> dockSource_;
    std::function<double()> clock_;
    std::vector<Entry> entries_;

    State state_{State::Idle};
    HintModifier mode_{HintModifier::Cmd}; // the modifier of the current hold
    // False after a cancel until every hint modifier has been released, so one hold can never hint twice.
    bool armed_{true};
    double pressedAtMs_{0.0};
    float opacity_{0.0f};

    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver fade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutHintOverlay)
};

} // namespace synth::ui
