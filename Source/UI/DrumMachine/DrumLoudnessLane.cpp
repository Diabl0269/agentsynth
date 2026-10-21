// DrumLoudnessLane: the bars, the one-drag ramp, the keyboard and the screen-reader text of the Loudness lane.

#include "DrumLoudnessLane.h"

#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMaxVelocity = 127;
constexpr float kValueTextPad = 2.0f;
constexpr float kValueTextHeight = 12.0f;
} // namespace

DrumLoudnessLane::DrumLoudnessLane() {
    setComponentID("drumLoudnessLane");
    setTitle("Loudness lane");
    setTooltip("Loudness: drag across the bars to set how loud each hit is. Left and Right pick a hit, Up and Down "
               "change its loudness");
    setWantsKeyboardFocus(true);
}

void DrumLoudnessLane::setHits(std::vector<Hit> hits, const juce::String& rowName, juce::Colour colour) {
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

void DrumLoudnessLane::setFocusedHit(int index) {
    focusedHit_ = hits_.empty() ? -1 : juce::jlimit(0, static_cast<int>(hits_.size()) - 1, index);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    repaint();
}

int DrumLoudnessLane::velocityOf(int index) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()))
        return 0;
    return preview_.size() == hits_.size() ? preview_[(size_t)index] : hits_[(size_t)index].velocity;
}

juce::Rectangle<int> DrumLoudnessLane::barsArea() const {
    return getLocalBounds().withTrimmedLeft(mapping_.gutterWidth).reduced(0, kBarInsetPx);
}

juce::Rectangle<int> DrumLoudnessLane::barBounds(int index) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()) || !mapping_.stepRange)
        return {};
    const auto area = barsArea();
    const auto range = mapping_.stepRange(hits_[(size_t)index].step);
    const int height =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(area.getHeight()) *
                                                 static_cast<double>(velocityOf(index)) / kMaxVelocity)));
    const int inset = std::min(2, std::max(0, (range.getLength() - 1) / 2));
    return {range.getStart() + inset, area.getBottom() - height, std::max(1, range.getLength() - 2 * inset), height};
}

//==============================================================================
void DrumLoudnessLane::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.bg1);
    g.setColour(theme.colors.surface);
    g.fillRect(0, 0, mapping_.gutterWidth, getHeight());
    g.setColour(theme.colors.border);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(mapping_.gutterWidth - 1, 0.0f, static_cast<float>(getHeight()));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1.0f, juce::Font::plain));
    g.setColour(theme.colors.textPrimary);
    g.drawText("Loudness", juce::Rectangle<int>(8, 0, mapping_.gutterWidth - 12, getHeight()),
               juce::Justification::centredLeft, true);

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedLeft(mapping_.gutterWidth));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value - 1.0f, juce::Font::plain));
        for (int i = 0; i < static_cast<int>(hits_.size()); ++i) {
            const auto bar = barBounds(i);
            if (bar.isEmpty())
                continue;
            g.setColour(theme.colors.accent);
            g.fillRoundedRectangle(bar.toFloat(), theme.metrics.cornerRadiusSmall);
            // The value reads on hover or while the bar is being set: a bar this wide has no room for it otherwise.
            if (dragging_ || (isMouseOver(true) && bar.contains(getMouseXYRelative()))) {
                g.setColour(theme.colors.textPrimary);
                const float y = std::max(0.0f, static_cast<float>(bar.getY()) - kValueTextHeight - kValueTextPad);
                g.drawText(juce::String(velocityOf(i)),
                           juce::Rectangle<float>(static_cast<float>(bar.getX()) - 8.0f, y,
                                                  static_cast<float>(bar.getWidth()) + 16.0f, kValueTextHeight),
                           juce::Justification::centred, false);
            }
        }
    }
    if (hasKeyboardFocus(false) && focusedHit_ >= 0) {
        const auto bar = barBounds(focusedHit_);
        if (!bar.isEmpty())
            paintFocusRingAlways(g, bar.toFloat().expanded(1.5f), *this, theme.metrics.cornerRadiusSmall);
    }
}

//==============================================================================
int DrumLoudnessLane::velocityAtY(int y) const {
    const auto area = barsArea();
    const double fraction = static_cast<double>(area.getBottom() - y) / std::max(1, area.getHeight());
    return juce::jlimit(1, kMaxVelocity, static_cast<int>(std::lround(fraction * kMaxVelocity)));
}

// A hit takes the straight line between two pointer samples at its column's centre when the pointer crosses it. A hit
// the pointer only rests on (a press, or the end of a drag short of its centre) takes the pointer's height for now,
// until a later crossing sets it exactly.
void DrumLoudnessLane::applyPoint(int x, int velocity) {
    if (preview_.size() != hits_.size()) {
        preview_.clear();
        crossed_.assign(hits_.size(), 0);
        for (const auto& hit : hits_)
            preview_.push_back(hit.velocity);
    }
    const int fromX = dragging_ ? lastX_ : x;
    const int lo = std::min(fromX, x);
    const int hi = std::max(fromX, x);
    for (size_t i = 0; i < hits_.size(); ++i) {
        const auto range = mapping_.stepRange(hits_[i].step);
        const double centre = static_cast<double>(range.getStart() + range.getEnd()) * 0.5;
        if (dragging_ && centre >= lo && centre <= hi) {
            const double t = hi == lo ? 1.0 : (centre - fromX) / (x - fromX);
            preview_[i] = juce::jlimit(1, kMaxVelocity,
                                       static_cast<int>(std::lround(lastVelocity_ + (velocity - lastVelocity_) * t)));
            crossed_[i] = 1;
        } else if (!crossed_[i] && range.getStart() <= x && x < range.getEnd()) {
            preview_[i] = velocity;
        }
    }
    lastX_ = x;
    lastVelocity_ = velocity;
    dragging_ = true;
    repaint();
}

void DrumLoudnessLane::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() || hits_.empty() || e.x < mapping_.gutterWidth)
        return;
    dragging_ = false;
    applyPoint(e.x, velocityAtY(e.y));
}

void DrumLoudnessLane::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    applyPoint(std::max(e.x, mapping_.gutterWidth), velocityAtY(e.y));
}

void DrumLoudnessLane::mouseUp(const juce::MouseEvent&) { endGesture(true); }

void DrumLoudnessLane::endGesture(bool commit) {
    if (!dragging_)
        return;
    dragging_ = false;
    std::vector<Hit> changed;
    for (size_t i = 0; i < hits_.size() && i < preview_.size(); ++i)
        if (preview_[i] != hits_[i].velocity)
            changed.push_back({hits_[i].step, preview_[i]});
    if (!commit)
        preview_.clear();
    repaint();
    if (commit && !changed.empty() && onCommit)
        onCommit(changed); // the owner rewrites the notes and hands the lane its hits back
    preview_.clear();
}

//==============================================================================
bool DrumLoudnessLane::keyPressed(const juce::KeyPress& key) {
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
    const int delta = (mods.isShiftDown() ? kKeyStepShift : kKeyStep) * (code == juce::KeyPress::upKey ? 1 : -1);
    const auto& hit = hits_[(size_t)focusedHit_];
    const int next = juce::jlimit(1, kMaxVelocity, hit.velocity + delta);
    if (next != hit.velocity && onCommit)
        onCommit({Hit{hit.step, next}});
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    return true;
}

juce::String DrumLoudnessLane::getAccessibilityValueText() const {
    if (focusedHit_ < 0 || focusedHit_ >= static_cast<int>(hits_.size()))
        return rowName_ + ", no hits";
    const auto& hit = hits_[(size_t)focusedHit_];
    return rowName_ + ", step " + juce::String(hit.step + 1) + ", loudness " + juce::String(hit.velocity);
}

void DrumLoudnessLane::focusGained(FocusChangeType) {
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void DrumLoudnessLane::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> DrumLoudnessLane::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

} // namespace synth::ui
