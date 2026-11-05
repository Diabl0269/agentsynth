// CardStepper.cpp -- the number field: its painting (a bar with a range fill, chevron arrows, the value centred),
// the click / hold / drag / type / reset gestures with their shared numbers from CardControlGestures, the keys, the
// in-place editor and the accessibility value.
#include "CardStepper.h"
#include "CardControlGestures.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kCompactHeight = 22;       // under this the field is the drum strip's compact one
constexpr int kArrowWidth = 24;          // an arrow's click target at the card height
constexpr int kCompactArrowWidth = 16;   // and at the compact height
constexpr int kVerticalArrowHeight = 14; // above and below the number in a tall cell
constexpr int kValuePadding = 8;         // each side of the widest value
constexpr float kChevron = 4.0f;         // half the chevron's span
constexpr float kFillAlpha = 0.14f;
constexpr float kDragFillAlpha = 0.28f;
constexpr float kCapHeightRatio = 0.72f; // digits sit this far above the baseline, as a share of the font height
constexpr float kLimitedArrowAlpha = 0.55f;

juce::Font valueFont(const juce::Component& comp, bool compact) {
    const auto& theme = synth::theme::themeOf(comp);
    return juce::Font(juce::FontOptions(theme.type.monoFamily, compact ? theme.type.value : theme.type.value + 1.0f,
                                        juce::Font::plain));
}

} // namespace

// What a screen reader reads after the name: the value as the parameter's own text ("24%", "C3"). Setting it
// types the value, as the double-click does.
class CardStepper::ValueInterface : public juce::AccessibilityTextValueInterface {
public:
    explicit ValueInterface(CardStepper& owner)
        : owner_(owner) {}
    bool isReadOnly() const override { return !owner_.isEnabled(); }
    juce::String getCurrentValueAsString() const override { return owner_.getValueText(); }
    void setValueAsString(const juce::String& text) override {
        if (owner_.onTypedValue)
            owner_.onTypedValue(text);
    }

private:
    CardStepper& owner_;
};

// Test mode: a hidden 1x1 child (id "<name> state") reports `value`, `valueText`, `editing`, `editorText`, `repeating`
// and the centre of each zone, so a scenario can read the field it drives.
class CardStepper::StateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit StateProbe(const CardStepper& owner)
        : owner_(owner) {
        setComponentID(owner.getTitle() + " state");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        into.setProperty("value", owner_.getValue());
        into.setProperty("valueText", owner_.getValueText());
        into.setProperty("editing", owner_.isEditing());
        into.setProperty("editorText", owner_.isEditing() ? owner_.editor_.getText() : juce::String());
        into.setProperty("repeating", owner_.isRepeating());
        into.setProperty("downX", owner_.zoneBounds(Zone::down).getCentreX());
        into.setProperty("numberX", owner_.zoneBounds(Zone::number).getCentreX());
        into.setProperty("upX", owner_.zoneBounds(Zone::up).getCentreX());
    }

private:
    const CardStepper& owner_;
};

CardStepper::CardStepper(const juce::String& name)
    : name_(name) {
    setTitle(name);
    probe_ = std::make_unique<StateProbe>(*this);
    addChildComponent(*probe_);
    probe_->setBounds(0, 0, 1, 1);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    editor_.setMultiLine(false);
    editor_.setJustification(juce::Justification::centred);
    editor_.setSelectAllWhenFocused(true);
    editor_.setTitle(name + " value");
    editor_.setIndents(0, 0);
    editor_.setBorder({});
    editor_.setScrollbarsShown(false);
    editor_.onReturnKey = [this] { commitEditor(); };
    editor_.onEscapeKey = [this] { closeEditor(); };
    editor_.onFocusLost = [this] { commitEditor(); };
    addChildComponent(editor_);
}

CardStepper::~CardStepper() { stopTimer(); }

void CardStepper::setTypable(bool typable) { typable_ = typable; }

void CardStepper::setOrientation(Orientation orientation) {
    orientation_ = orientation;
    resized();
    repaint();
}

void CardStepper::setRange(int minimum, int maximum, int defaultValue) {
    ranged_ = maximum > minimum;
    min_ = minimum;
    max_ = std::max(minimum, maximum);
    default_ = juce::jlimit(min_, max_, defaultValue);
    value_ = juce::jlimit(min_, max_, value_);
    repaint();
}

void CardStepper::setValue(int value) {
    value = ranged_ ? juce::jlimit(min_, max_, value) : value;
    if (value != value_)
        blocked_ = false;
    value_ = value;
    repaint();
}

void CardStepper::setValueText(const juce::String& text) {
    valueText_ = text;
    repaint();
}

void CardStepper::setWidestValueText(const juce::String& text) { widestText_ = text; }

int CardStepper::getIdealWidth(int forHeight) const {
    const bool compact = (forHeight > 0 ? forHeight : getHeight()) < kCompactHeight;
    const auto font = valueFont(*this, compact);
    const auto& text = widestText_.isNotEmpty() ? widestText_ : valueText_;
    // The value face is monospaced (0.6 em per glyph), so the width follows the character count, not a measurement
    // that changes with whichever mono font the platform falls back to: the card is the same size everywhere.
    const int textWidth = text.length() * (int)std::ceil(0.6f * font.getHeight());
    return 2 * (compact ? kCompactArrowWidth : kArrowWidth) + textWidth + 2 * kValuePadding;
}

int CardStepper::arrowSize() const {
    if (orientation_ == Orientation::vertical)
        return std::min(kVerticalArrowHeight, getHeight() / 3);
    return getHeight() < kCompactHeight ? kCompactArrowWidth : kArrowWidth;
}

float CardStepper::cornerRadius() const { return getHeight() < kCompactHeight ? 4.0f : 5.0f; }

juce::Rectangle<int> CardStepper::zoneBounds(Zone zone) const {
    auto area = getLocalBounds();
    const int arrow = arrowSize();
    if (orientation_ == Orientation::vertical) {
        auto top = area.removeFromTop(arrow);
        auto bottom = area.removeFromBottom(arrow);
        return zone == Zone::up ? top : zone == Zone::down ? bottom : area;
    }
    auto left = area.removeFromLeft(arrow);
    auto right = area.removeFromRight(arrow);
    return zone == Zone::down ? left : zone == Zone::up ? right : area;
}

CardStepper::Zone CardStepper::zoneAt(juce::Point<int> local) const {
    if (!getLocalBounds().contains(local))
        return Zone::none;
    for (const auto zone : {Zone::down, Zone::up})
        if (zoneBounds(zone).contains(local))
            return zone;
    return Zone::number;
}

bool CardStepper::atLimit(Zone arrow) const {
    return ranged_ && ((arrow == Zone::up && value_ >= max_) || (arrow == Zone::down && value_ <= min_));
}

void CardStepper::resized() {
    editor_.setBounds(zoneBounds(Zone::number).reduced(2, 1));
    editor_.setFont(valueFont(*this, getHeight() < kCompactHeight));
}

//==============================================================================
// Painting

void CardStepper::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& c = theme.colors;
    const bool compact = getHeight() < kCompactHeight;
    const bool vertical = orientation_ == Orientation::vertical;
    const float radius = cornerRadius();
    const auto bar = getLocalBounds().toFloat().reduced(0.5f);
    const bool hovered = hoverZone_ != Zone::none && isEnabled();

    if (!isEnabled())
        g.beginTransparencyLayer(synth::theme::AppLookAndFeel::kDisabledControlAlpha);
    g.setColour(editing_ ? c.bg0 : hovered ? c.surface : c.bg1);
    g.fillRoundedRectangle(bar, radius);

    if (!editing_ && ranged_) {
        const float share = (float)(value_ - min_) / (float)(max_ - min_);
        auto fill = bar;
        if (vertical)
            fill = fill.withTop(bar.getBottom() - bar.getHeight() * share);
        else
            fill = fill.withWidth(bar.getWidth() * share);
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(bar, radius);
        g.reduceClipRegion(clip);
        g.setColour(c.accent.withAlpha(dragging_ ? kDragFillAlpha : kFillAlpha));
        g.fillRect(fill);
        g.restoreState();
    }
    if (pressedZone_ == Zone::up || pressedZone_ == Zone::down) {
        g.setColour(c.accent.withAlpha(kFillAlpha));
        g.fillRoundedRectangle(zoneBounds(pressedZone_).toFloat().reduced(1.0f), radius - 1.0f);
    }
    g.setColour(editing_ || dragging_ ? c.accent : c.border);
    g.drawRoundedRectangle(bar, radius, 1.0f);

    if (!editing_) {
        for (const auto zone : {Zone::down, Zone::up}) {
            auto colour = hoverZone_ == zone || pressedZone_ == zone ? c.accent : hovered ? c.textPrimary : c.textMuted;
            if (atLimit(zone))
                colour = c.textDisabled.withMultipliedAlpha(kLimitedArrowAlpha);
            g.setColour(colour);
            const auto at = zoneBounds(zone).toFloat().getCentre();
            juce::Path chevron;
            if (vertical) {
                const float dir = zone == Zone::up ? -1.0f : 1.0f; // an up arrow sits above the number
                chevron.startNewSubPath(at.x - kChevron, at.y - dir * kChevron * 0.5f);
                chevron.lineTo(at.x, at.y + dir * kChevron * 0.5f);
                chevron.lineTo(at.x + kChevron, at.y - dir * kChevron * 0.5f);
            } else {
                const float dir = zone == Zone::up ? 1.0f : -1.0f; // "<" lowers, ">" raises
                chevron.startNewSubPath(at.x - dir * kChevron * 0.5f, at.y - kChevron);
                chevron.lineTo(at.x + dir * kChevron * 0.5f, at.y);
                chevron.lineTo(at.x - dir * kChevron * 0.5f, at.y + kChevron);
            }
            g.strokePath(chevron,
                         juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // The value sits on the middle of the digits, not the middle of the font's box, so it centres optically.
        const auto font = valueFont(*this, compact);
        g.setFont(font);
        g.setColour(blocked_ ? c.warning : dragging_ ? c.accent : c.textPrimary);
        const auto number = zoneBounds(Zone::number).toFloat();
        const float baseline = number.getCentreY() + font.getHeight() * kCapHeightRatio * 0.5f;
        g.drawSingleLineText(valueText_, juce::roundToInt(number.getCentreX()), juce::roundToInt(baseline),
                             juce::Justification::horizontallyCentred);
    }
    if (!isEnabled())
        g.endTransparencyLayer();
}

void CardStepper::paintOverChildren(juce::Graphics& g) {
    if (hasKeyboardFocus(false) || showsFocusRing(*this))
        paintFocusRingAlways(g, getLocalBounds().toFloat(), *this, cornerRadius());
}

juce::MouseCursor CardStepper::getMouseCursor() {
    return zoneAt(getMouseXYRelative()) == Zone::number && isEnabled() ? juce::MouseCursor::UpDownResizeCursor
                                                                       : juce::MouseCursor::NormalCursor;
}

juce::String CardStepper::getTooltip() {
    const auto own = SettableTooltipClient::getTooltip();
    if (own.isNotEmpty())
        return own;
    const bool vertical = orientation_ == Orientation::vertical;
    auto tip = name_ + ": click " + (vertical ? "above or below" : "an arrow") +
               " or press Up and Down to step, hold to repeat, drag the number (Shift for fine)";
    if (typable_)
        tip += ", double-click to type";
    if (ranged_)
        tip += ", Cmd-click to reset";
    return tip;
}

//==============================================================================
// Steps and gestures

void CardStepper::beginGesture() {
    if (gestureOpen_)
        return;
    gestureOpen_ = true;
    if (onGestureBegin)
        onGestureBegin();
}

void CardStepper::endGesture() {
    if (!gestureOpen_)
        return;
    gestureOpen_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

// One step inside a gesture: clamped to the range, so the owner only hears of a step that moves the value. False
// when the limit stopped it (the number turns amber until the value moves again).
bool CardStepper::applyStep(int delta) {
    if (!isEnabled() || !onStep || delta == 0)
        return false;
    if (ranged_) {
        const int next = juce::jlimit(min_, max_, value_ + delta);
        if (next == value_) {
            blocked_ = true;
            repaint();
            return false;
        }
        delta = next - value_;
        value_ = next;
        blocked_ = false;
    }
    beginGesture();
    onStep(delta);
    repaint();
    return true;
}

// A step from outside a mouse gesture (a key, an accessibility action): its own gesture.
void CardStepper::step(int delta) {
    const bool wasOpen = gestureOpen_;
    applyStep(delta);
    if (!wasOpen)
        endGesture();
}

void CardStepper::startHold(Zone zone) {
    holdZone_ = zone;
    holdStartMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimer(CardControlGestures::kHoldPauseMs);
}

void CardStepper::stopHold() {
    stopTimer();
    holdZone_ = Zone::none;
}

void CardStepper::timerCallback() {
    if (holdZone_ == Zone::none || !isEnabled() || !isVisible()) {
        stopHold();
        endGesture();
        return;
    }
    const int held = juce::roundToInt(juce::Time::getMillisecondCounterHiRes() - holdStartMs_);
    if (!applyStep(holdZone_ == Zone::up ? +1 : -1)) {
        stopHold(); // the limit: nothing more to repeat
        return;
    }
    startTimer(CardControlGestures::holdRepeatIntervalMs(held));
}

void CardStepper::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || !isEnabled() || editing_)
        return; // a right click is the card's control menu
    const auto zone = zoneAt(e.getPosition());
    dragging_ = false;
    blocked_ = false;
    if (zone == Zone::up || zone == Zone::down) {
        pressedZone_ = zone;
        grabKeyboardFocus();
        applyStep(zone == Zone::up ? +1 : -1);
        startHold(zone);
    } else if (zone == Zone::number) {
        grabKeyboardFocus();
        if (e.mods.isCommandDown()) {
            if (ranged_) {
                applyStep(default_ - value_);
                endGesture();
            }
            return;
        }
        pressedZone_ = Zone::number;
        dragLastY_ = e.position.y;
        dragRemainder_ = 0.0;
    }
    repaint();
}

void CardStepper::mouseDrag(const juce::MouseEvent& e) {
    if (pressedZone_ != Zone::number || !isEnabled())
        return;
    // The rate follows the Shift state of each move, and only the travel since the last move counts, so pressing or
    // releasing Shift mid-drag never makes the value jump.
    const double perStep = CardControlGestures::stepperDragPixelsPerStep(ranged_ ? max_ - min_ : 0) *
                           (e.mods.isShiftDown() ? CardControlGestures::kFineDragDivisor : 1.0);
    dragRemainder_ += (double)(dragLastY_ - e.position.y) / perStep; // up is more
    dragLastY_ = e.position.y;
    // A tiny nudge toward the next whole step, so 30 px at 3 px a step is 10 steps and not 9.99999 of them.
    // A tiny nudge toward the next whole step, so 30 px at 3 px a step is 10 steps and not 9.99999 of them.
    const int whole = (int)(dragRemainder_ + (dragRemainder_ >= 0.0 ? 1e-6 : -1e-6));
    if (whole == 0)
        return;
    dragRemainder_ -= whole;
    dragging_ = true;
    applyStep(whole);
    repaint();
}

void CardStepper::mouseUp(const juce::MouseEvent&) {
    stopHold();
    pressedZone_ = Zone::none;
    dragging_ = false;
    blocked_ = false;
    endGesture();
    repaint();
}

void CardStepper::mouseDoubleClick(const juce::MouseEvent& e) {
    if (typable_ && isEnabled() && !e.mods.isPopupMenu() && !e.mods.isCommandDown() &&
        zoneAt(e.getPosition()) == Zone::number)
        showEditor();
}

void CardStepper::mouseMove(const juce::MouseEvent& e) {
    const auto zone = zoneAt(e.getPosition());
    if (zone != hoverZone_) {
        hoverZone_ = zone;
        repaint();
    }
}

void CardStepper::mouseExit(const juce::MouseEvent&) {
    hoverZone_ = Zone::none;
    blocked_ = false;
    repaint();
}

//==============================================================================
// Keys, focus, the editor

// The hold ends with the press, and with anything that takes the focus or the field away from it.
void CardStepper::focusLost(FocusChangeType) {
    stopHold();
    pressedZone_ = Zone::none;
    dragging_ = false;
    blocked_ = false;
    endGesture();
    repaint();
}

void CardStepper::focusGained(FocusChangeType) { repaint(); }
void CardStepper::focusOfChildComponentChanged(FocusChangeType) { repaint(); }

void CardStepper::enablementChanged() {
    if (!isEnabled()) {
        stopHold();
        endGesture();
    }
    repaint();
}

void CardStepper::visibilityChanged() {
    if (!isShowing()) {
        stopHold();
        endGesture();
    }
}

void CardStepper::showEditor(const juce::String& initialText) {
    if (!typable_ || !isEnabled())
        return;
    stopHold();
    endGesture();
    editing_ = true;
    editor_.setText(initialText.isNotEmpty() ? initialText : valueText_, false);
    editor_.setVisible(true);
    editor_.grabKeyboardFocus();
    if (initialText.isEmpty())
        editor_.selectAll();
    else
        editor_.moveCaretToEnd();
    repaint();
}

// Return and a click elsewhere apply; the owner decides whether the text is a value.
void CardStepper::commitEditor() {
    if (!editing_)
        return;
    const auto text = editor_.getText();
    const bool hadFocus = editor_.hasKeyboardFocus(true);
    closeEditor();
    if (onTypedValue)
        onTypedValue(text);
    if (hadFocus)
        grabKeyboardFocus();
}

// The editor hides one message-loop turn later: it is still inside its own key handler now.
void CardStepper::closeEditor() {
    if (!editing_)
        return;
    editing_ = false;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CardStepper>(this)] {
        if (safe == nullptr)
            return;
        safe->editor_.setVisible(false);
        if (!safe->editing_ && safe->isShowing())
            safe->grabKeyboardFocus();
        safe->repaint();
    });
    repaint();
}

// A key a focused field does not use arrives here. Tab, Escape and modified keys stay with the card.
bool CardStepper::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    if (typable_ && code >= '0' && code <= '9' && !mods.isShiftDown()) {
        showEditor(juce::String::charToString((juce::juce_wchar)code));
        return true;
    }
    if (typable_ && code == juce::KeyPress::returnKey) {
        showEditor();
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) {
        step(+1);
        return true;
    }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) {
        step(-1);
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> CardStepper::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ValueInterface>(*this)});
}

} // namespace synth::ui
