// MacroGroupControllerGrouping.cpp
//
// Group/ungroup into a macro, collapse/expand, and membership add/remove. MacroGroupController is
// declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class. The colour picker, "Rename..." dialog and the macro context menu
// stay on GraphEditor (GraphEditorMacroPrompts.cpp) — see MacroGroupController.h's class comment
// for why (juce::Component::SafePointer<GraphEditor> needs a genuine GraphEditor&).

#include "MacroGroupController.h"
#include "MacroNesting.h"

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

    // The crossing plan is read off the LIVE graph, before the macro exists — resolveMemberNodeId (which
    // buildMacroPortCrossingPlan uses internally) only knows about macros already in the macro set, so this has to
    // work off the uuid list directly (see docs/macros/auto-ports.md#the-auto-port-preference).
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
        newId = host_.getMacros().add(macro);
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

    return host_.getMacros().add(macro);
}

void MacroGroupController::addSelectionToMacro(const juce::String& macroId,
                                               const std::vector<juce::String>& memberUuids, bool recordUndo) {
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

    // uuids genuinely new to THIS macro — the ones the port-crossing plan below cares about;
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

    // recordUndo=false runs doAdd() directly — an outer caller (GraphEditor's Cmd/Ctrl-drag
    // reparent finalize) already opened its own recordGraphAndMacroChange around a bigger gesture
    // and needs this membership change inside THAT one undo step, not a second one of its own.
    if (!recordUndo)
        doAdd();
    else if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doAdd);
    else
        doAdd();

    host_.requestRepaint();
}

namespace {
// Takes `uuid` out of `macroId` one level: into the parent macro when there is one (a nested
// macro's member leaves to its parent's own space), out of every macro otherwise. A macro left
// with no direct members and no children dissolves, exactly as removeMemberEverywhere does.
void moveMemberUpOneLevel(synth::MacroSet& macros, const juce::String& macroId, const juce::String& uuid) {
    const juce::String parentId = macros.parentOf(macroId);
    auto* macro = macros.find(macroId);
    if (parentId.isEmpty() || macro == nullptr) {
        macros.removeMemberEverywhere(uuid);
        return;
    }
    macro->members.erase(std::remove(macro->members.begin(), macro->members.end(), uuid), macro->members.end());
    macros.addMember(parentId, uuid);
    if (macro->members.empty() && macros.childrenOf(macroId).empty())
        macros.remove(macroId);
}

// The macro a selected node stands for in a collapse toggle: the outermost collapsed macro above
// it (a hidden node's card is what the user sees), else its direct owner.
const synth::Macro* toggleTargetFor(const synth::MacroSet& macros, const juce::String& uuid) {
    const juce::String collapsedId = macros.outermostCollapsedAncestorOf(uuid);
    return collapsedId.isNotEmpty() ? macros.find(collapsedId) : macros.findByMember(uuid);
}
} // namespace

void MacroGroupController::removeSelectionFromMacro(const juce::String& macroId,
                                                    const std::vector<juce::String>& memberUuids, bool recordUndo) {
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

    // A cable from a departing member to one that's staying is about to become a real
    // boundary crossing — computed off the PRE-remove graph, before anything moves.
    const auto removePlan = buildMacroPortCrossingPlanForRemovedMembers(macroId, toRemove);
    // The mirror image of addSelectionToMacro's macroPortsThatBecomeInteriorOnAdd — an
    // EXISTING port whose interior leg was exactly one of the departing members is now bridging
    // two things that are both external, so splice it out the same way ungroup does, instead of
    // leaving it stranded on the hull.
    const auto obsoletePorts = macroPortsThatBecomeObsoleteOnRemove(macroId, toRemove);

    auto& graph = host_.graph();
    auto doRemove = [this, macroId, toRemove, removePlan, obsoletePorts] {
        // Splice BEFORE the membership removal below: spliceMacroPorts/spliceOutMacroPort both
        // need the macro to still resolve, and removeMemberEverywhere can dissolve the macro
        // record outright if this drops its last member.
        if (!removePlan.empty())
            spliceMacroPorts(macroId, removePlan);
        for (const auto& portUuid : obsoletePorts)
            if (auto* liveMacro = host_.getMacros().find(macroId))
                spliceOutMacroPort(*liveMacro, portUuid);
        for (const auto& uuid : toRemove)
            moveMemberUpOneLevel(host_.getMacros(), macroId, uuid);
        host_.updateComponents();
    };

    // See addSelectionToMacro's matching comment above.
    if (!recordUndo)
        doRemove();
    else if (host_.undo())
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

namespace {
// The macros an ungroup acts on: each selected node's direct owner, widened to the OUTERMOST ancestor
// whose whole subtree is selected (selecting a parent selects every nested module, and that means
// "ungroup the parent", not "ungroup every level").
std::set<juce::String> ungroupTargets(const synth::MacroSet& macros, const std::set<juce::String>& selectedUuids) {
    std::set<juce::String> targets;
    for (const auto& uuid : selectedUuids) {
        const auto* owner = macros.findByMember(uuid);
        if (owner == nullptr)
            continue;
        juce::String target = owner->id;
        for (const auto& ancestorId : macros.ancestorChain(owner->id)) {
            const auto subtree = macros.descendantMembers(ancestorId);
            if (std::includes(selectedUuids.begin(), selectedUuids.end(), subtree.begin(), subtree.end()))
                target = ancestorId; // chain runs inner -> outer, so the last hit is the outermost
        }
        targets.insert(target);
    }
    return targets;
}
} // namespace

void MacroGroupController::ungroupSelection() {
    std::set<juce::String> selectedUuids;
    for (auto id : host_.getSelection().getSelected())
        if (const auto uuid = nodeUuidFor(id); uuid.isNotEmpty())
            selectedUuids.insert(uuid);
    const auto macroIdsToRemove = ungroupTargets(host_.getMacros(), selectedUuids);

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

            // What the ungrouped macro held, for the re-selection below: its own modules and every nested
            // child's (never a port node: those are spliced out or belong to a surviving child).
            std::vector<juce::String> promoted;
            for (const auto& uuid : macro_nesting::orderedDescendantMembers(host_.getMacros(), macroId)) {
                const auto* owner = host_.getMacros().findByMember(uuid);
                if (owner == nullptr || !owner->memberIsPort(uuid))
                    promoted.push_back(uuid);
            }

            // Ungroup removes THIS macro's input/output ports (a child keeps its own). Splice every one of them back
            // out FIRST — auto-created and hand-added alike — restoring the external<->internal wiring each one
            // proxied, before falling through to the plain-module behaviour below. Iterate a COPY: each call mutates
            // m->ports/m->members as it goes.
            const auto portsToSplice = m->ports;
            for (const auto& port : portsToSplice)
                spliceOutMacroPort(*m, port.nodeUuid);

            // Ungrouping is still presentation-only for the macro's real modules — every member left in m->members
            // at this point is an ordinary module, never deleted, never disconnected. Its direct members move up to
            // the parent macro (or top level); MacroSet::remove re-parents its child macros the same way.
            m = host_.getMacros().find(macroId);
            if (m == nullptr)
                continue;
            const juce::String parentId = m->parentId;
            if (parentId.isNotEmpty())
                for (const auto& uuid : m->members)
                    host_.getMacros().addMember(parentId, uuid);
            host_.getMacros().remove(macroId);

            for (const auto& uuid : promoted) {
                auto nodeId = resolveMemberNodeId(uuid);
                if (nodeId.uid != 0 &&
                    std::find(newSelection.begin(), newSelection.end(), nodeId) == newSelection.end())
                    newSelection.push_back(nodeId);
            }
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
        if (const auto* m = toggleTargetFor(host_.getMacros(), uuid)) {
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
    // Cmd+G's single entry point. Mixed selection (some selected nodes already in a
    // macro, some loose): toggle wins outright — the touched macros are toggled and the loose
    // modules are silently left out of any grouping.
    auto ids = host_.getSelection().getSelected();
    std::set<juce::String> touchedMacroIds;
    int looseCount = 0;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        const auto* m = uuid.isEmpty() ? nullptr : toggleTargetFor(host_.getMacros(), uuid);
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

// Selects the macro's members TRANSITIVELY (a nested child's members too), so a chip or card drag of
// a parent carries everything drawn inside it.
void MacroGroupController::selectMacro(const juce::String& macroId, bool additive) {
    if (host_.getMacros().find(macroId) == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : macro_nesting::orderedDescendantMembers(host_.getMacros(), macroId)) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    host_.applySelectionChange(additive ? synth::ui::unionSelection(host_.getSelection().getSelected(), memberIds)
                                        : memberIds);
}

// True when the selection is exactly the set selectMacro(macroId) would make.
bool MacroGroupController::isMacroSelected(const juce::String& macroId) const {
    if (host_.getMacros().find(macroId) == nullptr)
        return false;
    const auto members = host_.getMacros().descendantMembers(macroId);
    if (members.empty() || host_.getSelection().size() != (int)members.size())
        return false;

    for (const auto& uuid : members) {
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
        auto addBounds = [&groupBounds](juce::Rectangle<int> r) {
            if (!r.isEmpty())
                groupBounds = groupBounds.isEmpty() ? r : groupBounds.getUnion(r);
        };
        for (const auto& uuid : m->members) {
            if (portNodeUuids.count(uuid) > 0)
                continue;
            auto nodeId = resolveMemberNodeId(uuid);
            for (auto* comp : host_.modules()) {
                if (comp != nullptr && comp->getNodeId() == nodeId) {
                    addBounds(comp->getBounds());
                    break;
                }
            }
        }
        // A nested child counts by its footprint (hull if open, card if collapsed), so a parent
        // whose only content is a child still seeds its card where that child is drawn.
        for (const auto& childId : host_.getMacros().childrenOf(macroId))
            if (const auto* child = host_.getMacros().find(childId))
                addBounds(child->collapsed ? macroCableAnchorBounds(*child) : macroHullBounds(childId));
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

    // An installed hook owns the whole transaction (so a linked track's rename joins this
    // one); anything else falls through to the plain graph+macro step this has always pushed.
    if (!(recordMacroRenameHook && recordMacroRenameHook(macroId, newName, doRename))) {
        if (host_.undo())
            host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doRename);
        else
            doRename();
    }

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

namespace {
// Every module a macro's card lists: its transitive members in stable order, minus port nodes.
std::vector<juce::String> previewableMembers(const synth::MacroSet& macros, const juce::String& macroId) {
    std::vector<juce::String> out;
    for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, macroId)) {
        const auto* owner = macros.findByMember(uuid);
        if (owner == nullptr || !owner->memberIsPort(uuid))
            out.push_back(uuid);
    }
    return out;
}
} // namespace

// Transitive: a parent's card previews its nested children's modules too. A member that fronts a
// port of whichever macro directly owns it is a boundary jack, not a module, and is skipped.
std::vector<MacroGroupController::MacroMemberPreview>
MacroGroupController::macroMemberPreviews(const juce::String& macroId) const {
    std::vector<MacroMemberPreview> result;
    auto& graph = host_.graph();
    for (const auto& uuid : previewableMembers(host_.getMacros(), macroId)) {
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
    auto& graph = host_.graph();
    // Same transitive list and port exclusion as macroMemberPreviews above.
    for (const auto& uuid : previewableMembers(host_.getMacros(), macroId)) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;
        auto* node = graph.getNodeForId(nodeId);
        names.add(host_.getModuleTitle(nodeId, node != nullptr ? node->getProcessor() : nullptr));
    }
    return names;
}
