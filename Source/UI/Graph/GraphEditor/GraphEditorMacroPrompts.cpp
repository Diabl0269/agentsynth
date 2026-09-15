// GraphEditorMacroPrompts.cpp
//
// Every macro-related dialog/popup/menu builder that constructs a
// juce::Component::SafePointer<GraphEditor> for an async callback — NOT moved into
// MacroGroupController (FRO77 PR2) because a SafePointer needs a genuine GraphEditor&, not
// obtainable through the narrow GraphCanvasHost seam. See MacroGroupController.h's class comment.
// GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold
// the rest of the class.
//
// Every call below into a method that DID move (groupSelectionIntoMacro, setMacroCollapsed,
// selectMacro, ungroupSelection, addSelectionToMacro, removeSelectionFromMacro,
// deleteMacroAndMembers, macroBypassState/toggleMacroBypassed, macroMuteState/toggleMacroMuted,
// getMacroCardForTest, macroChipBounds, addMacroPort/renameMacroPort/removeMacroPort/
// moveMacroPortOrder/reorderMacroPortToIndex/changeMacroPortShape/changeMacroPortColour) is
// unchanged: each keeps its exact original name, resolving through GraphEditor's own
// one-line forwarder to macroController_ (declared in GraphEditor.h) exactly as it always
// resolved to GraphEditor's own body — the only genuine host-access rewrite in this file is
// showMacroAutoPortModal's buildMacroPortCrossingPlan() call (a private, no-forwarder method now
// owned outright by MacroGroupController), rewritten to go through macroController_ directly.

#include "GraphEditor.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "UI/Macros/MacroCardComponent.h"

// FRO13 (P9-7): canSaveTrackPresetForTrack's const-callable query — getMacros() itself is
// non-const, so MainComponent (a const TrackHeaderHost override) can't call findByMember() on it
// directly.
bool GraphEditor::isChannelMacroForTrack(const juce::String& memberUuid) const {
    const auto* macro = macros.findByMember(memberUuid);
    return macro != nullptr && synth::isChannelMacro(*macro, audioEngine.getGraph());
}

// MacroAutoPortPreference (GraphEditor.h) rationale:
// Tri-state, not a bool: "ask, then remember" needs a third value beyond on/off. Unset (the
// default) means "ask on the next group that has a crossing
// cable"; the other two mean "always do X, never ask." Persisted through
// juce::ApplicationProperties by PreferencesSettingsTab, mirroring its own
// key/getter/setter/*ForTest pattern exactly (see setDefaultDualIOForNewModules for the
// precedent), and pushed down here via setMacroAutoPortPreference(). The modal's own "Remember my
// choice" also persists directly through propertiesFile_ (see setPropertiesFile) — the same macro
// recolour favourites use — since the modal can fire before a Settings window (and therefore a
// PreferencesSettingsTab) has ever been constructed.
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
    // buildMacroPortCrossingPlan moved into MacroGroupController with no private forwarder (this
    // is its only caller outside the macro files) — the one genuine host-access rewrite in this
    // file; see the file header comment.
    const int crossingCount = (int)macroController_.buildMacroPortCrossingPlan(selection.getSelected()).size();

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

// Mirrors MainComponent::promptSaveSnippet's `juce::AlertWindow` idiom exactly (SafePointer +
// ModalCallbackFunction + a unique_ptr taken inside the callback). The collapsed card keeps its
// own nicer inline rename (MacroCardComponent::beginRename) — this is only for the case that has
// no card.
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

// The same picker the timeline ruler's marker menu and the track header swatch use
// (TimelineRulerComponent::buildMarkerColourPicker is the exact pattern this mirrors). Live
// preview while the user drags (writes straight to the macro, no undo step, so dragging never
// floods the undo stack); ONE undo step on commit, recorded via setMacroColour so the undo
// restores the ORIGINAL colour rather than the last preview value.
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

// `renameAction`, when supplied, replaces the default "Rename..." item's handler — the collapsed
// card passes its own inline-editor opener (MacroCardComponent::beginRename) here; every other
// caller (the hull menu) leaves it empty and gets promptRenameMacro's dialog, since there is no
// card to host an inline editor there.
//
// `addCandidateSelection` (T138): both the collapsed card's own right-click
// (MacroCardComponent::mouseDown) and the expanded hull's empty-space right-click
// (GraphEditor::mouseDown's macroHullAt branch) call selectMacro(macroId, false) BEFORE this
// method ever runs, so by the time it reads the CURRENT selection, any external batch the user
// picked before right-clicking is already gone — replaced by the macro's own members. Both call
// sites therefore capture the selection themselves right before that reselect and pass it here;
// "Add Selection to Macro" is computed against THIS list (falling back to the current live
// selection only when null — the ModuleComponent member-submenu graft, whose own narrower
// retarget-if-not-already-selected never destroys an external batch the same way). "Remove from
// Macro" always reads the CURRENT live selection regardless — after either reselect it correctly
// equals the macro's own members, which is exactly what removal should see.
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
    // FRO13 (P9-7, docs/mixer.md §5.7): only a mixer channel (a macro boxing a Channel Strip) can
    // be saved as a track preset — omitted entirely on an ordinary group, same "Mute Macro"
    // omit-when-meaningless precedent above.
    if (synth::isChannelMacro(*macro, audioEngine.getGraph())) {
        m.addItem("Save Track as Preset...", [safeThis, macroId] {
            if (safeThis == nullptr)
                return;
            safeThis->selectMacro(macroId, false);
            if (safeThis->onTrackPresetMenuAction)
                safeThis->onTrackPresetMenuAction(macroId, false);
        });
        m.addItem("Set as Default Track Preset", [safeThis, macroId] {
            if (safeThis == nullptr)
                return;
            safeThis->selectMacro(macroId, false);
            if (safeThis->onTrackPresetMenuAction)
                safeThis->onTrackPresetMenuAction(macroId, true);
        });
    }
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

void GraphEditor::promptConfigureMacroIO(const juce::String& macroId) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    auto* dialog = new synth::ui::MacroPortConfigDialog(macro->name, macroPortRowsForDialog(macroId));
    dialog->setColourPickerPropertiesFile(propertiesFile_); // T152; nullptr is fine (in-memory favs)

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Configure I/O";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    // T153: same reasoning as showMacroAutoPortModal above — the dialog's own keyPressed()
    // override (Escape -> onRequestClose) is the ONE Escape route, not competing with
    // juce::DialogWindow's default (which would just hide the window on a different dispatch
    // path, bypassing onRequestClose and every commit-on-close side effect it triggers).
    options.escapeKeyTriggersCloseButton = false;
    auto* window = options.launchAsync();

    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::Component::SafePointer<synth::ui::MacroPortConfigDialog> safeDialog(dialog);

    dialog->onRequestClose = [window] {
        if (window != nullptr)
            window->exitModalState(0);
    };

    // Every callback below defers its mutate-then-refresh to the next message-loop tick — a
    // button's onClick handler calling refreshPorts() synchronously would tear down and rebuild
    // the very row (and button) that is still inside its own click dispatch, which JUCE does not
    // support. callAsync sidesteps that exactly like AIChatComponent/ModuleComponent already do
    // for the same reason.
    dialog->onAddPort = [safeThis, safeDialog, macroId](bool isInput, synth::MacroPortKind kind, MacroPortShape shape,
                                                        int voiceCount, const juce::String& name) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, isInput, kind, shape, voiceCount, name] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->addMacroPort(macroId, isInput, kind, shape, voiceCount, name);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onRenamePort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, const juce::String& name) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, name] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->renameMacroPort(macroId, nodeUuid, name);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onDeletePort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->removeMacroPort(macroId, nodeUuid);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onReorderPort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, bool moveUp) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, moveUp] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->moveMacroPortOrder(macroId, nodeUuid, moveUp);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onReorderPortTo = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, int newIndexInGroup) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newIndexInGroup] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->reorderMacroPortToIndex(macroId, nodeUuid, newIndexInGroup);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onChangePortShape = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, MacroPortShape newShape,
                                                                int newVoiceCount) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newShape, newVoiceCount] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->changeMacroPortShape(macroId, nodeUuid, newShape, newVoiceCount);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onChangePortColour = [safeThis, safeDialog, macroId](const juce::String& nodeUuid,
                                                                 std::optional<juce::Colour> newColour) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newColour] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->changeMacroPortColour(macroId, nodeUuid, newColour);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };

    window->enterModalState(true, nullptr, true);
}

void GraphEditor::promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    juce::String currentName;
    for (const auto& p : macro->ports)
        if (p.nodeUuid == nodeUuid)
            currentName = p.name;

    // promptRenameMacro's own AlertWindow idiom exactly (see its comment for the ownership/
    // lifetime reasoning) — the port node's own context menu's quicker alternative to opening the
    // whole Configure I/O modal just to retype one name.
    auto* window = new juce::AlertWindow("Rename Port", "New name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", currentName, "Port name:");
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<GraphEditor> safeThis(this);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safeThis, window, macroId, nodeUuid](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;

                                auto* self = safeThis.getComponent();
                                if (self == nullptr)
                                    return;

                                const auto typed = owned->getTextEditorContents("name").trim();
                                if (typed.isEmpty())
                                    return; // empty/whitespace-only input cancels without renaming

                                self->renameMacroPort(macroId, nodeUuid, typed);
                            }),
                            false);
}
