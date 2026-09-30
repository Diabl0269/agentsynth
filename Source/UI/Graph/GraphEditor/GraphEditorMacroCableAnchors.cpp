// GraphEditorMacroCableAnchors.cpp
//
// Where cables land around a collapsed macro: reanchorCablesAroundCollapsedMacros(), the post-pass
// rebuildVisibleCables() (GraphEditorCables.cpp) runs over the enumerated cables. Nested macros map
// every hidden node to its OUTERMOST collapsed ancestor, the only card on screen for it
// (docs/layout/cables.md#nested-macros). GraphEditor is declared in GraphEditor.h; sibling
// GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include <cmath>
#include <limits>
#include <unordered_map>

using namespace detail;

namespace {
// Where a boundary cable lands on a collapsed macro card: the point where the ray from the
// card's centre toward the cable's other endpoint exits the card's rectangle. Reads as the cable
// touching the card's edge (closest to whatever it's connecting to) rather than floating at a
// fixed point with no relation to the rest of the wire.
juce::Point<float> projectToRectEdge(juce::Rectangle<int> rect, juce::Point<float> toward) {
    const auto centre = rect.getCentre().toFloat();
    const float dx = toward.x - centre.x;
    const float dy = toward.y - centre.y;
    if (dx == 0.0f && dy == 0.0f)
        return centre;

    const float halfW = rect.getWidth() * 0.5f;
    const float halfH = rect.getHeight() * 0.5f;
    const float tx = dx != 0.0f ? halfW / std::abs(dx) : std::numeric_limits<float>::infinity();
    const float ty = dy != 0.0f ? halfH / std::abs(dy) : std::numeric_limits<float>::infinity();
    const float t = std::min(tx, ty);
    return centre + juce::Point<float>(dx * t, dy * t);
}

// Case (b)'s directional edge anchor (see reanchorCablesAroundCollapsedMacros): the facing
// projection still decides the Y (so several crossing cables keep distinct heights instead of
// stacking), clamped into the same vertical jack band a real port jack lays out in; X is forced to
// the card's actual left/right edge -- not the port jacks' inset -- so an edge anchor and a port dot
// on the same side never land on the same pixel.
juce::Point<float> directionalEdgeAnchor(juce::Rectangle<int> cardBounds, juce::Point<float> facing, bool onLeftEdge) {
    const float y = juce::jlimit((float)(cardBounds.getY() + kMacroPortRowsTop),
                                 (float)(cardBounds.getBottom() - kMacroPortStripFooter), facing.y);
    const float x = onLeftEdge ? (float)cardBounds.getX() : (float)cardBounds.getRight();
    return juce::Point<float>(x, y);
}
} // namespace

// Post-pass extracted from rebuildVisibleCables() purely to keep that function under
// its ratchet -- no behavior change. Runs AFTER reanchorCablesToKnobTargets so a cable that is
// BOTH knob-bound and crosses a collapsed macro's boundary ends up re-anchored to the macro card
// (this pass wins), matching the rule that a collapsed macro always owns its boundary
// cables' endpoints.
//
// ---- Collapsed-macro cable treatment --------------------------------------------------
//
// A collapsed macro hides its member ModuleComponents (setVisible(false) in syncMacroCards),
// but their graph edges — and the cables computed from them — don't know that. A cable
// wholly inside one collapsed macro is dropped outright (both endpoints are off-screen, and
// there is nothing useful to draw); a cable crossing a collapsed macro's boundary is
// re-anchored on the card -- two treatments (docs/macros/ports.md#cable-rendering-across-the-boundary):
//   - the hidden endpoint IS one of the macro's own ports (a MacroInlet/Outlet or MIDI
//     variant fronting a synth::MacroPort) -> anchor at that port's own jack
//     (macroCardPortLayout), so the cable visibly enters/leaves through the port it actually
//     passes through;
//   - the hidden endpoint is an ordinary interior member wired to past the boundary (no port
//     involved) -> DIRECTIONAL edge anchor: the card's RIGHT edge if
//     the macro is the cable's SOURCE (signal leaving it), LEFT edge if it's the DESTINATION
//     (signal entering it) -- never a facing-the-other-endpoint projection, which is what put
//     both legs of a pass-through wire on the card's TOP edge when the other endpoint happened
//     to sit above the card. The Y coordinate still comes from that facing projection (so
//     several crossing cables keep spreading vertically instead of collapsing onto one pixel)
//     but is clamped into the card's port-row span (first row top to the strip footer), and X lands exactly on the
//     boundary (not inset like a real port jack) so this anchor never sits under a case-(a) port dot on the same
//     edge; that case is expected (docs/macros/ports.md#cable-rendering-across-the-boundary), not an error.
// Nesting: "the macro" for a hidden node is its OUTERMOST collapsed ancestor — the collapsed macro
// with no collapsed ancestor of its own, whose card is the one drawn — so a cable between two nodes
// under that same card is dropped however deep each sits, and only that card's OWN ports get a jack
// anchor (a nested child's port hidden under it takes the edge anchor like any interior member).
// The rectangle/jack projected against is macroCableAnchorBounds(macro) — the LIVE
// MacroCardComponent's bounds while a card exists, not the persisted `macro.bounds`, which is
// only written back on drop (finalizeMacroCardDrag) and would leave a cable pointing at the
// card's pre-drag position for the whole gesture otherwise.
void GraphEditor::reanchorCablesAroundCollapsedMacros(std::vector<VisibleCable>& cables) {
    if (macros.empty())
        return;

    // nodeID.uid -> the outermost collapsed macro hiding it.
    std::unordered_map<uint32_t, const synth::Macro*> collapsedMacroForNode;
    // nodeID.uid -> that node's own jack position, CARD-LOCAL, only for nodes that are a port of
    // the card's own macro (macroCardPortLayout already excludes an interior member with no
    // MacroPort entry, so absence from this map IS the "ordinary member" case above).
    std::unordered_map<uint32_t, juce::Point<int>> portJackLocalForNode;
    for (const auto& macro : macros.getAll()) {
        if (!macro.collapsed || !macros.isVisible(macro.id))
            continue; // expanded, or hidden under a collapsed ancestor whose card stands in for it
        for (const auto& uuid : macros.descendantMembers(macro.id)) {
            auto nodeId = macroController_.resolveMemberNodeId(uuid);
            if (nodeId.uid != 0)
                collapsedMacroForNode[nodeId.uid] = &macro;
        }
        for (const auto& port : macroController_.macroCardPortLayout(macro.id)) {
            auto nodeId = macroController_.resolveMemberNodeId(port.nodeUuid);
            if (nodeId.uid != 0)
                portJackLocalForNode[nodeId.uid] = port.jackPos;
        }
    }
    if (collapsedMacroForNode.empty())
        return;

    std::vector<VisibleCable> filtered;
    filtered.reserve(cables.size());
    for (auto& cable : cables) {
        auto srcIt = collapsedMacroForNode.find(cable.id.srcUid);
        auto dstIt = collapsedMacroForNode.find(cable.id.dstUid);
        const bool srcHidden = srcIt != collapsedMacroForNode.end();
        const bool dstHidden = dstIt != collapsedMacroForNode.end();

        if (srcHidden && dstHidden && srcIt->second == dstIt->second)
            continue; // wholly inside one collapsed card — nothing on screen to draw

        const auto originalP1 = cable.p1;
        const auto originalP2 = cable.p2;
        if (srcHidden) {
            const auto cardBounds = macroController_.macroCableAnchorBounds(*srcIt->second);
            auto jackIt = portJackLocalForNode.find(cable.id.srcUid);
            // Macro is the SOURCE -> signal LEAVES it -> anchor on the RIGHT edge.
            cable.p1 = jackIt != portJackLocalForNode.end()
                           ? (cardBounds.getPosition() + jackIt->second).toFloat()
                           : directionalEdgeAnchor(cardBounds, projectToRectEdge(cardBounds, originalP2), false);
        }
        if (dstHidden) {
            const auto cardBounds = macroController_.macroCableAnchorBounds(*dstIt->second);
            auto jackIt = portJackLocalForNode.find(cable.id.dstUid);
            // Macro is the DESTINATION -> signal ENTERS it -> anchor on the LEFT edge.
            cable.p2 = jackIt != portJackLocalForNode.end()
                           ? (cardBounds.getPosition() + jackIt->second).toFloat()
                           : directionalEdgeAnchor(cardBounds, projectToRectEdge(cardBounds, originalP1), true);
        }

        filtered.push_back(cable);
    }
    cables = std::move(filtered);
}
