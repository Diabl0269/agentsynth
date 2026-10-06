// ToolbarButtonArt.cpp -- the top bar's colour groups, the per-icon hover motions and the split of a
// recoloured icon into its static body and moving parts.
#include "ToolbarButton.h"

namespace synth::ui {

using synth::theme::Icon;

juce::Colour toolbarGroupHue(const synth::theme::Theme& theme, ToolbarGroup group) {
    const auto& c = theme.colors;
    switch (group) {
    case ToolbarGroup::File:
        return c.hueGreen;
    case ToolbarGroup::Edit:
        return c.hueAmber;
    case ToolbarGroup::AI:
        return c.hueRose;
    case ToolbarGroup::Housekeeping:
        return c.hueViolet;
    case ToolbarGroup::Feedback:
        return c.hueGreen;
    case ToolbarGroup::View:
        break;
    }
    return c.accent;
}

namespace {
// Undo's arrow turns about the middle of its curve: a fraction of the stroked arrow's bounds.
constexpr float kArrowPivot = 0.607f;
// The ease-out-back strength of the arrow's swing (about a 10 percent overshoot).
constexpr float kArrowOvershoot = 1.70158f;

float easeOutBack(float t, float strength) {
    const float u = t - 1.0f;
    return 1.0f + (strength + 1.0f) * u * u * u + strength * u * u;
}
} // namespace

// One small motion per icon, each inside 2 icon units of its rest position. Distances are in the
// 24-unit SVG grid, so they shrink with the 19 px glyph like the drawing does.
std::array<ToolbarPartMotion, 2> toolbarIconMotion(Icon icon) {
    std::array<ToolbarPartMotion, 2> m{};
    switch (icon) {
    case Icon::ActionSettings: // the cog turns
    case Icon::ThemeToggle:    // the half-moon turns
        m[0].degrees = 30.0f;
        break;
    case Icon::ActionUndo: // swings back with a small bounce, about the arrow's elbow
        m[0].degrees = -22.0f;
        m[0].pivot = {kArrowPivot, kArrowPivot};
        m[0].overshoot = kArrowOvershoot;
        break;
    case Icon::ActionRedo: // the mirror image of Undo
        m[0].degrees = 22.0f;
        m[0].pivot = {1.0f - kArrowPivot, kArrowPivot};
        m[0].overshoot = kArrowOvershoot;
        break;
    case Icon::ActionSave: // the shutter slides
        m[0].dy = 1.5f;
        break;
    case Icon::ActionLoad: // the arrow drops into the folder
        m[0].dy = 2.0f;
        break;
    case Icon::ActionNew: // the plus grows
        m[0].scale = 1.2f;
        break;
    case Icon::ActionFeedback: // the lines slide (inside the bubble, which is drawn at two thirds scale)
        m[0].dx = 1.0f;
        break;
    case Icon::ActionAutoArrange: // the right-hand tiles part
        m[0].dy = -1.5f;
        m[1].dy = 1.5f;
        break;
    case Icon::ToggleMinimap: // the view moves
        m[0].dx = 2.0f;
        m[0].dy = 1.0f;
        break;
    case Icon::ToggleMatrix: // the grid pulses
        m[0].scale = 1.12f;
        break;
    // The AI button has no moving part: its pulse and spark loop in code (ToolbarAiSpark.cpp).
    case Icon::TogglePanel: // the panel rises
        m[0].dy = -1.5f;
        break;
    case Icon::ToggleLibrary: // the leaning book tips further, on its bottom corner
        m[0].degrees = -10.0f;
        m[0].pivot = {0.15f, 1.0f};
        break;
    default:
        break;
    }
    return m;
}

juce::AffineTransform toolbarPartTransform(const ToolbarPartMotion& motion, juce::Rectangle<float> partBounds, float t,
                                           bool arriving) {
    if (arriving && motion.overshoot > 0.0f)
        t = easeOutBack(t, motion.overshoot);
    const auto pivot = partBounds.getRelativePoint(motion.pivot.x, motion.pivot.y);
    const float scale = 1.0f + (motion.scale - 1.0f) * t;
    return juce::AffineTransform::scale(scale, scale, pivot.x, pivot.y)
        .rotated(juce::degreesToRadians(motion.degrees * t), pivot.x, pivot.y)
        .translated(motion.dx * t, motion.dy * t);
}

namespace {
juce::Drawable* findDrawableWithID(juce::Drawable& root, const juce::String& id) {
    for (auto* child : root.getChildren())
        if (auto* d = dynamic_cast<juce::Drawable*>(child)) {
            if (d->getComponentID() == id)
                return d;
            if (auto* found = findDrawableWithID(*d, id))
                return found;
        }
    return nullptr;
}
} // namespace

// A part is taken out of the icon's tree rather than moved in place with Component::setTransform:
// a transformed child makes its DrawableComposite re-fit its bounds and shift its origin, which would
// drift the pivot. Detached, each piece is drawn with Drawable::draw and one fixed icon-to-screen
// transform, never drawWithin (a composite's drawable bounds are the union of its children, so the
// body's bounds change once a part is gone). The SVG parser bakes group transforms into the paths, so
// a detached part still draws in icon coordinates.
ToolbarIconArt splitToolbarIconArt(std::unique_ptr<juce::Drawable> icon) {
    ToolbarIconArt art;
    if (icon == nullptr)
        return art;
    const char* ids[2] = {"mv", "mv2"};
    for (size_t i = 0; i < 2; ++i)
        if (auto* part = findDrawableWithID(*icon, ids[i])) {
            if (auto* parent = part->getParentComponent())
                parent->removeChildComponent(part); // a DrawableComposite owns its children; now we do
            art.parts[i].reset(part);
            art.partBounds[i] = part->getDrawableBounds();
        }
    art.body = std::move(icon);
    return art;
}

// Below full opacity the whole glyph goes through one layer, so overlapping body and part shapes fade
// together instead of showing through each other.
void ToolbarIconArt::draw(juce::Graphics& g, const juce::AffineTransform& iconToScreen,
                          const std::array<juce::AffineTransform, 2>& partTransforms, float opacity) const {
    if (body == nullptr || opacity <= 0.0f)
        return;
    const bool layered = opacity < 1.0f;
    if (layered)
        g.beginTransparencyLayer(opacity);
    body->draw(g, 1.0f, iconToScreen);
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i] != nullptr)
            parts[i]->draw(g, 1.0f, partTransforms[i].followedBy(iconToScreen));
    if (layered)
        g.endTransparencyLayer();
}

} // namespace synth::ui
