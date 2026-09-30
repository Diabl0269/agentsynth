// GraphEditorMacroHullStrips.cpp
//
// Paint for the open macro's two port sidebars: a tinted strip down each inside edge of the hull, from the chip row to
// the hull's bottom, with the '+' (and '-') at its foot. Drawn from GraphContentComponent::paint just before the dashed
// hull outline, so the port widgets and the members (child components) sit on top of it and the outline draws over the
// strip's outer edge. Geometry comes only from MacroGroupController (macroHullBounds / macroHullStripWidths / the
// button bounds), the same source the port dock and the hit-testing read (docs/macros/ports.md#how-a-port-is-drawn).

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
        const auto hull = controller.macroHullBounds(macro.id);
        if (hull.isEmpty())
            continue;

        const auto [inW, outW] = controller.macroHullStripWidths(macro.id);
        const float top = (float)(hull.getY() + kMacroChipRowHeight);
        const float height = (float)hull.getBottom() - top;
        if (height <= 0.0f)
            continue;

        // Only the outer bottom corner follows the hull's own rounding; the other corners are square.
        juce::Path left, right;
        left.addRoundedRectangle((float)hull.getX(), top, (float)inW, height, kHullRadius, kHullRadius, false, false,
                                 true, false);
        right.addRoundedRectangle((float)(hull.getRight() - outW), top, (float)outW, height, kHullRadius, kHullRadius,
                                  false, false, false, true);
        g.setColour(colors.surfaceHi.withAlpha(macroStripFillAlpha(nameAlpha)));
        g.fillPath(left);
        g.fillPath(right);

        g.setColour(colors.border.withMultipliedAlpha(nameAlpha)); // the inner dividers fade with the names
        const float innerLeft = (float)(hull.getX() + inW) - 0.5f;
        const float innerRight = (float)(hull.getRight() - outW) + 0.5f;
        g.drawLine(innerLeft, top, innerLeft, (float)hull.getBottom() - 1.0f, 1.0f);
        g.drawLine(innerRight, top, innerRight, (float)hull.getBottom() - 1.0f, 1.0f);

        const auto glyphColour = colors.textMuted.withAlpha(0.75f);
        for (const bool isInput : {true, false}) {
            paintMacroPortFooterButton(g, controller.macroHullAddButtonBounds(macro.id, isInput).toFloat(), glyphColour,
                                       true);
            // The slot at full zoom: the glyph fades with the names even while it is not yet clickable.
            const auto minus = controller.macroHullRemoveButtonBounds(macro.id, isInput, 1.0f);
            if (nameAlpha > 0.0f && !minus.isEmpty())
                paintMacroPortFooterButton(g, minus.toFloat(), glyphColour.withMultipliedAlpha(nameAlpha), false);
        }
    }
}

} // namespace detail
