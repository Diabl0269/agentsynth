// GraphEditorMacroCards.cpp
//
// The two macro-presentation pieces that need a genuine GraphEditor& / juce::Component identity
// and so were NOT moved into MacroGroupController (FRO77 PR2) — see MacroGroupController.h's
// class comment: syncMacroCards() constructs `new MacroCardComponent(*this, ...)`, and
// categoryPreviewColour() calls juce::Component::getLookAndFeel(). GraphEditor is declared in
// GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// Syncs macro card components with `macros` and the visibility of their (possibly hidden)
// member ModuleComponents. Called at the end of updateComponents(), the same seam that
// syncs ModuleComponents themselves. Stays on GraphEditor (FRO77 PR2) — it constructs
// `new MacroCardComponent(*this, ...)`, which needs a genuine GraphEditor&; see
// MacroGroupController.h's class comment. Also GraphCanvasHost::syncMacroCards() —
// MacroGroupController::renameMacro()/setMacroColour() call it through the host afterward.
void GraphEditor::syncMacroCards() {
    auto& cards = content.getMacroCards();

    // 1. Remove cards for macros that no longer exist.
    for (int i = cards.size(); --i >= 0;) {
        auto* card = cards.getUnchecked(i);
        if (macros.find(card->getMacroId()) == nullptr) {
            // This card's own mouseDown may have armed a live body drag (beginMacroCardDrag ->
            // selectionDragActive) — its macro just vanished entirely (every member gone, so
            // MacroSet::retainOnly erased it), and no mouseUp is ever coming once the card is
            // destroyed below (FRO19). Cancel now rather than leaving selectionDragActive stuck.
            if (card->isBodyDragActive())
                cancelLiveDragGestures();
            content.removeChildComponent(card);
            cards.remove(i);
        }
    }

    // 2. Add cards for new macros; every card's bounds/visibility follow its macro's collapsed
    //    state (bounds are meaningless while expanded — see synth::Macro's comment).
    for (const auto& macro : macros.getAll()) {
        MacroCardComponent* card = nullptr;
        for (auto* c : cards) {
            if (c->getMacroId() == macro.id) {
                card = c;
                break;
            }
        }
        if (card == nullptr) {
            card = cards.add(new MacroCardComponent(*this, macro.id));
            content.addAndMakeVisible(card);
        }
        card->setBounds(macro.bounds);
        card->setVisible(macro.collapsed);
    }

    // 3. A member's own ModuleComponent is hidden exactly while its macro is collapsed — kept
    //    alive (not removed), so its position keeps tracking a card drag underneath.
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const juce::String uuid = macroController_.nodeUuidFor(comp->getNodeId());
        const auto* macro = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
        comp->setVisible(macro == nullptr || !macro->collapsed);
    }
}

// Theme colour for a module category, matching how the canvas colours cables/cards by
// category (synth::ui::themeColourForCategory) — used by the collapsed card's content
// preview so its boxes read as the same colours expanding the macro would show. Falls back
// to token defaults under the stock LookAndFeel headless tests install, same as
// colourForCable.
juce::Colour GraphEditor::categoryPreviewColour(synth::ui::ModuleCategory category) const {
    // Force the BySourceCategory branch of resolveCableBaseColour regardless of the user's actual
    // cableColourMode: the task asks for the preview to echo the module's CATEGORY specifically,
    // and this is also the one call that folds in a user's Appearance Settings category colour
    // override (cableColourOverrides) -- without it, a customised category palette would make the
    // collapsed-card preview lie about what expanding the macro shows. CableSignal::Audio is inert
    // here; the BySourceCategory branch never reads it.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    return synth::ui::resolveCableBaseColour(synth::ui::CableColourMode::BySourceCategory,
                                             synth::ui::CableSignal::Audio, category, colors, cableColourOverrides);
}
