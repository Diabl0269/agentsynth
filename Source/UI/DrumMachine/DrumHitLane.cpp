// DrumHitLane: the bars, the one-drag ramp, the keyboard and the screen-reader text of every lane under the grid.

#include "DrumHitLane.h"

#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kValueTextPad = 2.0f;
constexpr float kValueTextHeight = 12.0f;
constexpr int kLabelledBarMinWidth = 16;
constexpr float kCentreLineAlpha = 0.35f;
} // namespace

DrumHitLane::DrumHitLane(DrumLaneKind kind)
    : spec_(drumLaneSpec(kind)) {
    setComponentID(spec_.componentId);
    setTitle(spec_.name + " lane");
    setTooltip(spec_.tooltip);
    setWantsKeyboardFocus(true);
}

void DrumHitLane::setHits(std::vector<Hit> hits, const juce::String& rowName, juce::Colour colour) {
    const int focusStep =
        focusedHit_ >= 0 && focusedHit_ < static_cast<int>(hits_.size()) ? hits_[(size_t)focusedHit_].step : 0;
    hits_ = std::move(hits);
    preview_.clear();
    rowName_ = rowName;
    colour_ = colour;
    // Keep the keyboard on the same step; when it is gone, on the nearest hit after it.
    focusedHit_ = hits_.empty() ? -1 : static_cast<int>(hits_.size()) - 1;
    for (size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].step >= focusStep) {
            focusedHit_ = static_cast<int>(i);
            break;
        }
    repaint();
}

void DrumHitLane::setFocusedHit(int index) {
    focusedHit_ = hits_.empty() ? -1 : juce::jlimit(0, static_cast<int>(hits_.size()) - 1, index);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    repaint();
}

int DrumHitLane::valueOf(int index) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()))
        return 0;
    return preview_.size() == hits_.size() ? preview_[(size_t)index] : hits_[(size_t)index].value;
}

juce::Rectangle<int> DrumHitLane::barsArea() const {
    return getLocalBounds().withTrimmedLeft(mapping_.gutterWidth).reduced(0, kBarInsetPx);
}

// A bar is at least a pixel tall whatever its value (a chance of 0 or no repeat still has a bar to see and focus). A
// bipolar lane draws from its centre line: up for a positive value, down for a negative one.
juce::Rectangle<int> DrumHitLane::barBounds(int index) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()) || !mapping_.stepRange)
        return {};
    const auto area = barsArea();
    const auto range = mapping_.stepRange(hits_[(size_t)index].step);
    const double fraction = spec_.fractionOf(valueOf(index));
    const double span = static_cast<double>(area.getHeight());
    int top = 0, height = 1;
    if (spec_.bipolar) {
        const int centre = area.getCentreY();
        const int extent = static_cast<int>(std::lround(std::abs(fraction - 0.5) * span));
        height = std::max(1, extent);
        top = fraction >= 0.5 ? centre - height : centre;
    } else {
        height = std::max(1, static_cast<int>(std::lround(span * fraction)));
        top = area.getBottom() - height;
    }
    const int inset = std::min(2, std::max(0, (range.getLength() - 1) / 2));
    return {range.getStart() + inset, top, std::max(1, range.getLength() - 2 * inset), height};
}

//==============================================================================
void DrumHitLane::paintGutter(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.bg1);
    g.setColour(theme.colors.surface);
    g.fillRect(0, 0, mapping_.gutterWidth, getHeight());
    g.setColour(theme.colors.border);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(mapping_.gutterWidth - 1, 0.0f, static_cast<float>(getHeight()));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1.0f, juce::Font::plain));
    g.setColour(theme.colors.textPrimary);
    g.drawText(spec_.name, juce::Rectangle<int>(8, 0, mapping_.gutterWidth - 12, getHeight()),
               juce::Justification::centredLeft, true);
}

void DrumHitLane::paintBar(juce::Graphics& g, int index) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bar = barBounds(index);
    if (bar.isEmpty())
        return;
    g.setColour(theme.colors.accent);
    g.fillRoundedRectangle(bar.toFloat(), theme.metrics.cornerRadiusSmall);
    // The value reads on hover or while the bar is being set: a bar this narrow has no room for it otherwise. A
    // discrete lane (Repeat, Condition) labels every bar wide enough, since its value is a word.
    const bool hovered = isMouseOver(true) && bar.contains(getMouseXYRelative());
    if (!dragging_ && !hovered && !(spec_.isDiscrete() && bar.getWidth() >= kLabelledBarMinWidth))
        return;
    g.setColour(theme.colors.textPrimary);
    const float y = std::max(0.0f, static_cast<float>(bar.getY()) - kValueTextHeight - kValueTextPad);
    g.drawText(spec_.textOf(valueOf(index)),
               juce::Rectangle<float>(static_cast<float>(bar.getX()) - 8.0f, y,
                                      static_cast<float>(bar.getWidth()) + 16.0f, kValueTextHeight),
               juce::Justification::centred, false);
}

void DrumHitLane::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    paintGutter(g);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedLeft(mapping_.gutterWidth));
        if (spec_.bipolar) {
            g.setColour(theme.colors.textMuted.withAlpha(kCentreLineAlpha));
            g.drawHorizontalLine(barsArea().getCentreY(), static_cast<float>(mapping_.gutterWidth),
                                 static_cast<float>(getWidth()));
        }
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value - 1.0f, juce::Font::plain));
        for (int i = 0; i < static_cast<int>(hits_.size()); ++i)
            paintBar(g, i);
    }
    if (hasKeyboardFocus(false) && focusedHit_ >= 0) {
        const auto bar = barBounds(focusedHit_);
        if (!bar.isEmpty())
            paintFocusRingAlways(g, bar.toFloat().expanded(1.5f), *this, theme.metrics.cornerRadiusSmall);
    }
}

//==============================================================================
int DrumHitLane::valueAtY(int y) const {
    const auto area = barsArea();
    return spec_.valueAtFraction(static_cast<double>(area.getBottom() - y) / std::max(1, area.getHeight()));
}

// A hit takes the straight line between two pointer samples (in height, so a discrete lane steps through its options)
// at its column's centre when the pointer crosses it. A hit the pointer only rests on (a press, or the end of a drag
// short of its centre) takes the pointer's height for now, until a later crossing sets it exactly.
void DrumHitLane::applyPoint(int x, int y) {
    if (preview_.size() != hits_.size()) {
        preview_.clear();
        crossed_.assign(hits_.size(), 0);
        for (const auto& hit : hits_)
            preview_.push_back(hit.value);
    }
    const int fromX = dragging_ ? lastX_ : x;
    const int lo = std::min(fromX, x);
    const int hi = std::max(fromX, x);
    for (size_t i = 0; i < hits_.size(); ++i) {
        const auto range = mapping_.stepRange(hits_[i].step);
        const double centre = static_cast<double>(range.getStart() + range.getEnd()) * 0.5;
        if (dragging_ && centre >= lo && centre <= hi) {
            const double t = hi == lo ? 1.0 : (centre - fromX) / (x - fromX);
            preview_[i] = valueAtY(static_cast<int>(std::lround(lastY_ + (y - lastY_) * t)));
            crossed_[i] = 1;
        } else if (!crossed_[i] && range.getStart() <= x && x < range.getEnd()) {
            preview_[i] = valueAtY(y);
        }
    }
    lastX_ = x;
    lastY_ = y;
    dragging_ = true;
    repaint();
}

void DrumHitLane::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() || hits_.empty() || e.x < mapping_.gutterWidth)
        return;
    dragging_ = false;
    applyPoint(e.x, e.y);
}

void DrumHitLane::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    applyPoint(std::max(e.x, mapping_.gutterWidth), e.y);
}

void DrumHitLane::mouseUp(const juce::MouseEvent&) { endGesture(true); }

void DrumHitLane::endGesture(bool commit) {
    if (!dragging_)
        return;
    dragging_ = false;
    std::vector<Hit> changed;
    for (size_t i = 0; i < hits_.size() && i < preview_.size(); ++i)
        if (preview_[i] != hits_[i].value)
            changed.push_back({hits_[i].step, preview_[i]});
    if (!commit)
        preview_.clear();
    repaint();
    if (commit && !changed.empty() && onCommit)
        onCommit(changed); // the owner rewrites the notes and hands the lane its hits back
    preview_.clear();
}

//==============================================================================
bool DrumHitLane::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown() || hits_.empty())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        setFocusedHit(focusedHit_ + (code == juce::KeyPress::rightKey ? 1 : -1));
        return true;
    }
    if (code != juce::KeyPress::upKey && code != juce::KeyPress::downKey)
        return false;
    if (focusedHit_ < 0)
        return true;
    const int steps = (mods.isShiftDown() ? spec_.shiftStep : spec_.keyStep) * (code == juce::KeyPress::upKey ? 1 : -1);
    const auto& hit = hits_[(size_t)focusedHit_];
    const int next = spec_.stepped(hit.value, steps);
    if (next != hit.value && onCommit)
        onCommit({Hit{hit.step, next}});
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    return true;
}

juce::String DrumHitLane::getAccessibilityValueText() const {
    if (focusedHit_ < 0 || focusedHit_ >= static_cast<int>(hits_.size()))
        return rowName_ + ", no hits";
    const auto& hit = hits_[(size_t)focusedHit_];
    return rowName_ + ", step " + juce::String(hit.step + 1) + ", " + spec_.word + " " + spec_.textOf(hit.value);
}

void DrumHitLane::focusGained(FocusChangeType) {
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void DrumHitLane::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> DrumHitLane::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

} // namespace synth::ui
