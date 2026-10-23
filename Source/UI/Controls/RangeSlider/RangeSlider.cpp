// RangeSlider.cpp -- the two-handle slider: painting from theme tokens, the pointer (a handle drags, a click on the
// track moves the nearer one), the keys on each handle, the 90 ms glide of non-drag changes and the per-handle
// screen-reader value.
#include "UI/Controls/RangeSlider/RangeSlider.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

constexpr float kHandleDiameter = 14.0f;
constexpr int kHandleHit = 18;
constexpr float kTrackThickness = 4.0f;
constexpr double kGlideMs = 90.0;

class HandleValue : public juce::AccessibilityValueInterface {
public:
    HandleValue(std::function<double()> get, std::function<void(double)> set, std::function<juce::String()> text,
                std::function<juce::AccessibilityValueInterface::AccessibleValueRange()> range)
        : get_(std::move(get))
        , set_(std::move(set))
        , text_(std::move(text))
        , range_(std::move(range)) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return get_(); }
    juce::String getCurrentValueAsString() const override { return text_(); }
    void setValue(double v) override { set_(v); }
    void setValueAsString(const juce::String& s) override { set_(s.getDoubleValue()); }
    juce::AccessibilityValueInterface::AccessibleValueRange getRange() const override { return range_(); }

private:
    std::function<double()> get_;
    std::function<void(double)> set_;
    std::function<juce::String()> text_;
    std::function<juce::AccessibilityValueInterface::AccessibleValueRange()> range_;
};

} // namespace

// One handle: it takes the pointer for dragging and the keyboard, draws itself and its focus ring, and speaks its
// value.
class RangeSlider::HandleStop
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    HandleStop(RangeSlider& owner, Handle which)
        : owner_(owner)
        , which_(which) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setExplicitFocusOrder(1 + (int)which);
    }

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        const auto disc =
            juce::Rectangle<float>(kHandleDiameter, kHandleDiameter).withCentre(getLocalBounds().toFloat().getCentre());
        g.setColour(dragging_ || hover_ ? c.accent : c.textPrimary);
        g.fillEllipse(disc);
        g.setColour(c.border);
        g.drawEllipse(disc, 1.0f);
        paintFocusRing(g, disc.expanded(2.0f), *this, disc.getWidth() / 2.0f + 2.0f);
    }
    void mouseEnter(const juce::MouseEvent&) override {
        hover_ = true;
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override {
        hover_ = false;
        repaint();
    }
    void mouseDown(const juce::MouseEvent&) override {
        dragging_ = true;
        grabKeyboardFocus();
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const auto inOwner = e.getEventRelativeTo(&owner_);
        auto which = which_;
        // Two handles on one spot: the pointer's side decides which one the drag takes, so neither gets stuck.
        if (owner_.lower_ == owner_.upper_)
            which = inOwner.position.x < owner_.xFor(owner_.lower_) ? Handle::Lower : Handle::Upper;
        owner_.setHandle(which, owner_.valueAt(inOwner.position.x), true, false);
    }
    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
        repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override { return owner_.keyPressedOn(which_, key); }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    bool isDragging() const noexcept { return dragging_; }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        auto value =
            std::make_unique<HandleValue>([this] { return which_ == Handle::Lower ? owner_.lower_ : owner_.upper_; },
                                          [this](double v) { owner_.setHandle(which_, owner_.snap(v), true, true); },
                                          [this] { return owner_.spokenValue(which_); },
                                          [this] {
                                              return juce::AccessibilityValueInterface::AccessibleValueRange(
                                                  {owner_.min_, owner_.max_}, owner_.step_);
                                          });
        return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::slider,
                                                    juce::AccessibilityActions{},
                                                    juce::AccessibilityHandler::Interfaces{std::move(value)});
    }

private:
    RangeSlider& owner_;
    Handle which_;
    bool hover_ = false;
    bool dragging_ = false;
};

RangeSlider::RangeSlider()
    : lowerStop_(std::make_unique<HandleStop>(*this, Handle::Lower))
    , upperStop_(std::make_unique<HandleStop>(*this, Handle::Upper)) {
    addAndMakeVisible(*lowerStop_);
    addAndMakeVisible(*upperStop_);
    setSize(200, kPreferredHeight);
    updateNames();
}

RangeSlider::~RangeSlider() = default;

juce::Component& RangeSlider::getHandle(Handle which) noexcept {
    return which == Handle::Lower ? static_cast<juce::Component&>(*lowerStop_) : *upperStop_;
}

double RangeSlider::snap(double value) const {
    const double stepped = min_ + std::round((value - min_) / step_) * step_;
    return juce::jlimit(min_, max_, stepped);
}

float RangeSlider::trackLeft() const { return kHandleHit * 0.5f; }
float RangeSlider::trackRight() const { return (float)getWidth() - kHandleHit * 0.5f; }

float RangeSlider::xFor(double value) const {
    const double span = max_ - min_;
    const double t = span > 0.0 ? (value - min_) / span : 0.0;
    return trackLeft() + (float)t * juce::jmax(0.0f, trackRight() - trackLeft());
}

double RangeSlider::valueAt(float x) const {
    const float width = trackRight() - trackLeft();
    const double t = width > 0.0f ? (double)juce::jlimit(0.0f, 1.0f, (x - trackLeft()) / width) : 0.0;
    return snap(min_ + t * (max_ - min_));
}

juce::String RangeSlider::spokenValue(Handle which) const {
    const double v = which == Handle::Lower ? lower_ : upper_;
    if (textForValue)
        return textForValue(v);
    return juce::String(juce::roundToInt(v)) + (unit_.isEmpty() ? juce::String() : " " + unit_);
}

juce::String RangeSlider::nameOf(Handle which) const {
    return label_ + (which == Handle::Lower ? ", lower handle" : ", upper handle");
}

void RangeSlider::updateNames() {
    for (auto h : {Handle::Lower, Handle::Upper}) {
        auto& comp = getHandle(h);
        comp.setTitle(nameOf(h));
        static_cast<HandleStop&>(comp).setTooltip(nameOf(h) + ". Left and Right move it one step.");
    }
}

void RangeSlider::setLabel(const juce::String& label, const juce::String& unit) {
    label_ = label;
    unit_ = unit;
    updateNames();
}

void RangeSlider::setLimits(double minimum, double maximum, double step) {
    min_ = minimum;
    max_ = juce::jmax(minimum, maximum);
    step_ = step > 0.0 ? step : 1.0;
    setValues(lower_, upper_);
}

void RangeSlider::setValues(double lower, double upper) {
    if (lower > upper)
        std::swap(lower, upper);
    const double lo = snap(lower);
    const double hi = snap(upper);
    if (lo == lower_ && hi == upper_)
        return;
    lower_ = lo;
    upper_ = hi;
    glideTo(xFor(lower_), xFor(upper_));
    repaint();
}

void RangeSlider::setHandle(Handle which, double value, bool notify, bool glide) {
    double lo = lower_;
    double hi = upper_;
    const double v = snap(value);
    if (which == Handle::Lower)
        lo = juce::jmin(v, hi);
    else
        hi = juce::jmax(v, lo);
    if (lo == lower_ && hi == upper_)
        return;
    lower_ = lo;
    upper_ = hi;
    if (glide) {
        glideTo(xFor(lower_), xFor(upper_));
    } else {
        if (glideAnim_.isRunning())
            finishMotionForTest();
        shownLower_ = xFor(lower_);
        shownUpper_ = xFor(upper_);
        layoutHandles();
    }
    repaint();
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    for (auto* stop : {lowerStop_.get(), upperStop_.get()})
        if (auto* handler = stop->getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    if (notify && onChange)
        onChange(lower_, upper_);
}

void RangeSlider::step(Handle which, int steps) {
    const double current = which == Handle::Lower ? lower_ : upper_;
    setHandle(which, current + steps * step_, true, true);
}

bool RangeSlider::keyPressedOn(Handle which, const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int big = mods.isShiftDown() ? 10 : 1;
    if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        step(which, -big);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::rightKey) || key.isKeyCode(juce::KeyPress::upKey)) {
        step(which, big);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::pageDownKey)) {
        step(which, -10);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::pageUpKey)) {
        step(which, 10);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::homeKey)) {
        setHandle(which, min_, true, true);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::endKey)) {
        setHandle(which, max_, true, true);
        return true;
    }
    return false;
}

// ---- Motion ---------------------------------------------------------------------------------------------

void RangeSlider::finishMotionForTest() {
    glideAnim_.completeForTest();
    shownLower_ = xFor(lower_);
    shownUpper_ = xFor(upper_);
    layoutHandles();
    repaint();
}

void RangeSlider::glideTo(float lowerX, float upperX) {
    const double ms = motionMs(kGlideMs, 0.0);
    if (ms <= 0.0 || !isShowing()) {
        glideAnim_.stop(vblank_);
        shownLower_ = lowerX;
        shownUpper_ = upperX;
        layoutHandles();
        return;
    }
    fromLower_ = shownLower_;
    fromUpper_ = shownUpper_;
    glideAnim_.start(
        vblank_, ms, easeOutCubic,
        [this, lowerX, upperX](float t) {
            shownLower_ = fromLower_ + (lowerX - fromLower_) * t;
            shownUpper_ = fromUpper_ + (upperX - fromUpper_) * t;
            layoutHandles();
            repaint();
        },
        [this, lowerX, upperX] {
            shownLower_ = lowerX;
            shownUpper_ = upperX;
            layoutHandles();
            repaint();
        });
}

// ---- Layout, painting and the pointer -------------------------------------------------------------------

void RangeSlider::layoutHandles() {
    const int h = getHeight();
    lowerStop_->setBounds(juce::roundToInt(shownLower_) - kHandleHit / 2, 0, kHandleHit, h);
    upperStop_->setBounds(juce::roundToInt(shownUpper_) - kHandleHit / 2, 0, kHandleHit, h);
}

void RangeSlider::resized() {
    shownLower_ = xFor(lower_);
    shownUpper_ = xFor(upper_);
    layoutHandles();
}

juce::Point<int> RangeSlider::handleCentre(Handle which) const {
    return (which == Handle::Lower ? lowerStop_ : upperStop_)->getBounds().getCentre();
}

void RangeSlider::lookAndFeelChanged() { repaint(); }

void RangeSlider::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const float cy = (float)getHeight() * 0.5f;
    const juce::Rectangle<float> track(trackLeft(), cy - kTrackThickness * 0.5f, trackRight() - trackLeft(),
                                       kTrackThickness);
    g.setColour(c.border);
    g.fillRoundedRectangle(track, kTrackThickness * 0.5f);
    g.setColour(c.accent);
    g.fillRoundedRectangle(track.withLeft(shownLower_).withRight(juce::jmax(shownLower_, shownUpper_)),
                           kTrackThickness * 0.5f);
}

void RangeSlider::mouseDown(const juce::MouseEvent& e) {
    const float x = e.position.x;
    const bool lowerNearer =
        shownLower_ == shownUpper_ ? x < shownLower_ : std::abs(x - shownLower_) <= std::abs(x - shownUpper_);
    const auto which = lowerNearer ? Handle::Lower : Handle::Upper;
    getHandle(which).grabKeyboardFocus();
    setHandle(which, valueAt(x), true, true);
}

juce::String RangeSlider::getTooltip() { return label_ + ": drag a handle, or focus it and use Left and Right"; }

std::unique_ptr<juce::AccessibilityHandler> RangeSlider::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
