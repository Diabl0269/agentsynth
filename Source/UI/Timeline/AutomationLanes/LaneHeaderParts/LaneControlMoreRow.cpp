// Concern: the "+" row beside a lane header -- its labelled buttons, its slide open and shut, and how it closes.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlMoreRow.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kPadX = 4;
constexpr int kPadY = 3;
constexpr int kGapX = 6;
constexpr int kGapY = 4;
constexpr int kButtonPad = 8;
constexpr int kIconSize = 14;
constexpr int kIconGap = 4;
constexpr float kFontSize = 11.0f;
constexpr int kScreenMargin = 4;
} // namespace

LaneRowButton::LaneRowButton(std::optional<LaneControl> control, LaneControlKind kind)
    : juce::Button("laneRowButton")
    , control_(control)
    , kind_(kind) {
    label_ = control.has_value() ? laneControlName(*control, kind) : juce::String("Edit controls");
    setComponentID(control.has_value() ? juce::String("laneRowButton.") + laneControlInfo(*control).key
                                       : juce::String("laneRowButton.edit"));
    setTitle(label_);
    setTooltip(control.has_value() ? laneControlTooltip(*control, kind)
                                   : juce::String("Edit controls: choose which controls sit on this lane"));
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LaneRowButton::setDetail(const juce::String& detail) {
    if (control_.has_value())
        setTooltip(laneControlTooltip(*control_, kind_) + detail);
}

int LaneRowButton::getPreferredWidth() const {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(kFontSize)), label_, 0.0f, 0.0f);
    return 2 * kButtonPad + kIconSize + kIconGap + (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth());
}

void LaneRowButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colours = theme.colors;
    const auto bounds = getLocalBounds().toFloat();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour(colours.surfaceHi.withAlpha(highlighted || down ? 1.0f : 0.6f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour((control_.has_value() ? colours.border : colours.accent).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
    const auto icon = juce::Rectangle<float>((float)kButtonPad, bounds.getCentreY() - kIconSize / 2.0f,
                                             (float)kIconSize, (float)kIconSize);
    if (control_.has_value())
        paintLaneControlIcon(g, *control_, icon, laneControlColour(*control_, theme).withMultipliedAlpha(alpha));
    else
        paintLanePencilIcon(g, icon, colours.textPrimary);
    g.setColour(colours.textPrimary.withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions(kFontSize)));
    g.drawText(label_, getLocalBounds().withTrimmedLeft(kButtonPad + kIconSize + kIconGap),
               juce::Justification::centredLeft, false);
    synth::ui::paintFocusRing(g, bounds, *this, 5.0f);
}

bool LaneRowButton::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return false;
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return false;
}

LaneControlMoreRow::LaneControlMoreRow(LaneControlKind kind, juce::Component& opener)
    : kind_(kind)
    , opener_(opener)
    , updater_(this) {
    setComponentID("laneControlMoreRow");
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(true, true);
}

LaneControlMoreRow::~LaneControlMoreRow() {
    driver_.stop(updater_);
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void LaneControlMoreRow::setControls(const std::vector<LaneControl>& controls,
                                     const std::function<bool(LaneControl)>& isEnabled,
                                     const std::function<juce::String(LaneControl)>& detail) {
    buttons_.clear();
    removeAllChildren();
    const auto add = [this](std::optional<LaneControl> control) {
        auto button = std::make_unique<LaneRowButton>(control, kind_);
        auto* raw = button.get();
        button->onClick = [this, control] {
            close();
            if (onChosen)
                onChosen(control);
        };
        addAndMakeVisible(*raw);
        buttons_.push_back(std::move(button));
        return raw;
    };
    for (const auto control : controls) {
        auto* button = add(control);
        button->setEnabled(!isEnabled || isEnabled(control));
        if (detail)
            button->setDetail(detail(control));
    }
    add(std::nullopt);
}

// Flex-wrap: buttons fill a line until the next would pass the width, then wrap. Returns through the full size.
void LaneControlMoreRow::layoutButtons(int availableWidth) {
    const int maxWidth = juce::jmax(60, juce::jmin(kMaxWidth, availableWidth));
    int x = kPadX;
    int y = kPadY;
    int widest = 0;
    for (auto& button : buttons_) {
        const int w = juce::jmin(button->getPreferredWidth(), maxWidth - 2 * kPadX);
        if (x > kPadX && x + w > maxWidth - kPadX) {
            x = kPadX;
            y += LaneRowButton::kHeight + kGapY;
        }
        button->setBounds(x, y, w, LaneRowButton::kHeight);
        x += w + kGapX;
        widest = juce::jmax(widest, x - kGapX + kPadX);
    }
    fullWidth_ = juce::jmin(maxWidth, widest);
    fullHeight_ = y + LaneRowButton::kHeight + kPadY;
}

void LaneControlMoreRow::open(juce::Component& parent, juce::Point<int> anchor) {
    anchor_ = anchor;
    layoutButtons(parent.getWidth() - anchor.x - kScreenMargin);
    if (getParentComponent() != &parent)
        parent.addChildComponent(*this);
    open_ = true;
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    setVisible(true);
    toFront(false);
    startReveal(true);
}

void LaneControlMoreRow::reposition(juce::Point<int> anchor) {
    anchor_ = anchor;
    if (!open_ || getParentComponent() == nullptr)
        return;
    applyReveal(reveal_);
}

// The row's bounds are its revealed part: buttons are laid out for the full width and the shrunken bounds clip them,
// which reads as a wipe from the left. The top is clamped so the whole row stays inside the parent.
void LaneControlMoreRow::applyReveal(float t) {
    reveal_ = t;
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;
    const int y = juce::jlimit(0, juce::jmax(0, parent->getHeight() - fullHeight_), anchor_.y);
    const int x = juce::jmax(0, juce::jmin(anchor_.x, parent->getWidth() - fullWidth_ - kScreenMargin));
    const int width = juce::jmax(0, (int)std::lround((double)fullWidth_ * (double)t));
    setBounds(x, y, width, fullHeight_);
    setVisible(width > 0);
}

void LaneControlMoreRow::startReveal(bool opening) {
    const float from = reveal_;
    const float to = opening ? 1.0f : 0.0f;
    revealFrom_ = from;
    revealTo_ = to;
    if (!FadeVisibility::canAnimateIn(&opener_) || prefersReducedMotion()) {
        driver_.stop(updater_);
        applyReveal(to);
        if (!opening)
            finishClose();
        return;
    }
    driver_.start(
        updater_, motionMs(kOpenMs, kOpenMs), opening ? easeOutCubic : easeInCubic,
        [this, from, to](float t) { applyReveal(from + (to - from) * t); },
        [this, opening] {
            driver_.stop(updater_);
            if (!opening)
                finishClose();
        });
}

void LaneControlMoreRow::stepForTest(float t) {
    if (!driver_.isRunning())
        return;
    applyReveal(revealFrom_ + (revealTo_ - revealFrom_) * t);
    if (t < 1.0f)
        return;
    driver_.stop(updater_);
    if (!open_)
        finishClose();
}

void LaneControlMoreRow::close() {
    if (!open_)
        return;
    open_ = false;
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
    startReveal(false);
    if (onDismissed)
        onDismissed();
}

void LaneControlMoreRow::finishClose() {
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
}

void LaneControlMoreRow::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)fullWidth_, (float)fullHeight_);
    g.setColour(colours.bg1.withAlpha(0.94f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(colours.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

bool LaneControlMoreRow::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        close();
        opener_.grabKeyboardFocus();
        return true;
    }
    return false;
}

// Any press outside the row and the button that opened it closes the row.
void LaneControlMoreRow::mouseDown(const juce::MouseEvent& e) {
    if (!open_)
        return;
    const auto* hit = e.eventComponent;
    if (hit != nullptr && (hit == this || isParentOf(hit) || hit == &opener_ || opener_.isParentOf(hit)))
        return;
    close();
}

} // namespace synth::ui
