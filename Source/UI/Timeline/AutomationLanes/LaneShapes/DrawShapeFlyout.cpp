#include "DrawShapeFlyout.h"

#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"

namespace synth::ui {

// Concern: the flyout's rows, its keyboard and the pick/close path.

class DrawShapeFlyout::Row
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    Row(DrawShape shape, bool isCurrent, juce::String hint, std::function<void()> onPick, std::function<void()> onFocus)
        : shape_(shape)
        , current_(isCurrent)
        , hint_(std::move(hint))
        , onPick_(std::move(onPick))
        , onFocus_(std::move(onFocus)) {
        const juce::String title = juce::String(drawShapeName(shape)) + " shape";
        setComponentID("timelineShapeRow" + juce::String(drawShapeName(shape)));
        setTitle(title);
        setTooltip(formatShortcutHint(title, hint_));
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    const juce::String& getHint() const noexcept { return hint_; }
    float getHoverForTest() const noexcept { return hover_.value(); }

    void paint(juce::Graphics& g) override {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const auto ink = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
        const auto muted = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;
        const auto accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::orange;
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
        if (hover_.value() > 0.0f) {
            g.setColour(ink.withAlpha(0.08f * hover_.value()));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
        if (current_) {
            g.setColour(accent.withAlpha(0.22f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
        auto inner = getLocalBounds().reduced(10, 0);
        paintDrawShapeIcon(g, shape_, inner.removeFromLeft(20).toFloat().withSizeKeepingCentre(20.0f, 20.0f), ink);
        inner.removeFromLeft(8);
        const auto font = lf != nullptr
                              ? juce::Font(juce::FontOptions(lf->getTheme().type.uiFamily,
                                                             lf->getTheme().type.label + 1.0f, juce::Font::plain))
                              : juce::Font(juce::FontOptions(13.0f));
        g.setFont(font);
        g.setColour(ink);
        g.drawText(drawShapeName(shape_), inner, juce::Justification::centredLeft, false);
        g.setColour(muted);
        g.drawText(hint_, inner, juce::Justification::centredRight, false);
        paintFocusRing(g, getLocalBounds().toFloat().reduced(1.0f), *this, 4.0f);
    }

    void mouseEnter(const juce::MouseEvent&) override { hover_.setShown(true); }
    void mouseExit(const juce::MouseEvent&) override { hover_.setShown(false); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && e.getDistanceFromDragStart() < 6 && onPick_)
            onPick_();
    }
    void focusGained(FocusChangeType) override {
        if (onFocus_)
            onFocus_();
        repaint();
    }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    DrawShape shape_;
    bool current_;
    juce::String hint_;
    std::function<void()> onPick_;
    std::function<void()> onFocus_;
    FadeAmount hover_{*this};
};

DrawShapeFlyout::DrawShapeFlyout(DrawShape current, HintFn hintFor, std::function<void(DrawShape)> onPick)
    : current_(current)
    , focused_(current)
    , onPick_(std::move(onPick)) {
    setTitle("Draw shapes");
    setComponentID("timelineShapeFlyout");
    for (auto shape : kAllDrawShapes) {
        auto row = std::make_unique<Row>(
            shape, shape == current, hintFor ? hintFor(shape) : juce::String(), [this, shape] { pick(shape); },
            [this, shape] { focused_ = shape; });
        addAndMakeVisible(*row);
        rows_[(std::size_t)shape] = std::move(row);
    }
    setSize(kWidth, kRowHeight * (int)kAllDrawShapes.size());
}

DrawShapeFlyout::~DrawShapeFlyout() = default;

juce::Component* DrawShapeFlyout::getRow(DrawShape shape) const noexcept { return rows_[(std::size_t)shape].get(); }

juce::String DrawShapeFlyout::getRowTooltip(DrawShape shape) const { return rows_[(std::size_t)shape]->getTooltip(); }

void DrawShapeFlyout::resized() {
    auto area = getLocalBounds();
    for (auto shape : kAllDrawShapes)
        rows_[(std::size_t)shape]->setBounds(area.removeFromTop(kRowHeight));
}

// Closes first (the fade, then the real close a turn later; at once headless), then reports the pick, so the pick
// can never act on a half-closed popup. The callback is copied out because a synchronous close can free this.
void DrawShapeFlyout::pick(DrawShape shape) {
    auto callback = onPick_;
    closeHostingWindow(*this);
    if (callback)
        callback(shape);
}

bool DrawShapeFlyout::pickAtScreenPoint(juce::Point<int> screenPoint) {
    for (auto shape : kAllDrawShapes) {
        if (rows_[(std::size_t)shape]->getScreenBounds().contains(screenPoint)) {
            pick(shape);
            return true;
        }
    }
    return false;
}

void DrawShapeFlyout::moveFocus(int index) {
    const int count = (int)kAllDrawShapes.size();
    index = (index % count + count) % count;
    focused_ = kAllDrawShapes[(std::size_t)index];
    rows_[(std::size_t)index]->grabKeyboardFocus();
    repaint();
}

bool DrawShapeFlyout::keyPressed(const juce::KeyPress& key) {
    const int index = (int)focused_;
    if (key == juce::KeyPress::downKey)
        moveFocus(index + 1);
    else if (key == juce::KeyPress::upKey)
        moveFocus(index - 1);
    else if (key == juce::KeyPress::homeKey)
        moveFocus(0);
    else if (key == juce::KeyPress::endKey)
        moveFocus((int)kAllDrawShapes.size() - 1);
    else if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        pick(focused_);
    else if (key == juce::KeyPress::escapeKey)
        closeHostingWindow(*this);
    else
        return false;
    return true;
}

// Once the callout is showing, the reveal starts and keyboard focus lands on the current shape's row. It uses
// current_, not focused_: the callout first hands focus to the top row, which overwrites focused_.
void DrawShapeFlyout::parentHierarchyChanged() {
    reveal_.startIfInCallout();
    if (findParentComponentOfClass<juce::CallOutBox>() == nullptr)
        return;
    juce::Component::SafePointer<DrawShapeFlyout> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe != nullptr)
            safe->moveFocus((int)safe->current_);
    });
}

void DrawShapeCallOutBox::inputAttemptWhenModal() {
    if (holdGestureDown_ && holdGestureDown_())
        return;
    ++dismissAttempts_;
    PopupMotion::dismissCallOut(*this);
}

namespace {

// Owns the content and the box, and goes with the box's modal state (the same shape as juce's own launcher).
class DrawShapeCallOutLauncher final : public juce::ModalComponentManager::Callback {
public:
    DrawShapeCallOutLauncher(std::unique_ptr<DrawShapeFlyout> c, juce::Rectangle<int> area,
                             std::function<bool()> holdGestureDown)
        : content(std::move(c))
        , callout(*content, area, nullptr, std::move(holdGestureDown)) {
        callout.setVisible(true);
        callout.enterModalState(true, this);
    }

    void modalStateFinished(int) override {}

    std::unique_ptr<DrawShapeFlyout> content;
    DrawShapeCallOutBox callout;
};

} // namespace

DrawShapeCallOutBox& launchDrawShapeCallOut(std::unique_ptr<DrawShapeFlyout> flyout, juce::Rectangle<int> area,
                                            std::function<bool()> holdGestureDown) {
    return (new DrawShapeCallOutLauncher(std::move(flyout), area, std::move(holdGestureDown)))->callout;
}

namespace test_hooks {
std::function<void(std::unique_ptr<DrawShapeFlyout>)>& drawShapeFlyoutHookForTest() {
    static std::function<void(std::unique_ptr<DrawShapeFlyout>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
