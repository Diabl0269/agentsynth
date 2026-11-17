#include "SideMenuMark.h"

#include "UI/Timeline/TrackColour.h"
#include <algorithm>

namespace synth::ui {

// Concern: the menu mark shared by the icon buttons that have a side menu, and the gesture that opens it.

namespace {
constexpr float kNotchDisabledAlpha = 0.4f;
constexpr float kMinNotchContrast = 3.5f; // above the 3:1 the notch must reach, for the fill's own variation
constexpr int kHoldDragPx = 4;
// The rounded corner cuts the triangle's tip by radius * (sqrt 2 - 1) along the diagonal; the leg grows by twice that
// (2 - sqrt 2) a radius, past a 3.5 px base, so the visible notch keeps its depth on a round chip.
constexpr float kLegPerRadius = 0.586f;
constexpr float kLegBase = 3.5f;

float worstContrast(juce::Colour ink, std::initializer_list<juce::Colour> fills) {
    float worst = 21.0f;
    for (const auto fill : fills)
        worst = std::min(worst, colourContrast(ink, fill));
    return worst;
}
} // namespace

// The theme inks are tried first so the notch looks like part of the theme; the lit accent chip can swallow the
// primary text, so the best of the four wins, and a fill none of them reads on gets plain black or white.
juce::Colour sideMenuNotchInk(const synth::theme::Colors& colours, std::initializer_list<juce::Colour> fills) {
    const juce::Colour candidates[] = {colours.iconInk, colours.iconPaper, colours.textPrimary, colours.bg0};
    auto best = candidates[0];
    for (const auto& candidate : candidates)
        if (worstContrast(candidate, fills) > worstContrast(best, fills))
            best = candidate;
    if (worstContrast(best, fills) >= kMinNotchContrast)
        return best;
    for (const auto fallback : {juce::Colours::black, juce::Colours::white})
        if (worstContrast(fallback, fills) > worstContrast(best, fills))
            best = fallback;
    return best;
}

float sideMenuNotchLegFor(float chipRadius) noexcept {
    return std::max(kSideMenuNotchLeg, kLegPerRadius * chipRadius + kLegBase);
}

// The notch is part of the chip: clipped to the chip's rounded border (squashed with it while pressed) and sitting in
// its lower-right corner, outside the glyph's box.
void paintSideMenuNotch(juce::Graphics& g, juce::Rectangle<float> chip, float chipRadius, juce::Point<float> squash,
                        juce::Colour ink, bool enabled) {
    const auto squashTransform = juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(), chip.getCentreY());
    juce::Path chipPath;
    chipPath.addRoundedRectangle(chip, chipRadius);
    chipPath.applyTransform(squashTransform);
    juce::Path notch;
    const float leg = sideMenuNotchLegFor(chipRadius);
    notch.addTriangle(chip.getRight() - leg, chip.getBottom(), chip.getRight(), chip.getBottom(), chip.getRight(),
                      chip.getBottom() - leg);
    notch.applyTransform(squashTransform);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(chipPath);
    g.setColour(enabled ? ink : ink.withMultipliedAlpha(kNotchDisabledAlpha));
    g.fillPath(notch);
}

bool isInSideMenuCorner(juce::Point<int> point, juce::Rectangle<int> chipBounds) noexcept {
    return juce::Rectangle<int>(chipBounds.getRight() - kSideMenuCornerSize,
                                chipBounds.getBottom() - kSideMenuCornerSize, kSideMenuCornerSize, kSideMenuCornerSize)
        .contains(point);
}

void SideMenuGesture::open() {
    if (onOpen)
        onOpen();
}

// A right-click or a corner press opens at once and is not a click; any other left press arms the hold timer and is
// left to the button. A hold that fires opens the menu and eats the release, so it does not also click.
bool SideMenuGesture::mouseDown(const juce::MouseEvent& e) {
    menuPress_ = false;
    holdFired_ = false;
    if (e.mods.isPopupMenu() ||
        (e.mods.isLeftButtonDown() && chipBounds && isInSideMenuCorner(e.getPosition(), chipBounds()))) {
        menuPress_ = true;
        if (onPressSwallowed)
            onPressSwallowed();
        open();
        return true;
    }
    if (e.mods.isLeftButtonDown())
        startTimer(kHoldMs);
    return false;
}

bool SideMenuGesture::mouseDrag(const juce::MouseEvent& e) {
    if (menuPress_ || holdFired_)
        return true;
    if (e.getDistanceFromDragStart() > kHoldDragPx)
        stopTimer();
    return false;
}

bool SideMenuGesture::mouseUp(const juce::MouseEvent& e) {
    stopTimer();
    if (menuPress_) {
        menuPress_ = false;
        return true;
    }
    if (holdFired_) {
        if (onPressSwallowed)
            onPressSwallowed();
        // holdFired_ stays set across the callback: the menu ignores outside input while the press is down.
        if (onHoldReleased)
            onHoldReleased(e.getScreenPosition());
        holdFired_ = false;
        return true;
    }
    return false;
}

void SideMenuGesture::mouseExit() { stopTimer(); }

void SideMenuGesture::timerCallback() {
    stopTimer();
    holdFired_ = true;
    if (onPressSwallowed)
        onPressSwallowed();
    open();
}

} // namespace synth::ui
