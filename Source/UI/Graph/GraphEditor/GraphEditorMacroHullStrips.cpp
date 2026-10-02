// GraphEditorMacroHullStrips.cpp
//
// Paint for the open macro's two port sidebars: a tinted strip down each inside edge of the hull, from the hull's top
// (under the name pill and collapse button, which paint after it) to the hull's bottom, with the '+' (and '-') at its
// foot. Drawn from GraphContentComponent::paint just before the dashed hull outline, so the port widgets and the
// members (child components) sit on top of it and the outline draws over the strip's outer edge. Geometry comes only
// from the painted hull (GraphEditor::paintedMacroHullBounds, so a drag holds the border still), MacroGroupController's
// macroHullStripWidths and the button bounds, the same source the port dock and the hit-testing read
// (docs/macros/ports.md#how-a-port-is-drawn).

#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace detail {

void paintMacroPortStrips(juce::Graphics& g, GraphEditor& editor, float zoom) {
    if (editor.getMacros().empty())
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&editor.getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    auto& controller = editor.getMacroController();
    constexpr float kHullRadius = 10.0f;
    const float nameAlpha = macroPortNameAlphaAtZoom(zoom);

    // Parents first, so a nested child's strips paint over its parent's interior.
    for (const auto* macroPtr : macro_nesting::macrosParentsFirst(editor.getMacros())) {
        const auto& macro = *macroPtr;
        const auto hull = editor.paintedMacroHullBounds(macro.id);
        if (hull.isEmpty())
            continue;

        const auto [inW, outW] = controller.macroHullStripWidths(macro.id);
        // The fill spans from the hull top so no gap shows under the name pill; only the port ROWS start below the
        // chip row (kMacroChipRowHeight), and those positions are untouched.
        const float top = (float)hull.getY();
        const float height = (float)hull.getBottom() - top;
        if (height <= 0.0f)
            continue;

        // The outer top and bottom corners follow the hull's own rounding; the inner corners are square.
        juce::Path left, right;
        // Zoomed out the strip narrows to a rail along the outline (macroStripPaintedWidth); the layout widths stay.
        const float paintedIn = macroStripPaintedWidth((float)inW, nameAlpha);
        const float paintedOut = macroStripPaintedWidth((float)outW, nameAlpha);
        left.addRoundedRectangle((float)hull.getX(), top, paintedIn, height, kHullRadius, kHullRadius, true, false,
                                 true, false);
        right.addRoundedRectangle((float)hull.getRight() - paintedOut, top, paintedOut, height, kHullRadius,
                                  kHullRadius, false, true, false, true);
        g.setColour(colors.surfaceHi.withAlpha(macroStripFillAlpha(nameAlpha)));
        g.fillPath(left);
        g.fillPath(right);

        g.setColour(colors.border.withMultipliedAlpha(nameAlpha)); // the inner dividers fade with the names
        const float innerLeft = (float)hull.getX() + paintedIn - 0.5f;
        const float innerRight = (float)hull.getRight() - paintedOut + 0.5f;
        g.drawLine(innerLeft, top, innerLeft, (float)hull.getBottom() - 1.0f, 1.0f);
        g.drawLine(innerRight, top, innerRight, (float)hull.getBottom() - 1.0f, 1.0f);

        const auto glyphColour = colors.textMuted.withAlpha(0.75f);
        for (const bool isInput : {true, false}) {
            if (nameAlpha > 0.0f) // the '+' fades with the names and is clickable only once '-' is
                paintMacroPortFooterButton(g, controller.macroHullAddButtonBounds(macro.id, isInput).toFloat(),
                                           glyphColour.withMultipliedAlpha(nameAlpha), true);
            // The slot at full zoom: the glyph fades with the names even while it is not yet clickable.
            const auto minus = controller.macroHullRemoveButtonBounds(macro.id, isInput, 1.0f);
            if (nameAlpha > 0.0f && !minus.isEmpty())
                paintMacroPortFooterButton(g, minus.toFloat(), glyphColour.withMultipliedAlpha(nameAlpha), false);
        }
    }
}

} // namespace detail
