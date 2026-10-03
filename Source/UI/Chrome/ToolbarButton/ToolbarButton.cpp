// ToolbarButton.cpp -- the top-bar button's state: its art (rebuilt per theme) and its hover, press and
// lit tweens. Painting lives in AppLookAndFeel::drawToolbarButton (AppLookAndFeelToolbarButton.cpp).
#include "ToolbarButton.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// Motion timings (docs/layout/animation.md#motion-rules): hover in / out, press, a lit chip filling / emptying.
constexpr double kHoverInMs = 110.0;
constexpr double kHoverOutMs = 90.0;
constexpr double kPressMs = 80.0;
constexpr double kLitInMs = 160.0;
constexpr double kLitOutMs = 110.0;
// How far the glyph lifts on hover (px) and how much the chip squashes when pressed.
constexpr float kHoverLiftPx = 1.0f;
constexpr float kPressSquashX = 0.08f;
constexpr float kPressSquashY = 0.14f;
// The lit glyph's soft shapes are ink at this alpha.
constexpr float kLitSoftAlpha = 0.4f;
} // namespace

ToolbarButton::ToolbarButton(const juce::String& name)
    : juce::DrawableButton(name, juce::DrawableButton::ImageAboveTextLabel) {}

ToolbarButton::~ToolbarButton() {
    hover_.driver.stop(updater_);
    press_.driver.stop(updater_);
    lit_.driver.stop(updater_);
}

void ToolbarButton::setIcon(synth::theme::Icon icon, ToolbarGroup group) {
    icon_ = icon;
    group_ = group;
    motion_ = toolbarIconMotion(icon);
    refreshArt();
}

// Both arts are rebuilt from the icon library's untinted original on every call, so a theme switch
// (applyToolbarIcons, or a look-and-feel change) never recolours an already-recoloured glyph. On a
// lit chip the roles invert: the colour and paper shapes turn ink, soft shapes ink at 40 percent, and
// ink details take the group colour, so the glyph reads dark on its filled chip.
void ToolbarButton::refreshArt() {
    restArt_ = {};
    litArt_ = {};
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr && icon_ != synth::theme::Icon::kCount) {
        const auto& c = lf->getTheme().colors;
        const auto hue = toolbarGroupHue(lf->getTheme(), group_);
        const synth::theme::IconRoleColours rest{hue, hue.withMultipliedAlpha(synth::theme::kRoleSoftAlpha), c.iconInk,
                                                 c.iconPaper};
        const synth::theme::IconRoleColours lit{c.iconInk, c.iconInk.withMultipliedAlpha(kLitSoftAlpha), hue,
                                                c.iconInk};
        restArt_ = splitToolbarIconArt(lf->getRoleIcon(icon_, rest));
        litArt_ = splitToolbarIconArt(lf->getRoleIcon(icon_, lit));
    }
    repaint();
}

void ToolbarButton::lookAndFeelChanged() {
    juce::DrawableButton::lookAndFeelChanged();
    refreshArt();
}

// Hover, press and lit each follow the button's own state, so a real mouse enter / exit / press and a
// setToggleState() (which JUCE reports through buttonStateChanged) all drive them. Reduce Motion is
// read once when a hover starts.
void ToolbarButton::buttonStateChanged() {
    juce::DrawableButton::buttonStateChanged();
    const bool enabled = isEnabled();
    const float hoverTarget = enabled && getState() != buttonNormal ? 1.0f : 0.0f;
    if (hoverTarget > hover_.target)
        motionEnabled_ = !prefersReducedMotion();
    retarget(hover_, hoverTarget, kHoverInMs, kHoverOutMs);
    retarget(press_, enabled && getState() == buttonDown ? 1.0f : 0.0f, kPressMs, kPressMs);
    retarget(lit_, getToggleState() ? 1.0f : 0.0f, kLitInMs, kLitOutMs);
}

// A retarget runs from the CURRENT value, eases out arriving and in leaving, and lands at once when
// the button is not on screen (no VBlank reaches it: headless tests, a hidden window).
void ToolbarButton::retarget(Fade& fade, float target, double inMs, double outMs) {
    if (target == fade.target)
        return;
    fade.target = target;
    if (!isShowing()) {
        fade.driver.stop(updater_);
        fade.value = target;
        repaint();
        return;
    }
    const float from = fade.value;
    const bool arriving = target > from;
    fade.driver.start(
        updater_, arriving ? inMs : outMs, arriving ? easeOutCubic : easeInCubic,
        [this, &fade, from, target](float t) {
            fade.value = from + (target - from) * t;
            repaint();
        },
        [this, &fade] {
            fade.value = fade.target;
            repaint();
        });
}

juce::AffineTransform ToolbarButton::getPartTransform(int index) const {
    const auto i = (size_t)index;
    if (!motionEnabled_ || index < 0 || index > 1 || restArt_.parts[i] == nullptr)
        return {};
    return toolbarPartTransform(motion_[i], restArt_.partBounds[i], hover_.value);
}

float ToolbarButton::getIconLift() const { return motionEnabled_ ? kHoverLiftPx * hover_.value : 0.0f; }

juce::Point<float> ToolbarButton::getChipSquash() const {
    if (!motionEnabled_)
        return {1.0f, 1.0f};
    return {1.0f - kPressSquashX * press_.value, 1.0f - kPressSquashY * press_.value};
}

void ToolbarButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        lf->drawToolbarButton(g, *this, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    else
        synth::theme::paintToolbarButton(g, *this, synth::theme::themeOf(*this));
}

} // namespace synth::ui
