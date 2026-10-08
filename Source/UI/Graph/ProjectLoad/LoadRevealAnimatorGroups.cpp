// LoadRevealAnimatorGroups.cpp -- what pops together and in which order. A group is a top-level card (a module, or a
// collapsed macro with its hidden members), or an open macro with every card inside it, nested macros included: an
// open macro's border fades in while its cards pop together. The order is the signal flow across groups, with every
// cardless node (a hidden attenuverter) passed through, and the output dock last (SignalFlowOrder.h).

#include "LoadRevealAnimator.h"

#include "MacroOwnerIndex.h"
#include "MacroSet.h"
#include "SignalFlowOrder.h"

namespace {

// The outermost macro around `macroId`: the one whose border (or card) shows on the canvas.
juce::String outermostMacro(const synth::MacroSet& macros, juce::String macroId) {
    for (int guard = 0; guard < 64; ++guard) {
        const auto* macro = macros.find(macroId);
        if (macro == nullptr || macro->parentId.isEmpty() || macros.find(macro->parentId) == nullptr)
            break;
        macroId = macro->parentId;
    }
    return macroId;
}

} // namespace

void LoadRevealAnimator::buildGroups(const std::set<uint32_t>& pendingNodes) {
    groups_.clear();
    groupOfNode_.clear();
    groupOfHull_.clear();
    cableGroups_.clear();
    pendingNodes_.clear();
    if (!hooks_.cards || !hooks_.graph || !hooks_.macros)
        return;
    auto& graph = hooks_.graph();
    const auto& macros = hooks_.macros();
    const synth::MacroOwnerIndex owners(macros);
    std::map<juce::String, int> groupOfKey;
    const auto groupFor = [&](const juce::String& key) {
        const auto [it, added] = groupOfKey.emplace(key, (int)groups_.size());
        if (added)
            groups_.emplace_back();
        return it->second;
    };
    const auto uuidOf = [&graph](uint32_t uid) {
        const auto* node = graph.getNodeForId(juce::AudioProcessorGraph::NodeID(uid));
        return node != nullptr ? node->properties["uuid"].toString() : juce::String();
    };
    const auto macroKeyOf = [&](const juce::String& uuid) {
        const auto* owner = uuid.isNotEmpty() ? owners.ownerOf(uuid) : nullptr;
        return owner != nullptr ? "m:" + outermostMacro(macros, owner->id) : juce::String();
    };

    for (const auto& card : hooks_.cards()) {
        if (card.comp == nullptr)
            continue;
        juce::String key =
            card.nodeUid != 0 ? macroKeyOf(uuidOf(card.nodeUid)) : "m:" + outermostMacro(macros, card.macroId);
        if (key.isEmpty())
            key = "n:" + juce::String(card.nodeUid);
        const int g = groupFor(key);
        groups_[(size_t)g].cards.emplace_back(card.comp);
        if (card.nodeUid != 0)
            groupOfNode_[card.nodeUid] = g;
    }
    for (const auto& macro : macros.getAll())
        if (!macro.collapsed) // a border hidden inside a collapsed card paints nothing anyway
            if (const auto it = groupOfKey.find("m:" + outermostMacro(macros, macro.id)); it != groupOfKey.end()) {
                groupOfHull_[macro.id] = it->second;
                groups_[(size_t)it->second].hulls.push_back(macro.id);
            }

    // Vertices: the groups, then one per node that belongs to none (a hidden attenuverter, a card-less node).
    std::unordered_map<uint32_t, int> vertexOfNode;
    int vertexCount = (int)groups_.size();
    for (auto* node : graph.getNodes()) {
        const uint32_t uid = node->nodeID.uid;
        int v = groupOfNode(uid);
        if (v < 0) {
            const auto key = macroKeyOf(node->properties["uuid"].toString());
            const auto it = key.isNotEmpty() ? groupOfKey.find(key) : groupOfKey.end();
            if (it != groupOfKey.end())
                groupOfNode_[uid] = v = it->second; // a member hidden inside a collapsed macro card
            else
                v = vertexCount++;
        }
        vertexOfNode[uid] = v;
        if (v < (int)groups_.size() && hooks_.isOutputNode && hooks_.isOutputNode(uid))
            groups_[(size_t)v].hasOutputNode = true;
    }
    std::set<std::pair<int, int>> edgeSet;
    for (const auto& c : graph.getConnections()) {
        const auto from = vertexOfNode.find(c.source.nodeID.uid);
        const auto to = vertexOfNode.find(c.destination.nodeID.uid);
        if (from != vertexOfNode.end() && to != vertexOfNode.end() && from->second != to->second)
            edgeSet.insert({from->second, to->second});
    }
    std::vector<bool> last((size_t)vertexCount, false);
    for (size_t g = 0; g < groups_.size(); ++g)
        last[g] = groups_[g].hasOutputNode;
    const auto levels = synth::ui::signalFlowLevels(vertexCount, {edgeSet.begin(), edgeSet.end()}, last);

    for (uint32_t uid : pendingNodes)
        if (const int g = groupOfNode(uid); g >= 0) {
            pendingNodes_.insert(uid);
            ++groups_[(size_t)g].pendingNodes;
        }
    std::vector<int> groupLevels(levels.begin(), levels.begin() + (long)groups_.size());
    std::vector<bool> pending;
    for (const auto& group : groups_)
        pending.push_back(group.pendingNodes > 0);
    timeline_.start(motion_, groupLevels, pending);
    if (hooks_.cables)
        for (const auto& cable : hooks_.cables())
            cableGroups_.emplace_back(groupOfNode(cable.id.srcUid), groupOfNode(cable.id.dstUid));
}
