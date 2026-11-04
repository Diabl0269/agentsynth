// NoteRangePicker.cpp -- the keyboard strip with its band and the overview bar: state, the scrolling window, drawing,
// the 120 ms glide and the accessibility parts. The mouse, keys and the note editor are NoteRangePickerGestures.cpp;
// the pointer maths is NoteRangeGeometry.h. docs/layout/note-range-picker.md.
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
constexpr float kLabelClear = 8.0f;   ///< Between a handle's centre and the name beside it.
constexpr float kChevronSpace = 8.0f; ///< Room for the chevron before a pinned name.

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

const char* partTooltip(NoteRangePicker::Part part) {
    switch (part) {
    case NoteRangePicker::Part::LowEdge:
        return "Lowest note. Left and Right move it a note, Shift an octave, Enter types a note, Page Up and Page "
               "Down show other octaves";
    case NoteRangePicker::Part::HighEdge:
        return "Highest note. Left and Right move it a note, Shift an octave, Enter types a note, Page Up and Page "
               "Down show other octaves";
    case NoteRangePicker::Part::Band:
        break;
    }
    return "Move the whole range. Left and Right move it a note, Shift an octave, Page Up and Page Down show other "
           "octaves";
}

} // namespace

// One of the three Tab stops. It takes no mouse (the picker handles every pointer event itself), only the keyboard,
// the focus ring and the accessibility name. Taking focus scrolls its part into view.
class NoteRangePicker::Stop
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    Stop(NoteRangePicker& owner, Part part)
        : owner_(owner)
        , part_(part) {
        setTitle(partName(part));
        setTooltip(partTooltip(part));
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
        setExplicitFocusOrder(1 + (int)part);
    }

    Part part() const noexcept { return part_; }
    void paint(juce::Graphics& g) override { paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f); }
    bool keyPressed(const juce::KeyPress& key) override { return owner_.keyPressedOn(part_, key); }
    void focusGained(FocusChangeType) override {
        owner_.scrollTo(owner_.revealTarget(part_, owner_.low_, owner_.high_));
        repaint();
    }
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
    , bandStop_(std::make_unique<Stop>(*this, Part::Band))
    , noteEditor_(std::make_unique<juce::TextEditor>()) {
    addAndMakeVisible(*lowStop_);
    addAndMakeVisible(*highStop_);
    addAndMakeVisible(*bandStop_);
    addChildComponent(*noteEditor_);
    noteEditor_->setMultiLine(false);
    noteEditor_->setSelectAllWhenFocused(true);
    noteEditor_->setJustification(juce::Justification::centred);
    noteEditor_->setIndents(2, 0);
    noteEditor_->setBorder(juce::BorderSize<int>(0));
    noteEditor_->setInputRestrictions(5, "ABCDEFGabcdefg#-0123456789"); // not-ui-text: characters a note name may hold
    noteEditor_->onReturnKey = [this] { finishNoteEdit(true); };
    noteEditor_->onEscapeKey = [this] {
        const Part edge = editing_;
        finishNoteEdit(false);
        getStop(edge).grabKeyboardFocus();
    };
    noteEditor_->onFocusLost = [this] { finishNoteEdit(false); };
    setSize(kPreferredWidth, kPreferredHeight);
    scroll_ = scrollToShowRange();
    landShown();
    updateTitle();
}

NoteRangePicker::~NoteRangePicker() = default;

juce::String NoteRangePicker::nameOf(int pitch) const { return noteNameWithOctave(pitch, middleCOctave_); }

// ---- Positions ------------------------------------------------------------------------------------------

int NoteRangePicker::overviewHeight() const noexcept { return getHeight() >= kPreferredHeight ? kOverviewHeight : 0; }

int NoteRangePicker::stripTop() const noexcept { return overviewHeight() > 0 ? kOverviewHeight + kOverviewGap : 0; }

int NoteRangePicker::stripHeight() const noexcept { return getHeight() - stripTop(); }

float NoteRangePicker::whiteKeyWidth() const { return keyW_; }

float NoteRangePicker::toX(float units) const { return (units - shownScroll_) * keyW_; }

float NoteRangePicker::bandX0() const { return toX(shownA0_); }

float NoteRangePicker::bandX1() const { return toX(shownA1_); }

float NoteRangePicker::keyCentreX(int pitch) const { return keySpan(pitch, shownScroll_, keyW_).centre(); }

juce::Rectangle<int> NoteRangePicker::bandBounds() const {
    const int x0 = juce::roundToInt(bandX0());
    return {x0, stripTop(), std::max(1, juce::roundToInt(bandX1()) - x0), stripHeight()};
}

juce::Rectangle<float> NoteRangePicker::overviewFrame() const {
    const float w = (float)getWidth();
    return {overviewX(shownScroll_, w), 0.3f, overviewX((float)keys_, w), (float)kOverviewHeight - 0.6f};
}

juce::Component& NoteRangePicker::getStop(Part part) noexcept {
    return part == Part::LowEdge ? *lowStop_ : (part == Part::HighEdge ? *highStop_ : *bandStop_);
}

NoteRangePicker::Part NoteRangePicker::getFocusedPart() const {
    if (highStop_->hasKeyboardFocus(false))
        return Part::HighEdge;
    return lowStop_->hasKeyboardFocus(false) ? Part::LowEdge : Part::Band;
}

// A note name sits inside its end of the band, clear of the handle. When the band is too narrow for both names it goes
// outside, beside its handle; when its edge is off the strip it is pinned at the strip's end with a chevron.
NoteRangePicker::LabelSpot NoteRangePicker::labelSpot(Part edge) const {
    const float w = (float)getWidth();
    const float x0 = bandX0();
    const float x1 = bandX1();
    const float own = edge == Part::LowEdge ? x0 : x1;
    if (own < 1.0f || own > w - 1.0f)
        return LabelSpot::Pinned;
    const float lowW = (float)theme::AppLookAndFeel::uiTextWidth(nameOf(low_), kLabelFont) + 2.0f;
    const float highW = (float)theme::AppLookAndFeel::uiTextWidth(nameOf(high_), kLabelFont) + 2.0f;
    const float pad = 2.0f * kLabelClear;
    const float inner = std::min(x1, w) - std::max(x0, 0.0f);
    const bool bothInside = x0 >= 1.0f && x1 <= w - 1.0f;
    const bool fits =
        bothInside ? inner >= lowW + highW + pad + 2.0f : inner >= (edge == Part::LowEdge ? lowW : highW) + pad;
    if (fits)
        return LabelSpot::Inside;
    if (edge == Part::LowEdge)
        return x0 - kLabelClear - lowW >= 0.0f ? LabelSpot::Outside : LabelSpot::Inside;
    return x1 + kLabelClear + highW <= w ? LabelSpot::Outside : LabelSpot::Inside;
}

juce::Rectangle<float> NoteRangePicker::labelArea(Part edge) const {
    const float w = (float)getWidth();
    const float top = (float)(stripTop() + stripHeight()) - 12.0f;
    const float width =
        (float)theme::AppLookAndFeel::uiTextWidth(nameOf(edge == Part::LowEdge ? low_ : high_), kLabelFont) + 2.0f;
    const auto spot = labelSpot(edge);
    const float own = edge == Part::LowEdge ? bandX0() : bandX1();
    if (spot == LabelSpot::Pinned)
        return {own < 1.0f ? kChevronSpace : w - kChevronSpace - width, top, width, 10.0f};
    if (edge == Part::LowEdge)
        return {spot == LabelSpot::Outside ? own - kLabelClear - width : own + kLabelClear, top, width, 10.0f};
    return {spot == LabelSpot::Outside ? own + kLabelClear : own - kLabelClear - width, top, width, 10.0f};
}

// A name is shown unless it would run into the other one; a one-note band shows its name once.
bool NoteRangePicker::labelVisible(Part edge) const {
    if (edge == Part::LowEdge)
        return true;
    if (low_ == high_)
        return false;
    return labelArea(Part::HighEdge).getX() > labelArea(Part::LowEdge).getRight() + 2.0f;
}

// ---- Range, scale and the shown octaves ---------------------------------------------------------------------

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
    noteEditor_->setVisible(false);
    // A range already on the strip leaves the keys where they are; one off it brings the strip to it.
    const auto extent = rangeExtent(lo, hi);
    commit(lo, hi, false, isInView(extent, scroll_, keys_) ? scroll_ : scrollCentredOn(extent, keys_));
}

float NoteRangePicker::scrollToShowRange() const {
    const auto extent = rangeExtent(low_, high_);
    return isInView(extent, scroll_, keys_) ? scroll_ : scrollCentredOn(extent, keys_);
}

// Where the strip must be for `part` of the range lo..hi to show with a key to spare: the least move from now.
float NoteRangePicker::revealTarget(Part part, int lo, int hi) const {
    const auto extent = part == Part::LowEdge    ? rangeExtent(lo, lo)
                        : part == Part::HighEdge ? rangeExtent(hi, hi)
                                                 : rangeExtent(lo, hi);
    return scrollToReveal(extent, scroll_, keys_);
}

// The one place the range or the window moves. The band and the keys glide together to their place.
void NoteRangePicker::commit(int lo, int hi, bool notify, float scrollTarget) {
    scrollTarget = clampScroll(scrollTarget, keys_);
    const bool rangeChanged = lo != low_ || hi != high_;
    if (!rangeChanged && scrollTarget == scroll_)
        return;
    low_ = lo;
    high_ = hi;
    scroll_ = scrollTarget;
    const auto extent = rangeExtent(low_, high_);
    glideTo(extent.x0, extent.x1, scroll_);
    if (rangeChanged)
        updateTitle();
    repaint();
    if (notify && rangeChanged && onChange)
        onChange(low_, high_);
}

void NoteRangePicker::scrollTo(float target) { commit(low_, high_, false, target); }

// The strip follows the pointer at once (no glide): it scrolls under a drag.
void NoteRangePicker::setScrollNow(float scroll) {
    scroll_ = clampScroll(scroll, keys_);
    shownScroll_ = fromScroll_ = toScroll_ = scroll_;
    layoutStops();
    repaint();
}

void NoteRangePicker::scrollBy(float keys) { setScrollNow(scroll_ + keys); }

void NoteRangePicker::updateTitle() {
    setTitle("Note range, " + nameOf(low_) + " to " + nameOf(high_));
    lowStop_->setDescription(nameOf(low_));
    highStop_->setDescription(nameOf(high_));
    bandStop_->setDescription(nameOf(low_) + " to " + nameOf(high_));
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
}

int NoteRangePicker::stepNote(int pitch, int direction) const {
    if (scale_.has_value())
        return scale_->stepBy(pitch, direction);
    return std::clamp(pitch + direction, 0, 127);
}

// ---- Motion -----------------------------------------------------------------------------------------------

void NoteRangePicker::landShown() {
    shownScroll_ = fromScroll_ = toScroll_ = scroll_;
    const auto extent = rangeExtent(low_, high_);
    shownA0_ = fromA0_ = toA0_ = extent.x0;
    shownA1_ = fromA1_ = toA1_ = extent.x1;
    layoutStops();
    repaint();
}

// The band's edges and the keys ease to their place in 120 ms. Reduce Motion: they land at once and the band fades in
// over 80 ms. Animations Off, or a picker that is not on screen: they land at once.
void NoteRangePicker::glideTo(float a0, float a1, float scroll) {
    glideAnim_.stop(vblank_);
    fadeAnim_.stop(vblank_);
    bandAlpha_ = 1.0f;
    fromScroll_ = shownScroll_;
    fromA0_ = shownA0_;
    fromA1_ = shownA1_;
    toScroll_ = scroll;
    toA0_ = a0;
    toA1_ = a1;
    if (!isShowing() || animationsOff()) {
        landShown();
        return;
    }
    if (prefersReducedMotion()) {
        landShown();
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
    glideAnim_.start(vblank_, kGlideMs, easeOutCubic, [this](float t) {
        shownScroll_ = fromScroll_ + (toScroll_ - fromScroll_) * t;
        shownA0_ = fromA0_ + (toA0_ - fromA0_) * t;
        shownA1_ = fromA1_ + (toA1_ - fromA1_) * t;
        layoutStops();
        repaint();
    });
}

void NoteRangePicker::finishMotionForTest() {
    glideAnim_.completeForTest();
    fadeAnim_.completeForTest();
    landShown();
}

// ---- Layout -----------------------------------------------------------------------------------------------

void NoteRangePicker::resized() {
    keys_ = visibleKeysFor((float)getWidth());
    keyW_ = getWidth() > 0 ? (float)getWidth() / (float)keys_ : 1.0f;
    scroll_ = clampScroll(scroll_, keys_);
    scroll_ = scrollToShowRange();
    noteEditor_->setVisible(false);
    landShown();
}

void NoteRangePicker::layoutStops() {
    const int h = stripHeight();
    const int w = getWidth();
    const int handleH = std::min((int)kHandleHeight, std::max(4, h - 8)) + 2;
    const int top = stripTop() + (h - handleH) / 2;
    const int x0 = juce::roundToInt(bandX0());
    const int x1 = juce::roundToInt(bandX1());
    const auto edgeX = [w](int x) { return std::clamp(x - 4, 0, std::max(0, w - 8)); };
    lowStop_->setBounds(edgeX(x0), top, 8, handleH);
    highStop_->setBounds(edgeX(x1), top, 8, handleH);
    const int bx0 = std::max(x0, 0) + 1;
    const int bx1 = std::min(x1, w) - 1;
    bandStop_->setBounds(bx0, stripTop() + 1, std::max(4, bx1 - bx0), std::max(2, h - 2));
}

// ---- Painting ---------------------------------------------------------------------------------------------

void NoteRangePicker::paintOverview(juce::Graphics& g) const {
    const auto& c = theme::themeOf(*this).colors;
    const float w = (float)getWidth();
    const juce::Rectangle<float> bar(0.0f, 0.0f, w, (float)kOverviewHeight);
    g.setColour(c.bg0);
    g.fillRoundedRectangle(bar, 2.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bar.reduced(0.3f), 2.0f, 0.6f);
    g.setColour(c.textDisabled);
    for (int octave = 0; octave <= 10; ++octave) {
        const float x = overviewX((float)(octave * 7), w);
        g.drawLine(x, 5.0f, x, (float)kOverviewHeight, 0.6f);
    }
    const auto extent = rangeExtent(low_, high_);
    const float x0 = overviewX(extent.x0, w);
    const float x1 = overviewX(extent.x1, w);
    g.setColour(c.accent);
    g.fillRoundedRectangle(x0, 2.0f, std::max(2.0f, x1 - x0), 4.0f, 1.0f);
    const auto frame = overviewFrame();
    g.setColour(c.textPrimary.withAlpha(0.08f));
    g.fillRoundedRectangle(frame, 2.0f);
    g.setColour(c.textMuted);
    g.drawRoundedRectangle(frame, 2.0f, 1.0f);
}

void NoteRangePicker::paintKeys(juce::Graphics& g) const {
    const auto& c = theme::themeOf(*this).colors;
    const float width = (float)getWidth();
    const float height = (float)stripHeight();
    for (int pass = 0; pass < 2; ++pass) {
        for (int p = 0; p < 128; ++p) {
            const bool black = isBlackKey(p);
            if (black != (pass == 1))
                continue;
            const auto span = keySpan(p, shownScroll_, keyW_);
            if (span.x1 < 0.0f || span.x0 > width)
                continue;
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
    const float height = (float)stripHeight();
    const float x0 = bandX0();
    const float x1 = bandX1();
    const juce::Rectangle<float> band(x0, 0.5f, std::max(1.0f, x1 - x0), height - 1.0f);
    g.setColour(c.accent.withAlpha(0.25f * bandAlpha_));
    g.fillRect(band);
    g.setColour(c.accent.withAlpha(bandAlpha_));
    g.drawRect(band, 1.0f);

    const float handleH = std::min(kHandleHeight, std::max(4.0f, height - 8.0f));
    for (const float x : {x0, x1}) {
        if (x < -kHandleWidth || x > (float)getWidth() + kHandleWidth)
            continue;
        const juce::Rectangle<float> handle(x - kHandleWidth * 0.5f, (height - handleH) * 0.5f, kHandleWidth, handleH);
        g.fillRoundedRectangle(handle, 2.0f);
    }

    g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    const auto drawLabel = [&](const juce::String& text, juce::Rectangle<float> area, juce::Justification where) {
        g.setColour(c.bg0);
        for (const auto& offset : {std::pair<float, float>{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
            g.drawText(text, area.translated(offset.first, offset.second), where);
        g.setColour(c.textPrimary);
        g.drawText(text, area, where);
    };
    const float shift = (float)stripTop();
    const auto chevron = [&](float x, float dir, float y) {
        juce::Path p;
        p.startNewSubPath(x - dir * 1.5f, y - 3.0f);
        p.lineTo(x + dir * 1.5f, y);
        p.lineTo(x - dir * 1.5f, y + 3.0f);
        g.setColour(c.textPrimary);
        g.strokePath(p, juce::PathStrokeType(1.2f));
    };
    for (const Part edge : {Part::LowEdge, Part::HighEdge}) {
        if (!labelVisible(edge))
            continue;
        const bool low = edge == Part::LowEdge;
        const auto area = labelArea(edge).translated(0.0f, -shift);
        drawLabel(nameOf(low ? low_ : high_), area,
                  low ? juce::Justification::centredLeft : juce::Justification::centredRight);
        if (labelSpot(edge) == LabelSpot::Pinned) {
            const bool left = (low ? bandX0() : bandX1()) < 1.0f;
            chevron(left ? 4.0f : (float)getWidth() - 4.0f, left ? -1.0f : 1.0f, area.getCentreY());
        }
    }
}

void NoteRangePicker::paint(juce::Graphics& g) {
    if (overviewHeight() > 0)
        paintOverview(g);
    g.saveState();
    g.setOrigin(0, stripTop());
    g.reduceClipRegion(0, 0, getWidth(), stripHeight());
    paintKeys(g);
    paintBand(g);
    g.restoreState();
}

// ---- Parts under the pointer, tooltip and accessibility ---------------------------------------------------

std::optional<NoteRangePicker::Part> NoteRangePicker::partAt(float x) const {
    const float w = (float)getWidth();
    const float x0 = bandX0();
    const float x1 = bandX1();
    const float lowDistance = x0 >= -kEdgeGrabPx && x0 <= w + kEdgeGrabPx ? std::abs(x - x0) : 1.0e9f;
    const float highDistance = x1 >= -kEdgeGrabPx && x1 <= w + kEdgeGrabPx ? std::abs(x - x1) : 1.0e9f;
    if (std::min(lowDistance, highDistance) <= kEdgeGrabPx) {
        if (lowDistance == highDistance)
            return x < x0 ? Part::LowEdge : Part::HighEdge;
        return lowDistance < highDistance ? Part::LowEdge : Part::HighEdge;
    }
    if (x > x0 && x < x1)
        return Part::Band;
    return std::nullopt;
}

std::optional<NoteRangePicker::Part> NoteRangePicker::labelAt(const juce::Point<float>& p) const {
    for (const auto edge : {Part::LowEdge, Part::HighEdge})
        if (labelVisible(edge) && labelArea(edge).expanded(1.0f, 2.0f).contains(p))
            return edge;
    return std::nullopt;
}

juce::String NoteRangePicker::getTooltip() {
    const auto p = getMouseXYRelative().toFloat();
    if (p.y < (float)stripTop() && overviewHeight() > 0)
        return "The whole keyboard. Click or drag to show other octaves";
    if (const auto label = labelAt(p))
        return *label == Part::LowEdge ? "Click to type the lowest note" : "Click to type the highest note";
    if (const auto part = partAt(p.x)) {
        if (*part == Part::Band)
            return "Drag to move the range a note at a time, Shift for octaves";
        return partName(*part);
    }
    return "Click to move the range here";
}

std::unique_ptr<juce::AccessibilityHandler> NoteRangePicker::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
