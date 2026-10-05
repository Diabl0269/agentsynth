#pragma once

// The small icon buttons of the mod dot's panel (show in timeline, remove) and the glyphs the panel draws as
// paths, like the library's fold chevron: there is no icon set entry for them and a panel window would not
// inherit one anyway. A real Tab stop with the accent focus ring, a screen-reader name and a tooltip.

#include "ModDotMotion.h"
#include "ModDotPalette.h"
#include "UI/Layout/FocusRing.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

enum class ModDotGlyph { Timeline, Trash, List, Crosshair };

/** The colour `glyph` is drawn in whatever the state (resting, hovered, focused, pressed): the panel's icons are
 *  always in colour, never greyed until a state change. Remove is the negative colour, the lanes of "show in
 *  timeline" and the crosshair the positive one, the list the accent. `hover` (0..1) only brightens it. */
juce::Colour modDotGlyphColour(const ModDotPalette& palette, ModDotGlyph glyph, float hover = 0.0f);

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
    /** The colour the glyph is drawn in right now. */
    juce::Colour glyphColour() const { return modDotGlyphColour(modDotPaletteFor(*this), glyph_, hover_.value()); }

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
                         danger_ ? p.negative.brighter(0.2f) : modDotGlyphColour(p, glyph_, hover_.value()));
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
