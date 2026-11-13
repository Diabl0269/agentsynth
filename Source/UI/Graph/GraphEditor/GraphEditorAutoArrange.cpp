// GraphEditorAutoArrange.cpp
//
// Auto-arrange (Cmd+L, toolbar): the canvas half. It flattens the live canvas into the blocks and edges the pure
// layout (UI/Layout/HierarchicalArrange.h) works on, and writes the result back as ONE undo step covering node
// positions AND collapsed-card bounds. GraphEditor is declared in GraphEditor.h. (docs/layout/layout.md#auto-arrange)

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/HierarchicalArrange.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace {
using synth::LayoutUtil::ArrangeBlock;
using synth::LayoutUtil::ArrangeEdge;
using synth::LayoutUtil::ArrangeInput;
using synth::LayoutUtil::ArrangeKind;
using NodeID = juce::AudioProcessorGraph::NodeID;

juce::String nodeKey(NodeID id) { return "n:" + juce::String(static_cast<juce::int64>(id.uid)); }
juce::String macroKey(const juce::String& macroId) { return "m:" + macroId; }

// What the canvas looks like right now, flattened for the pure layout. Ids are the layout-unit keys
// ("n:<node uid>" / "m:<macro id>") so a block id maps straight back to the thing to move.
class ArrangeCanvas {
public:
    ArrangeCanvas(juce::AudioProcessorGraph& graphIn, synth::MacroSet& macrosIn,
                  juce::OwnedArray<ModuleComponent>& modulesIn, MacroGroupController& controllerIn)
        : graph(graphIn)
        , macros(macrosIn)
        , modules(modulesIn)
        , ctl(controllerIn) {
        for (auto* node : graph.getNodes())
            uuidToUid[node->properties["uuid"].toString()] = node->nodeID.uid;
    }

    ArrangeInput build(const std::vector<AudioEngine::ModulationRouting>& routings,
                       const std::vector<juce::String>& trackUuids) {
        classifyModules();
        ArrangeInput in;
        for (auto* comp : blockModules)
            if (macros.findByMember(uuidOf(comp)) == nullptr)
                in.blocks.push_back(moduleBlock(*comp));
        for (const auto& macro : macros.getAll())
            if (macro.parentId.isEmpty())
                if (ArrangeBlock block; macroBlock(macro, block))
                    in.blocks.push_back(std::move(block));
        for (const auto& [id, target] : aliasOf)
            in.aliases.push_back({id, target});

        for (const auto& conn : graph.getConnections())
            in.edges.push_back({nodeKey(conn.source.nodeID), nodeKey(conn.destination.nodeID), false});
        for (const auto& r : routings)
            if (r.hasSource && r.hasDest)
                in.edges.push_back({nodeKey(r.sourceNodeID), nodeKey(r.destNodeID), true});
        in.trackStarts = trackStarts(trackUuids);
        return in;
    }

    // Where each rigid block (a collapsed card, or an open macro with nothing to arrange inside) is now, so the
    // adapter can turn the new position into the delta the controller moves it by.
    const std::map<juce::String, juce::Point<int>>& currentPositions() const { return currentPos; }

private:
    juce::AudioProcessorGraph& graph;
    synth::MacroSet& macros;
    juce::OwnedArray<ModuleComponent>& modules;
    MacroGroupController& ctl;
    std::map<juce::String, juce::uint32> uuidToUid;
    std::vector<ModuleComponent*> blockModules;                    // visible, arranged cards
    std::map<juce::String, std::vector<ModuleComponent*>> inMacro; // direct block members per macro id
    std::map<juce::String, juce::String> aliasOf;                  // hidden member / port node -> stand-in block
    std::map<juce::String, juce::Point<int>> currentPos;

    juce::String uuidOf(ModuleComponent* comp) const { return ctl.nodeUuidFor(comp->getNodeId()); }

    // Sorts every module card into: arranged block (loose or a direct member of an open macro), alias (a port
    // node, or a member hidden inside a collapsed macro: it stands in as its macro), or ignored (the output dock,
    // which reflowOutputDock owns).
    void classifyModules() {
        for (auto* comp : modules) {
            if (comp == nullptr || comp->getModule() == nullptr || synth::isOutputDockProcessor(comp->getModule()))
                continue;
            const auto uuid = uuidOf(comp);
            const auto* owner = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
            const auto hiddenBy = uuid.isEmpty() ? juce::String() : macros.outermostCollapsedAncestorOf(uuid);
            if (hiddenBy.isNotEmpty()) {
                aliasOf[nodeKey(comp->getNodeId())] = macroKey(hiddenBy);
            } else if (owner != nullptr && owner->memberIsPort(uuid)) {
                aliasOf[nodeKey(comp->getNodeId())] = macroKey(owner->id);
            } else if (comp->isVisible()) {
                blockModules.push_back(comp);
                if (owner != nullptr)
                    inMacro[owner->id].push_back(comp);
            }
        }
    }

    ArrangeBlock moduleBlock(ModuleComponent& comp) const {
        ArrangeBlock b;
        b.id = nodeKey(comp.getNodeId());
        b.kind = ArrangeKind::Module;
        b.size = {comp.getWidth(), comp.getHeight()};
        b.order = comp.getNodeId().uid;
        const auto* mb = dynamic_cast<ModuleBase*>(comp.getModule());
        b.roleRank = mb != nullptr ? synth::LayoutUtil::arrangeRoleRank(mb->getModuleType()) : 50;
        return b;
    }

    // Stable across runs: the lowest node id among everything the macro holds.
    long long macroOrder(const synth::Macro& macro) const {
        long long best = std::numeric_limits<long long>::max();
        for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, macro.id))
            if (auto it = uuidToUid.find(uuid); it != uuidToUid.end())
                best = std::min<long long>(best, it->second);
        return best;
    }

    bool macroBlock(const synth::Macro& macro, ArrangeBlock& out) {
        out.id = macroKey(macro.id);
        out.order = macroOrder(macro);
        out.roleRank = 4;
        if (macro.collapsed) {
            const auto card = ctl.macroCableAnchorBounds(macro);
            if (card.isEmpty())
                return false;
            out.kind = ArrangeKind::CollapsedMacro;
            out.size = {card.getWidth(), card.getHeight()};
            currentPos[out.id] = card.getPosition();
            return true;
        }
        for (auto* comp : inMacro[macro.id])
            out.children.push_back(moduleBlock(*comp));
        for (const auto& child : macros.getAll())
            if (child.parentId == macro.id)
                if (ArrangeBlock block; macroBlock(child, block))
                    out.children.push_back(std::move(block));
        const auto hull = ctl.macroHullBounds(macro.id);
        if (out.children.empty()) {
            // Nothing to arrange inside (a macro of ports only): keep it as one rigid rectangle.
            if (hull.isEmpty())
                return false;
            out.kind = ArrangeKind::CollapsedMacro;
            out.size = {hull.getWidth(), hull.getHeight()};
            currentPos[out.id] = hull.getPosition();
            return true;
        }
        out.kind = ArrangeKind::OpenMacro;
        int rows = 0;
        for (const auto& port : ctl.macroHullPortLayout(macro.id))
            rows = std::max(rows, port.row + port.rows);
        out.portRows = rows;
        return true;
    }

    // Timeline order first (each track's own source node), then any other track source (a hand-built patch, or no
    // timeline attached) by node id.
    std::vector<juce::String> trackStarts(const std::vector<juce::String>& trackUuids) const {
        std::vector<juce::String> starts;
        std::set<juce::uint32> seen;
        for (const auto& uuid : trackUuids)
            if (auto it = uuidToUid.find(uuid); it != uuidToUid.end() && seen.insert(it->second).second)
                starts.push_back(nodeKey(NodeID(it->second)));
        std::vector<juce::uint32> rest;
        for (auto* node : graph.getNodes())
            if (synth::isTrackSourceNode(node->getProcessor()) && seen.insert(node->nodeID.uid).second)
                rest.push_back(node->nodeID.uid);
        std::sort(rest.begin(), rest.end());
        for (auto uid : rest)
            starts.push_back(nodeKey(NodeID(uid)));
        return starts;
    }
};
} // namespace

// One undo step (node x/y AND macro bounds), geometry written synchronously, then only the light refresh. The whole
// mutation, dock re-derivation included, runs inside the record so undo restores every position it wrote.
//
// "Home" is redefined here: the transient displaced (macro and module card) / expand-nudge records describe an earlier
// arrangement, so they are cleared, or a later collapse would drag neighbours "back" to spots this layout has just
// left.
void GraphEditor::autoArrange(bool record) {
    auto& graph = audioEngine.getGraph();
    ArrangeCanvas canvas(graph, macros, content.getModules(), macroController_);
    const auto routings = audioEngine.getModulationRoutings();
    const auto input = canvas.build(routings, trackSourceOrder ? trackSourceOrder() : std::vector<juce::String>());
    if (input.blocks.empty())
        return;
    const auto layout = synth::LayoutUtil::computeHierarchicalArrange(input);
    const auto current = canvas.currentPositions();

    auto apply = [this, &graph, &layout, &current] {
        for (const auto& [id, pos] : layout.positions) {
            if (id.startsWith("n:")) {
                const NodeID nodeId(
                    static_cast<juce::uint32>(id.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue()));
                if (auto* node = graph.getNodeForId(nodeId)) {
                    node->properties.set("x", pos.x);
                    node->properties.set("y", pos.y);
                }
                if (auto* comp = moduleComponentFor(nodeId))
                    comp->setTopLeftPosition(pos);
            } else if (auto found = current.find(id); found != current.end()) {
                // A rigid macro (card, or an open macro with nothing inside): hidden members travel with it.
                macroController_.moveUnitBy(id, pos - found->second);
            }
        }

        macroController_.clearModuleDisplacements();
        std::vector<juce::String> macroIds;
        for (const auto& macro : macros.getAll())
            macroIds.push_back(macro.id);
        for (const auto& id : macroIds)
            if (auto* macro = macros.find(id)) {
                macro->displaced.clear();
                macro->hasExpandRecord = false;
                macro->expandNudge = {};
                macro->preExpandCardOrigin = {};
            }

        // The dock lines up with the first row: its shared y is Audio Output's own stored y.
        if (const auto dock = synth::outputDockNodes(graph); !dock.empty())
            dock.back()->properties.set("y", layout.firstRowY);

        syncMacroCards();
        macroController_.dockMacroPortWidgets();
        reflowOutputDock();
        repaintCanvas();
    };

    CardGlideAnimator::Scope glide(cardGlide_); // the moved cards slide; the undo record lands inside the scope
    if (undoManager && record)
        undoManager->recordGraphAndMacroChange(graph, macros, apply);
    else
        apply();
}
