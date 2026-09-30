#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// SidePane.h (docs/layout/side-pane.md): the reusable left side pane of a bottom-panel tab. A panel
// owns one SidePane as a child (so it travels with the panel into a detached window or the Own-panel
// strip), hands it a SidePaneContent, and lays it out at the left of its body using
// getOccupiedWidth(). Open/closed and width persist per tab in the user settings.
namespace synth::ui {

/** What a tab shows in its side pane. */
class SidePaneContent {
public:
    virtual ~SidePaneContent() = default;

    /** The component placed inside the pane; it must outlive the pane's use of it. */
    virtual juce::Component& getPaneComponent() = 0;
    /** The pane's accessible name. */
    virtual juce::String getPaneTitle() const = 0;
};

class SidePane
    : public juce::Component
    , public juce::ChangeBroadcaster {
public:
    static constexpr int kDefaultWidth = 200;
    static constexpr int kMinWidth = 160;
    static constexpr int kMaxWidth = 320;
    static constexpr int kGrabWidth = 6;
    static constexpr double kOpenMs = 160.0;
    static constexpr double kCloseMs = 110.0;

    SidePane();
    ~SidePane() override;

    /** Shows `content` (not owned) in the pane, or removes it when null. Without content the pane
     *  occupies no width and its toggle button hides. Message thread only. */
    void setContent(SidePaneContent* content);
    bool hasContent() const noexcept { return content_ != nullptr; }

    /** Loads (and from now on saves) open state and width under `tabKey` in `settings` (nullable, must
     *  outlive the pane). A tab with nothing stored opens at kDefaultWidth, open when `defaultOpen` (the
     *  Mixer) and closed otherwise (the Timeline's routing pane, which is opt-in). */
    void setPersistence(juce::PropertiesFile* settings, const juce::String& tabKey, bool defaultOpen = true);

    bool isOpen() const noexcept { return open_; }
    /** Opens or closes with the width tween (immediately when the pane is not on screen). */
    void setOpen(bool open);
    void toggle() { setOpen(!open_); }

    /** The pane's full width when open, clamped to kMinWidth..kMaxWidth; persisted. */
    int getContentWidth() const noexcept { return width_; }
    void setContentWidth(int width);

    /** The width the owner should give this component right now: the tween's current width, 0 when
     *  closed or without content. */
    int getOccupiedWidth() const noexcept;
    /** True from the start of an open/close tween until it lands. */
    bool isAnimating() const noexcept { return animating_; }

    /** Fired whenever getOccupiedWidth() changes; the owner lays out again. */
    std::function<void()> onOccupiedWidthChanged;

    /** How long a tween over `distance` (0..1 of the full travel) takes: the nominal open or close duration
     *  scaled by the distance left, never under 1 ms. */
    static double tweenDurationMs(bool opening, float distance) noexcept;

    static juce::String openKeyFor(const juce::String& tabKey) { return "sidePaneOpen." + tabKey; }
    static juce::String widthKeyFor(const juce::String& tabKey) { return "sidePaneWidth." + tabKey; }

    /** The right-edge grab strip; a test drives it with hand-built mouse events. */
    juce::Component& getGrabEdgeForTest() noexcept { return edge_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class GrabEdge : public juce::Component {
    public:
        explicit GrabEdge(SidePane& owner);
        void paint(juce::Graphics& g) override;
        void mouseEnter(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

    private:
        SidePane& owner_;
        bool hovered_ = false;
    };

    void animateTo(float target);
    void applyFraction(float fraction);
    void persist();

    SidePaneContent* content_ = nullptr;
    juce::PropertiesFile* settings_ = nullptr;
    juce::String tabKey_;
    bool open_ = true;
    int width_ = kDefaultWidth;
    float fraction_ = 1.0f;
    bool animating_ = false;
    int pressWidth_ = kDefaultWidth;
    float pressX_ = 0.0f;
    bool widthDragged_ = false;

    GrabEdge edge_{*this};
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver driver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidePane)
};

} // namespace synth::ui
