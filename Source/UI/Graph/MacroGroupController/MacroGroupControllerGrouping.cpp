// MacroGroupControllerGrouping.cpp
//
// Group/ungroup into a macro, collapse/expand, and membership add/remove. MacroGroupController is
// declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class. The colour picker, "Rename..." dialog and the macro context menu
// stay on GraphEditor (GraphEditorMacroPrompts.cpp) — see MacroGroupController.h's class comment
// for why (juce::Component::SafePointer<GraphEditor> needs a genuine GraphEditor&).

#include "MacroGroupController.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace detail;

juce::String MacroGroupController::groupSelectionIntoMacro(bool autoCreatePorts) {
    auto ids = host_.getSelection().getSelected();
    if (ids.size() < 2) {
        host_.reportStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    std::vector<juce::String> memberUuids;
    juce::Rectangle<int> groupBounds;
    for (auto id : ids) {
        // A module freshly dropped onto the canvas has no "uuid" property yet — it's only ever
        // lazily assigned on first save (synth::AIStateMapper::graphToJSON). Assign it here too,
        // the same way, so grouping newly-placed modules doesn't drop them from the selection.
        auto* node = host_.graph().getNodeForId(id);
        const juce::String uuid = node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
        if (uuid.isEmpty())
            continue; // no persistent identity to group by — shouldn't happen for a real module

        if (host_.getMacros().findByMember(uuid) != nullptr) {
            // Flat model, deliberately refused rather than silently merging/re-parenting — see
            // synth::Macro's class comment.
            host_.reportStatusMessage("Can't group: a selected module is already in a macro. Ungroup it first.");
            return {};
        }
        memberUuids.push_back(uuid);

        for (auto* comp : host_.modules()) {
            if (comp != nullptr && comp->getNodeId() == id) {
                groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                break;
            }
        }
    }

    if (memberUuids.size() < 2) {
        host_.reportStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    // Founder-review fix F5 (docs/macros_implementation.md §7 item 6.1): the crossing plan is read off the LIVE
    // graph now, before the macro exists — resolveMemberNodeId (which buildMacroPortCrossingPlan
    // uses internally) only knows about macros already in the macro set, so this has to work off
    // the uuid list directly.
    std::vector<MacroPortCrossingGroup> portPlan;
    if (autoCreatePorts)
        portPlan = buildMacroPortCrossingPlan(memberUuids);

    synth::Macro macro;
    macro.name = "Macro";
    macro.members = memberUuids;
    macro.collapsed = true;
    const auto origin = groupBounds.isEmpty() ? juce::Point<int>() : groupBounds.getTopLeft();
    macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);

    auto& graph = host_.graph();
    juce::String newId;
    auto doGroup = [this, macro, portPlan, &newId] {
        newId = host_.getMacros().add(macro).id;
        // Splice BEFORE updateComponents(): the spliced port nodes must exist, and be macro
        // members, before the card/hull layout that updateComponents() triggers runs against
        // them — never group-then-add as a second pass.
        if (!portPlan.empty())
            spliceMacroPorts(newId, portPlan);
        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doGroup);
    else
        doGroup();

    if (!newId.isEmpty())
        selectMacro(newId, false);

    host_.requestRepaint();
    return newId;
}

juce::String MacroGroupController::addMacroForMembers(const std::vector<juce::String>& memberUuids,
                                                      const juce::String& name, juce::Point<int> origin) {
    if (memberUuids.empty())
        return {};

    // Same flat-model refusal groupSelectionIntoMacro() applies: a member already claimed by another
    // macro aborts the whole call rather than silently re-parenting it.
    for (const auto& uuid : memberUuids)
        if (host_.getMacros().findByMember(uuid) != nullptr)
            return {};

    synth::Macro macro;
    macro.name = name;
    macro.members = memberUuids;
    macro.collapsed = true;
    macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);

    return host_.getMacros().add(macro).id;
}

void MacroGroupController::addSelectionToMacro(const juce::String& macroId,
                                               const std::vector<juce::String>& memberUuids) {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || memberUuids.empty())
        return;

    // Same flat-model refusal groupSelectionIntoMacro() applies: abort the WHOLE add rather than
    // adding the rest and silently skipping the uuid that's already spoken for.
    for (const auto& uuid : memberUuids) {
        if (!macro->hasMember(uuid) && host_.getMacros().findByMember(uuid) != nullptr) {
            host_.reportStatusMessage("Can't add: a selected module is already in a macro. Ungroup it first.");
            return;
        }
    }

    // T138: uuids genuinely new to THIS macro — the ones the port-crossing plan below cares about;
    // a uuid already a member of macroId is silently skipped by addMember() below same as always.
    std::vector<juce::String> toAdd;
    for (const auto& uuid : memberUuids)
        if (!macro->hasMember(uuid))
            toAdd.push_back(uuid);

    // Computed off the PRE-add graph/macro state — pure reads, no mutation yet.
    const auto addPlan = buildMacroPortCrossingPlanForNewMembers(macroId, toAdd);
    const auto portsToSpliceOut = macroPortsThatBecomeInteriorOnAdd(macroId, toAdd);

    auto& graph = host_.graph();
    auto doAdd = [this, macroId, memberUuids, addPlan, portsToSpliceOut] {
        for (const auto& uuid : memberUuids)
            host_.getMacros().addMember(macroId, uuid);
        if (!addPlan.empty())
            spliceMacroPorts(macroId, addPlan);
        // Splice-out AFTER splice-in — see this method's header comment for the ordering
        // rationale (kept in MacroGroupController.h's declaration).
        for (const auto& portUuid : portsToSpliceOut) {
            if (auto* liveMacro = host_.getMacros().find(macroId))
                spliceOutMacroPort(*liveMacro, portUuid);
        }
        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doAdd);
    else
        doAdd();

    host_.requestRepaint();
}

void MacroGroupController::removeSelectionFromMacro(const juce::String& macroId,
                                                    const std::vector<juce::String>& memberUuids) {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || memberUuids.empty())
        return;

    // Ports have their own delete affordance — pulling one out of `members` here would desync
    // Macro::ports without splicing its cable back the way that affordance does.
    std::vector<juce::String> toRemove;
    for (const auto& uuid : memberUuids)
        if (macro->hasMember(uuid) && !macro->memberIsPort(uuid))
            toRemove.push_back(uuid);
    if (toRemove.empty())
        return;

    // T138: a cable from a departing member to one that's staying is about to become a real
    // boundary crossing — computed off the PRE-remove graph, before anything moves.
    const auto removePlan = buildMacroPortCrossingPlanForRemovedMembers(macroId, toRemove);

    auto& graph = host_.graph();
    auto doRemove = [this, macroId, toRemove, removePlan] {
        // Splice BEFORE the membership removal below: spliceMacroPorts needs the macro to still
        // resolve, and removeMemberEverywhere can dissolve the macro record outright if this
        // drops its last member.
        if (!removePlan.empty())
            spliceMacroPorts(macroId, removePlan);
        for (const auto& uuid : toRemove)
            host_.getMacros().removeMemberEverywhere(uuid);
        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doRemove);
    else
        doRemove();

    host_.requestRepaint();
}

void MacroGroupController::removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId) {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return;
    const auto* macro = host_.getMacros().findByMember(uuid);
    if (macro == nullptr)
        return;
    removeSelectionFromMacro(macro->id, {uuid});
}

bool MacroGroupController::selectionHasCrossingMacroCable() const {
    // NodeID-based: a freshly-dropped, never-saved module has no "uuid" property yet, so gating
    // on resolvable uuids would silently miss the crossing cable on the single most common real
    // path. buildMacroPortCrossingPlan()'s NodeID overload needs no uuid at all.
    const auto ids = host_.getSelection().getSelected();
    if (ids.size() < 2)
        return false;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isNotEmpty() && host_.getMacros().findByMember(uuid) != nullptr)
            return false;
    }
    return !buildMacroPortCrossingPlan(ids).empty();
}

void MacroGroupController::ungroupSelection() {
    auto ids = host_.getSelection().getSelected();
    std::set<juce::String> macroIdsToRemove;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (auto* m = host_.getMacros().findByMember(uuid))
            macroIdsToRemove.insert(m->id);
    }

    if (macroIdsToRemove.empty()) {
        host_.reportStatusMessage("Select a macro's modules to ungroup it.");
        return;
    }

    auto& graph = host_.graph();
    auto doUngroup = [this, macroIdsToRemove] {
        std::vector<juce::AudioProcessorGraph::NodeID> newSelection;
        for (const auto& macroId : macroIdsToRemove) {
            auto* m = host_.getMacros().find(macroId);
            if (m == nullptr)
                continue; // defensive: shouldn't happen mid-transaction

            // Founder review, second pass: "ungroup leaves the macro input/output in place (They
            // should be removed)". Splice every one of this macro's ports back out FIRST — auto-
            // created and hand-added alike — restoring the external<->internal wiring each one
            // proxied, before falling through to the plain-module behaviour below. Iterate a COPY:
            // each call mutates m->ports/m->members as it goes.
            const auto portsToSplice = m->ports;
            for (const auto& port : portsToSplice)
                spliceOutMacroPort(*m, port.nodeUuid);

            // Ungrouping is still presentation-only for the macro's real modules — every member
            // left in m->members at this point is an ordinary module, never deleted, never
            // disconnected.
            for (const auto& uuid : m->members) {
                auto nodeId = resolveMemberNodeId(uuid);
                if (nodeId.uid != 0)
                    newSelection.push_back(nodeId);
            }
            host_.getMacros().remove(macroId);
        }
        host_.updateComponents();
        host_.setSelectedNodes(newSelection);
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doUngroup);
    else
        doUngroup();

    host_.requestRepaint();
}

void MacroGroupController::toggleSelectionMacrosCollapsed() {
    auto ids = host_.getSelection().getSelected();
    std::set<juce::String> touchedMacroIds;
    bool anyExpanded = false;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (auto* m = host_.getMacros().findByMember(uuid)) {
            touchedMacroIds.insert(m->id);
            if (!m->collapsed)
                anyExpanded = true;
        }
    }

    // Refused only when the selection touches NO macro at all.
    if (touchedMacroIds.empty()) {
        host_.reportStatusMessage("Select a macro's modules to collapse or expand it.");
        return;
    }

    // DETERMINISTIC RULE: if any touched macro is expanded, collapse them ALL; otherwise every
    // touched macro is already collapsed, so expand them all.
    const bool targetCollapsed = anyExpanded;

    // ONE undo entry for the whole gesture, not one per macro — applyMacroCollapsed (the raw
    // mutation) runs inside one recorded change.
    auto& graph = host_.graph();
    auto doToggleAll = [this, touchedMacroIds, targetCollapsed] {
        for (const auto& macroId : touchedMacroIds)
            applyMacroCollapsed(macroId, targetCollapsed);
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doToggleAll);
    else
        doToggleAll();

    host_.requestRepaint();
}

void MacroGroupController::groupOrToggleSelectionMacros() {
    // Cmd+G's single entry point (P8-14). Mixed selection (some selected nodes already in a
    // macro, some loose): toggle wins outright — the touched macros are toggled and the loose
    // modules are silently left out of any grouping.
    auto ids = host_.getSelection().getSelected();
    std::set<juce::String> touchedMacroIds;
    int looseCount = 0;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        const auto* m = uuid.isEmpty() ? nullptr : host_.getMacros().findByMember(uuid);
        if (m != nullptr)
            touchedMacroIds.insert(m->id);
        else
            ++looseCount;
    }

    if (touchedMacroIds.empty()) {
        // Nothing selected touches a macro — Cmd+G means exactly what it always meant: group.
        // requestGroupSelectionIntoMacro() (GraphEditor, stays behind) carries its own refusal/
        // status behaviour and additionally gates the auto-port-preference modal.
        host_.requestGroupSelectionIntoMacro();
        return;
    }

    toggleSelectionMacrosCollapsed();

    if (looseCount > 0) {
        const juce::String macroWord = touchedMacroIds.size() == 1 ? "macro" : "macros";
        const juce::String moduleWord = looseCount == 1 ? "module" : "modules";
        const juce::String verb = looseCount == 1 ? "was" : "were";
        host_.reportStatusMessage("Toggled " + juce::String((int)touchedMacroIds.size()) + " " + macroWord + "; " +
                                  moduleWord + " outside a macro " + verb + " left alone.");
    }
}

const synth::Macro* MacroGroupController::macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return nullptr;
    return host_.getMacros().findByMember(uuid);
}

void MacroGroupController::selectMacro(const juce::String& macroId, bool additive) {
    auto* m = host_.getMacros().find(macroId);
    if (m == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    host_.applySelectionChange(additive ? synth::ui::unionSelection(host_.getSelection().getSelected(), memberIds)
                                        : memberIds);
}

bool MacroGroupController::isMacroSelected(const juce::String& macroId) const {
    const auto* m = host_.getMacros().find(macroId);
    if (m == nullptr || m->members.empty() || host_.getSelection().size() != (int)m->members.size())
        return false;

    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0 || !host_.getSelection().contains(nodeId))
            return false;
    }
    return true;
}

void MacroGroupController::applyMacroCollapsed(const juce::String& macroId, bool collapsed) {
    auto* m = host_.getMacros().find(macroId);
    if (m == nullptr || m->collapsed == collapsed)
        return;

    if (collapsed) {
        // Collapsing FROM expanded: seed the card at the current member bounding box's top-left,
        // sized to the standard card footprint. Port members are EXCLUDED from this union, the
        // same way macroHullBounds() excludes them.
        std::set<juce::String> portNodeUuids;
        for (const auto& p : m->ports)
            portNodeUuids.insert(p.nodeUuid);

        juce::Rectangle<int> groupBounds;
        for (const auto& uuid : m->members) {
            if (portNodeUuids.count(uuid) > 0)
                continue;
            auto nodeId = resolveMemberNodeId(uuid);
            for (auto* comp : host_.modules()) {
                if (comp != nullptr && comp->getNodeId() == nodeId) {
                    groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                    break;
                }
            }
        }
        const auto origin = groupBounds.isEmpty() ? m->bounds.getTopLeft() : groupBounds.getTopLeft();
        m->bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);
    }
    m->collapsed = collapsed;
    host_.updateComponents();
}

void MacroGroupController::setMacroCollapsed(const juce::String& macroId, bool collapsed) {
    auto& graph = host_.graph();
    auto doToggle = [this, macroId, collapsed] { applyMacroCollapsed(macroId, collapsed); };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doToggle);
    else
        doToggle();

    host_.requestRepaint();
}

void MacroGroupController::renameMacro(const juce::String& macroId, const juce::String& newName) {
    auto& graph = host_.graph();
    auto doRename = [this, macroId, newName] {
        if (auto* m = host_.getMacros().find(macroId))
            m->name = newName;
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doRename);
    else
        doRename();

    host_.syncMacroCards();
    host_.requestRepaint();
}

void MacroGroupController::setMacroColour(const juce::String& macroId, juce::Colour colour) {
    auto& graph = host_.graph();
    auto doRecolour = [this, macroId, colour] {
        if (auto* m = host_.getMacros().find(macroId))
            m->colour = colour;
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doRecolour);
    else
        doRecolour();

    host_.syncMacroCards();
    host_.requestRepaint();
}

std::vector<MacroGroupController::MacroMemberPreview>
MacroGroupController::macroMemberPreviews(const juce::String& macroId) const {
    std::vector<MacroMemberPreview> result;
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return result;

    auto& graph = host_.graph();
    for (const auto& uuid : macro->members) {
        // A port node is a boundary jack, not a module to preview (founder-review fix G6).
        if (macro->memberIsPort(uuid))
            continue;

        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;

        ModuleComponent* comp = nullptr;
        for (auto* c : host_.modules()) {
            if (c != nullptr && c->getNodeId() == nodeId) {
                comp = c;
                break;
            }
        }
        if (comp == nullptr)
            continue;

        MacroMemberPreview preview;
        preview.bounds = comp->getBounds();
        preview.category = categoryForNode(graph.getNodeForId(nodeId));
        result.push_back(preview);
    }
    return result;
}

juce::StringArray MacroGroupController::macroMemberNames(const juce::String& macroId) const {
    juce::StringArray names;
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return names;

    auto& graph = host_.graph();
    for (const auto& uuid : macro->members) {
        // Same exclusion as macroMemberPreviews above (founder-review fix G6).
        if (macro->memberIsPort(uuid))
            continue;

        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;
        auto* node = graph.getNodeForId(nodeId);
        names.add(host_.getModuleTitle(nodeId, node != nullptr ? node->getProcessor() : nullptr));
    }
    return names;
}
