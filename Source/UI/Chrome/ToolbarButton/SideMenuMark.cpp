#include "SideMenuMark.h"

#include "UI/Timeline/TrackColour.h"
#include <algorithm>

namespace synth::ui {

// Concern: the menu mark shared by the icon buttons that have a side menu, and the gesture that opens it.

namespace {
constexpr float kNotchDisabledAlpha = 0.4f;
constexpr float kMinNotchContrast = 3.8f; // 3.5:1 plus room for Rich depth's wash and highlight painted over the chip
// How far the chip's own colour is pushed to make the notch ink: toward black on a light chip, toward white on a dark
// one.
constexpr float kDarkenStep = 0.5f;
constexpr float kLightenStep = 0.45f;
constexpr float kInkStepEvery = 0.02f; // when that misses the floor, the same push grows by this much
constexpr float kInkPushLimit = 0.98f; // and never reaches pure black or white
// Rich depth shades the lower part of a lit chip (where the notch sits) by about this much, so the ink also holds its
// floor against each fill darkened by it.
constexpr float kInsetShade = 0.12f;
constexpr int kHoldDragPx = 4;
// The rounded corner cuts the triangle's tip by radius * (sqrt 2 - 1) along the diagonal; the leg grows by twice that
// (2 - sqrt 2) a radius, past a 3.5 px base, so the visible notch keeps its depth on a round chip.
constexpr float kLegPerRadius = 0.586f;
constexpr float kLegBase = 3.5f;

bool holdsFloor(juce::Colour ink, std::initializer_list<juce::Colour> fills) {
    for (const auto fill : fills)
        if (colourContrast(ink, fill) < kMinNotchContrast ||
            colourContrast(ink, fill.darker(kInsetShade)) < kMinNotchContrast)
            return false;
    return true;
}
} // namespace

// The notch is the chip's own painted colour pushed away from itself: darker on a light chip, lighter on a dark one
// (a chip is light when black reads better on it than white). The push starts at kDarkenStep / kLightenStep and, if the
// contrast floor against any fill (or that fill under Rich depth's inset shade) is missed, grows in small steps, never
// to pure black or white.
juce::Colour sideMenuNotchInk(std::initializer_list<juce::Colour> fills) {
    if (fills.size() == 0)
        return juce::Colours::grey;
    const auto chip = fills.begin()->withAlpha(1.0f);
    const bool lightChip = colourContrast(chip, juce::Colours::black) > colourContrast(chip, juce::Colours::white);
    const auto extreme = lightChip ? juce::Colours::black : juce::Colours::white;
    auto amount = lightChip ? kDarkenStep : kLightenStep;
    auto ink = chip.interpolatedWith(extreme, amount);
    while (!holdsFloor(ink, fills) && amount + kInkStepEvery <= kInkPushLimit) {
        amount += kInkStepEvery;
        ink = chip.interpolatedWith(extreme, amount);
    }
    return ink;
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
    // On a very small chip the corner stops short of the centre, so a press on the glyph is never a menu press.
    const int size = std::min(kSideMenuCornerSize, std::min(chipBounds.getWidth(), chipBounds.getHeight()) / 2 - 1);
    return juce::Rectangle<int>(chipBounds.getRight() - size, chipBounds.getBottom() - size, size, size)
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
