#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth {

/**
 * The content of a HostedPluginEditorWindow: the plugin's own editor, with a thin strip above it that holds
 * the "Adding controls" tab while the card's "add by moving a control" mode is on
 * (docs/control/plugin-card-layout.md#add-by-moving-a-control-in-the-plugin). The strip grows the window
 * instead of covering the plugin's controls: the editor keeps its size, sits below the strip, and the frame
 * follows whatever size the editor asks for. The tab slides down from the window's top edge with a small bounce
 * and slides back up when the mode ends; at once when not on screen or under Reduce Motion.
 * Message thread only.
 */
class HostedPluginEditorFrame final : public juce::Component {
public:
    static constexpr int kTabHeight = 30;

    /** Takes `inner` (the plugin's editor, or the neutral placeholder) and sizes itself to it. */
    explicit HostedPluginEditorFrame(std::unique_ptr<juce::Component> inner);
    ~HostedPluginEditorFrame() override;

    juce::Component& inner() noexcept { return *inner_; }
    const juce::Component& inner() const noexcept { return *inner_; }

    /** Slides the tab in (or out); idempotent. */
    void setAddingControls(bool on);
    bool isAddingControls() const noexcept { return adding_; }
    /** The strip's height right now, 0 when the tab is away. */
    int getStripHeight() const noexcept { return strip_; }
    /** The tab's Done button; null-safe for tests that drive it. */
    juce::Button& doneButton() noexcept;
    bool hasTabForTest() const noexcept;

    /** Done was pressed on the tab. The owner ends the mode; the frame does not slide out by itself. */
    std::function<void()> onDone;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void childBoundsChanged(juce::Component* child) override;

private:
    class Tab;
    void setStrip(int pixels);
    void animateTo(float target);

    std::unique_ptr<juce::Component> inner_;
    std::unique_ptr<Tab> tab_;
    juce::VBlankAnimatorUpdater updater_;
    synth::ui::AnimationDriver anim_;
    float amount_ = 0.0f; // 0 = tab away, 1 = fully shown; the open bounce goes a little past 1
    bool adding_ = false;
    bool laidOut_ = false;
    int strip_ = 0;
    bool layingOut_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginEditorFrame)
};

} // namespace synth
