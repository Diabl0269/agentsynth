#pragma once

// The small icon buttons of the mod dot's panel (back, show in timeline, remove) and the glyphs the panel draws as
// paths, like the library's fold chevron: there is no icon set entry for them and a parentless callout would not
// inherit one anyway. A real Tab stop with the accent focus ring, a screen-reader name and a tooltip.

#include "ModDotMotion.h"
#include "ModDotPalette.h"
#include "UI/Layout/FocusRing.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

enum class ModDotGlyph { Back, Timeline, Trash };

/** Strokes `glyph` inside `area` (a square). */
void paintModDotGlyph(juce::Graphics& g, ModDotGlyph glyph, juce::Rectangle<float> area, juce::Colour colour);
/** The fold chevron: pointing right at `progress` 0 (folded), down at 1, turning 90 degrees between. */
void paintModDotChevron(juce::Graphics& g, juce::Rectangle<float> area, float progress, juce::Colour colour);

class ModDotGlyphButton final : public juce::Button {
public:
    static constexpr int kSize = 24;

    static constexpr double kPulseMs = 420.0;

    ModDotGlyphButton(ModDotGlyph glyph, const juce::String& name, const juce::String& tooltip)
        : juce::Button(name)
        , glyph_(glyph)
        , hover_(*this)
        , updater_(this) {
        setTitle(name);
        setTooltip(tooltip);
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    ~ModDotGlyphButton() override { pulseAnim_.stop(updater_); }

    /** Draws the button in the negative colour at full strength with one short pulse (the "choose which one to
     *  remove" state of a double-click on the dot); a pulse is skipped under reduced motion. */
    void setDanger(bool on) {
        if (danger_ == on)
            return;
        danger_ = on;
        pulseAnim_.stop(updater_);
        pulse_ = 0.0f;
        if (on && modDotMotionAllowed(*this)) {
            pulse_ = 1.0f;
            pulseAnim_.start(
                updater_, kPulseMs, easeOutCubic,
                [this](float t) {
                    pulse_ = 1.0f - t;
                    repaint();
                },
                [this] {
                    pulse_ = 0.0f;
                    repaint();
                });
        }
        repaint();
    }
    bool isDanger() const noexcept { return danger_; }

    bool keyPressed(const juce::KeyPress& key) override {
        if (isEnabled() && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)) {
            triggerClick();
            return true;
        }
        return false;
    }
    void mouseEnter(const juce::MouseEvent& e) override {
        juce::Button::mouseEnter(e);
        hover_.setHovered(true);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        juce::Button::mouseExit(e);
        hover_.setHovered(false);
    }

    void paintButton(juce::Graphics& g, bool, bool down) override {
        const auto p = modDotPaletteFor(*this);
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(p.hover.withAlpha(down ? 1.0f : hover_.value()));
        g.fillRoundedRectangle(bounds, 5.0f);
        if (danger_) {
            g.setColour(p.negative.withAlpha(0.16f + 0.34f * pulse_));
            g.fillRoundedRectangle(bounds, 5.0f);
        }
        paintModDotGlyph(g, glyph_, bounds.withSizeKeepingCentre(14.0f, 14.0f),
                         danger_ || (glyph_ == ModDotGlyph::Trash && hover_.value() > 0.5f)
                             ? p.negative
                             : p.muted.interpolatedWith(p.text, hover_.value()));
        paintFocusRing(g, bounds, *this, 5.0f);
    }

private:
    ModDotGlyph glyph_;
    ModDotHoverFade hover_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver pulseAnim_;
    bool danger_ = false;
    float pulse_ = 0.0f;
};

} // namespace synth::ui
