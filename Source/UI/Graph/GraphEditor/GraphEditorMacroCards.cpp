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

// Live macro-port jack-colour preview.
// The colour lives on a macro SET, not the graph, so no listener repaints the two surfaces that draw
// the jack on their own -- the collapsed card and the port's docked widget. The preview is view-layer
// only (never MacroPort::colour, so a drag pushes no undo); the single commit on close stores it.
GraphEditor::MacroPortRecolourTargets GraphEditor::findMacroPortRecolourTargets(const juce::String& macroId,
                                                                                const juce::String& nodeUuid) {
    MacroPortRecolourTargets targets;

    // The collapsed card draws EVERY port's jack, so it alone must repaint to show a new colour.
    targets.card = macroController_.getMacroCard(macroId);

    // The port fronts a docked ModuleComponent; while the macro is collapsed it is hidden, so a repaint is
    // a harmless no-op until it expands -- its first expand-time paint already reads the colour.
    const auto nodeId = macroController_.resolveMemberNodeId(nodeUuid);
    if (nodeId.uid != 0)
        targets.widget = moduleComponentFor(nodeId);

    return targets;
}

GraphEditor::MacroPortRecolourTargets GraphEditor::repaintMacroPortColourTargets(const juce::String& macroId,
                                                                                 const juce::String& nodeUuid) {
    // A bare canvas repaint reaches the card but not the docked widget; force BOTH — thin over the
    // shared finder so a preview and a commit can never target different surfaces.
    auto targets = findMacroPortRecolourTargets(macroId, nodeUuid);
    if (targets.card != nullptr)
        targets.card->repaint();
    if (targets.widget != nullptr)
        targets.widget->repaint();
    return targets;
}

void GraphEditor::previewMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                                         juce::Colour colour) {
    // Arm the view-layer preview and repaint only the changed surfaces, so a picker drag touches no
    // MacroPort::colour. Two guards: (1) the modal FREEZES the graph for its session, so the targets are
    // stable -- resolve ONCE when this node's session first arms, reuse the cached pair for every later
    // tick, re-resolving on a new node; the pair holds raw surfaces, safe exactly like a returned one.
    // (2) setPortColourPreview reports the changed surfaces, so a redundant re-press repaints nothing.
    auto& session = previewSessionTargets_;
    if (previewSessionNode_ != nodeUuid) {
        previewSessionNode_ = nodeUuid;
        session = findMacroPortRecolourTargets(macroId, nodeUuid);
    }
    if (auto* card = session.card) {
        if (card->setPortColourPreview(nodeUuid, colour))
            card->repaint();
    }
    if (auto* widget = session.widget) {
        if (widget->setPortColourPreview(colour))
            widget->repaint();
    }
}

void GraphEditor::clearMacroPortColourPreview(const juce::String& macroId, const juce::String& nodeUuid) {
    // Disarm the preview so the jack falls back to the now-stored colour. clearPortColourPreview reports
    // whether a surface actually disarmed, so a commit to an unpreviewed port issues no repaint. End this
    // picker's session so a later arm re-resolves; route the repaint through the same finder.
    auto targets = findMacroPortRecolourTargets(macroId, nodeUuid);
    previewSessionNode_.clear();
    previewSessionTargets_ = MacroPortRecolourTargets{};

    bool cleared = false;
    if (auto* card = targets.card)
        cleared |= card->clearPortColourPreview(nodeUuid);
    if (auto* widget = targets.widget)
        cleared |= widget->clearPortColourPreview();
    if (!cleared)
        return;
    if (targets.card != nullptr)
        targets.card->repaint();
    if (targets.widget != nullptr)
        targets.widget->repaint();
}

// Teardown backstop for a picker abandoned without committing: its CallOutBox can outlive the dialog,
// which the commit path's own clear can't reach. Disarm whatever THIS session armed, by the node/targets
// it cached -- no node arg; a no-op when nothing is armed.
void GraphEditor::cancelArmedMacroPortColourPreview() {
    if (previewSessionNode_.isEmpty())
        return;
    auto node = previewSessionNode_;
    auto targets = previewSessionTargets_;
    previewSessionNode_.clear();
    previewSessionTargets_ = MacroPortRecolourTargets{};

    bool cleared = false;
    if (auto* card = targets.card)
        cleared |= card->clearPortColourPreview(node);
    if (auto* widget = targets.widget)
        cleared |= widget->clearPortColourPreview();
    if (!cleared)
        return;
    if (targets.card != nullptr)
        targets.card->repaint();
    if (targets.widget != nullptr)
        targets.widget->repaint();
}
