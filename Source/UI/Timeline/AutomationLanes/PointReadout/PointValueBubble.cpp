#include "PointValueBubble.h"

#include "UI/Chrome/ShortcutHint/ShortcutHintLayout.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kGapPx = 9; // between the point's centre and the bubble's edge
constexpr int kPadX = 6;  // horizontal text padding
constexpr int kPadY = 4;  // vertical text padding in total
constexpr float kFontPx = 10.0f;

struct Palette {
    juce::Colour fill{juce::Colours::black};
    juce::Colour text{juce::Colours::lightgrey};
    juce::Colour border{juce::Colours::grey};
    float fontPx = kFontPx;
};

Palette paletteFor(const juce::Component& c) {
    Palette p;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& theme = lf->getTheme();
        p.fill = theme.colors.surfaceHi;
        p.text = theme.colors.textPrimary;
        p.border = theme.colors.border;
        p.fontPx = theme.type.value + 1.0f;
    }
    return p;
}

juce::Font monoFont(float px) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
}

// Above the anchor, centred, kept inside `area`; below the anchor when there is no room above.
juce::Rectangle<int> boxFor(juce::Point<float> anchor, int w, int h, juce::Rectangle<int> area) {
    const int x =
        juce::jlimit(area.getX(), std::max(area.getX(), area.getRight() - w), (int)std::lround(anchor.x) - w / 2);
    int y = (int)std::lround(anchor.y) - kGapPx - h;
    if (y < area.getY())
        y = (int)std::lround(anchor.y) + kGapPx;
    y = juce::jlimit(area.getY(), std::max(area.getY(), area.getBottom() - h), y);
    return {x, y, w, h};
}
} // namespace

PointValueBubble::PointValueBubble(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

PointValueBubble::~PointValueBubble() { fade_.stop(vblank_); }

juce::Rectangle<int> PointValueBubble::getBounds() const {
    if (!content_)
        return {};
    const auto palette = paletteFor(owner_);
    const int w =
        (int)std::ceil(juce::GlyphArrangement::getStringWidth(monoFont(palette.fontPx), content_->text)) + 2 * kPadX;
    const int h = (int)std::ceil(palette.fontPx) + kPadY;
    return boxFor(content_->anchor, w, h, owner_.getLocalBounds());
}

// Headless, hidden or under Reduce Motion there is nothing worth animating: it lands at once.
bool PointValueBubble::animates() const { return owner_.isShowing() && !prefersReducedMotion(); }

void PointValueBubble::show(juce::Point<float> anchor, const juce::String& text) {
    const auto before = getBounds();
    content_ = Content{anchor, text};
    const bool appeared = !wanted_;
    wanted_ = true;
    if (appeared)
        fadeIn();
    else
        owner_.repaint(before.getUnion(getBounds()).expanded(2));
}

void PointValueBubble::hide() {
    if (!wanted_)
        return;
    wanted_ = false;
    fadeOut();
}

void PointValueBubble::fadeIn() {
    if (!animates()) {
        fade_.stop(vblank_);
        setOpacity(1.0f);
        return;
    }
    const float from = opacity_;
    fade_.start(vblank_, hint::resumeDurationMs(from, kFadeInMs), easeOutCubic,
                [this, from](float e) { setOpacity(hint::tweenUp(from, e)); });
}

void PointValueBubble::fadeOut() {
    if (!animates() || opacity_ <= 0.0f) {
        fade_.stop(vblank_);
        setOpacity(0.0f);
        return;
    }
    const float from = opacity_;
    fade_.start(vblank_, kFadeOutMs, easeInCubic, [this, from](float e) { setOpacity(hint::tweenDown(from, e)); });
}

void PointValueBubble::setOpacity(float value) {
    opacity_ = juce::jlimit(0.0f, 1.0f, value);
    if (opacity_ <= 0.0f && !wanted_)
        content_.reset(); // fully gone: nothing left to draw
    owner_.repaint();
}

void PointValueBubble::paint(juce::Graphics& g) {
    if (!content_ || opacity_ <= 0.0f)
        return;
    const auto palette = paletteFor(owner_);
    const auto box = getBounds().toFloat();
    g.setColour(palette.fill.withMultipliedAlpha(0.95f * opacity_));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(palette.border.withMultipliedAlpha(opacity_));
    g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(palette.text.withMultipliedAlpha(opacity_));
    g.setFont(monoFont(palette.fontPx));
    g.drawText(content_->text, box.toNearestInt(), juce::Justification::centred, false);
}

} // namespace synth::ui
