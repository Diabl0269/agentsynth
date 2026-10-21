// StepGridPageStrip.cpp -- the page bars, the playhead pulse (started by the grid on a playhead step, never a
// free-running timer), the keyboard and the screen-reader text.
#include "StepGridPageStrip.h"
#include "StepGridModel.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kFollowWidth = 62;
constexpr int kBarGap = 3;
constexpr int kBarWidth = 52;
constexpr double kPulseMs = 220.0;

} // namespace

StepGridPageStrip::StepGridPageStrip() {
    setTitle("Pages");
    setTooltip("Pages of 16 steps. Click a page to view it; Left and Right move, Enter views the page");
    setWantsKeyboardFocus(true);
    follow_.setTitle("Follow");
    follow_.setTooltip("Follow: turn pages with the playhead");
    follow_.setClickingTogglesState(true);
    follow_.setWantsKeyboardFocus(true);
    setTogglePillStyle(follow_, true);
    follow_.onClick = [this] {
        if (onFollow)
            onFollow(follow_.getToggleState());
    };
    addAndMakeVisible(follow_);
    setVisible(false);
}

void StepGridPageStrip::setPages(int count, int viewed, int playing, int steps) {
    count = std::max(1, count);
    viewed = juce::jlimit(0, count - 1, viewed);
    const bool changed = count != count_ || viewed != viewed_ || playing != playing_ || steps != steps_;
    count_ = count;
    viewed_ = viewed;
    playing_ = playing;
    steps_ = steps;
    focused_ = juce::jlimit(0, count_ - 1, focused_);
    setVisible(count_ > 1);
    if (changed)
        repaint();
}

// The bar under the pointer says which steps it holds; elsewhere the strip's own tooltip.
juce::String StepGridPageStrip::getTooltip() {
    const int page = pageAt(getMouseXYRelative());
    return page >= 0 ? stepgrid::pageDescription(page, steps_) : juce::SettableTooltipClient::getTooltip();
}

void StepGridPageStrip::resized() { follow_.setBounds(getWidth() - kFollowWidth, 0, kFollowWidth, getHeight()); }

juce::Rectangle<int> StepGridPageStrip::pageBounds(int page) const {
    const int x = stepgrid::kGutterWidth + page * (kBarWidth + kBarGap);
    return {x, 2, kBarWidth, getHeight() - 4};
}

int StepGridPageStrip::pageAt(juce::Point<int> p) const {
    for (int i = 0; i < count_; ++i)
        if (pageBounds(i).contains(p))
            return i;
    return -1;
}

void StepGridPageStrip::pulse(int page) {
    if (page < 0 || page >= count_ || !isShowing())
        return;
    pulsePage_ = page;
    pulseAnim_.start(
        vblank_, motionMs(kPulseMs, 80.0), [](float t) { return t; },
        [this](float t) {
            pulseAmount_ = 1.0f - t;
            repaint();
        },
        [this] {
            pulseAmount_ = 0.0f;
            repaint();
        });
}

void StepGridPageStrip::paint(juce::Graphics& g) {
    const auto& c = theme::themeOf(*this).colors;
    g.setFont(theme::AppLookAndFeel::uiFont(11.0f));
    for (int i = 0; i < count_; ++i) {
        const auto bar = pageBounds(i).toFloat();
        const bool viewed = i == viewed_;
        const bool playing = i == playing_;
        auto fill = viewed ? c.accent.withAlpha(0.35f) : c.surfaceHi;
        if (playing && i == pulsePage_)
            fill = fill.interpolatedWith(c.accent, 0.6f * pulseAmount_);
        g.setColour(fill);
        g.fillRoundedRectangle(bar, 4.0f);
        g.setColour(playing ? c.accent : c.border);
        g.drawRoundedRectangle(bar.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(viewed ? c.textPrimary : c.textMuted);
        g.drawText(stepgrid::pageLabel(i, steps_), bar.toNearestInt(), juce::Justification::centred);
    }
}

void StepGridPageStrip::paintOverChildren(juce::Graphics& g) {
    if (hasKeyboardFocus(false))
        paintFocusRingAlways(g, pageBounds(focused_).toFloat(), *this, 4.0f);
}

void StepGridPageStrip::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;
    const int page = pageAt(e.getPosition());
    if (page >= 0) {
        focused_ = page;
        if (onViewPage)
            onViewPage(page);
    }
}

bool StepGridPageStrip::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        focused_ = juce::jlimit(0, count_ - 1, focused_ + (code == juce::KeyPress::rightKey ? 1 : -1));
        repaint();
        return true;
    }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey) {
        if (onViewPage)
            onViewPage(focused_);
        return true;
    }
    return false;
}

void StepGridPageStrip::focusGained(FocusChangeType) { repaint(); }
void StepGridPageStrip::focusLost(FocusChangeType) { repaint(); }

// "Page 2 of 3, playing": the focused page, which one is playing.
juce::String StepGridPageStrip::describe() const {
    auto text = "Page " + juce::String(focused_ + 1) + " of " + juce::String(count_);
    if (focused_ == playing_)
        text << ", playing";
    if (focused_ == viewed_)
        text << ", viewed";
    return text;
}

std::unique_ptr<juce::AccessibilityHandler> StepGridPageStrip::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return describe(); });
}

} // namespace synth::ui
