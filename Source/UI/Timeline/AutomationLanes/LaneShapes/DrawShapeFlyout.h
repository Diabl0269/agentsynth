#pragma once

#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/FadeAmount.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The pen's shape flyout: a vertical list of the six Draw shapes (icon, name, shortcut hint) in a juce::CallOutBox
// under the Draw button. The current shape's row is lit. Click, Enter or Space picks a row; Up/Down (wrapping),
// Home/End move; Escape closes. Closing goes through PopupMotion::dismissCallOut. Message thread only.
class DrawShapeFlyout : public juce::Component {
public:
    static constexpr int kRowHeight = 30;
    static constexpr int kWidth = 200;

    using HintFn = std::function<juce::String(DrawShape)>;

    /** `hintFor` gives a row's shortcut text ("Shift+3", or empty); `onPick` runs after the flyout has closed. */
    DrawShapeFlyout(DrawShape current, HintFn hintFor, std::function<void(DrawShape)> onPick);
    ~DrawShapeFlyout() override;

    /** Picks `shape`: closes the flyout, then reports it. */
    void pick(DrawShape shape);

    DrawShape getCurrentShape() const noexcept { return current_; }
    /** The row that holds (or would hold, off screen) keyboard focus. */
    DrawShape getFocusedShape() const noexcept { return focused_; }
    juce::Component* getRow(DrawShape shape) const noexcept;
    juce::String getRowTooltip(DrawShape shape) const;
    /** Picks the row under a screen point (a press-hold-release that ends on the list). False when none is there. */
    bool pickAtScreenPoint(juce::Point<int> screenPoint);

    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void resized() override;

private:
    class Row;
    void moveFocus(int index);

    DrawShape current_;
    DrawShape focused_;
    std::function<void(DrawShape)> onPick_;
    CalloutReveal reveal_{*this};
    std::array<std::unique_ptr<Row>, kAllDrawShapes.size()> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawShapeFlyout)
};

// The CallOutBox the flyout opens in. While `holdGestureDown` says the press that opened it (a long press on the
// pen) is still down, input outside the box is not a dismissal: the release that ends the press must not close it.
// Once the press is up, input outside closes it through PopupMotion::dismissCallOut.
class DrawShapeCallOutBox : public juce::CallOutBox {
public:
    DrawShapeCallOutBox(juce::Component& content, juce::Rectangle<int> area, juce::Component* parent,
                        std::function<bool()> holdGestureDown)
        : juce::CallOutBox(content, area, parent)
        , holdGestureDown_(std::move(holdGestureDown)) {}

    void inputAttemptWhenModal() override;
    int getDismissAttemptsForTest() const noexcept { return dismissAttempts_; }

private:
    std::function<bool()> holdGestureDown_;
    int dismissAttempts_ = 0;
};

/** Opens `flyout` modally in a DrawShapeCallOutBox pointing at `area` (screen), the way juce::CallOutBox's own
 *  launchAsynchronously does; the box and the content go when the modal state ends. */
DrawShapeCallOutBox& launchDrawShapeCallOut(std::unique_ptr<DrawShapeFlyout> flyout, juce::Rectangle<int> area,
                                            std::function<bool()> holdGestureDown);

namespace test_hooks {
/** When set, opening the pen's flyout hands the content here instead of launching a CallOutBox (which cannot open
 *  in a headless test). The test keeps the pointer alive. */
std::function<void(std::unique_ptr<DrawShapeFlyout>)>& drawShapeFlyoutHookForTest();
} // namespace test_hooks

} // namespace synth::ui
