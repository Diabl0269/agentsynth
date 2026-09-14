// GraphEditorPersistence.cpp
//
// Auto-arrange, and patch save/load/new-patch. GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "../../AI/AIStateMapper/AIStateMapper.h"
#include "../../PresetManager.h"
#include "../../SnippetManager.h"
#include "../ModuleComponent/ModuleComponent.h"

void GraphEditor::autoArrange() {
    auto& graph = audioEngine.getGraph();
    if (undoManager)
        undoManager->captureBeforeState(graph);

    auto sizeOf = [this](synth::LayoutUtil::NodeID id) -> juce::Point<int> {
        for (auto* c : content.getModules()) {
            if (c != nullptr && c->getNodeId() == id)
                return {c->getWidth(), c->getHeight()};
        }
        return {synth::LayoutUtil::kSingleWidth, 300};
    };

    std::vector<std::pair<synth::LayoutUtil::NodeID, synth::LayoutUtil::NodeID>> extra;
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasSource && r.hasDest)
            extra.push_back({r.sourceNodeID, r.destNodeID});
    }

    auto layout = synth::LayoutUtil::computeAutoArrange(graph, sizeOf, extra);
    for (const auto& a : layout) {
        if (auto* n = graph.getNodeForId(a.id)) {
            n->properties.set("x", a.pos.x);
            n->properties.set("y", a.pos.y);
        }
    }

    updateComponents();

    if (undoManager)
        undoManager->pushSnapshotFromCapture(graph);
}

void GraphEditor::savePreset(juce::File file) {
    auto json = synth::AIStateMapper::graphToJSON(audioEngine.getGraph());
    // Re-merge whatever unknown top-level keys were stashed on the last load (e.g. a future
    // build's "timeline") — graphToJSON only knows about the keys this build understands.
    json = patchDocument.toVar(json);
    file.replaceWithText(juce::JSON::toString(json));
}

bool GraphEditor::loadFactoryPreset(int index) {
    // Tear down existing module components (which stops their ScopeComponent timers) BEFORE
    // PresetManager clears the graph and frees the old VisualBuffers — otherwise a scope timer can
    // fire and read a freed buffer (use-after-free). Mirrors the safe order used by the test harness.
    detachAllModuleComponents();
    bool loaded = synth::PresetManager::loadPreset(index, audioEngine.getGraph());
    updateComponents();
    return loaded;
}

void GraphEditor::newPatch() {
    auto& graph = audioEngine.getGraph();

    auto doClear = [this, &graph] {
        // Detach BEFORE clearing — same ordering as loadFactoryPreset — so no ScopeComponent
        // timer fires against a freed VisualBuffer after graph.clear().
        detachAllModuleComponents();
        graph.clear();
        // A fresh patch has no file behind it — preserved keys are per-loaded-file and must
        // never be resurrected into it.
        patchDocument.clear();
        macros.clear();

        // T187: seed a fresh Audio Output immediately, in the same undo step as the clear, so
        // the first channel's Master splice (synth::spliceMasterNode) has something to target
        // right away — otherwise a bare Track Audio in a brand-new project is silently unheard
        // until the user manually adds an Audio Output.
        if (auto processor = synth::AIStateMapper::createModule("Audio Output")) {
            if (auto node = graph.addNode(std::move(processor))) {
                synth::AIStateMapper::ensureNodeUuid(node.get());
                node->properties.set("x", synth::LayoutUtil::kArrangeOriginX);
                node->properties.set("y", synth::LayoutUtil::kArrangeOriginY);
            }
        }

        updateComponents(); // reconciles the view around the fresh Audio Output
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doClear);
    } else {
        doClear();
    }
    repaint();
}

void GraphEditor::loadPreset(juce::File file, bool append) {
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not parse preset file.");
        return;
    }
    auto& graph = audioEngine.getGraph();

    if (append) {
        // "Add on top": the loaded patch is a self-contained sub-graph, so drop it through the
        // same machinery a pasted group uses. prepareForInsert renumbers its node ids to a free
        // base, so it COEXISTS with - and never matches-and-overwrites - the current patch; its Audio
        // Output is deduped onto the graph's existing one (see applyJSONToGraph); and it is a
        // trusted user file, so it is inserted on the trusted path, which carries the patch's "state"
        // (a loaded Sampler, a Wavetable table) the untrusted validator a dropped snippet runs would
        // refuse. The whole thing is one undo batch, and the freshly added modules are left selected so
        // the user can nudge the dropped group right away; the view is then fit to show the result.
        std::vector<juce::AudioProcessorGraph::NodeID> added;
        std::vector<synth::Macro> addedMacros;
        auto doInsert = [this, &graph, &json, &added, &addedMacros] {
            detachAllModuleComponents(); // stops scope timers before the graph is mutated
            added = synth::SnippetManager::insertSnippet(json, graph, juce::Point<int>(),
                                                         /*includeExtraState=*/true, &addedMacros,
                                                         /*trustedPayload=*/true);
            for (auto& macro : addedMacros)
                macros.add(macro);
            updateComponents(); // reconcile the view, prune stale macros against surviving nodes
        };
        if (undoManager)
            undoManager->recordGraphAndMacroChange(graph, macros, doInsert);
        else
            doInsert();

        if (added.empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                                   "Could not apply preset to graph.");
            return;
        }
        // Leave the imported modules selected and bring the (kept-coordinates) group on-screen.
        applySelectionChange(added);
        fitViewToModules();
        repaint();
        return;
    }

    // REPLACE: rebuild the graph from scratch. Preserving each node's saved uid (the
    // applyJSONToGraph trusted path) keeps ids stable across an undo/redo, and wrapping the whole
    // rebuild in an undo batch is what makes the load reversible at all: an unwrapped clear+rebuild
    // left the history unaware of the swap, so a following redo had nothing to restore.
    auto doReplace = [this, &graph, &json] {
        detachAllModuleComponents(); // stops scope timers before the graph is cleared
        const bool ok = synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true);
        if (ok)
            patchDocument.loadFromVar(json); // preserve per-file unknown keys (a trusted load)
        updateComponents();                  // reconcile the view to whatever state the graph is in
    };
    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doReplace);
    else
        doReplace();
    fitViewToModules();
    repaint();
}
