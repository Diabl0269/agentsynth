// NoteRangePicker.cpp -- the keyboard strip with its band: drawing, the mouse (edges add or remove notes, the middle
// moves the range by octaves), the keys on three focusable parts, and the 120 ms glide of the band to its place.
// The pointer maths is NoteRangeGeometry.h. docs/layout/note-range-picker.md.
#include "NoteRangePicker.h"
#include "Music/NoteNames.h"
#include "NoteRangeGeometry.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

using namespace noterange;

namespace {

constexpr float kEdgeGrabPx = 5.0f;
constexpr float kHandleWidth = 4.0f;
constexpr float kHandleHeight = 26.0f;
constexpr double kGlideMs = 120.0;
constexpr double kReducedFadeMs = 80.0;
constexpr float kLabelFont = 8.0f;

const char* partName(NoteRangePicker::Part part) {
    switch (part) {
    case NoteRangePicker::Part::LowEdge:
        return "Lowest note";
    case NoteRangePicker::Part::HighEdge:
        return "Highest note";
    case NoteRangePicker::Part::Band:
        break;
    }
    return "Move the whole range";
}

} // namespace

// One of the three Tab stops. It takes no mouse (the picker handles every pointer event itself), only the keyboard,
// the focus ring and the accessibility name.
class NoteRangePicker::Stop
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    Stop(NoteRangePicker& owner, Part part)
        : owner_(owner)
        , part_(part) {
        setTitle(partName(part));
        setTooltip(partName(part));
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
        setExplicitFocusOrder(1 + (int)part);
    }

    Part part() const noexcept { return part_; }
    void paint(juce::Graphics& g) override { paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f); }
    bool keyPressed(const juce::KeyPress& key) override { return owner_.keyPressedOn(part_, key); }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::button);
    }

private:
    NoteRangePicker& owner_;
    Part part_;
};

NoteRangePicker::NoteRangePicker()
    : lowStop_(std::make_unique<Stop>(*this, Part::LowEdge))
    , highStop_(std::make_unique<Stop>(*this, Part::HighEdge))
    , bandStop_(std::make_unique<Stop>(*this, Part::Band)) {
    addAndMakeVisible(*lowStop_);
    addAndMakeVisible(*highStop_);
    addAndMakeVisible(*bandStop_);
    setSize(kPreferredWidth, kPreferredHeight);
    base_ = windowBaseFor(low_, high_, 0);
    updateTitle();
}

NoteRangePicker::~NoteRangePicker() = default;

juce::String NoteRangePicker::nameOf(int pitch) const { return noteNameWithOctave(pitch, middleCOctave_); }

float NoteRangePicker::whiteKeyWidth() const { return (float)getWidth() / (float)kWhiteKeys; }

float NoteRangePicker::keyCentreX(int pitch) const { return keySpan(pitch, base_, (float)getWidth()).centre(); }

juce::Rectangle<int> NoteRangePicker::bandBounds() const {
    const int x0 = juce::roundToInt(shownX0_);
    return {x0, 0, std::max(1, juce::roundToInt(shownX1_) - x0), getHeight()};
}

juce::Component& NoteRangePicker::getStop(Part part) noexcept {
    return part == Part::LowEdge ? *lowStop_ : (part == Part::HighEdge ? *highStop_ : *bandStop_);
}

NoteRangePicker::Part NoteRangePicker::getFocusedPart() const {
    if (highStop_->hasKeyboardFocus(false))
        return Part::HighEdge;
    return lowStop_->hasKeyboardFocus(false) ? Part::LowEdge : Part::Band;
}

// ---- Range, scale and the visible octaves ----------------------------------------------------------------

void NoteRangePicker::setScale(const MusicalScale* scale) {
    if (scale == nullptr || scale->isChromatic())
        scale_.reset();
    else
        scale_ = *scale;
    repaint();
}

void NoteRangePicker::setMiddleCOctave(int octave) {
    middleCOctave_ = octave;
    updateTitle();
    repaint();
}

void NoteRangePicker::setRange(int lo, int hi) {
    lo = std::clamp(lo, 0, 127);
    hi = std::clamp(hi, 0, 127);
    if (lo > hi)
        std::swap(lo, hi);
    commit(lo, hi, false);
}

// The one place the range changes. The visible octaves follow the range except while a drag is in progress (the strip
// would move under the pointer); they catch up when the drag ends.
void NoteRangePicker::commit(int lo, int hi, bool notify) {
    if (lo == low_ && hi == high_)
        return;
    low_ = lo;
    high_ = hi;
    if (!drag_.active)
        base_ = windowBaseFor(low_, high_, base_);
    glideTo(keySpan(low_, base_, (float)getWidth()).x0, keySpan(high_, base_, (float)getWidth()).x1);
    updateTitle();
    repaint();
    if (notify && onChange)
        onChange(low_, high_);
}

void NoteRangePicker::afterRangeChanged() {
    const int base = windowBaseFor(low_, high_, base_);
    if (base == base_)
        return;
    base_ = base;
    glideTo(keySpan(low_, base_, (float)getWidth()).x0, keySpan(high_, base_, (float)getWidth()).x1);
    repaint();
}

void NoteRangePicker::updateTitle() {
    setTitle("Note range, " + nameOf(low_) + " to " + nameOf(high_));
    lowStop_->setDescription(nameOf(low_));
    highStop_->setDescription(nameOf(high_));
    bandStop_->setDescription(nameOf(low_) + " to " + nameOf(high_));
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
}

// ---- Motion -----------------------------------------------------------------------------------------------

// The band's edges ease to their place in 120 ms. Reduce Motion: they land at once and the band fades in over 80 ms.
// Animations Off, or a picker that is not on screen: they land at once.
void NoteRangePicker::glideTo(float x0, float x1) {
    glideAnim_.stop(vblank_);
    fadeAnim_.stop(vblank_);
    bandAlpha_ = 1.0f;
    const auto land = [this, x0, x1] {
        shownX0_ = x0;
        shownX1_ = x1;
        layoutStops();
        repaint();
    };
    if (!isShowing() || animationsOff()) {
        land();
        return;
    }
    if (prefersReducedMotion()) {
        land();
        bandAlpha_ = 0.4f;
        fadeAnim_.start(
            vblank_, kReducedFadeMs, [](float t) { return t; },
            [this](float t) {
                bandAlpha_ = 0.4f + 0.6f * t;
                repaint();
            },
            [this] {
                bandAlpha_ = 1.0f;
                repaint();
            });
        return;
    }
    fromX0_ = shownX0_;
    fromX1_ = shownX1_;
    glideAnim_.start(vblank_, kGlideMs, easeOutCubic, [this, x0, x1](float t) {
        shownX0_ = fromX0_ + (x0 - fromX0_) * t;
        shownX1_ = fromX1_ + (x1 - fromX1_) * t;
        layoutStops();
        repaint();
    });
}

void NoteRangePicker::finishMotionForTest() {
    glideAnim_.completeForTest();
    fadeAnim_.completeForTest();
}

// ---- Layout -----------------------------------------------------------------------------------------------

void NoteRangePicker::resized() {
    shownX0_ = keySpan(low_, base_, (float)getWidth()).x0;
    shownX1_ = keySpan(high_, base_, (float)getWidth()).x1;
    layoutStops();
}

void NoteRangePicker::layoutStops() {
    const int h = getHeight();
    const int handleH = std::min((int)kHandleHeight, std::max(4, h - 8)) + 2;
    const int top = (h - handleH) / 2;
    const int x0 = juce::roundToInt(shownX0_);
    const int x1 = juce::roundToInt(shownX1_);
    lowStop_->setBounds(x0 - 4, top, 8, handleH);
    highStop_->setBounds(x1 - 4, top, 8, handleH);
    bandStop_->setBounds(x0 + 5, 1, std::max(4, x1 - x0 - 10), std::max(2, h - 2));
}

// ---- Painting ---------------------------------------------------------------------------------------------

void NoteRangePicker::paintKeys(juce::Graphics& g) const {
    const auto& c = theme::themeOf(*this).colors;
    const float width = (float)getWidth();
    const float height = (float)getHeight();
    for (int pass = 0; pass < 2; ++pass) {
        for (int p = base_; p < base_ + kSpan; ++p) {
            const bool black = isBlackKey(p);
            if (black != (pass == 1))
                continue;
            const auto span = keySpan(p, base_, width);
            const bool lit = scale_.has_value() && scale_->contains(p);
            juce::Rectangle<float> key(span.x0, 0.0f, span.x1 - span.x0, black ? height * 0.62f : height);
            g.setColour(black ? (lit ? c.bg1.brighter(0.5f) : c.bg0)
                              : (lit ? c.surfaceHi.brighter(0.4f) : c.surfaceHi));
            g.fillRect(key);
            g.setColour(c.bg0);
            g.drawRect(key, 0.6f);
        }
    }
}

void NoteRangePicker::paintBand(juce::Graphics& g) const {
    const auto& c = theme::themeOf(*this).colors;
    const float height = (float)getHeight();
    const juce::Rectangle<float> band(shownX0_, 0.5f, std::max(1.0f, shownX1_ - shownX0_), height - 1.0f);
    g.setColour(c.accent.withAlpha(0.25f * bandAlpha_));
    g.fillRect(band);
    g.setColour(c.accent.withAlpha(bandAlpha_));
    g.drawRect(band, 1.0f);

    const float handleH = std::min(kHandleHeight, std::max(4.0f, height - 8.0f));
    for (const float x : {shownX0_, shownX1_}) {
        const juce::Rectangle<float> handle(x - kHandleWidth * 0.5f, (height - handleH) * 0.5f, kHandleWidth, handleH);
        g.fillRoundedRectangle(handle, 2.0f);
    }

    g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    const auto lowName = nameOf(low_);
    const auto highName = nameOf(high_);
    const float lowWidth = (float)theme::AppLookAndFeel::uiTextWidth(lowName, kLabelFont) + 2.0f;
    const float highWidth = (float)theme::AppLookAndFeel::uiTextWidth(highName, kLabelFont) + 2.0f;
    const juce::Rectangle<float> lowArea(band.getX() + 5.0f, height - 12.0f, lowWidth, 10.0f);
    const juce::Rectangle<float> highArea(band.getRight() - 5.0f - highWidth, height - 12.0f, highWidth, 10.0f);
    const auto drawLabel = [&](const juce::String& text, juce::Rectangle<float> area, juce::Justification where) {
        g.setColour(c.bg0);
        for (const auto& offset : {std::pair<float, float>{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
            g.drawText(text, area.translated(offset.first, offset.second), where);
        g.setColour(c.textPrimary);
        g.drawText(text, area, where);
    };
    drawLabel(lowName, lowArea, juce::Justification::centredLeft);
    if (highArea.getX() > lowArea.getRight() + 2.0f)
        drawLabel(highName, highArea, juce::Justification::centredRight);
}

void NoteRangePicker::paint(juce::Graphics& g) {
    paintKeys(g);
    paintBand(g);
}

// ---- Mouse ------------------------------------------------------------------------------------------------

std::optional<NoteRangePicker::Part> NoteRangePicker::partAt(float x) const {
    const float lowDistance = std::abs(x - shownX0_);
    const float highDistance = std::abs(x - shownX1_);
    if (std::min(lowDistance, highDistance) <= kEdgeGrabPx) {
        if (lowDistance == highDistance)
            return x < shownX0_ ? Part::LowEdge : Part::HighEdge;
        return lowDistance < highDistance ? Part::LowEdge : Part::HighEdge;
    }
    if (x > shownX0_ && x < shownX1_)
        return Part::Band;
    return std::nullopt;
}

void NoteRangePicker::mouseDown(const juce::MouseEvent& e) {
    const auto part = partAt(e.position.x);
    if (!part.has_value())
        return;
    drag_ = {true, *part, e.position.x, low_, high_};
    getStop(*part).grabKeyboardFocus();
}

void NoteRangePicker::mouseDrag(const juce::MouseEvent& e) {
    if (!drag_.active)
        return;
    if (drag_.part == Part::Band)
        dragBand(e);
    else
        dragEdge(drag_.part, e.position.x);
}

void NoteRangePicker::mouseUp(const juce::MouseEvent&) {
    if (!drag_.active)
        return;
    const bool changed = low_ != drag_.startLow || high_ != drag_.startHigh;
    drag_.active = false;
    afterRangeChanged();
    if (changed && onGestureEnd)
        onGestureEnd(low_, high_);
}

void NoteRangePicker::mouseMove(const juce::MouseEvent& e) {
    const auto part = partAt(e.position.x);
    hoverOnKeys_ = !part.has_value();
    hover_ = part.value_or(Part::Band);
}

int NoteRangePicker::stepNote(int pitch, int direction) const {
    if (scale_.has_value())
        return scale_->stepBy(pitch, direction);
    return std::clamp(pitch + direction, 0, 127);
}

// The edge goes to the key under the pointer (the low edge's left side, the high edge's right side), then to the
// nearest scale note.
void NoteRangePicker::dragEdge(Part part, float x) {
    const float half = whiteKeyWidth() * 0.5f;
    const bool low = part == Part::LowEdge;
    int pitch = pitchAtX(low ? x + half : x - half, base_, (float)getWidth());
    if (scale_.has_value())
        pitch = scale_->snapPitch(pitch);
    if (low)
        commit(std::min(pitch, high_), high_, true);
    else
        commit(low_, std::max(pitch, low_), true);
}

// A drag of the middle moves the range by whole octaves; with Option, by one note at a time. The range stays inside
// the visible octaves until the pointer is released.
void NoteRangePicker::dragBand(const juce::MouseEvent& e) {
    const float octaveWidth = whiteKeyWidth() * 7.0f;
    const float dx = e.position.x - drag_.startX;
    const bool byNote = e.mods.isAltDown();
    const int amount = byNote ? juce::roundToInt(dx / (octaveWidth / 12.0f)) : juce::roundToInt(dx / octaveWidth) * 12;
    int lo = drag_.startLow;
    int hi = drag_.startHigh;
    if (byNote && scale_.has_value()) {
        const int direction = amount < 0 ? -1 : 1;
        for (int i = 0; i < std::abs(amount); ++i) {
            const int nextLo = stepNote(lo, direction);
            const int nextHi = stepNote(hi, direction);
            if (nextLo == lo || nextHi == hi)
                break;
            lo = nextLo;
            hi = nextHi;
        }
    } else {
        lo += amount;
        hi += amount;
    }
    if (lo < base_ || hi > base_ + kSpan - 1 || lo < 0 || hi > 127)
        return;
    commit(lo, hi, true);
}

// ---- Keyboard ---------------------------------------------------------------------------------------------

// Left and Right: the focused edge moves one scale note (Shift: an octave); the range moves an octave (Option: one
// scale note). Returns whether the key was one of those.
bool NoteRangePicker::keyPressedOn(Part part, const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey)
        return false;
    const int direction = code == juce::KeyPress::rightKey ? 1 : -1;
    const int lowBefore = low_;
    const int highBefore = high_;
    const bool octave = key.getModifiers().isShiftDown();
    const auto snapped = [this](int pitch) {
        pitch = std::clamp(pitch, 0, 127);
        return scale_.has_value() ? scale_->snapPitch(pitch) : pitch;
    };
    if (part == Part::LowEdge) {
        const int pitch = octave ? snapped(low_ + direction * 12) : stepNote(low_, direction);
        commit(std::clamp(pitch, 0, high_), high_, true);
    } else if (part == Part::HighEdge) {
        const int pitch = octave ? snapped(high_ + direction * 12) : stepNote(high_, direction);
        commit(low_, std::clamp(pitch, low_, 127), true);
    } else if (key.getModifiers().isAltDown()) {
        const int nextLo = stepNote(low_, direction);
        const int nextHi = stepNote(high_, direction);
        if (nextLo != low_ && nextHi != high_)
            commit(nextLo, nextHi, true);
    } else {
        const int shift = std::clamp(direction * 12, -low_, 127 - high_);
        commit(low_ + shift, high_ + shift, true);
    }
    // One key step is one finished gesture (so one undo step), but only when it moved something.
    if ((low_ != lowBefore || high_ != highBefore) && onGestureEnd)
        onGestureEnd(low_, high_);
    return true;
}

// ---- Tooltip and accessibility ----------------------------------------------------------------------------

juce::String NoteRangePicker::getTooltip() {
    const auto part = partAt((float)getMouseXYRelative().x);
    if (part.has_value())
        return partName(*part);
    return "Note range: drag an edge to add or remove notes, drag the middle to move the range";
}

std::unique_ptr<juce::AccessibilityHandler> NoteRangePicker::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
