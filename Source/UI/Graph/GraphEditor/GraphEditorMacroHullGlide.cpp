// GraphEditorMacroHullGlide.cpp -- the glide of expanded macro borders (MacroHullGlide.h): what is drawn
// for a border while it glides, the snapshot a change site takes before it moves borders, and the driver.
// GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest.

#include "GraphEditor.h"

// Everything that draws or docks against a border (the dashed outline, the port strips, the chip and buttons,
// the docked port widgets) reads this, so they all glide together.
juce::Rectangle<int> GraphEditor::paintedMacroHullBounds(const juce::String& macroId) const {
    return hullGlide_.apply(macroId, macroHullTargetBounds(macroId));
}

// Collapsed macros draw a card, not a border, so they never glide.
MacroHullGlide::Hulls GraphEditor::snapshotPaintedHulls() const {
    MacroHullGlide::Hulls hulls;
    for (const auto& macro : macros.getAll())
        if (!macros.isEffectivelyCollapsed(macro.id))
            hulls[macro.id] = paintedMacroHullBounds(macro.id);
    return hulls;
}

// Call after a change that may have moved borders, with a snapshotPaintedHulls() taken before it. A border
// already gliding is restarted from where it is drawn now, so a second change never makes it jump.
void GraphEditor::glideHullsFrom(const MacroHullGlide::Hulls& before) {
    MacroHullGlide::Hulls after;
    for (const auto& [id, rect] : before)
        if (macros.find(id) != nullptr && !macros.isEffectivelyCollapsed(id))
            after[id] = macroHullTargetBounds(id);
    if (!hullGlide_.arm(before, after))
        return;

    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    const auto frame = [safeEditor] {
        if (safeEditor == nullptr)
            return;
        safeEditor->macroController_.dockMacroPortWidgets();
        safeEditor->repaintCanvas();
    };
    hullGlideDriverAnim_.start(
        vblankUpdater, 220.0, synth::ui::easeOutCubic,
        [safeEditor, frame](float t) {
            if (safeEditor != nullptr)
                safeEditor->hullGlide_.applyTweenAt(t);
            frame();
        },
        [safeEditor, frame] {
            if (safeEditor != nullptr)
                safeEditor->hullGlide_.finish();
            frame();
        });
    macroController_.dockMacroPortWidgets();
    repaintCanvas();
}

void GraphEditor::finishHullGlideForTest() {
    hullGlide_.finish();
    macroController_.dockMacroPortWidgets();
    repaintCanvas();
}
