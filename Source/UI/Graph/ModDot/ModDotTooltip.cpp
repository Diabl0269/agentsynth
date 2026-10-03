#include "ModDotTooltip.h"

#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kGap = 2.0f;
constexpr float kPadX = 6.0f;
constexpr float kHeight = 18.0f;
constexpr float kSwatch = 6.0f;
constexpr float kSwatchGap = 5.0f;
constexpr float kFontPx = 11.0f;
constexpr float kRadius = 5.0f;

juce::Font monoFont(const juce::Component& c) {
    juce::String family = juce::Font::getDefaultMonospacedFontName();
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        family = lf->getTheme().type.monoFamily;
    return juce::Font(juce::FontOptions(family, kFontPx, juce::Font::plain));
}
} // namespace

juce::Rectangle<float> modDotTooltipRect(juce::Rectangle<float> knob, juce::Point<float> size,
                                         juce::Rectangle<float> viewport) {
    auto place = [&size, &viewport](float x, float y) {
        const float maxX = std::max(viewport.getX(), viewport.getRight() - size.x);
        const float maxY = std::max(viewport.getY(), viewport.getBottom() - size.y);
        return juce::Rectangle<float>(juce::jlimit(viewport.getX(), maxX, x), juce::jlimit(viewport.getY(), maxY, y),
                                      size.x, size.y);
    };
    auto rect = place(knob.getRight() + kGap, knob.getY() - kGap - size.y);
    if (rect.intersects(knob))
        rect = place(knob.getRight() + kGap, knob.getBottom() + kGap); // no room above: below-right
    if (rect.intersects(knob))
        rect = place(knob.getX() - kGap - size.x, knob.getY() - kGap - size.y); // no room at the right: above-left
    return rect;
}

ModDotTooltip::ModDotTooltip(juce::Component& canvas)
    : canvas_(canvas)
    , vblank_(&canvas) {}

ModDotTooltip::~ModDotTooltip() { fade_.stop(vblank_); }

juce::Point<float> ModDotTooltip::sizeFor(const juce::String& text) const {
    const float textW = juce::GlyphArrangement::getStringWidth(monoFont(canvas_), text);
    return {std::ceil(textW) + kPadX * 2.0f + kSwatch + kSwatchGap, kHeight};
}

juce::Rectangle<float> ModDotTooltip::getBounds() const {
    if (!content_ || content_->knob == nullptr)
        return {};
    auto* knob = content_->knob.getComponent();
    const auto knobRect = canvas_.getLocalArea(knob, knob->getLocalBounds()).toFloat();
    // The visible viewport is the editor the canvas is a child of, in the canvas' own (zoomed) space.
    const auto* editor = canvas_.getParentComponent();
    const auto viewport = editor != nullptr ? canvas_.getLocalArea(editor, editor->getLocalBounds()).toFloat()
                                            : canvas_.getLocalBounds().toFloat();
    return modDotTooltipRect(knobRect, sizeFor(content_->text), viewport);
}

bool ModDotTooltip::animates() const { return canvas_.isShowing(); }

void ModDotTooltip::show(juce::Component& knob, const juce::String& name, float amount) {
    const auto before = getBounds();
    content_ = Content{juce::Component::SafePointer<juce::Component>(&knob), formatModHoverChipText(name, amount),
                       amount >= 0.0f};
    const bool appeared = !wanted_;
    wanted_ = true;
    if (appeared)
        fadeTo(1.0f, kFadeInMs, easeOutCubic);
    else
        canvas_.repaint((before.getUnion(getBounds())).getSmallestIntegerContainer().expanded(2));
}

void ModDotTooltip::hide() {
    if (!wanted_)
        return;
    wanted_ = false;
    fadeTo(0.0f, kFadeOutMs, easeInCubic);
}

// Headless or hidden there is nothing to animate: it lands at once. Under Reduce Motion it is a plain,
// short, linear fade rather than the eased one.
void ModDotTooltip::fadeTo(float target, double fullMs, std::function<float(float)> easing) {
    if (!animates()) {
        fade_.stop(vblank_);
        setOpacity(target);
        return;
    }
    const float from = opacity_;
    if (prefersReducedMotion()) {
        fullMs = kReducedFadeMs;
        easing = [](float t) { return t; };
    }
    const double ms = std::max(1.0, std::abs((double)target - (double)from) * fullMs);
    fade_.start(vblank_, ms, std::move(easing),
                [this, from, target](float e) { setOpacity(from + (target - from) * e); });
}

void ModDotTooltip::setOpacity(float value) {
    opacity_ = juce::jlimit(0.0f, 1.0f, value);
    if (opacity_ <= 0.0f && !wanted_)
        content_.reset(); // fully gone: nothing left to draw
    canvas_.repaint();
}

void ModDotTooltip::paint(juce::Graphics& g) {
    if (!content_ || opacity_ <= 0.0f)
        return;
    const auto box = getBounds();
    if (box.isEmpty())
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&canvas_.getLookAndFeel());
    const synth::theme::Theme* theme = lf != nullptr ? &lf->getTheme() : nullptr;
    static const synth::theme::Theme fallback{};
    const auto& colors = (theme != nullptr ? *theme : fallback).colors;

    // Opaque even when a theme's surfaceHi is translucent, like the hover chip: it sits over cards.
    g.setColour(colors.surface.overlaidWith(colors.surfaceHi).withAlpha(1.0f).withMultipliedAlpha(opacity_));
    g.fillRoundedRectangle(box, kRadius);
    g.setColour(colors.border.withMultipliedAlpha(opacity_));
    g.drawRoundedRectangle(box.reduced(0.5f), kRadius, 1.0f);

    const auto swatch =
        juce::Rectangle<float>(kSwatch, kSwatch).withCentre({box.getX() + kPadX + kSwatch * 0.5f, box.getCentreY()});
    g.setColour((content_->positive ? colors.modRingPositive : colors.modRingNegative).withMultipliedAlpha(opacity_));
    g.fillEllipse(swatch);

    g.setColour(colors.textPrimary.withMultipliedAlpha(opacity_));
    g.setFont(monoFont(canvas_));
    g.drawText(content_->text, box.withTrimmedLeft(kPadX + kSwatch + kSwatchGap).withTrimmedRight(kPadX),
               juce::Justification::centredLeft, false);
}

} // namespace synth::ui
