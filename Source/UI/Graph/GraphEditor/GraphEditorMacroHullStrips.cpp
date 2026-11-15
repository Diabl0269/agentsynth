// GraphEditorMacroHullStrips.cpp
//
// Paint for an open macro on the canvas: its dashed border with the name chip and collapse button
// (paintExpandedMacroHulls), and its two port sidebars: a tinted strip down each inside edge of the hull, from the
// hull's top (under the name pill and collapse button, which paint after it) to the hull's bottom, with the '+' (and
// '-') at its foot. Drawn from GraphContentComponent::paint just before the dashed hull outline, so the port widgets
// and the members (child components) sit on top of it and the outline draws over the strip's outer edge. Geometry comes
// only from the painted hull (GraphEditor::paintedMacroHullBounds, so a drag holds the border still),
// MacroGroupController's macroHullStripWidths and the button bounds, the same source the port dock and the hit-testing
// read (docs/macros/ports.md#how-a-port-is-drawn).

#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "GraphEditorPaintMemo.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"

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
        const float revealAlpha = editor.getLoadReveal().hullAlpha(macro.id); // fades in as a project opens
        if (hull.isEmpty() || editor.getCardGlide().isBorderHeld(macro.id) || revealAlpha <= 0.0f)
            continue;
        const LoadRevealAnimator::ScopedFade revealFade(g, revealAlpha);

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

std::vector<CardGlideAnimator::Border> captureMacroBorders(GraphEditor& editor, float zoom) {
    std::vector<CardGlideAnimator::Border> out;
    const auto& macros = editor.getMacros();
    if (macros.empty())
        return out;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&editor.getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    auto& controller = editor.getMacroController();
    const auto strip = colors.surfaceHi.withAlpha(macroStripFillAlpha(macroPortNameAlphaAtZoom(zoom)));
    for (const auto& macro : macros.getAll()) {
        CardGlideAnimator::Border b;
        b.key = CardGlideAnimator::macroKey(macro.id);
        b.name = macro.name;
        b.colour = macro.colour;
        b.stripFill = strip;
        if (!macros.isEffectivelyCollapsed(macro.id)) {
            b.hull = editor.paintedMacroHullBounds(macro.id);
            b.open = !b.hull.isEmpty();
        }
        if (b.open) {
            b.chip = controller.macroChipBounds(macro.id);
            std::tie(b.inWidth, b.outWidth) = controller.macroHullStripWidths(macro.id);
            for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, macro.id))
                if (const auto id = controller.resolveMemberNodeId(uuid); id.uid != 0)
                    b.members.push_back(id.uid);
        }
        out.push_back(std::move(b));
    }
    return out;
}

// ---- Expanded-macro grouping hull ----------------------
// A collapsed macro reads as a card; an expanded one left no on-canvas trace that its members
// were still grouped — Cmd+G on them again just refused with "already in a macro", with nothing
// visible to explain why. Draws a light dashed outline + name chip around the live union of
// member bounds so the grouping stays visible while expanded. Extracted out of
// GraphContentComponent::paint (rather than inlined there) to keep that function under the
// check-function-sizes.sh ratchet — this is its own named step, not a collaborator with state of
// its own, so a free function beside paint() rather than a new class earns its keep here.
// Whether a repaint of `clip` touches anything paintExpandedMacroHulls draws for the macro: everything sits inside its
// border except a name chip wider than the border, which only the chip row can reach. A glide frame repaints a small
// area, and dashing every border and measuring every chip's name for it cost as much as the rest of that paint.
static bool intersectsHullOrChip(juce::Rectangle<int> clip, juce::Rectangle<int> hull, GraphEditor& editor,
                                 const juce::String& macroId) {
    if (clip.intersects(hull.expanded(4)))
        return true;
    const bool inChipRow = clip.getBottom() >= hull.getY() && clip.getY() <= hull.getY() + 24;
    return inChipRow && clip.intersects(editor.getMacroController().macroChipBounds(macroId).expanded(2));
}

void paintExpandedMacroHulls(juce::Graphics& g, GraphEditor& editor) {
    if (editor.getMacros().empty())
        return;

    // Parents first, so a nested child's outline and chip draw over its parent's. A macro that is
    // collapsed, or hidden inside a collapsed ancestor, gets an empty hull and is skipped below.
    for (const auto* macroPtr : macro_nesting::macrosParentsFirst(editor.getMacros())) {
        const auto& macro = *macroPtr;

        // macroHullBounds is the ONE definition of this rectangle for hit-testing
        // (GraphEditor::macroHullAt, used by mouseDown/mouseUp for hull click-to-select and the
        // hull's right-click macro menu). What gets PAINTED goes through paintedMacroHullBounds
        // instead, which is macroHullBounds itself except while a reparent drag is dragging one of
        // THIS macro's own members — see its doc comment (GraphEditor.h) for why the two diverge
        // only in that one case (a member being pulled out must visibly shrink the hull away from
        // it, which the live union alone can never do).
        const auto hull = editor.paintedMacroHullBounds(macro.id);
        if (hull.isEmpty() || editor.getCardGlide().isBorderHeld(macro.id) ||
            !intersectsHullOrChip(g.getClipBounds(), hull, editor, macro.id))
            continue;
        const float revealAlpha = editor.getLoadReveal().hullAlpha(macro.id); // fades in as a project opens
        if (revealAlpha <= 0.0f)
            continue;
        const LoadRevealAnimator::ScopedFade revealFade(g, revealAlpha);
        ++graph_editor_paint::workCounters().hullsPainted;

        // A live reparent drag whose leave OR join candidate (GraphEditor::getMacroDragLeaveId
        // / getMacroDragJoinId) is THIS macro gets the SAME dashed hull, just emphasized — heavier,
        // fully opaque, and topped with a solid stroke — rather than a second visual language for
        // "about to change" (docs/macros/menu-and-membership.md). A transfer emphasises both, and a macro
        // the drag has already moved its module into stays emphasised until the drop. A whole macro's drag names the
        // parent it would leave on MacroGroupController (its join goes through the same join id).
        const bool isDragCandidate =
            macro.id == editor.getMacroDragLeaveId() || macro.id == editor.getMacroController().macroLeaveCandidate() ||
            editor.getMacroController().isGroupDropLeaving(macro.id) || macro.id == editor.getMacroDragJoinId() ||
            macro.id == editor.getMacroDragLiveOwnerId();

        MacroFoldAnimator::paintDashedBorder(g, hull.toFloat(), macro.colour, isDragCandidate);

        // A tab overlapping the hull's own top edge, not floating above it — a macro whose
        // members sit near the top of the canvas would otherwise clip the label off-canvas with
        // nothing to scroll up to. macroChipBounds is the ONE definition of this rect - hit-
        // testing (GraphEditor::macroChipAt, the chip's drag/rename affordance) must see exactly
        // what gets painted here, so paint uses the same font macroChipBounds measures with
        // rather than computing its own width.
        const juce::String label = macro.name.isNotEmpty() ? macro.name : juce::String("Macro");
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        const auto chipBounds = editor.getMacroController().macroChipBounds(macro.id);
        juce::Rectangle<float> chip = chipBounds.toFloat();
        g.setColour(macro.colour.withAlpha(0.85f));
        g.fillRoundedRectangle(chip, 6.0f);

        // Grip affordance: three short vertical lines at the chip's left edge, so it reads as a
        // drag handle rather than a plain label. Restrained and inside the 18px chip height.
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        const float gripX = chip.getX() + 6.0f;
        const float gripTop = chip.getY() + 5.0f;
        const float gripBottom = chip.getBottom() - 5.0f;
        for (int i = 0; i < 3; ++i) {
            const float x = gripX + (float)i * 3.0f;
            g.drawLine(x, gripTop, x, gripBottom, 1.0f);
        }

        g.setColour(juce::Colours::white);
        g.drawText(label, chip.withLeft(chip.getX() + 12.0f), juce::Justification::centred, false);

        // Collapse button: the chip's own drag/rename affordance never
        // looked like "collapse me" — the only routes back to a collapsed card were the right-
        // click menu and an undocumented double-click. A small button at the OTHER end of the
        // same row, pointing the opposite way from MacroCardComponent's expand chevron, reads as
        // the same control in its two states. macroCollapseButtonBounds is the ONE definition of
        // this rect — hit-testing (GraphEditor::macroCollapseButtonAt, used by mouseDown) must see
        // exactly what gets painted here.
        const auto collapseBounds = editor.getMacroController().macroCollapseButtonBounds(macro.id).toFloat();
        g.setColour(macro.colour.withAlpha(0.85f));
        g.fillRoundedRectangle(collapseBounds, 4.0f);

        // The hull is the open macro, so this is the shared fold arrow open (points down), the pair of
        // the folded card's closed one. White on the macro's own colour, not a theme token: the chip
        // fill is user-chosen, so only a fixed contrast colour reads on every macro colour.
        constexpr float kCollapseChevronSize = 8.0f;
        synth::theme::paintDisclosureChevron(
            g,
            juce::Rectangle<float>(kCollapseChevronSize, kCollapseChevronSize).withCentre(collapseBounds.getCentre()),
            1.0f, juce::Colours::white.withAlpha(0.85f));
    }
}

} // namespace detail
