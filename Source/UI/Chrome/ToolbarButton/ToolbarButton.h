#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/IconLibrary.h"
#include "UI/Theme/Theme.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// ToolbarButton (docs/layout/chrome.md#toolbar): one button of the top bar. A bold, multi-colour
// icon on a chip tinted with its group's colour, over a caption that is never coloured. The button
// carries the state (group, icon art, the hover / press / lit tweens); AppLookAndFeel::drawToolbarButton
// paints it. It stays a juce::DrawableButton so ToolbarComponent and the toolbar's text, title and
// tooltip code keep treating it like one, but it never uses DrawableButton's image children.
namespace synth::ui {

// The colour groups of the top bar. Feedback keeps its place beside Settings but has its own colour.
enum class ToolbarGroup { File, Edit, View, AI, Housekeeping, Feedback };

// The group's colour in `theme`: green, amber, the accent, rose, violet, green (Feedback).
juce::Colour toolbarGroupHue(const synth::theme::Theme& theme, ToolbarGroup group);

// One moving part's hover motion, at full hover, in icon units (the 24-unit SVG grid). Rotation and
// scale turn about `pivot`, a fraction of the part's own bounds. A non-zero `overshoot` makes the
// part overshoot its full motion and settle back while the hover arrives (an ease-out-back curve).
struct ToolbarPartMotion {
    float dx = 0.0f;
    float dy = 0.0f;
    float degrees = 0.0f;
    float scale = 1.0f;
    juce::Point<float> pivot{0.5f, 0.5f};
    float overshoot = 0.0f;
};

// The hover motion of an icon's parts "mv" and "mv2"; a part an icon lacks has no motion.
std::array<ToolbarPartMotion, 2> toolbarIconMotion(synth::theme::Icon icon);

// `motion` at hover amount `t` (0..1) for a part whose icon-space bounds are `partBounds`. `arriving`
// is true while the hover is growing; only then does an overshooting motion bounce.
juce::AffineTransform toolbarPartTransform(const ToolbarPartMotion& motion, juce::Rectangle<float> partBounds, float t,
                                           bool arriving = true);

// A recoloured icon split into its static body and up to two moving parts, all in icon space.
struct ToolbarIconArt {
    std::unique_ptr<juce::Drawable> body;
    std::array<std::unique_ptr<juce::Drawable>, 2> parts;
    std::array<juce::Rectangle<float>, 2> partBounds;

    bool isValid() const noexcept { return body != nullptr; }
    // Draws body and parts through `iconToScreen`, each part moved by `partTransforms[i]` first.
    void draw(juce::Graphics& g, const juce::AffineTransform& iconToScreen,
              const std::array<juce::AffineTransform, 2>& partTransforms, float opacity) const;
};

// Splits `icon` (null allowed: an empty art) at its "mv" / "mv2" groups.
ToolbarIconArt splitToolbarIconArt(std::unique_ptr<juce::Drawable> icon);

class ToolbarButton : public juce::DrawableButton {
public:
    explicit ToolbarButton(const juce::String& name);
    ~ToolbarButton() override;

    // The glyph and its group; rebuilds the art from the current look-and-feel (none headless).
    void setIcon(synth::theme::Icon icon, ToolbarGroup group);
    // Rebuilds the art from the current look-and-feel's theme. Message thread only.
    void refreshArt();

    synth::theme::Icon getIcon() const noexcept { return icon_; }
    ToolbarGroup getGroup() const noexcept { return group_; }
    // The art drawn at rest (lit = false) and on a lit chip (lit = true); invalid until art exists.
    const ToolbarIconArt& getArt(bool lit) const noexcept { return lit ? litArt_ : restArt_; }

    // 0..1: how far the hover, press and lit tweens have gone.
    float getHoverAmount() const noexcept { return hover_.value; }
    float getPressAmount() const noexcept { return press_.value; }
    float getLitAmount() const noexcept { return lit_.value; }

    // False under Reduce Motion: then nothing moves and only colours change.
    bool isMotionEnabled() const noexcept { return motionEnabled_; }
    // Part `index`'s hover transform in icon units right now (identity without motion).
    juce::AffineTransform getPartTransform(int index) const;
    // How far the icon is lifted right now, in px (0 without motion).
    float getIconLift() const;
    // The chip's press squash right now as x and y scale (1 without motion).
    juce::Point<float> getChipSquash() const;

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

protected:
    void buttonStateChanged() override;
    void lookAndFeelChanged() override;

private:
    struct Fade {
        float value = 0.0f;
        float target = 0.0f;
        AnimationDriver driver;
    };
    void retarget(Fade& fade, float target, double inMs, double outMs);

    synth::theme::Icon icon_ = synth::theme::Icon::kCount;
    ToolbarGroup group_ = ToolbarGroup::View;
    ToolbarIconArt restArt_;
    ToolbarIconArt litArt_;
    std::array<ToolbarPartMotion, 2> motion_{};
    bool motionEnabled_ = true;

    juce::VBlankAnimatorUpdater updater_{this};
    Fade hover_;
    Fade press_;
    Fade lit_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarButton)
};

} // namespace synth::ui
