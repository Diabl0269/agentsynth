// GraphEditorMacroGrouping.cpp
//
// Group/ungroup into a macro, collapse/expand, macro colour and the macro context menu.
// GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory
// hold the rest of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "../../AI/AIStateMapper.h"
#include "../MacroCardComponent.h"
#include "../ModuleComponent.h"
#include "../Theme/AppLookAndFeel.h"

using namespace detail;

juce::String GraphEditor::groupSelectionIntoMacro(bool autoCreatePorts) {
    auto ids = selection.getSelected();
    if (ids.size() < 2) {
        if (onStatusMessage)
            onStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    std::vector<juce::String> memberUuids;
    juce::Rectangle<int> groupBounds;
    for (auto id : ids) {
        // A module freshly dropped onto the canvas has no "uuid" property yet — it's only ever
        // lazily assigned on first save (synth::AIStateMapper::graphToJSON). Assign it here too,
        // the same way, so grouping newly-placed modules doesn't drop them from the selection.
        auto* node = audioEngine.getGraph().getNodeForId(id);
        const juce::String uuid = node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
        if (uuid.isEmpty())
            continue; // no persistent identity to group by — shouldn't happen for a real module

        if (macros.findByMember(uuid) != nullptr) {
            // Flat model, deliberately refused rather than silently merging/re-parenting — see
            // synth::Macro's class comment. A status message, not a silent no-op: Cmd+G doing
            // nothing with no explanation reads as broken, and a test can only assert "nothing
            // happened" against a no-op, which is indistinguishable from an actual bug.
            if (onStatusMessage)
                onStatusMessage("Can't group: a selected module is already in a macro. Ungroup it first.");
            return {};
        }
        memberUuids.push_back(uuid);

        for (auto* comp : content.getModules()) {
            if (comp != nullptr && comp->getNodeId() == id) {
                groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                break;
            }
        }
    }

    if (memberUuids.size() < 2) {
        if (onStatusMessage)
            onStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    // Founder-review fix F5 (docs/macros_implementation.md §7 item 6.1): the crossing plan is read off the LIVE
    // graph now, before the macro exists — resolveMemberNodeId (which buildMacroPortCrossingPlan
    // uses internally) only knows about macros already in `macros`, so this has to work off the
    // uuid list directly. Pure read; nothing below mutates the graph until doGroup runs.
    std::vector<MacroPortCrossingGroup> portPlan;
    if (autoCreatePorts)
        portPlan = buildMacroPortCrossingPlan(memberUuids);

    synth::Macro macro;
    macro.name = "Macro";
    macro.members = memberUuids;
    macro.collapsed = true;
    const auto origin = groupBounds.isEmpty() ? juce::Point<int>() : groupBounds.getTopLeft();
    macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);

    auto& graph = audioEngine.getGraph();
    juce::String newId;
    auto doGroup = [this, macro, portPlan, &newId] {
        newId = macros.add(macro).id;
        // Splice BEFORE updateComponents(): the spliced port nodes must exist, and be macro
        // members, before the card/hull layout that updateComponents() triggers runs against
        // them — never group-then-add as a second pass.
        if (!portPlan.empty())
            spliceMacroPorts(newId, portPlan);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doGroup);
    else
        doGroup();

    if (!newId.isEmpty())
        selectMacro(newId, false);

    repaint();
    return newId;
}

juce::String GraphEditor::addMacroForMembers(const std::vector<juce::String>& memberUuids, const juce::String& name,
                                             juce::Point<int> origin) {
    if (memberUuids.empty())
        return {};

    // Same flat-model refusal groupSelectionIntoMacro() applies: a member already claimed by another
    // macro aborts the whole call rather than silently re-parenting it.
    for (const auto& uuid : memberUuids)
        if (macros.findByMember(uuid) != nullptr)
            return {};

    synth::Macro macro;
    macro.name = name;
    macro.members = memberUuids;
    macro.collapsed = true;
    macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);

    return macros.add(macro).id;
}

void GraphEditor::addSelectionToMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || memberUuids.empty())
        return;

    // Same flat-model refusal groupSelectionIntoMacro() applies: abort the WHOLE add rather than
    // adding the rest and silently skipping the uuid that's already spoken for.
    for (const auto& uuid : memberUuids) {
        if (!macro->hasMember(uuid) && macros.findByMember(uuid) != nullptr) {
            if (onStatusMessage)
                onStatusMessage("Can't add: a selected module is already in a macro. Ungroup it first.");
            return;
        }
    }

    // T138: uuids genuinely new to THIS macro — the ones the port-crossing plan below cares about;
    // a uuid already a member of macroId is silently skipped by addMember() below same as always,
    // and contributes no new crossing (its ports, if any, are already correct).
    std::vector<juce::String> toAdd;
    for (const auto& uuid : memberUuids)
        if (!macro->hasMember(uuid))
            toAdd.push_back(uuid);

    // Computed off the PRE-add graph/macro state — pure reads, no mutation yet. See
    // buildMacroPortCrossingPlanForNewMembers/macroPortsThatBecomeInteriorOnAdd's own header
    // comments for what each one covers (new crossings created by the join; existing ports the
    // join makes redundant).
    const auto addPlan = buildMacroPortCrossingPlanForNewMembers(macroId, toAdd);
    const auto portsToSpliceOut = macroPortsThatBecomeInteriorOnAdd(macroId, toAdd);

    auto& graph = audioEngine.getGraph();
    auto doAdd = [this, macroId, memberUuids, addPlan, portsToSpliceOut] {
        for (const auto& uuid : memberUuids)
            macros.addMember(macroId, uuid);
        if (!addPlan.empty())
            spliceMacroPorts(macroId, addPlan);
        // Splice-out AFTER splice-in: a port this add makes redundant is never one addPlan just
        // created (addPlan only fronts brand-new members, never macroId's own existing ports), so
        // the two never race — but ordering them this way keeps every port mutation for this one
        // undo step grouped by "what's new" then "what's now redundant", matching the header
        // comments' own order.
        for (const auto& portUuid : portsToSpliceOut) {
            if (auto* liveMacro = macros.find(macroId))
                spliceOutMacroPort(*liveMacro, portUuid);
        }
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doAdd);
    else
        doAdd();

    repaint();
}

void GraphEditor::removeSelectionFromMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || memberUuids.empty())
        return;

    // Ports have their own delete affordance (ModuleComponent::buildMacroPortContextMenu) — pulling
    // one out of `members` here would desync Macro::ports (every port's nodeUuid must be a member)
    // without splicing its cable back the way that affordance does.
    std::vector<juce::String> toRemove;
    for (const auto& uuid : memberUuids)
        if (macro->hasMember(uuid) && !macro->memberIsPort(uuid))
            toRemove.push_back(uuid);
    if (toRemove.empty())
        return;

    // T138: a cable from a departing member to one that's staying is about to become a real
    // boundary crossing — computed off the PRE-remove graph, before anything moves. See
    // buildMacroPortCrossingPlanForRemovedMembers's own header comment.
    const auto removePlan = buildMacroPortCrossingPlanForRemovedMembers(macroId, toRemove);

    auto& graph = audioEngine.getGraph();
    auto doRemove = [this, macroId, toRemove, removePlan] {
        // Splice BEFORE the membership removal below: spliceMacroPorts needs macros.find(macroId)
        // to still resolve, and removeMemberEverywhere can dissolve the macro record outright if
        // this drops its last member (MacroSet's own "zero members" rule) — doing it last means
        // that dissolve, if it happens, always comes after the boundary is already correct.
        if (!removePlan.empty())
            spliceMacroPorts(macroId, removePlan);
        for (const auto& uuid : toRemove)
            macros.removeMemberEverywhere(uuid);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRemove);
    else
        doRemove();

    repaint();
}

void GraphEditor::removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId) {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return;
    const auto* macro = macros.findByMember(uuid);
    if (macro == nullptr)
        return;
    removeSelectionFromMacro(macro->id, {uuid});
}

bool GraphEditor::selectionHasCrossingMacroCable() const {
    // NodeID-based (not selectedMemberUuidsReadOnly()-style uuid resolution): a freshly-dropped,
    // never-saved module has no "uuid" property yet, so gating on resolvable uuids would silently
    // miss the crossing cable on the single most common real path (select two brand-new modules
    // wired to an external one, group immediately). buildMacroPortCrossingPlan()'s NodeID overload
    // needs no uuid at all. See its header comment for the full reasoning.
    const auto ids = selection.getSelected();
    if (ids.size() < 2)
        return false;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        // Only a module that has already been saved once can have a uuid, and only such a module
        // can already be a macro member — a selection touching an existing macro is refused
        // outright by groupSelectionIntoMacro() (see its own guard), so there is nothing to ask
        // about: mirror that refusal here rather than showing a modal for a grouping that can't
        // happen.
        if (uuid.isNotEmpty() && macros.findByMember(uuid) != nullptr)
            return false;
    }
    return !buildMacroPortCrossingPlan(ids).empty();
}

void GraphEditor::requestGroupSelectionIntoMacro() {
    const bool hasCrossing = selectionHasCrossingMacroCable();

    if (macroAutoPortPreference_ == MacroAutoPortPreference::Unset && hasCrossing) {
        juce::Component::SafePointer<GraphEditor> safeThis(this);
        auto respond = [safeThis](bool createPorts, bool remember) {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            if (remember) {
                self->macroAutoPortPreference_ =
                    createPorts ? MacroAutoPortPreference::AutoCreatePorts : MacroAutoPortPreference::LeaveCablesAsIs;
                // Persisted here (not only through PreferencesSettingsTab) because this modal can
                // fire before a Settings window — and therefore a PreferencesSettingsTab — has
                // ever been constructed. Same propertiesFile_ the macro recolour favourites shelf
                // already persists through (setPropertiesFile); the key matches the constexpr
                // PreferencesSettingsTab.cpp duplicates for its own read.
                if (self->propertiesFile_ != nullptr) {
                    self->propertiesFile_->setValue("macroAutoCreatePorts", createPorts ? "auto" : "leave");
                    self->propertiesFile_->saveIfNeeded();
                }
            }
            self->groupSelectionIntoMacro(createPorts);
        };
        if (macroAutoPortModalForTest)
            macroAutoPortModalForTest(respond);
        else
            showMacroAutoPortModal(respond);
        return;
    }

    groupSelectionIntoMacro(macroAutoPortPreference_ == MacroAutoPortPreference::AutoCreatePorts);
}

void GraphEditor::showMacroAutoPortModal(std::function<void(bool createPorts, bool remember)> respond) {
    const int crossingCount = (int)buildMacroPortCrossingPlan(selection.getSelected()).size();

    auto* dialog = new synth::ui::MacroAutoPortPromptDialog(crossingCount);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Macro Ports";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    // T153: the dialog's own keyPressed() override is the ONE Escape route (see its class comment
    // for the "Escape == Leave Cables As Is" decision) — juce::DialogWindow's own default Escape
    // handling (a Button shortcut dispatched on a DIFFERENT path than the keyPressed bubble our
    // override sits on) would otherwise race it and just hide the window with `onChoice` never
    // firing, which is the exact "no macro, no status message" bug this fixes.
    options.escapeKeyTriggersCloseButton = false;
    auto* window = options.launchAsync();

    dialog->onChoice = [window, respond](bool createPorts, bool remember) {
        if (window != nullptr)
            window->exitModalState(0);
        respond(createPorts, remember);
    };
}

void GraphEditor::ungroupSelection() {
    auto ids = selection.getSelected();
    std::set<juce::String> macroIdsToRemove;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (auto* m = macros.findByMember(uuid))
            macroIdsToRemove.insert(m->id);
    }

    if (macroIdsToRemove.empty()) {
        if (onStatusMessage)
            onStatusMessage("Select a macro's modules to ungroup it.");
        return;
    }

    auto& graph = audioEngine.getGraph();
    auto doUngroup = [this, macroIdsToRemove] {
        std::vector<juce::AudioProcessorGraph::NodeID> newSelection;
        for (const auto& macroId : macroIdsToRemove) {
            auto* m = macros.find(macroId);
            if (m == nullptr) {
                continue; // defensive: shouldn't happen mid-transaction
            }

            // Founder review, second pass: "ungroup leaves the macro input/output in place (They
            // should be removed)". Splice every one of this macro's ports back out FIRST — auto-
            // created and hand-added alike, no provenance distinction needed (spliceOutMacroPort's
            // own header comment) — restoring the external<->internal wiring each one proxied,
            // before falling through to the plain-module behaviour below. Iterate a COPY: each
            // call mutates m->ports/m->members as it goes.
            const auto portsToSplice = m->ports;
            for (const auto& port : portsToSplice)
                spliceOutMacroPort(*m, port.nodeUuid);

            // Ungrouping is still presentation-only for the macro's real modules (docs/macros.md
            // section 1/section 4): every member left in m->members at this point is an ordinary
            // module, never deleted, never disconnected — only the macro record itself (below) and
            // the ports just spliced above are removed.
            for (const auto& uuid : m->members) {
                auto nodeId = resolveMemberNodeId(uuid);
                if (nodeId.uid != 0)
                    newSelection.push_back(nodeId);
            }
            macros.remove(macroId);
        }
        updateComponents();
        setSelectedNodes(newSelection);
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doUngroup);
    else
        doUngroup();

    repaint();
}

void GraphEditor::toggleSelectionMacrosCollapsed() {
    auto ids = selection.getSelected();
    std::set<juce::String> touchedMacroIds;
    bool anyExpanded = false;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (auto* m = macros.findByMember(uuid)) {
            touchedMacroIds.insert(m->id);
            if (!m->collapsed)
                anyExpanded = true;
        }
    }

    // Refused only when the selection touches NO macro at all — unlike the old collapse-only
    // command, a selection sitting entirely inside an already-collapsed macro is a legitimate
    // toggle target (it expands), not a no-op.
    if (touchedMacroIds.empty()) {
        if (onStatusMessage)
            onStatusMessage("Select a macro's modules to collapse or expand it.");
        return;
    }

    // DETERMINISTIC RULE (see the header doc comment): if any touched macro is expanded, collapse
    // them ALL; otherwise every touched macro is already collapsed, so expand them all.
    const bool targetCollapsed = anyExpanded;

    // ONE undo entry for the whole gesture, not one per macro. Calling setMacroCollapsed in this
    // loop would record a separate recordGraphAndMacroChange per touched macro, so a single
    // Cmd+Alt+G over a selection spanning three macros would need three Cmd+Z to undo — the
    // undo history should mirror the gesture the user made, not the macros it happened to reach.
    // Hence applyMacroCollapsed (the raw mutation) inside one recorded change.
    auto& graph = audioEngine.getGraph();
    auto doToggleAll = [this, touchedMacroIds, targetCollapsed] {
        for (const auto& macroId : touchedMacroIds)
            applyMacroCollapsed(macroId, targetCollapsed);
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doToggleAll);
    else
        doToggleAll();

    repaint();
}

void GraphEditor::groupOrToggleSelectionMacros() {
    // Cmd+G's single entry point (P8-14). "Group" is a strong cross-application convention
    // (Figma, Illustrator, Sketch, most DAWs), so the key is honoured exactly when it applies —
    // a selection that is not yet grouped. Once the selection IS in a macro, grouping stops being
    // a meaningful verb for it (the flat model refuses nested macros anyway — see
    // groupSelectionIntoMacro's refusal above), so the key is free to mean the thing the user
    // actually reaches for at that point: toggling the touched macro(s) collapsed/expanded,
    // exactly like the explicit Cmd+Alt+G binding (toggleSelectionMacrosCollapsed) which is kept
    // as its own binding on purpose — it's unambiguous when the user wants to be certain, and
    // removing it would break anyone's saved keybinding.
    //
    // Mixed selection (some selected nodes already in a macro, some loose): toggle wins outright.
    // The touched macros are toggled and the loose modules are silently left out of any grouping
    // — NOT grouped in with them, and NOT refused — because grouping would either violate the
    // flat, no-nested-macros model or silently drop the loose modules from the gesture, and a
    // flat refusal here would read as broken for a shortcut that usually just works. A status
    // message says what happened instead.
    auto ids = selection.getSelected();
    std::set<juce::String> touchedMacroIds;
    int looseCount = 0;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        const auto* m = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
        if (m != nullptr)
            touchedMacroIds.insert(m->id);
        else
            ++looseCount;
    }

    if (touchedMacroIds.empty()) {
        // Nothing selected touches a macro — Cmd+G means exactly what it always meant: group.
        // requestGroupSelectionIntoMacro() carries its own refusal/status behaviour (fewer than
        // two modules) unchanged, and additionally gates the auto-port-preference modal (founder-
        // review fix F5, docs/macros_implementation.md §7 item 6.2) when it applies.
        requestGroupSelectionIntoMacro();
        return;
    }

    toggleSelectionMacrosCollapsed();

    if (looseCount > 0 && onStatusMessage) {
        const juce::String macroWord = touchedMacroIds.size() == 1 ? "macro" : "macros";
        const juce::String moduleWord = looseCount == 1 ? "module" : "modules";
        const juce::String verb = looseCount == 1 ? "was" : "were";
        onStatusMessage("Toggled " + juce::String((int)touchedMacroIds.size()) + " " + macroWord + "; " + moduleWord +
                        " outside a macro " + verb + " left alone.");
    }
}

const synth::Macro* GraphEditor::macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return nullptr;
    return macros.findByMember(uuid);
}

void GraphEditor::selectMacro(const juce::String& macroId, bool additive) {
    auto* m = macros.find(macroId);
    if (m == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    applySelectionChange(additive ? synth::ui::unionSelection(selection.getSelected(), memberIds) : memberIds);
}

bool GraphEditor::isMacroSelected(const juce::String& macroId) const {
    const auto* m = macros.find(macroId);
    if (m == nullptr || m->members.empty() || selection.size() != (int)m->members.size())
        return false;

    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0 || !selection.contains(nodeId))
            return false;
    }
    return true;
}

void GraphEditor::applyMacroCollapsed(const juce::String& macroId, bool collapsed) {
    {
        auto* m = macros.find(macroId);
        if (m == nullptr || m->collapsed == collapsed)
            return;

        if (collapsed) {
            // Collapsing FROM expanded: seed the card at the current member bounding box's
            // top-left, sized to the standard card footprint rather than the (possibly huge)
            // group — that is the whole point of collapsing.
            //
            // Port members are EXCLUDED from this union, the same way macroHullBounds() excludes
            // them (P8-15 fix F2): a port widget is DOCKED outside the hull (to its left/right
            // edge), so folding it into "the group" here would seed the collapsed card's top-left
            // ~kMacroPortDockGap+widget-width to the left of where the ordinary members actually
            // sit, for any macro with even one input port.
            std::set<juce::String> portNodeUuids;
            for (const auto& p : m->ports)
                portNodeUuids.insert(p.nodeUuid);

            juce::Rectangle<int> groupBounds;
            for (const auto& uuid : m->members) {
                if (portNodeUuids.count(uuid) > 0)
                    continue;
                auto nodeId = resolveMemberNodeId(uuid);
                for (auto* comp : content.getModules()) {
                    if (comp != nullptr && comp->getNodeId() == nodeId) {
                        groupBounds =
                            groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                        break;
                    }
                }
            }
            const auto origin = groupBounds.isEmpty() ? m->bounds.getTopLeft() : groupBounds.getTopLeft();
            m->bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);
        }
        m->collapsed = collapsed;
        updateComponents();
    }
}

void GraphEditor::setMacroCollapsed(const juce::String& macroId, bool collapsed) {
    auto& graph = audioEngine.getGraph();
    auto doToggle = [this, macroId, collapsed] { applyMacroCollapsed(macroId, collapsed); };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doToggle);
    else
        doToggle();

    repaint();
}

void GraphEditor::renameMacro(const juce::String& macroId, const juce::String& newName) {
    auto& graph = audioEngine.getGraph();
    auto doRename = [this, macroId, newName] {
        if (auto* m = macros.find(macroId))
            m->name = newName;
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRename);
    else
        doRename();

    syncMacroCards();
    repaint();
}

void GraphEditor::setMacroColour(const juce::String& macroId, juce::Colour colour) {
    auto& graph = audioEngine.getGraph();
    auto doRecolour = [this, macroId, colour] {
        if (auto* m = macros.find(macroId))
            m->colour = colour;
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRecolour);
    else
        doRecolour();

    syncMacroCards();
    repaint();
}

void GraphEditor::promptRenameMacro(const juce::String& macroId) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    if (promptRenameMacroForTest) {
        promptRenameMacroForTest(macroId);
        return;
    }

    auto* window = new juce::AlertWindow("Rename Macro", "New name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", macro->name, "Macro name:");
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // SafePointer + a unique_ptr taken inside the callback — MainComponent::promptSaveSnippet's
    // AlertWindow idiom exactly (see its comment for why the dialog must outlive this call, and
    // why the AlertWindow is owned inside the callback rather than by a member).
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, window, macroId](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;

                                auto* self = safeThis.getComponent();
                                if (self == nullptr)
                                    return;

                                const auto typed = owned->getTextEditorContents("name").trim();
                                if (typed.isEmpty())
                                    return; // empty/whitespace-only input cancels without renaming

                                self->renameMacro(macroId, typed);
                            }),
                            false);
}

std::unique_ptr<synth::ui::ColourPickerPopup> GraphEditor::buildMacroColourPicker(const juce::String& macroId) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return nullptr;

    // The colour a no-net-change close restores, and what a "keep the final pick" undo step
    // restores TO — the exact shape TimelineRulerComponent::buildMarkerColourPicker uses.
    const juce::Colour originalColour = macro->colour;
    juce::Component::SafePointer<GraphEditor> safeThis(this);

    return std::make_unique<synth::ui::ColourPickerPopup>(
        originalColour, propertiesFile_,
        [safeThis, macroId](juce::Colour c) {
            // Live preview: writes the macro directly, no undo — every drag repaints live. Goes
            // straight at the macro rather than through setMacroColour, which records an undo
            // step per call.
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            if (auto* m = self->macros.find(macroId))
                m->colour = c;
            self->syncMacroCards();
            self->repaint();
        },
        [safeThis, macroId, originalColour](juce::Colour finalColour) {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return; // the editor (or its window) is gone — nothing left to restore or undo
            auto* m = self->macros.find(macroId);
            if (m == nullptr)
                return; // the macro was deleted while the popup was open
            if (finalColour.getARGB() == originalColour.getARGB()) {
                // No net change: put back exactly what was there (a preview may have nudged it)
                // and record no undo step.
                m->colour = originalColour;
                self->syncMacroCards();
                self->repaint();
                return;
            }
            // ONE undo step whose undo restores the ORIGINAL colour: silently put the original
            // back first (outside the recorded mutation, so it does not itself become undoable),
            // then perform the real edit as the one recorded step.
            m->colour = originalColour;
            self->setMacroColour(macroId, finalColour);
        });
}

void GraphEditor::promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea) {
    auto popup = buildMacroColourPicker(macroId);
    if (popup == nullptr)
        return;
    juce::CallOutBox::launchAsynchronously(std::move(popup), screenArea, nullptr);
}

std::unique_ptr<synth::ui::ColourPickerPopup> GraphEditor::createMacroColourPickerForTest(const juce::String& macroId) {
    return buildMacroColourPicker(macroId);
}

juce::PopupMenu
GraphEditor::buildMacroMenu(const juce::String& macroId, std::function<void()> renameAction,
                            const std::vector<juce::AudioProcessorGraph::NodeID>* addCandidateSelection) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return {};

    const bool collapsed = macro->collapsed;
    juce::Component::SafePointer<GraphEditor> safeThis(this);

    // T138: "Add Selection to Macro" is computed from `addCandidateSelection` when the caller
    // supplied one — both the collapsed card's own right-click (MacroCardComponent::mouseDown) and
    // the expanded hull's empty-space right-click (GraphEditor::mouseDown's macroHullAt branch)
    // call selectMacro(macroId, false) BEFORE this method ever runs *only when the prior selection
    // was completely empty* — which means by the time this reads the CURRENT selection it may
    // already be just the macro's own members, with any external batch (or a member subset picked
    // for removal) the user had before right-clicking gone. Both call sites capture the selection
    // themselves right before that conditional reselect and pass it in here, so this always sees
    // the true pre-click batch regardless of whether the reselect ran. Falls back to the current
    // live selection when null (the ModuleComponent member-submenu graft, whose own narrower
    // retarget-if-not-already-selected never destroys an external batch the same way, and where
    // "Add" barely applies anyway since the clicked module is already this macro's member).
    std::vector<juce::String> addableUuids;
    const auto& addCandidates = addCandidateSelection != nullptr ? *addCandidateSelection : selection.getSelected();
    for (auto id : addCandidates) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isNotEmpty() && !macro->hasMember(uuid))
            addableUuids.push_back(uuid);
    }

    // "Remove from Macro" always reads the CURRENT live selection — after either forced reselect
    // above it correctly equals the macro's own members, which is exactly what removal should see;
    // unaffected by the add-candidate capture, since removing never needs to see PAST selection.
    std::vector<juce::String> removableUuids;
    for (auto id : selection.getSelected()) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (macro->hasMember(uuid)) {
            // A port is a boundary jack, not a module the user put in the box (docs/macros_ports.md
            // §5.1) — it has its own "Delete Port" affordance and must never be pulled out of
            // `members` by this generic path.
            if (!macro->memberIsPort(uuid))
                removableUuids.push_back(uuid);
        }
    }

    juce::PopupMenu m;
    m.addItem(collapsed ? "Expand" : "Collapse", [safeThis, macroId, collapsed] {
        if (safeThis != nullptr)
            safeThis->setMacroCollapsed(macroId, !collapsed);
    });

    // The collapsed card passes its own inline-TextEditor opener here; everywhere else (the
    // expanded hull's right-click menu) there is no card to host that editor, so it falls back to
    // the AlertWindow dialog.
    if (renameAction)
        m.addItem("Rename...", std::move(renameAction));
    else
        m.addItem("Rename...", [safeThis, macroId] {
            if (safeThis != nullptr)
                safeThis->promptRenameMacro(macroId);
        });

    // Anchor re-derived NOW, inside the click handler, rather than captured at menu-build time —
    // the macro could collapse/expand or the card/chip could move between the right-click and the
    // menu choice (same reasoning as TimelineRulerComponent::openMarkerContextMenu's own comment).
    m.addItem("Change Colour...", [safeThis, macroId] {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        const auto* liveMacro = self->macros.find(macroId);
        if (liveMacro == nullptr)
            return;

        juce::Rectangle<int> anchor;
        if (liveMacro->collapsed) {
            if (auto* card = self->getMacroCardForTest(macroId))
                anchor = card->getScreenBounds();
        } else {
            anchor = self->content.localAreaToGlobal(self->macroChipBounds(macroId));
        }
        if (anchor.isEmpty())
            anchor = self->getScreenBounds(); // fallback: nothing resolved, anchor on the editor

        self->promptRecolourMacro(macroId, anchor);
    });
    // Unifies docs/macros_implementation.md §7 items 3 (add) and 5 (rename/reorder) into ONE modal per an
    // explicit founder request, rather than separate "Add Input"/"Add Output"/"Rename..."/
    // "Reorder" menu items.
    m.addItem("Configure I/O...", [safeThis, macroId] {
        if (safeThis != nullptr)
            safeThis->promptConfigureMacroIO(macroId);
    });
    m.addSeparator();
    // Bypass/mute fan-out (§5.6, T142): each item names the action a click is about to perform,
    // so a Mixed or fully-off state reads as targeting ON ("Bypass"/"Mute") and a fully-on state
    // reads as targeting OFF ("Enable"/"Unmute") — the same convergence rule toggleMacroBypassed/
    // toggleMacroMuted apply. Mute is omitted entirely when no member could possibly honour it
    // (e.g. a macro made only of Macro In/Out ports), rather than offering a command that can only
    // ever no-op.
    m.addItem(macroBypassState(macroId) == MacroToggleState::AllOn ? "Enable Macro" : "Bypass Macro",
              [safeThis, macroId] {
                  if (safeThis != nullptr)
                      safeThis->toggleMacroBypassed(macroId);
              });
    if (macroHasMuteEligibleMember(macroId)) {
        m.addItem(macroMuteState(macroId) == MacroToggleState::AllOn ? "Unmute Macro" : "Mute Macro",
                  [safeThis, macroId] {
                      if (safeThis != nullptr)
                          safeThis->toggleMacroMuted(macroId);
                  });
    }
    m.addSeparator();
    // These two act on the CURRENT SELECTION (onSaveSnippetRequested / ungroupSelection), not on
    // macroId directly, so each selects THIS macro immediately before acting — a caller no longer
    // has to pre-select it (the hull right-click site still does, redundantly but harmlessly, per
    // its own comment). This is what makes buildMacroMenu correct when grafted onto a member
    // module's own right-click menu (founder-review item 4): that menu's whole point is to leave
    // the module selection alone for its OWN items, so without this, invoking either item here
    // would act on whatever was selected when the menu opened rather than on this macro.
    m.addItem("Save as Snippet...", [safeThis, macroId] {
        if (safeThis == nullptr)
            return;
        safeThis->selectMacro(macroId, false);
        if (safeThis->onSaveSnippetRequested)
            safeThis->onSaveSnippetRequested();
    });
    m.addItem("Ungroup", [safeThis, macroId] {
        if (safeThis == nullptr)
            return;
        safeThis->selectMacro(macroId, false);
        safeThis->ungroupSelection();
    });
    // T138: unlike the two items above, these act on the captured selection (addableUuids/
    // removableUuids), not on whatever is selected at click time — see the capture comment above.
    // Omitted entirely (not shown disabled) when there is nothing they could do, matching "Mute
    // Macro"'s own precedent of omitting a command that can only ever no-op.
    if (!addableUuids.empty()) {
        m.addItem("Add Selection to Macro", [safeThis, macroId, addableUuids] {
            if (safeThis != nullptr)
                safeThis->addSelectionToMacro(macroId, addableUuids);
        });
    }
    if (!removableUuids.empty()) {
        m.addItem(removableUuids.size() == 1 ? "Remove from Macro" : "Remove Selection from Macro",
                  [safeThis, macroId, removableUuids] {
                      if (safeThis != nullptr)
                          safeThis->removeSelectionFromMacro(macroId, removableUuids);
                  });
    }
    m.addSeparator();
    m.addItem("Delete Macro && Modules", [safeThis, macroId] {
        if (safeThis != nullptr)
            safeThis->deleteMacroAndMembers(macroId);
    });

    return m;
}

std::vector<GraphEditor::MacroMemberPreview> GraphEditor::macroMemberPreviews(const juce::String& macroId) const {
    std::vector<MacroMemberPreview> result;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return result;

    auto& graph = audioEngine.getGraph();
    for (const auto& uuid : macro->members) {
        // A port node is a boundary jack, not a module to preview (founder-review fix G6) — the
        // content preview draws one glyph per module the user actually grouped, matching the
        // "N modules" count text below it (MacroCardComponent::getModuleCountText).
        if (macro->memberIsPort(uuid))
            continue;

        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;

        ModuleComponent* comp = nullptr;
        for (auto* c : const_cast<GraphContentComponent&>(content).getModules()) {
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

juce::StringArray GraphEditor::macroMemberNames(const juce::String& macroId) const {
    juce::StringArray names;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return names;

    auto& graph = audioEngine.getGraph();
    for (const auto& uuid : macro->members) {
        // Same exclusion as macroMemberPreviews above (founder-review fix G6): the tooltip lists
        // the modules the user grouped, not the boundary-jack nodes a crossing cable spliced in.
        if (macro->memberIsPort(uuid))
            continue;

        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;
        auto* node = graph.getNodeForId(nodeId);
        names.add(getModuleTitle(nodeId, node != nullptr ? node->getProcessor() : nullptr));
    }
    return names;
}

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
