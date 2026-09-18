// MacroGroupControllerGeometry.cpp
//
// Macro hull/chip/card geometry and hit-testing, the collapse button, port-dock layout
// (dockMacroPortWidgets()), and general-purpose node-uuid plumbing. MacroGroupController is
// declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class. GraphEditor::syncMacroCards() is NOT here — see
// MacroGroupController.h's class comment for why it stays on GraphEditor.

#include "MacroGroupController.h"

#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"

using namespace detail;

juce::String MacroGroupController::nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = host_.graph().getNodeForId(nodeId))
        return node->properties["uuid"].toString();
    return {};
}

juce::AudioProcessorGraph::NodeID MacroGroupController::resolveMemberNodeId(const juce::String& memberUuid) const {
    for (auto* node : host_.graph().getNodes()) {
        if (node->properties["uuid"].toString() == memberUuid)
            return node->nodeID;
    }
    return {};
}

MacroGroupController::MacroPortOwner
MacroGroupController::macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return {};
    const auto* macro = host_.getMacros().findByMember(uuid);
    if (macro == nullptr)
        return {};
    for (const auto& p : macro->ports)
        if (p.nodeUuid == uuid)
            return {macro, &p};
    return {macro, nullptr};
}

namespace {
// Margin added around the union of member bounds for the expanded-macro grouping hull — the ONE
// value paint (GraphContentComponent::paint) and hit-testing (macroHullAt) both use, via
// macroHullBounds below.
constexpr int kMacroHullMargin = 14;
// Depth reserved ABOVE the member row for the name chip, so the painted chip never overlaps a
// member's ModuleComponent (which would swallow the drag). Must stay >= the chip's own height.
constexpr int kMacroChipHeight = 18;
constexpr int kMacroChipTopMargin = kMacroChipHeight + 6;
} // namespace

namespace {
// Free-function twin of MacroGroupController::resolveMemberNodeId, for computeMacroHullBounds
// below (a free function itself, taking GraphCanvasHost& rather than a live `this`).
juce::AudioProcessorGraph::NodeID resolveMemberNodeIdIn(GraphCanvasHost& host, const juce::String& memberUuid) {
    for (auto* node : host.graph().getNodes())
        if (node->properties["uuid"].toString() == memberUuid)
            return node->nodeID;
    return {};
}

// Shared by macroHullBounds and macroHullBoundsExcluding (FRO40) — the latter is the former with
// one extra uuid left out of the union, needed because the plain hull is a LIVE union of member
// bounds: the member being dragged OUT of it keeps inflating its own hull, so it could never test
// as "outside" without excluding itself first (see macroDragJoinOrLeaveTarget's own comment).
juce::Rectangle<int> computeMacroHullBounds(GraphCanvasHost& host, const synth::Macro& macro,
                                            const juce::String& extraExcludedUuid) {
    // Port members are EXCLUDED from the union: they dock to this hull's own edge
    // (dockMacroPortWidgets, P8-15 fix F2), and if they also counted toward the bounds that
    // DEFINE the hull, docking one would grow the hull, which would push it out again, forever —
    // the exact feedback loop the fix's own review called out.
    std::set<juce::String> excludedUuids;
    for (const auto& p : macro.ports)
        excludedUuids.insert(p.nodeUuid);
    if (extraExcludedUuid.isNotEmpty())
        excludedUuids.insert(extraExcludedUuid);

    std::unordered_map<uint32_t, ModuleComponent*> compByNodeUid;
    for (auto* comp : host.modules())
        if (comp != nullptr)
            compByNodeUid[comp->getNodeId().uid] = comp;

    juce::Rectangle<int> hull;
    for (const auto& uuid : macro.members) {
        if (excludedUuids.count(uuid) > 0)
            continue; // a port's own fronting node, or the LEAVE test's own dragged member
        auto nodeId = resolveMemberNodeIdIn(host, uuid);
        auto it = compByNodeUid.find(nodeId.uid);
        if (it == compByNodeUid.end())
            continue;
        hull = hull.isEmpty() ? it->second->getBounds() : hull.getUnion(it->second->getBounds());
    }
    if (hull.isEmpty()) {
        // A macro made ENTIRELY of ports (no ordinary member) has nothing left to union. Fall
        // back to the macro's own persisted `bounds` — the same footprint its collapsed card uses
        // — so its ports still have an edge to dock against rather than piling up at the canvas
        // origin.
        if (macro.bounds.isEmpty())
            return {};
        hull = macro.bounds;
    }

    // The top margin is DEEPER than the other three: the name chip is drawn at the hull's
    // top-left and doubles as the macro's drag handle, but it is PAINTED, not a component, so it
    // has no z-order of its own — reserving kMacroChipTopMargin above the member row keeps it on
    // empty canvas where GraphEditor's own mouse handlers get it.
    auto expanded = hull.expanded(kMacroHullMargin);
    expanded.setTop(hull.getY() - kMacroChipTopMargin);
    return expanded;
}
} // namespace

juce::Rectangle<int> MacroGroupController::macroHullBounds(const juce::String& macroId) const {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || macro->collapsed)
        return {};
    return computeMacroHullBounds(host_, *macro, {});
}

juce::Rectangle<int> MacroGroupController::macroHullBoundsExcluding(const juce::String& macroId,
                                                                    const juce::String& excludedMemberUuid) const {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || macro->collapsed)
        return {};
    return computeMacroHullBounds(host_, *macro, excludedMemberUuid);
}

juce::String MacroGroupController::macroDragJoinOrLeaveTarget(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                                              juce::Point<int> canvasCentre) const {
    const juce::String uuid = nodeUuidFor(draggedNodeId);
    if (uuid.isEmpty())
        return {};

    if (const auto* currentMacro = host_.getMacros().findByMember(uuid)) {
        // LEAVE test: outside the hull it would have EXCLUDING its own contribution -> leaving.
        const auto hullExcludingSelf = macroHullBoundsExcluding(currentMacro->id, uuid);
        if (!hullExcludingSelf.isEmpty() && !hullExcludingSelf.contains(canvasCentre))
            return currentMacro->id;
        return {};
    }

    // JOIN test: not a member of anything, so any EXPANDED macro whose hull contains the centre
    // is a candidate — macroHullAt already skips collapsed macros and picks the smallest hull
    // when more than one overlaps.
    return macroHullAt(canvasCentre);
}

juce::String MacroGroupController::macroHullAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed)
            continue;
        const auto bounds = macroHullBounds(macro.id);
        if (bounds.isEmpty() || !bounds.contains(canvasPos))
            continue;
        // Smallest hull wins when hulls overlap — the more specific (smaller) macro is the one
        // the click most plausibly aimed at.
        const int area = bounds.getWidth() * bounds.getHeight();
        if (area < bestArea) {
            bestArea = area;
            best = macro.id;
        }
    }
    return best;
}

juce::Rectangle<int> MacroGroupController::macroChipBounds(const juce::String& macroId) const {
    const auto hull = macroHullBounds(macroId);
    if (hull.isEmpty())
        return {};

    // Measured with a LOCAL font rather than a juce::Graphics context, so this can be called from
    // hit-testing (mouseDown/mouseMove) as well as paint - GraphContentComponent::paint uses this
    // exact same font when it draws the chip, so the drawn rect and the hit rect never diverge.
    const auto* macro = host_.getMacros().find(macroId);
    const juce::String label = (macro != nullptr && macro->name.isNotEmpty()) ? macro->name : juce::String("Macro");
    juce::Font font(juce::FontOptions(11.0f, juce::Font::bold));
    // +28, not the original card's +16: paint reserves the left ~12px for the grip-line affordance
    // (see GraphContentComponent::paint), so the label needs the extra room to not look crowded.
    const int labelW = (int)font.getStringWidthFloat(label) + 28;
    return juce::Rectangle<int>(hull.getX() + 8, hull.getY(), labelW, kMacroChipHeight);
}

juce::String MacroGroupController::macroChipAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed)
            continue;
        const auto bounds = macroChipBounds(macro.id);
        if (bounds.isEmpty() || !bounds.contains(canvasPos))
            continue;
        const int area = bounds.getWidth() * bounds.getHeight();
        if (area < bestArea) {
            bestArea = area;
            best = macro.id;
        }
    }
    return best;
}

namespace {
// Size of the collapse button's square hit zone, and its margin from the hull's right edge —
// mirrors MacroCardComponent::getExpandButtonBounds' own fixed-size-plus-margin shape.
constexpr int kMacroCollapseButtonSize = 14;
constexpr int kMacroCollapseButtonMargin = 6;
} // namespace

juce::Rectangle<int> MacroGroupController::macroCollapseButtonBounds(const juce::String& macroId) const {
    const auto hull = macroHullBounds(macroId);
    if (hull.isEmpty())
        return {};

    // Vertically centred in the same chip row macroChipBounds occupies; horizontally at the
    // row's RIGHT end, mirroring the chip's own left-end placement.
    return juce::Rectangle<int>(hull.getRight() - kMacroCollapseButtonMargin - kMacroCollapseButtonSize,
                                hull.getY() + (kMacroChipHeight - kMacroCollapseButtonSize) / 2,
                                kMacroCollapseButtonSize, kMacroCollapseButtonSize);
}

juce::String MacroGroupController::macroCollapseButtonAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed)
            continue;
        const auto bounds = macroCollapseButtonBounds(macro.id);
        if (bounds.isEmpty() || !bounds.contains(canvasPos))
            continue;
        const int area = bounds.getWidth() * bounds.getHeight();
        if (area < bestArea) {
            bestArea = area;
            best = macro.id;
        }
    }
    return best;
}

juce::Rectangle<int> MacroGroupController::macroCableAnchorBounds(const synth::Macro& macro) const {
    for (auto* card : host_.macroCards())
        if (card != nullptr && card->getMacroId() == macro.id)
            return card->getBounds();
    return macro.bounds;
}

std::vector<MacroGroupController::MacroCardPort>
MacroGroupController::macroCardPortLayout(const juce::String& macroId) const {
    std::vector<MacroCardPort> result;
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || macro->ports.empty())
        return result;

    auto ports = macro->ports;
    std::sort(ports.begin(), ports.end(),
              [](const synth::MacroPort& a, const synth::MacroPort& b) { return a.order < b.order; });

    std::vector<const synth::MacroPort*> inputs, outputs;
    for (const auto& p : ports)
        (p.isInput ? inputs : outputs).push_back(&p);

    // Evenly spaced within the fixed jack band regardless of count, so N ports on one side never
    // outgrow the card's fixed footprint.
    auto placeSide = [&](const std::vector<const synth::MacroPort*>& side, int x) {
        const int n = (int)side.size();
        const int bandHeight = kMacroCardJackBandBottom - kMacroCardJackBandTop;
        for (int i = 0; i < n; ++i) {
            const int y = kMacroCardJackBandTop + (bandHeight * (i + 1)) / (n + 1);
            MacroCardPort port;
            port.nodeUuid = side[i]->nodeUuid;
            port.isInput = side[i]->isInput;
            port.kind = side[i]->kind;
            port.name = side[i]->name;
            port.jackPos = {x, y};
            port.colour = side[i]->colour; // T152; MacroCardComponent falls back to the kind tint
            result.push_back(port);
        }
    };
    placeSide(inputs, kMacroCardJackInsetX);
    placeSide(outputs, synth::LayoutUtil::kSingleWidth - kMacroCardJackInsetX);
    return result;
}

std::optional<MacroGroupController::MacroCardPort>
MacroGroupController::macroCardPortForPoint(const juce::String& macroId, juce::Point<int> cardLocalPos) const {
    for (const auto& port : macroCardPortLayout(macroId))
        if (cardLocalPos.toFloat().getDistanceFrom(port.jackPos.toFloat()) < kMacroCardJackHitRadius)
            return port;
    return std::nullopt;
}

MacroCardComponent* MacroGroupController::getMacroCardForTest(const juce::String& macroId) {
    for (auto* card : host_.macroCards())
        if (card != nullptr && card->getMacroId() == macroId)
            return card;
    return nullptr;
}

namespace {
// Docked macro-port widget layout (P8-15 founder-review fix F2, docs/macros_ports.md §5.4). Small
// and fixed regardless of anything else on the canvas — the widget's own getWidth()/getHeight()
// (set by ModuleComponent::layoutMacroPortWidget, called before this ever runs) decide how big;
// this only decides WHERE.
constexpr int kMacroPortDockGap = 6;     // clearance between a widget's inner edge and the hull
constexpr int kMacroPortDockMarginY = 8; // clearance below the hull's own top edge for port #0
constexpr int kMacroPortDockSpacing = 6; // vertical gap between two stacked ports on one side
} // namespace

void MacroGroupController::dockMacroPortWidgets() {
    std::unordered_map<uint32_t, ModuleComponent*> compByNodeUid;
    for (auto* comp : host_.modules())
        if (comp != nullptr)
            compByNodeUid[comp->getNodeId().uid] = comp;

    auto& graph = host_.graph();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed || macro.ports.empty())
            continue; // hidden with the rest of its members; the collapsed CARD draws its jacks

        const auto hull = macroHullBounds(macro.id);
        if (hull.isEmpty())
            continue;

        auto ports = macro.ports;
        std::sort(ports.begin(), ports.end(),
                  [](const synth::MacroPort& a, const synth::MacroPort& b) { return a.order < b.order; });

        int inputY = hull.getY() + kMacroPortDockMarginY;
        int outputY = hull.getY() + kMacroPortDockMarginY;
        for (const auto& port : ports) {
            auto nodeId = resolveMemberNodeId(port.nodeUuid);
            auto it = compByNodeUid.find(nodeId.uid);
            if (it == compByNodeUid.end())
                continue;
            auto* comp = it->second;

            const int x =
                port.isInput ? hull.getX() - kMacroPortDockGap - comp->getWidth() : hull.getRight() + kMacroPortDockGap;
            const int y = port.isInput ? inputY : outputY;
            (port.isInput ? inputY : outputY) += comp->getHeight() + kMacroPortDockSpacing;

            comp->setTopLeftPosition(x, y);
            if (auto* node = graph.getNodeForId(nodeId)) {
                node->properties.set("x", x);
                node->properties.set("y", y);
            }
        }
    }
}
