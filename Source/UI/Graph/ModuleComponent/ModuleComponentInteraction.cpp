// ModuleComponentInteraction.cpp -- automation/parameter callback reflection, macro/poly/dual-IO
// state application, the module and macro-port right-click context menus, mouse handling (drag,
// connection, selection, right-click-any-knob automate), and the inline title-rename editor.
// ModuleComponent is declared in ModuleComponent.h; the rest of its implementation lives in the
// sibling ModuleComponent*.cpp units next to this one (FRO65 split of the former single
// ModuleComponent.cpp).
#include "AI/AIStateMapper/AIStateMapper.h" // kMaxModuleDisplayNameChars — one cap for typed and loaded titles
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/MacroControlModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include <cmath>

using namespace detail;

// Automation -> UI reflection. `sliderParams`/`comboParams` are index-parallel to
// `sliders`/`comboBoxes` (see the header), so this is a straight linear scan for pointer identity —
// no name matching, no ambiguity between two params that happen to share a display name. A `param`
// nobody built a control for (nullptr entries included) simply matches nothing and returns.
void ModuleComponent::reflectParameterValue(const juce::AudioProcessorParameter* param, float normalized) {
    if (param == nullptr || module == nullptr)
        return; // nothing to reflect into, or this component is mid-teardown (detachFromProcessor)

    for (int i = 0; i < sliderParams.size(); ++i) {
        if (sliderParams[i] != param)
            continue;
        // Denormalise via the SAME RangedAudioParameter the slider's own NormalisableRange mirrors
        // (see SliderParameterAttachment's ctor) — the slider's range is already in these units, so
        // this is exactly the value the attachment itself would have pushed.
        const double denormalised = static_cast<double>(sliderParams[i]->convertFrom0to1(normalized));
        sliders[i]->setValue(denormalised, juce::dontSendNotification);
        return;
    }

    for (int i = 0; i < comboParams.size(); ++i) {
        if (comboParams[i] != param)
            continue;
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(comboParams[i])) {
            const int index = juce::jlimit(0, choiceParam->choices.size() - 1,
                                           static_cast<int>(std::lround(choiceParam->convertFrom0to1(normalized))));
            comboBoxes[i]->setSelectedItemIndex(index, juce::dontSendNotification);
        }
        return;
    }
}

// Right-click-any-knob -> "Automate '<Param>'" plus the MIDI Learn block (FRO130,
// appendMidiLearnMenuItems, ModuleComponentMidiLearn.cpp). `param` may be null (a control this
// component built without a real RangedAudioParameter behind it, e.g. the ExternalMidiModule
// device/channel combos — never true for anything reaching here through `sliders`, but checked
// anyway since sliderParams can hold a null entry per its own header comment).
void ModuleComponent::showAutomateMenuForSlider(juce::RangedAudioParameter* param) {
    if (param == nullptr)
        return;

    // The popup's actions run asynchronously (showMenuAsync), so `this` must be re-checked rather
    // than captured raw — the module (and its GraphEditor selection) could be gone by the time the
    // user picks an item (a delete, an undo, a preset load while the menu is open).
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    const auto nodeIdCopy = nodeId;
    const juce::String paramId = param->paramID;

    juce::PopupMenu menu;
    menu.addItem("Automate '" + param->getName(100) + "'", [safeThis, nodeIdCopy, paramId] {
        if (safeThis == nullptr)
            return;
        if (safeThis->owner.onAutomateParameterRequested)
            safeThis->owner.onAutomateParameterRequested(nodeIdCopy, paramId);
    });
    appendMidiLearnMenuItems(menu, param);
    // Routed through showContextMenuHook_ (rather than a direct showMenuAsync) so a test can
    // capture the built menu headlessly, the same seam buildModuleContextMenu()/
    // buildMacroPortContextMenu() already use — this menu had never needed it before FRO130 added
    // MIDI items worth asserting on.
    showContextMenuHook_(menu);
}

void ModuleComponent::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(newValue);

    // When bypass or mute changes, schedule a repaint on the message thread.
    // parameterValueChanged can be called from the audio thread — NEVER call
    // repaint() directly here.  SafePointer ensures the lambda is a no-op if
    // the component has been destroyed before the async call fires.
    if (module == nullptr)
        return;

    const auto& params = module->getParameters();
    if (parameterIndex < 0 || parameterIndex >= params.size())
        return;

    auto* param = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[parameterIndex]);
    if (param == nullptr)
        return;

    if (param->paramID == "bypassed" || param->paramID == "muted") {
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->repaint();
        });
    } else if (param->paramID == "poly") {
        // The module's channel layout just changed underneath its existing cables — re-anchor them
        // so a poly pair fans out to all voices and a mono pair collapses back to one.
        // Graph mutation is message-thread-only. The toggle-button path is already on that thread,
        // and running inline there keeps the rewire inside the parameter gesture's undo snapshot.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            applyPolyStateChange();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis == nullptr || safeThis->module == nullptr)
                    return;
                // Deferred, so the gesture's snapshot has already closed — take our own transaction.
                if (auto* undo = safeThis->undoManager)
                    undo->recordStructuralChange(safeThis->owner.getAudioEngine().getGraph(),
                                                 [safeThis] { safeThis->applyPolyStateChange(); });
                else
                    safeThis->applyPolyStateChange();
            });
        }
    } else if (param->paramID == "dualIO") {
        // Dual I/O only remaps visible jacks onto the same raw ch0/ch1 — tearing cables down
        // and rebuilding through resolvePolyLink would drop the right leg.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            applyDualIOLayoutChange();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr)
                    safeThis->applyDualIOLayoutChange();
            });
        }
    } else if (param->paramID == "macroCount") {
        // Resizing touches the component tree and the graph, so it must happen on the message
        // thread even though this callback can arrive from the audio thread.
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->applyMacroCountChange();
        });
    } else if (getType(module) == ModuleType::ADSR &&
               (param->paramID == "attack" || param->paramID == "hold" || param->paramID == "decay" ||
                param->paramID == "sustain" || param->paramID == "release" || param->paramID == "attackCurve" ||
                param->paramID == "decayCurve" || param->paramID == "releaseCurve")) {
        // FRO112: keep the envelope graph in sync with knob drags, automation, undo/redo and
        // preset loads. syncEnvelopeCurveFromParams itself no-ops while envelopeCurveGestureActive
        // (a live graph drag is already the source of truth for that span).
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            syncEnvelopeCurveFromParams();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr)
                    safeThis->syncEnvelopeCurveFromParams();
            });
        }
    } else if (getType(module) == ModuleType::ADSR && param->paramID == "tempoSync") {
        // FRO117: keep the MS|BPM toggle pair in sync with automation/undo/preset loads, the
        // same reverse-sync shape as the envelope graph branch above.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            syncEnvelopeSyncToggleFromParam();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr)
                    safeThis->syncEnvelopeSyncToggleFromParam();
            });
        }
    }
}

void ModuleComponent::applyMacroCountChange() {
    if (module == nullptr || dynamic_cast<MacroControlModule*>(module) == nullptr)
        return;

    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::layoutMacroBank(int count) {
    using namespace synth::LayoutUtil;

    const int margin = 20;

    // Header row: the Knobs count on the left, the Bipolar toggle beside it.
    for (int i = 0; i < sliders.size(); ++i) {
        if (!sliders[i]->getComponentID().equalsIgnoreCase("Knobs"))
            continue;
        sliderLabels[i]->setBounds(margin, 30, 80, 18);
        sliders[i]->setBounds(margin, 48, 80, 40);
        sliders[i]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 16);
    }

    for (auto* toggle : toggles) {
        if (toggle->getComponentID().equalsIgnoreCase("Bipolar"))
            toggle->setBounds(margin + 96, 56, 100, 24);
    }

    // One row per macro: knob on the left, value beside it, output jack (drawn in paint()) on
    // the right edge at the same y. Rows beyond the count are hidden, not destroyed — their
    // parameters still exist and keep their values if the bank is grown again.
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i) {
        const juce::String id = MacroControlModule::macroName(i);
        const bool visible = i < count;

        for (int s = 0; s < sliders.size(); ++s) {
            if (!sliders[s]->getComponentID().equalsIgnoreCase(id))
                continue;

            sliders[s]->setVisible(visible);
            if (s < sliderLabels.size())
                sliderLabels[s]->setVisible(false); // the jack label already names the row

            if (visible) {
                sliders[s]->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
                const int centreY = macroRowCentreY(i);
                sliders[s]->setBounds(margin, centreY - (kMacroRowH - 8) / 2, 140, kMacroRowH - 8);
            }
        }
    }
}

void ModuleComponent::captureLogicalPortMaps() {
    cachedInputPortMap.clear();
    cachedOutputPortMap.clear();

    auto* modBase = dynamic_cast<ModuleBase*>(module);
    if (modBase == nullptr)
        return;

    for (int raw = 0; raw < modBase->getTotalNumInputChannels(); ++raw)
        cachedInputPortMap.push_back(modBase->mapInputChannel(raw));
    for (int raw = 0; raw < modBase->getTotalNumOutputChannels(); ++raw)
        cachedOutputPortMap.push_back(modBase->mapOutputChannel(raw));
}

void ModuleComponent::applyPolyStateChange() {
    if (module == nullptr)
        return;

    const auto previousInputMap = cachedInputPortMap;
    const auto previousOutputMap = cachedOutputPortMap;
    captureLogicalPortMaps(); // adopt the new layout before the graph is touched
    owner.rewireForPolyChange(this, previousInputMap, previousOutputMap);
    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::updateDualIOTooltip() {
    if (dualIOButton == nullptr)
        return;
    const bool dual = dynamic_cast<ModuleBase*>(module) != nullptr && static_cast<ModuleBase*>(module)->isDualIO();
    dualIOButton->setTooltip(dual ? "Dual I/O on - separate Left and Right jacks"
                                  : "Dual I/O off - one Audio jack (Left + Right)");
}

void ModuleComponent::applyDualIOLayoutChange() {
    if (module == nullptr)
        return;

    captureLogicalPortMaps();
    updateDualIOTooltip();
    owner.completeStereoPairConnections(this);
    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (!undoManager || module == nullptr)
        return;

    if (gestureIsStarting) {
        // Capture full graph snapshot at gesture start
        gestureStartValues[parameterIndex] = 1.0f; // flag that gesture is active
        undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
    } else {
        auto it = gestureStartValues.find(parameterIndex);
        if (it != gestureStartValues.end()) {
            // Capture after snapshot and push as undo action
            auto* graphEditor = &owner;
            undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
            gestureStartValues.erase(it);
        }
    }
}

juce::PopupMenu ModuleComponent::buildMacroPortContextMenu() {
    juce::PopupMenu m;
    const auto ownership = owner.getMacroController().macroPortOwnerFor(nodeId);

    if (ownership.macro != nullptr && ownership.port != nullptr) {
        const juce::String macroId = ownership.macro->id;
        const juce::String uuid = ownership.port->nodeUuid;
        m.addItem("Rename Port...", [this, macroId, uuid] { owner.promptRenameMacroPort(macroId, uuid); });
        m.addItem("Configure I/O...", [this, macroId] { owner.promptConfigureMacroIO(macroId); });
        m.addSeparator();
        // Splices the boundary cable back together rather than dropping it (founder-review fix
        // G7) — GraphEditor::deleteMacroPortNode, sharing spliceOutMacroPort with ungroup.
        m.addItem("Delete Port",
                  [this, macroId, uuid] { owner.getMacroController().deleteMacroPortNode(macroId, uuid); });
    } else {
        // Defensive: macroPortOwnerFor's own header comment says this shouldn't happen (every
        // port node is constructed as a macro member with a matching MacroPort entry), but a port
        // node still needs SOME way out of the graph rather than none at all if it ever does.
        m.addItem("Delete Port", [this] {
            owner.setSelectedNodes({nodeId});
            owner.deleteSelection();
        });
    }

    return m;
}

juce::PopupMenu ModuleComponent::buildModuleContextMenu() {
    juce::PopupMenu m;

    // Selection actions (issue #156). Offered whenever this module is selected — a
    // single-module snippet is legal, it is just a group of one.
    const int selectionCount = owner.getSelectionCount();
    const juce::String groupSuffix =
        selectionCount > 1 ? " " + juce::String(selectionCount) + " Modules" : juce::String();

    m.addItem("Copy" + groupSuffix, [this] { owner.copySelection(); });
    m.addItem("Duplicate" + groupSuffix, [this] { owner.duplicateSelection(); });

    // Paste lands next to whatever was copied, not on this module — pasting on top of the
    // card the menu was opened from would hide the thing that just arrived.
    const int clipboardCount = owner.getClipboardModuleCount();
    juce::PopupMenu::Item paste(clipboardCount > 1 ? "Paste " + juce::String(clipboardCount) + " Modules" : "Paste");
    paste.setEnabled(clipboardCount > 0);
    paste.action = [this] { owner.pasteClipboard(); };
    m.addItem(paste);

    m.addItem(selectionCount > 1 ? "Save Selection as Snippet..." : "Save as Snippet...", [this] {
        if (owner.onSaveSnippetRequested)
            owner.onSaveSnippetRequested();
    });
    if (selectionCount > 1) {
        // Deliberately calls requestGroupSelectionIntoMacro() directly, NOT the Cmd+G dispatch
        // (GraphEditor::groupOrToggleSelectionMacros) — a menu item names one verb
        // ("Create Macro") and must keep doing exactly what it says, even for a selection
        // that already touches a macro (where it still refuses, same as always).
        // requestGroupSelectionIntoMacro() gates the auto-port-preference modal (founder-review
        // fix F5, docs/macros/auto-ports.md#auto-creating-ports-when-grouping) the same way Cmd+G does.
        m.addItem("Create Macro from " + juce::String(selectionCount) + " Modules",
                  [this] { owner.requestGroupSelectionIntoMacro(); });
        m.addItem("Delete " + juce::String(selectionCount) + " Selected Modules", [this] { owner.deleteSelection(); });
    }

    // FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): "Make Channel" for the chain this selection
    // belongs to (shown only when one resolves, disabled once it already has a channel), and "Duplicate into Channel"
    // when this module is shared into a channel macro from outside it.
    owner.addMakeChannelMenuItem(m);
    owner.addDuplicateIntoChannelMenuItems(m, nodeId);

    // Cmd+Alt+G's toggle, reachable from a member module's own menu too: an expanded
    // macro's card (the collapsed card's own menu) doesn't exist while expanded, so this
    // is the only always-reachable UI for the round trip. Shown for either state now —
    // toggleSelectionMacrosCollapsed() picks the right direction from the touched
    // macro's own current state, matching the label offered here.
    const auto* macro = owner.getMacroController().macroForNode(nodeId);
    if (macro != nullptr)
        m.addItem(macro->collapsed ? "Expand Macro" : "Collapse Macro",
                  [this] { owner.getMacroController().toggleSelectionMacrosCollapsed(); });
    // T138: a top-level escape hatch for the single most common macro-membership gesture — right-
    // click a member module you want OUT, without hunting for the nested "Macro: <name>" submenu's
    // own "Remove from Macro" item (found via live testing 2026-09-10: a user's first instinct was
    // "right-click the module and remove it from the macro", not "open its macro's own submenu").
    // Acts on THIS module alone via removeNodeFromMacro(nodeId), never the live selection — so it
    // behaves the same whether or not this module happens to be selected, unlike the submenu's own
    // item (which reads live selection, correctly retargeted to just this module by mouseDown()
    // above before this menu was ever built). Never reached for a port module in the first place —
    // mouseDown() routes isMacroPortType(getType(module)) to buildMacroPortContextMenu() instead,
    // this method's own early-return above — so `macro != nullptr` here always means an ordinary
    // member.
    if (macro != nullptr)
        m.addItem("Remove from Macro", [this] { owner.getMacroController().removeNodeFromMacro(nodeId); });

    m.addSeparator();

    // Bypass toggle (only for actual modules)
    if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
        m.addItem(mod->isBypassed() ? "Enable Module" : "Bypass Module", [this] {
            if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
                mod->setBypassed(!mod->isBypassed());
                repaint();
            }
        });
        m.addSeparator();
    }

    // "Replace with..." submenu (only for actual modules, not AudioGraphIOProcessor).
    // Audio Input is a ModuleBase but is still a singleton I/O node: replacing it with an
    // Oscillator would silently leave the patch with no way to get the device's input in,
    // and the library row it came from greyed out.
    if (dynamic_cast<ModuleBase*>(module) != nullptr && !GraphEditor::isSingletonIOModule(module->getName())) {
        juce::PopupMenu replaceMenu;
        auto currentType = getType(module);

        struct ModEntry {
            const char* name;
            ModuleType type;
        };
        struct Category {
            const char* header;
            std::vector<ModEntry> modules;
        };
        std::vector<Category> categories = {
            {"Sources",
             {{"Oscillator", ModuleType::Oscillator},
              {"Wavetable", ModuleType::Wavetable},
              {"Noise", ModuleType::Noise},
              {"Sampler", ModuleType::Sampler},
              {"LFO", ModuleType::LFO}}},
            {"Sequencing",
             {{"Sequencer", ModuleType::Sequencer},
              {"Poly Sequencer", ModuleType::PolySequencer},
              {"MIDI Keyboard", ModuleType::MidiKeyboard},
              {"Poly MIDI", ModuleType::PolyMidi},
              {"External MIDI", ModuleType::ExternalMidi}}},
            {"Envelopes & Control",
             {{"ADSR", ModuleType::ADSR},
              {"Envelope Follower", ModuleType::EnvelopeFollower},
              {"VCA", ModuleType::VCA}}},
            {"Filters", {{"Filter", ModuleType::Filter}, {"Parametric EQ", ModuleType::ParametricEQ}}},
            {"Modulation FX",
             {{"Chorus", ModuleType::Chorus},
              {"Phaser", ModuleType::Phaser},
              {"Flanger", ModuleType::Flanger},
              {"Distortion", ModuleType::Distortion},
              {"Ring Modulator", ModuleType::RingModulator},
              {"Bitcrusher", ModuleType::Bitcrusher},
              {"Pitch Shifter", ModuleType::PitchShifter}}},
            {"Time FX", {{"Delay", ModuleType::Delay}, {"Reverb", ModuleType::Reverb}}},
            {"Dynamics",
             {{"Compressor", ModuleType::Compressor}, {"Limiter", ModuleType::Limiter}, {"Gate", ModuleType::Gate}}},
            {"Utility",
             {{"Sample & Hold", ModuleType::SampleHold},
              {"Comparator", ModuleType::Comparator},
              {"Math", ModuleType::Math}}},
        };

        for (auto& cat : categories) {
            juce::PopupMenu catMenu;
            bool hasItems = false;
            for (auto& mod : cat.modules) {
                if (mod.type == currentType)
                    continue;
                juce::String typeName(mod.name);
                catMenu.addItem(typeName, [this, typeName] { owner.replaceModule(this, typeName); });
                hasItems = true;
            }
            if (hasItems)
                replaceMenu.addSubMenu(cat.header, catMenu);
        }

        m.addSubMenu("Replace with...", replaceMenu);
        m.addSeparator();
    }

    m.addItem("Delete Module", [this] { owner.deleteModule(this); });

    // Founder-review item 4 (docs/macros/menu-and-membership.md#the-macro-menus-entry-points): this module's own menu
    // also offers the macro it belongs to, as an appended submenu — never folded into the items above, and never built
    // when this module is in no macro (a module in no macro sees no change at all). buildMacroMenu
    // itself now selects THIS macro before running its "Ungroup"/"Save as Snippet..." items (see
    // its own comment), which is what makes it safe to graft on here without disturbing the module
    // selection the rest of this menu (Copy/Duplicate/Delete Module, above) acts on: nothing in
    // THIS method calls selectMacro, so the module retargeted at the top of mouseDown() stays
    // selected right up until a macro submenu item actually fires.
    if (macro != nullptr)
        m.addSubMenu("Macro: " + macro->name, owner.buildMacroMenu(macro->id));

    return m;
}

void ModuleComponent::mouseDown(const juce::MouseEvent& e) {
    // Clicking anywhere on a card commits an open inline title editor — this card's or another's.
    // FIRST, before the child-control guard below returns, so a press on a knob dismisses it too.
    // A press inside the editor itself never reaches here: it is a plain child with no mouse
    // listener registered on it, so JUCE delivers that press to the editor.
    owner.commitAnyOpenTitleRename();

    // A click that landed on a CHILD control this component attached itself to as a
    // MouseListener (currently just the generic auto-UI sliders — see createControls()) rather
    // than on this component's own body. e.getPosition() below is in THAT CHILD's local space, not
    // this one's, so none of the body-click geometry further down may run against it — checked
    // first, by identity against `sliders` (index-parallel to `sliderParams`, exactly like
    // reflectParameterValue()'s lookup).
    if (e.eventComponent != this) {
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i] == e.eventComponent) {
                if (e.mods.isPopupMenu())
                    showAutomateMenuForSlider(sliderParams[i]);
                return;
            }
        }
        // FRO130: every other learnable control (toggles, combos, header buttons, bespoke-card
        // knobs already matched above via `sliders`) -- ONE registry lookup rather than a new
        // per-kind identity loop (docs/control/midi-remote-ui.md#right-click-midi-learn--coverage,
        // Source/UI/CLAUDE.md). No "Automate" item here: that has only ever existed for sliders.
        if (e.mods.isPopupMenu()) {
            if (auto* param = midiLearnableRegistry_.find(e.eventComponent))
                showMidiLearnOnlyMenu(param);
        }
        return; // some other attached child's own click — nothing for the module body to do
    }

    auto port = getPortForPoint(e.getPosition());
    if (port) {
        if (e.mods.isPopupMenu()) {
            // Right click -> Disconnect
            juce::PopupMenu m;
            m.addItem("Disconnect",
                      [this, port] { owner.disconnectPort(this, port->index, port->isInput, port->isMidi); });

            m.showMenuAsync(juce::PopupMenu::Options());
        } else if (e.getNumberOfClicks() >= 2 && owner.getDoubleClickPortDisconnectEnabled()) {
            // Issue #216: intercept the second click so it does not start another cable drag.
            if (owner.isPortConnected(this, port->index, port->isInput, port->isMidi))
                owner.disconnectPort(this, port->index, port->isInput, port->isMidi);
            return;
        } else {
            // Start Connection Drag
            owner.beginConnectionDrag(this, port->index, port->isInput, port->isMidi, e.getScreenPosition());
        }
    } else {
        // Click on Body
        if (getType(module) == ModuleType::Attenuverter)
            return; // cannot drag

        // Docked macro-port widget (P8-15 fix F2): not individually selectable or draggable via a
        // LEFT click — its position is fully derived by GraphEditor::dockMacroPortWidgets()
        // against its macro's hull (docs/macros/ports.md#how-a-port-is-drawn), and a body drag/select here would fight
        // that on every layout pass. A WHOLE-macro drag (via the collapsed card, or selecting the
        // macro through selectMacro()) still carries it along: that path adds every member —
        // ports included — to the selection directly, never through this component's own
        // mouseDown, so it is unaffected by this early return.
        //
        // RIGHT-click is the one deliberate exception (founder-review fix G7: "they cannot be
        // removed") — falls through to the right-button branch below, which opens
        // buildMacroPortContextMenu() instead of the generic module menu, rather than leaving the
        // port with no delete affordance of its own once its macro is gone (ungroup) or Configure
        // I/O is otherwise inconvenient to reach.
        if (isMacroPortType(getType(module)) && !e.mods.isRightButtonDown())
            return;

        // Double-click the HEADER opens the inline rename. Intercepted here, before any drag is
        // armed, exactly like the port double-click above: the first click of the pair has already
        // armed (and its mouseUp disarmed) a body drag, and letting the second click arm another one
        // would leave bodyDragActive set under an open editor, so the next stray mouseDrag would
        // move the card out from under the cursor. Returning before dragStartPosition/bodyDragActive
        // are touched is what keeps the two gestures from fighting.
        if (e.getNumberOfClicks() >= 2 && !e.mods.isRightButtonDown() && e.getPosition().y < kHeaderHeight) {
            beginTitleRename();
            return;
        }

        // TRUE right button only, deliberately not isPopupMenu(): on macOS JUCE defines
        // popupMenuClickModifier as (rightButtonModifier | ctrlModifier), so isPopupMenu() is also
        // true for Ctrl+LEFT-click — and Ctrl+left-click is the insert-between drag modifier plus
        // the additive-selection toggle (see below). A card cannot open a menu and start a drag from
        // the same press, so the legacy one-button-mouse affordance loses here; right-click and
        // two-finger tap still open the menu on every platform.
        if (e.mods.isRightButtonDown()) {
            // A docked macro-port widget gets its OWN small menu (Delete, plus Rename/Configure
            // I/O when its macro is still alive) rather than the generic module menu below — it
            // has no Copy/Duplicate/Bypass/Replace concept, and is never part of the ordinary
            // module selection (the early return above), so there is nothing for a selection
            // retarget to do here either.
            if (isMacroPortType(getType(module))) {
                auto menu = buildMacroPortContextMenu();
                showContextMenuHook_(menu);
                return;
            }

            // Right-clicking outside the current selection retargets it to this module, so the
            // menu always acts on something the user can see is selected. This has to run BEFORE
            // buildModuleContextMenu() below reads owner.getSelectionCount() / builds its items,
            // not folded into that method itself: it is a real side effect of the CLICK, whereas
            // buildModuleContextMenu() also needs to be callable standalone (from a test) without
            // repeating a gesture that already happened.
            if (!owner.isNodeSelected(nodeId))
                owner.selectModule(nodeId, false);

            auto menu = buildModuleContextMenu();
            showContextMenuHook_(menu);
        } else {
            // ---- Selection semantics (issue #156) + Ctrl insert-between ----
            //
            // Ctrl+CLICK is an additive-select toggle and Ctrl+DRAG is an insert-between move, and
            // at mouse-down those are indistinguishable — so we arm BOTH and let mouse-up decide,
            // the same deferred classification the piano roll uses for Cmd on a note body
            // (cmdToggleNote_). The drag has to win at press time because it needs state (dragger,
            // undo capture, landing ghost) that cannot be conjured later; the toggle is the one that
            // can be completed retroactively, so mouseUp finishes it only if nothing moved.
            //
            // The selection is COLLAPSED onto this module for the duration, not added to: a group
            // drag suppresses smart connections entirely (shouldOfferSmartConnections) and moves
            // every member, so a leftover multi-selection would silently disable insert. The
            // pre-press selection is restored in mouseUp if the press turns out to be a click.
            //
            // Ctrl is tested BEFORE the additive modifiers and that ordering is load-bearing: on
            // Windows/Linux JUCE defines commandModifier AS ctrlModifier, so isCommandDown() is true
            // whenever Ctrl is down. Testing additive first would early-return there and a Ctrl+drag
            // could never arm the dragger — insert would be macOS-only. Taking this branch keeps
            // Ctrl+click toggling on those platforms anyway, via the deferred completion below. The
            // SAME reasoning is why the Cmd branch just below can never fire on Windows/Linux either
            // — this Ctrl branch already claimed the press there.
            //
            // reparentArmed is set from e.mods.isCommandDown() ALONE, unconditionally, before this
            // whole chain — deliberately NOT from "which branch fired" (ctrlTogglePending ||
            // cmdReparentPending), which is also true for a PLAIN macOS Ctrl+drag (Ctrl and Cmd are
            // genuinely distinct keys there). Gating on that instead would silently compound the
            // shipped insert-between gesture with a join/leave it was never designed to also do —
            // exactly the FRO40 regression this member exists to prevent. A plain click/drag with
            // neither modifier reaches the branches below with reparentArmed already correctly
            // false, same as it always was before this feature. On Windows/Linux the two keys
            // cannot be told apart at press time at all (isCommandDown() is true whenever Ctrl is),
            // so reparentArmed is true there and mouseUp arbitrates by whether the drag actually
            // crossed a hull (see mouseUp's own comment).
            reparentArmed = e.mods.isCommandDown();

            if (e.mods.isCtrlDown()) {
                ctrlTogglePending = true;
                ctrlPressSelection = owner.getSelectedNodes();
                owner.selectModule(nodeId, false);
            } else if (e.mods.isCommandDown()) {
                // FRO40: Cmd+drag across an expanded macro's hull JOINS/LEAVES that macro; Cmd+CLICK
                // (no movement) is still an additive-select toggle. Mirrors ctrlTogglePending exactly
                // — arm BOTH the deferred toggle and the drag, collapse the selection onto this
                // module, and fall through to the shared arming code below (never `return` here).
                cmdReparentPending = true;
                cmdPressSelection = owner.getSelectedNodes();
                owner.selectModule(nodeId, false);
            } else if (e.mods.isShiftDown()) {
                // Shift-click toggles membership and does NOT begin a drag: a modifier-click is
                // an edit to the selection, not a move.
                owner.selectModule(nodeId, true);
                return;
            } else if (!owner.isNodeSelected(nodeId)) {
                // A plain click on an already-selected module keeps the whole group intact so it can
                // be dragged; clicking anything else collapses the selection onto it.
                owner.selectModule(nodeId, false);
            }

            dragStartPosition = getPosition();
            bodyDragActive = true;
            if (undoManager)
                undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
            dragger.startDraggingComponent(this, e);
            // Record every selected module's origin so they can all follow this one.
            owner.beginSelectionDrag();
            // Show grid + ghost for this module-body drag.
            owner.beginDragPreview(getWidth(), getHeight(), getNodeId());
        }
    }
}

juce::String ModuleComponent::cardTitle() const { return owner.getModuleTitle(nodeId, module); }

void ModuleComponent::beginTitleRename() {
    if (module == nullptr || getType(module) == ModuleType::Attenuverter || isMacroPortType(getType(module)))
        return; // no header, nothing to rename here — Configure I/O renames the PORT (its own name)

    // Any editor already open commits first, and that mutates the graph — so nothing may hold state
    // across it. Same ordering as TimelineRulerComponent::beginRenameMarker.
    finishTitleRename(true);

    titleEditor = std::make_unique<juce::TextEditor>("moduleTitleRenameEditor");
    titleEditor->setComponentID("moduleTitleRenameEditor");
    titleEditor->setMultiLine(false);
    titleEditor->setReturnKeyStartsNewLine(false);
    titleEditor->setJustification(juce::Justification::centred);
    titleEditor->setInputRestrictions(synth::kMaxModuleDisplayNameChars);
    // Seeded with what the header currently SHOWS, so a first rename starts from "Chorus 2" rather
    // than from an empty box the user has to retype.
    titleEditor->setText(cardTitle(), juce::dontSendNotification);
    titleEditor->setBounds(2, 1, std::max(40, getWidth() - 4), kHeaderHeight - 2);
    titleEditor->onReturnKey = [this] { finishTitleRename(true); };
    titleEditor->onEscapeKey = [this] { finishTitleRename(false); };
    // Clicking away commits; Escape is the only cancel. Matches every other in-place rename here.
    titleEditor->onFocusLost = [this] { finishTitleRename(true); };
    addAndMakeVisible(*titleEditor);
    titleEditor->selectAll();
    titleEditor->grabKeyboardFocus();
}

void ModuleComponent::finishTitleRename(bool commit) {
    if (titleEditor == nullptr)
        return;

    // Detach FIRST: destroying the editor moves focus off it, which fires onFocusLost, which
    // re-enters here and finds a null editor and stops.
    auto editor = std::move(titleEditor);
    const juce::String typed = editor->getText();
    editor.reset();

    if (!commit)
        return;

    // Typing the auto-numbered name back is the same as having no custom title, so it stores as
    // blank and keeps following the numbering instead of freezing today's number in place.
    owner.setModuleDisplayName(nodeId, typed.trim() == module->getName() ? juce::String() : typed);
    repaint();
}

void ModuleComponent::moved() {
    if (module != nullptr)
        owner.updateModulePosition(this);
}

void ModuleComponent::mouseDrag(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.dragConnection(e.getScreenPosition());
    } else {
        if (!bodyDragActive)
            return; // modifier-click toggled selection; the dragger was never armed

        dragger.dragComponent(this, e, nullptr);
        // Carry every other selected module by the same delta from its own recorded origin.
        owner.dragSelectionBy(getPosition() - dragStartPosition, this);
        // Update the landing ghost to follow the live drag position.
        owner.updateDragPreview(getPosition());

        // Gap 3: re-derive reparentArmed live for a SINGLE-module drag, so Cmd pressed or released
        // mid-drag arms/disarms reparent on the spot instead of only whatever mouseDown latched —
        // see reparentArmed's own comment on ModuleComponent.h. A multi-selection group drag never
        // touches the flag here; it keeps mouseDown's latch for its whole gesture, unchanged.
        if (!owner.isSelectionDragActive())
            reparentArmed = e.mods.isCommandDown();

        // FRO40: gated on reparentArmed, NOT on ctrlTogglePending || cmdReparentPending — the
        // latter is also true for a plain macOS Ctrl+drag, which must never highlight or act on a
        // hull crossing (see reparentArmed's own comment on ModuleComponent.h). The CENTRE, not
        // the top-left, is what macroDragJoinOrLeaveTarget tests against (docs/macros/ports.md).
        if (reparentArmed)
            owner.updateMacroDragCandidate(nodeId, getBounds().getCentre());
        else
            owner.clearMacroDragCandidate();

        if (auto* p = getParentComponent())
            p->repaint();
    }
}

void ModuleComponent::mouseUp(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.endConnectionDrag(e.getScreenPosition());
        return;
    }

    if (!bodyDragActive)
        return;
    bodyDragActive = false;

    const bool moved = getPosition() != dragStartPosition;

    // Captured into a local BEFORE clearing the member below — mouseUp's own reparent-vs-plain-
    // finalize choice further down still needs it, and this is the ONE gate for that choice (see
    // reparentArmed's own comment on ModuleComponent.h for why it is NOT `ctrlTogglePending ||
    // cmdReparentPending`).
    const bool wasReparentArmed = reparentArmed;
    reparentArmed = false;

    // Ctrl/Cmd-press armed a drag AND a pending selection toggle at once; the press turning out to
    // be a click is what decides it was really the toggle. Restore the selection the press
    // collapsed and flip this module's membership. A Ctrl/Cmd+DRAG leaves the collapsed single
    // selection alone, exactly like a plain drag does.
    if (ctrlTogglePending) {
        if (!moved) {
            owner.setSelectedNodes(ctrlPressSelection);
            owner.selectModule(nodeId, true); // additive: toggles membership
        }
        ctrlTogglePending = false;
        ctrlPressSelection.clear();
    }
    if (cmdReparentPending) {
        // FRO40: Cmd+CLICK (no movement) mirrors Ctrl's deferred toggle exactly.
        if (!moved) {
            owner.setSelectedNodes(cmdPressSelection);
            owner.selectModule(nodeId, true);
        }
        cmdReparentPending = false;
        cmdPressSelection.clear();
    }

    if (!moved) {
        // Click without movement: drop the recorded origins without re-resolving positions.
        owner.cancelSelectionDrag();
        owner.clearMacroDragCandidate();
        owner.endDragPreview();
        return;
    }

    // FRO40 + the Windows/Linux Ctrl-vs-Cmd arbitration (see mouseDown's/reparentArmed's own
    // comments): a reparent-armed drag that crossed a macro hull boundary reparents, landing
    // position + membership + port splicing in ONE undo step. The LIVE candidate every mouseDrag
    // tick computed above IS the answer — mouseUp never re-queries geometry of its own, so what
    // gets finalized is exactly what was highlighted. Anything else (reparent not armed at all —
    // a plain drag, or on macOS a Ctrl-only drag — or a reparent-armed drag that never crossed a
    // hull) keeps the plain finalize path below, which is also what already carries Ctrl's
    // insert-between behaviour (SmartConnectionEngine samples isInsertModifierDown() live, from
    // inside finalizeModuleDrag) — on Windows/Linux that means a Ctrl-drag over a cable INSIDE a
    // hull performs BOTH insert-between and join/leave from the one gesture, because the platform
    // has no way to ask for one without the other (docs/macros/menu-and-membership.md#cmd-drag-across-a-hull-border).
    const juce::String macroCandidate = wasReparentArmed ? owner.getMacroDragCandidateId() : juce::String();
    if (macroCandidate.isNotEmpty()) {
        const bool isJoin = owner.getMacroController().macroForNode(nodeId) == nullptr;
        // This capture was never going to be consumed by a pushSnapshotFromCapture — the reparent
        // finalize below consumes it itself instead (GraphEditor::finalizeMacroMembershipDrag's
        // own comment has the full story on why it needs the ORIGINAL mousedown-time capture
        // rather than a fresh one). Finalize is the LAST thing this call does — nothing below may
        // touch `this` again.
        owner.finalizeMacroMembershipDrag(this, macroCandidate, isJoin);
        return;
    }

    // Snap to grid and resolve overlap BEFORE the undo snapshot so the snapped/cleared final
    // position is what gets captured in the diff.
    //
    // A group drag resolves as one rigid body (finalizeSelectionDrag); resolving each member
    // independently would spiral them apart and destroy the arrangement.
    if (owner.isSelectionDragActive())
        owner.finalizeSelectionDrag();
    else
        owner.finalizeModuleDrag(this);

    if (undoManager)
        undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
    owner.clearMacroDragCandidate();
    owner.endDragPreview();
}
