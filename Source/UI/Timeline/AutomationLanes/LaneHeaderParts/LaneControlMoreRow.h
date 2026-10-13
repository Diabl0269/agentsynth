#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

// One labelled button of the "+" row: the control's picture and name. A Tab stop with the accent ring; Return and Space
// press it. `control` empty is the "Edit controls" button, which has an accent border.
class LaneRowButton : public juce::Button {
public:
    LaneRowButton(std::optional<LaneControl> control, LaneControlKind kind);

    std::optional<LaneControl> getControl() const noexcept { return control_; }
    int getPreferredWidth() const;
    void setDetail(const juce::String& detail);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int kHeight = 22;

private:
    std::optional<LaneControl> control_;
    LaneControlKind kind_;
    juce::String label_;
};

// The inline row that opens beside a lane header when "+" is pressed: a labelled button for every control the header
// does not show, in catalogue order, ending with "Edit controls". It floats over the lanes area (a child of the
// timeline), is clamped inside it, slides open left to right in 180 ms (at once under Reduce Motion) and closes on a
// click anywhere else, on Esc or when a button is pressed. It is created once per lane and only shown and hidden.
class LaneControlMoreRow : public juce::Component {
public:
    static constexpr int kOpenMs = 180;
    static constexpr int kMaxWidth = 470;

    LaneControlMoreRow(LaneControlKind kind, juce::Component& opener);
    ~LaneControlMoreRow() override;

    /** Rebuilds the buttons: one per entry of `controls` (a disabled one stays, dimmed), then "Edit controls". */
    void setControls(const std::vector<LaneControl>& controls, const std::function<bool(LaneControl)>& isEnabled,
                     const std::function<juce::String(LaneControl)>& detail);
    /** A button pressed: the row has already closed. */
    std::function<void(std::optional<LaneControl>)> onChosen;
    /** The row closed itself (Esc, a click away, a button). */
    std::function<void()> onDismissed;

    /** Adds the row to `parent` and slides it open with its left edge at `anchor` (parent coordinates), clamped
     *  inside `parent`. */
    void open(juce::Component& parent, juce::Point<int> anchor);
    void close();
    bool isOpen() const noexcept { return open_; }
    /** Re-clamps after the opener moved. */
    void reposition(juce::Point<int> anchor);
    /** The buttons in order. */
    const std::vector<std::unique_ptr<LaneRowButton>>& getButtons() const noexcept { return buttons_; }
    /** The width the row shows at full reveal. */
    int getFullWidth() const noexcept { return fullWidth_; }
    /** Steps the reveal by hand (t in 0..1; 1 lands it). */
    void stepForTest(float t);
    bool isAnimating() const noexcept { return driver_.isRunning(); }

    void paint(juce::Graphics& g) override;
    void resized() override {}
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void layoutButtons(int availableWidth);
    void applyReveal(float t);
    void finishClose();
    void startReveal(bool opening);

    LaneControlKind kind_;
    juce::Component& opener_;
    std::vector<std::unique_ptr<LaneRowButton>> buttons_;
    bool open_ = false;
    int fullWidth_ = 0;
    int fullHeight_ = 0;
    juce::Point<int> anchor_;
    float reveal_ = 0.0f;
    float revealFrom_ = 0.0f;
    float revealTo_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
};

} // namespace synth::ui
