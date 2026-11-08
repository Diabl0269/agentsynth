#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/**
 * A small message pill with one optional action ("Made 4 modules poly" + Undo), shown over the bottom of the canvas
 * and gone by itself (docs/layout/chrome.md#toast). It slides up 10 px while it fades in (170 ms, `easeOutCubic`) and
 * fades out (130 ms); a fade only under Reduce Motion, nothing animated under Animations Off or while the parent is not
 * on screen. It hides itself ~4 s after the last show() unless the pointer is over it or its action holds the
 * keyboard focus. The action is a real focusable button with a tooltip; it dismisses the toast, then runs.
 *
 * One toast at a time: a second show() replaces the first in place. Message thread only. The parent decides where it
 * sits (placeIn) and must keep it on top of its siblings (show() raises it).
 */
class ToastComponent
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr int kAutoHideMs = 4000;
    static constexpr double kEnterMs = 170.0;
    static constexpr double kExitMs = 130.0;
    static constexpr double kReducedMs = 80.0;
    static constexpr int kSlidePx = 10;
    static constexpr int kHeight = 36;
    static constexpr int kBottomMargin = 20;
    /** The severity icon's square and the gap after it; together the room the text gives up. */
    static constexpr int kIconSize = 14;
    static constexpr int kIconGap = 8;
    static constexpr int kIconSlot = kIconSize + kIconGap;

    ToastComponent();
    ~ToastComponent() override;

    /** How the toast reads: info is the plain pill; warning and error add a bar and border in that theme colour. */
    enum class Severity { info, warning, error };

    /** Shows `message`; `actionLabel` empty means no action button. `actionTooltip` is the button's tooltip. */
    void show(const juce::String& message, const juce::String& actionLabel = {}, std::function<void()> action = {},
              const juce::String& actionTooltip = {}, Severity severity = Severity::info);
    /** Fades out and hides. A no-op when not shown. */
    void dismiss();
    /** Hides at once, with no animation. */
    void dismissNow();

    /** Centres the toast horizontally in `area` (the parent's coordinates), `kBottomMargin` above its bottom edge. */
    void placeIn(juce::Rectangle<int> area);

    bool isToastShown() const noexcept { return shown_; }
    juce::Button& getActionButton() noexcept { return action_; }
    const juce::String& getMessageForTest() const noexcept { return message_; }
    Severity getSeverityForTest() const noexcept { return severity_; }
    juce::Rectangle<int> getIconBoundsForTest() const { return iconBounds(); }
    juce::Rectangle<int> getTextBoundsForTest() const { return textBounds(); }
    /** The 0..1 shown amount as the animation has it (1 once landed). */
    float getProgressForTest() const noexcept { return progress_; }
    /** Runs the auto-hide as if its time had elapsed. */
    void expireForTest() { timerCallback(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class StateProbe;
    juce::Rectangle<int> iconBounds() const;
    juce::Rectangle<int> textBounds() const;
    void timerCallback() override;
    int preferredWidth() const;
    void applyProgress(float progress);
    void animateTo(bool shown);
    juce::Rectangle<int> restingBounds() const;

    std::unique_ptr<juce::Component> stateProbe_; // test mode reads severity and message from it
    juce::String message_;
    Severity severity_ = Severity::info;
    TextLinkButton action_{"Undo", juce::Justification::centred};
    std::function<void()> onAction_;
    juce::Rectangle<int> area_;
    bool shown_ = false;
    float progress_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver driver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToastComponent)
};

} // namespace synth::ui
