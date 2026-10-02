#include "DrawShapeStrip.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

// Concern: the shape strip's buttons, their stroke-path icons and the slide that shows and hides them.

namespace {

constexpr int kShapeRadioGroupId = 4301;

// Each icon is a stroke in a 24 x 24 box. The two empty sub-paths pin every icon's bounds to the whole
// box, so the button scales all six by the same amount; butt caps keep those empty sub-paths invisible.
juce::Path iconPathFor(DrawShape shape) {
    juce::Path p;
    auto polyline = [&p](std::initializer_list<juce::Point<float>> pts) {
        bool first = true;
        for (auto pt : pts) {
            if (first)
                p.startNewSubPath(pt);
            else
                p.lineTo(pt);
            first = false;
        }
    };
    switch (shape) {
    case DrawShape::Free:
        p.startNewSubPath(3.0f, 15.0f);
        p.cubicTo(6.0f, 4.0f, 9.0f, 4.0f, 11.0f, 12.0f);
        p.cubicTo(13.0f, 20.0f, 17.0f, 20.0f, 21.0f, 8.0f);
        break;
    case DrawShape::Line:
        polyline({{4.0f, 19.0f}, {20.0f, 5.0f}});
        break;
    case DrawShape::Sine:
        for (int i = 0; i <= 36; ++i) {
            const float x = 3.0f + 18.0f * (float)i / 36.0f;
            const float y = 12.0f - 6.0f * std::sin(6.2831853f * (float)i / 36.0f);
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        break;
    case DrawShape::Triangle:
        polyline({{3.0f, 17.0f}, {7.5f, 7.0f}, {12.0f, 17.0f}, {16.5f, 7.0f}, {21.0f, 17.0f}});
        break;
    case DrawShape::Saw:
        polyline({{3.0f, 17.0f}, {11.0f, 7.0f}, {11.0f, 17.0f}, {19.0f, 7.0f}, {19.0f, 17.0f}});
        break;
    case DrawShape::Square:
        polyline(
            {{3.0f, 17.0f}, {3.0f, 7.0f}, {9.0f, 7.0f}, {9.0f, 17.0f}, {15.0f, 17.0f}, {15.0f, 7.0f}, {21.0f, 7.0f}});
        break;
    }
    p.startNewSubPath(0.0f, 0.0f);
    p.startNewSubPath(24.0f, 24.0f);
    return p;
}

std::unique_ptr<juce::Drawable> iconFor(DrawShape shape, juce::Colour colour) {
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath(iconPathFor(shape));
    d->setFill(juce::FillType(juce::Colours::transparentBlack));
    d->setStrokeType(juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    d->setStrokeFill(juce::FillType(colour));
    return d;
}

} // namespace

// The buttons follow the edit-tool buttons' focus convention: never focused, never grabbing focus on a
// click, so picking a shape leaves the keyboard on the lane being edited. Their keyboard path is the
// Shift+digit shortcut each tooltip names (set by the panel, which owns the bindings).
DrawShapeStrip::DrawShapeStrip(juce::Component& animationHost)
    : animationHost_(animationHost)
    , vblank_(&animationHost) {
    setComponentID("timelineShapeStrip");
    setInterceptsMouseClicks(false, true);
    for (auto shape : kAllDrawShapes) {
        const auto title = juce::String(drawShapeName(shape)) + " shape";
        auto button = std::make_unique<juce::DrawableButton>(title, juce::DrawableButton::ImageOnButtonBackground);
        button->setComponentID("timelineShape" + juce::String(drawShapeName(shape)));
        button->setTitle(title);
        button->setTooltip(title);
        button->setClickingTogglesState(true);
        button->setRadioGroupId(kShapeRadioGroupId);
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
        button->onClick = [this, shape] {
            if (onShapeClicked)
                onShapeClicked(shape);
        };
        addAndMakeVisible(*button);
        buttons_[(std::size_t)shape] = std::move(button);
    }
    setActiveShape(DrawShape::Free);
    applyTheme(nullptr);
    setVisible(false);
}

DrawShapeStrip::~DrawShapeStrip() { slideAnim_.stop(vblank_); }

juce::DrawableButton* DrawShapeStrip::getButton(DrawShape shape) const noexcept {
    return buttons_[(std::size_t)shape].get();
}

void DrawShapeStrip::setActiveShape(DrawShape shape) {
    for (auto candidate : kAllDrawShapes)
        buttons_[(std::size_t)candidate]->setToggleState(candidate == shape, juce::dontSendNotification);
}

void DrawShapeStrip::applyTheme(juce::LookAndFeel* lookAndFeel) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(lookAndFeel);
    const auto ink = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    for (auto shape : kAllDrawShapes) {
        auto& button = *buttons_[(std::size_t)shape];
        button.setImages(iconFor(shape, ink).get());
        if (lf != nullptr)
            button.setColour(juce::DrawableButton::backgroundOnColourId, lf->getTheme().colors.toolActive);
    }
}

// Motion rules: a small reveal arrives in 160 ms easeOutCubic and leaves in 110 ms easeInCubic, and a
// reversal mid-flight starts from where the strip is now (PanelSlide::retarget). Off screen there is no
// VBlank to wait for, so it lands at once.
void DrawShapeStrip::setShowing(bool showing) {
    const float target = showing ? 1.0f : 0.0f;
    if (slide_.getTarget() == target && !slide_.isMoving() && slide_.getProgress() == target)
        return;
    if (!slide_.retarget(target, animationHost_.isShowing())) {
        slideAnim_.stop(vblank_);
        slideFrame();
        return;
    }
    slideFrame();
    slideAnim_.start(
        vblank_, showing ? kSlideInMs : kSlideOutMs, showing ? easeOutCubic : easeInCubic,
        [this](float t) {
            slide_.applyTweenAt(t);
            slideFrame();
        },
        [this] {
            slide_.finish();
            slideFrame();
        });
}

// Hidden at rest when closed, so a zero-width strip is neither a hint target nor in the accessibility tree.
void DrawShapeStrip::slideFrame() {
    setVisible(slide_.getProgress() > 0.0f);
    if (onSlideFrame)
        onSlideFrame();
}

// The buttons ride on the strip's right edge: as the strip widens they move out from behind the Draw
// button to its left, rather than being uncovered in place.
void DrawShapeStrip::resized() {
    int x = getWidth() - getOpenWidth();
    for (auto shape : kAllDrawShapes) {
        buttons_[(std::size_t)shape]->setBounds(juce::Rectangle<int>(x, 0, kButtonWidth, getHeight()).reduced(2));
        x += kButtonWidth;
    }
}

} // namespace synth::ui
