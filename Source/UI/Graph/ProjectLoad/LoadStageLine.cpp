// LoadStageLine.cpp -- layout over the canvas, the fade, and the pill with its progress line.

#include "LoadStageLine.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kWidth = 240;
constexpr int kHeight = 34;
constexpr int kTopGap = 12;
} // namespace

LoadStageLine::LoadStageLine(juce::Component& canvas)
    : canvas_(canvas) {
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setTitle("Project loading");
    canvas_.addComponentListener(this);
}

LoadStageLine::~LoadStageLine() {
    fadeDriver_.stop(vblank_);
    canvas_.removeComponentListener(this);
}

void LoadStageLine::follow() {
    auto* parent = getParentComponent();
    if (parent == nullptr || !parent->isParentOf(&canvas_))
        return;
    const auto area = parent->getLocalArea(canvas_.getParentComponent(), canvas_.getBounds());
    setBounds(
        juce::Rectangle<int>(kWidth, kHeight).withCentre({area.getCentreX(), area.getY() + kTopGap + kHeight / 2}));
}

// The text changes in place; only appearing and leaving fade (160 ms in, 110 ms out, the small reveal's numbers).
void LoadStageLine::update(const juce::String& stage, float progress, bool shown) {
    if (stage != stage_ || progress != progress_) {
        stage_ = stage;
        progress_ = juce::jlimit(0.0f, 1.0f, progress);
        setDescription(stage_);
        repaint();
    }
    if (shown == shown_)
        return;
    shown_ = shown;
    if (shown) {
        follow();
        toFront(false);
        setVisible(true);
    }
    const float from = fade_;
    const float to = shown ? 1.0f : 0.0f;
    if (!isShowing()) {
        fade_ = to;
        landFade();
        return;
    }
    const double ms = shown ? motionMs(160.0, 80.0) : motionMs(110.0, 80.0);
    fadeDriver_.start(
        vblank_, juce::jmax(1.0, ms), shown ? easeOutCubic : easeInCubic,
        [this, from, to](float t) {
            fade_ = from + (to - from) * t;
            setAlpha(fade_);
        },
        [this] { landFade(); });
}

void LoadStageLine::landFade() {
    setAlpha(fade_);
    if (!shown_)
        setVisible(false);
}

void LoadStageLine::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colors.surfaceHi.withAlpha(0.94f));
    g.fillRoundedRectangle(box, 8.0f);
    g.setColour(colors.border);
    g.drawRoundedRectangle(box, 8.0f, 1.0f);

    auto inner = getLocalBounds().reduced(12, 6);
    const auto bar = inner.removeFromBottom(3).toFloat();
    g.setColour(colors.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(stage_, inner, juce::Justification::centred, true);
    g.setColour(colors.border);
    g.fillRoundedRectangle(bar, 1.5f);
    g.setColour(colors.accent);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * progress_), 1.5f);
}

} // namespace synth::ui
