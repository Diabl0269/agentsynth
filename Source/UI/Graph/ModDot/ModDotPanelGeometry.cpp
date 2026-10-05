// The panel's placement rules and its single outline path (see the header).

#include "ModDotPanelGeometry.h"

namespace synth::ui::modDotPanel {

int maxPanelHeight(juce::Rectangle<int> area) { return juce::jmax(0, area.getHeight() - 2 * kScreenMargin); }

// Side first: right of the dot, unless the panel would run off the right edge and the left side has the room. Then the
// vertical slide: the top starts where the arrow sits kArrowInset below it, and is clamped so the whole panel is on
// screen; the arrow is placed afterwards, on the dot's centre line, so sliding never moves the tip off the dot.
Placement place(juce::Rectangle<int> dot, juce::Point<int> size, juce::Rectangle<int> area) {
    Placement out;
    const int reach = kArrowLength + kGap;
    const int width = size.x;
    const int height = juce::jmin(size.y, maxPanelHeight(area));
    const int rightX = dot.getRight() + reach;
    const int leftX = dot.getX() - reach - width;
    const bool fitsRight = rightX + width <= area.getRight() - kScreenMargin;
    const bool fitsLeft = leftX >= area.getX() + kScreenMargin;
    const bool useRight = fitsRight || !fitsLeft;
    out.arrowOnLeft = useRight;
    const int x = juce::jlimit(area.getX() + kScreenMargin,
                               juce::jmax(area.getX() + kScreenMargin, area.getRight() - kScreenMargin - width),
                               useRight ? rightX : leftX);

    const int minTop = area.getY() + kScreenMargin;
    const int maxTop = juce::jmax(minTop, area.getBottom() - kScreenMargin - height);
    const int top = juce::jlimit(minTop, maxTop, dot.getCentreY() - kArrowInset);
    out.panel = {x, top, width, height};

    const int reachY = (int)kCorner + kArrowHalfBase + 2;
    out.tipY = juce::jlimit(top + reachY, juce::jmax(top + reachY, top + height - reachY), dot.getCentreY());
    out.tipX = out.arrowOnLeft ? x - kArrowLength : x + width + kArrowLength;
    return out;
}

// Clockwise from the top-left corner; each corner is a quadratic curve, the arrow a plain triangle on its edge.
juce::Path outline(const Placement& placement) {
    const auto r = placement.panel.toFloat();
    const float c = juce::jmin(kCorner, r.getHeight() * 0.5f, r.getWidth() * 0.5f);
    const float tipY = (float)placement.tipY;
    const float half = (float)kArrowHalfBase;
    const float len = (float)kArrowLength;
    juce::Path p;
    p.startNewSubPath(r.getX() + c, r.getY());
    p.lineTo(r.getRight() - c, r.getY());
    p.quadraticTo(r.getRight(), r.getY(), r.getRight(), r.getY() + c);
    if (!placement.arrowOnLeft) {
        p.lineTo(r.getRight(), tipY - half);
        p.lineTo(r.getRight() + len, tipY);
        p.lineTo(r.getRight(), tipY + half);
    }
    p.lineTo(r.getRight(), r.getBottom() - c);
    p.quadraticTo(r.getRight(), r.getBottom(), r.getRight() - c, r.getBottom());
    p.lineTo(r.getX() + c, r.getBottom());
    p.quadraticTo(r.getX(), r.getBottom(), r.getX(), r.getBottom() - c);
    if (placement.arrowOnLeft) {
        p.lineTo(r.getX(), tipY + half);
        p.lineTo(r.getX() - len, tipY);
        p.lineTo(r.getX(), tipY - half);
    }
    p.lineTo(r.getX(), r.getY() + c);
    p.quadraticTo(r.getX(), r.getY(), r.getX() + c, r.getY());
    p.closeSubPath();
    return p;
}

} // namespace synth::ui::modDotPanel
