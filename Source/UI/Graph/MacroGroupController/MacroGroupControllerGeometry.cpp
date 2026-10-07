// MacroGroupControllerGeometry.cpp
//
// Macro hull/chip/card geometry and hit-testing, the collapse button, port-dock layout
// (dockMacroPortWidgets()), and general-purpose node-uuid plumbing. MacroGroupController is
// declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class. GraphEditor::syncMacroCards() is NOT here — see
// MacroGroupController.h's class comment for why it stays on GraphEditor.

#include "MacroGroupController.h"
#include "MacroGroupControllerInternal.h"
#include "ModelCardBounds.h"

#include "AudioEngine/NodeUuidCache.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/HierarchicalArrange.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

using namespace detail;

namespace {
// Interned once: properties["uuid"] with a string literal builds a juce::Identifier per call, a locked
// StringPool search, and the member lookups below run it per node, per member, per hull, many times a paint.
const juce::Identifier& uuidKey() {
    static const juce::Identifier key("uuid");
    return key;
}

// The node carrying `memberUuid`. Every hull, port row and port dock resolves its members through here, many times per
// edit and per paint; a whole-graph scan per member made each pass O(macros x members x nodes). One cache serves every
// graph on the message thread (NodeUuidCache refills when the graph or a remembered node changes, and never answers a
// node that does not carry the uuid). An empty uuid keeps the plain scan's answer: the first node with no uuid.
juce::AudioProcessorGraph::NodeID memberNodeId(juce::AudioProcessorGraph& graph, const juce::String& memberUuid) {
    ++graph_editor_paint::workCounters().nodeScans;
    if (memberUuid.isEmpty()) {
        for (auto* node : graph.getNodes())
            if (node->properties[uuidKey()].toString().isEmpty())
                return node->nodeID;
        return {};
    }
    static synth::NodeUuidCache cache;
    auto* node = cache.find(graph, memberUuid);
    return node != nullptr ? node->nodeID : juce::AudioProcessorGraph::NodeID{};
}
} // namespace

juce::String MacroGroupController::nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = host_.graph().getNodeForId(nodeId))
        return node->properties[uuidKey()].toString();
    return {};
}

juce::AudioProcessorGraph::NodeID MacroGroupController::resolveMemberNodeId(const juce::String& memberUuid) const {
    return memberNodeId(host_.graph(), memberUuid);
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
static_assert(kMacroChipTopMargin == kMacroChipRowHeight,
              "the port rows start below the chip row (the strip fill itself spans from the hull top)");
static_assert(kMacroHullMargin == synth::LayoutUtil::kHullMargin &&
                  kMacroChipTopMargin == synth::LayoutUtil::kHullChipRow &&
                  kMacroPortRowsBelowChip == synth::LayoutUtil::kHullPortRowsBelowChip &&
                  kMacroPortRowHeight == synth::LayoutUtil::kHullPortRowHeight &&
                  kMacroPortStripFooter == synth::LayoutUtil::kHullPortFooter,
              "synth::LayoutUtil::openMacroHull (shared with auto-arrange) draws the hull these constants describe");
static_assert(kMacroHullMargin + kMacroHullStripWidth == synth::LayoutUtil::kMacroHullSideOutset,
              "LayoutUtil::kMacroHullSideOutset is the hull's side reach; keep it in sync");

// {input, output} sidebar strip widths for a macro: FIXED and equal on both sides, reserved even with no ports,
// and independent of the port names and of zoom (a long name is ellipsised in its column). The open macro's strip
// (`openHull`) adds room for its inner jack.
std::pair<int, int> macroStripWidths(bool openHull) {
    const int width = openHull ? kMacroHullStripWidth : kMacroCardStripWidth;
    return {width, width};
}
} // namespace

namespace {
// Free-function twin of MacroGroupController::resolveMemberNodeId, for computeMacroHullBounds
// below (a free function itself, taking GraphCanvasHost& rather than a live `this`).
juce::AudioProcessorGraph::NodeID resolveMemberNodeIdIn(GraphCanvasHost& host, const juce::String& memberUuid) {
    return memberNodeId(host.graph(), memberUuid);
}

// Rows a port's docked widget takes: one, except a Stereo port whose widget has two jack rows.
// Reads the port node's processor, exactly as ModuleComponent::layoutMacroPortWidget sizes the
// widget from it.
int macroPortRowCountIn(GraphCanvasHost& host, const juce::String& portUuid) {
    auto* node = host.graph().getNodeForId(resolveMemberNodeIdIn(host, portUuid));
    if (node == nullptr || node->getProcessor() == nullptr)
        return 1;
    auto* proc = node->getProcessor();
    if (proc->acceptsMidi() || proc->producesMidi())
        return 1;
    if (auto* mb = dynamic_cast<ModuleBase*>(proc))
        return juce::jmax(1, mb->getVisibleInputPortCount(), mb->getVisibleOutputPortCount());
    return 1;
}

using CompByNodeUid = macro_geometry::CardsByNodeUid;

juce::Rectangle<int> hullBoundsIn(GraphCanvasHost& host, const CompByNodeUid& compByNodeUid, const synth::Macro& macro,
                                  const juce::String& extraExcludedUuid);

// A nested child's footprint inside its parent's hull: its own full hull while expanded, its live
// card while collapsed (the card a drag is moving, falling back to the persisted bounds like
// macroCableAnchorBounds does).
juce::Rectangle<int> childFootprintIn(GraphCanvasHost& host, const CompByNodeUid& compByNodeUid,
                                      const synth::Macro& child, const juce::String& extraExcludedUuid) {
    if (!child.collapsed)
        return hullBoundsIn(host, compByNodeUid, child, extraExcludedUuid);
    for (auto* card : host.macroCards())
        if (card != nullptr && card->getMacroId() == child.id)
            return card->getBounds();
    return child.bounds;
}

// The union the hull is built around: the macro's direct non-port members plus every child's
// footprint (childFootprintIn). Port members are EXCLUDED: they dock to this hull's own edge
// (dockMacroPortWidgets), and if they also counted toward the bounds that DEFINE the hull, docking
// one would grow the hull, which would push it out again, forever. A child's ports are its own
// members, not this macro's, so they only count through the child's footprint.
juce::Rectangle<int> memberUnionIn(GraphCanvasHost& host, const CompByNodeUid& compByNodeUid, const synth::Macro& macro,
                                   const juce::String& extraExcludedUuid) {
    juce::Rectangle<int> hull;
    auto add = [&hull](juce::Rectangle<int> r) {
        if (!r.isEmpty())
            hull = hull.isEmpty() ? r : hull.getUnion(r);
    };
    for (const auto& uuid : macro.members) {
        if (macro.memberIsPort(uuid) || uuid == extraExcludedUuid)
            continue; // a port's own fronting node, or the LEAVE test's own dragged member
        const auto nodeId = resolveMemberNodeIdIn(host, uuid);
        if (auto it = compByNodeUid.find(nodeId.uid); it != compByNodeUid.end())
            add(it->second->getBounds());
        else if (auto* node = host.graph().getNodeForId(nodeId))
            // No card yet (detached for an AI edit plan): the member's model rect.
            add(synth::modelCardBounds(*node,
                                       [&host](const juce::String& t) { return host.estimateModuleSizeForType(t); }));
    }
    const auto& macros = host.getMacros();
    for (const auto& childId : macros.childrenOf(macro.id))
        if (const auto* child = macros.find(childId))
            add(childFootprintIn(host, compByNodeUid, *child, extraExcludedUuid));
    return hull;
}

// Shared by macroHullBounds and macroHullBoundsExcluding — the latter is the former with
// one extra uuid left out of the union, needed because the plain hull is a LIVE union of member
// bounds: the member being dragged OUT of it keeps inflating its own hull, so it could never test
// as "outside" without excluding itself first (see macroDragJoinOrLeaveTarget's own comment). The
// exclusion is passed down into nested children, so a member of a child is excluded too.
juce::Rectangle<int> computeMacroHullBounds(GraphCanvasHost& host, const synth::Macro& macro,
                                            const juce::String& extraExcludedUuid) {
    return hullBoundsIn(host, macro_geometry::cardsByNodeUid(host), macro, extraExcludedUuid);
}

juce::Rectangle<int> hullBoundsIn(GraphCanvasHost& host, const CompByNodeUid& compByNodeUid, const synth::Macro& macro,
                                  const juce::String& extraExcludedUuid) {
    auto hull = memberUnionIn(host, compByNodeUid, macro, extraExcludedUuid);
    if (hull.isEmpty()) {
        // A macro made ENTIRELY of ports (no ordinary member) has nothing left to union. Fall
        // back to the macro's own persisted `bounds` — the same footprint its collapsed card uses
        // — so its ports still have an edge to dock against rather than piling up at the canvas
        // origin.
        if (macro.bounds.isEmpty())
            return {};
        hull = macro.bounds;
    }

    // The hull around the members: deeper top for the chip row (PAINTED, not a component, so reserving the depth
    // keeps it on empty canvas where GraphEditor's own mouse handlers get it), a fixed port strip on each side
    // INSIDE the hull so members never move when a port is added, and a bottom that grows to hold the port rows
    // and the '+'/'-' footer. The one definition lives in synth::LayoutUtil::openMacroHull, shared with auto-arrange.
    int inputRows = 0, outputRows = 0;
    for (const auto& p : macro.ports)
        (p.isInput ? inputRows : outputRows) += macroPortRowCountIn(host, p.nodeUuid);
    return synth::LayoutUtil::openMacroHull(hull, juce::jmax(inputRows, outputRows));
}
} // namespace

namespace macro_geometry {

CardsByNodeUid cardsByNodeUid(GraphCanvasHost& host) {
    CardsByNodeUid cards;
    for (auto* comp : host.modules())
        if (comp != nullptr)
            cards[comp->getNodeId().uid] = comp;
    return cards;
}

// Effective collapse: a macro inside a collapsed ancestor has no hull either, whatever its own flag says.
juce::Rectangle<int> openHullBounds(GraphCanvasHost& host, const CardsByNodeUid& cards, const juce::String& macroId) {
    const auto* macro = host.getMacros().find(macroId);
    if (macro == nullptr || host.getMacros().isEffectivelyCollapsed(macroId))
        return {};
    return hullBoundsIn(host, cards, *macro, {});
}

} // namespace macro_geometry

juce::Rectangle<int> MacroGroupController::macroHullBounds(const juce::String& macroId) const {
    return macro_geometry::openHullBounds(host_, macro_geometry::cardsByNodeUid(host_), macroId);
}

juce::Rectangle<int> MacroGroupController::macroHullBoundsExcluding(const juce::String& macroId,
                                                                    const juce::String& excludedMemberUuid) const {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || host_.getMacros().isEffectivelyCollapsed(macroId))
        return {};
    return computeMacroHullBounds(host_, *macro, excludedMemberUuid);
}

// The border a reparent drag treats as "the macro" while it runs: the live hull of the dragged
// node's own macro and of every ancestor, snapshotted INCLUDING the dragged card and held until
// clearFrozenDragHulls. The live union would collapse the moment an edge member started moving (it
// defines that edge), so the member would count as outside on the first tick; the snapshot keeps
// the border where it was at press, and the member only leaves once its centre leaves it.
void MacroGroupController::freezeHullsForDrag(juce::AudioProcessorGraph::NodeID draggedNodeId) {
    frozenDragHulls_.clear();
    frozenDragNode_ = draggedNodeId;
    const auto& macros = host_.getMacros();
    const auto* own = macros.findByMember(nodeUuidFor(draggedNodeId));
    if (own == nullptr)
        return;
    auto chain = macros.ancestorChain(own->id);
    chain.push_back(own->id);
    for (const auto& id : chain)
        if (const auto hull = macroHullBounds(id); !hull.isEmpty())
            frozenDragHulls_[id] = hull;
}

void MacroGroupController::clearFrozenDragHulls() {
    frozenDragHulls_.clear();
    frozenDragNode_ = {};
}

bool MacroGroupController::hasFrozenDragHulls() const { return !frozenDragHulls_.empty(); }

juce::Rectangle<int> MacroGroupController::frozenDragHull(const juce::String& macroId) const {
    const auto it = frozenDragHulls_.find(macroId);
    return it != frozenDragHulls_.end() ? it->second : juce::Rectangle<int>();
}

// The rect chip / collapse / '+' '-' buttons and hull hit tests read: the live hull, unless the
// editor installed a provider (GraphEditor::paintedMacroHullBounds) that holds the border still
// during a reparent drag. Port widgets are NOT re-docked per tick, so while the border is
// shrinking away from a member being pulled out the widgets stay on the frozen border.
juce::Rectangle<int> MacroGroupController::paintedHullBounds(const juce::String& macroId) const {
    return paintedHullProvider_ ? paintedHullProvider_(macroId) : macroHullBounds(macroId);
}

namespace {
// The shared body of macroHullAt / macroChipAt / macroCollapseButtonAt: of the macros whose
// `boundsOf` rect contains the point, the DEEPEST wins (a nested child's rect always lies inside its
// parent's, and the innermost is what the press aimed at), then the smallest area — the more specific
// macro — when two unrelated macros at the same depth overlap. `boundsOf` returns an empty rect for a
// macro that is not hittable (collapsed, or hidden by a collapsed ancestor).
template <typename BoundsOf>
juce::String deepestMacroAt(const synth::MacroSet& macros, juce::Point<int> canvasPos, BoundsOf&& boundsOf) {
    juce::String best;
    int bestDepth = -1;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : macros.getAll()) {
        const auto bounds = boundsOf(macro);
        if (bounds.isEmpty() || !bounds.contains(canvasPos))
            continue;
        const int depth = macros.depth(macro.id);
        const int area = bounds.getWidth() * bounds.getHeight();
        if (depth > bestDepth || (depth == bestDepth && area < bestArea)) {
            bestDepth = depth;
            bestArea = area;
            best = macro.id;
        }
    }
    return best;
}
} // namespace

// The ONE query behind the Cmd-drag-across-a-hull gesture (docs/macros/menu-and-membership.md).
//
// LEAVE test: the plain hull is a LIVE union of member bounds, so the member being dragged OUT
// keeps inflating its own macro's hull and would never test as outside it. The test is therefore
// against the border frozen at press (freezeHullsForDrag) while a drag holds one, else against
// macroHullBoundsExcluding(current, uuid). An EMPTY excluding hull (the dragged node is
// the macro's only ordinary member, so there is nothing left to union) also means "staying": with
// no remaining body to leave, the gesture is a plain move.
//
// JOIN test: the same live-union trap bites from the other side. The dragged module is one of
// A's members, so A's live hull always contains the module's own centre; the JOIN scan therefore
// skips the macro being left and only considers macros where the live hull carries no
// contribution from it. That is what makes `leave` + `join` a transfer.
//
// NESTED macros: the trap repeats at every level, since a parent's live hull unions its child's, so
// each ANCESTOR of the macro being left is tested through macroHullBoundsExcluding too. The join
// candidate is the deepest expanded hull under the centre. Pulling C's member out into parent P's
// space yields {leave C, join P}; out of P as well, {leave C, join none}: the finalize walks the
// member out through every level in that one gesture. Still inside C, only a hull nested BELOW C
// counts (a transfer down into a child of C).
MacroGroupController::MacroDragTargets
MacroGroupController::macroDragJoinOrLeaveTarget(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                                 juce::Point<int> canvasCentre) const {
    const juce::String uuid = nodeUuidFor(draggedNodeId);
    if (uuid.isEmpty())
        return {};

    const auto& macros = host_.getMacros();
    const auto* currentMacro = macros.findByMember(uuid);
    if (currentMacro == nullptr)
        return {{}, macroHullAt(canvasCentre)};

    const auto hullExcludingSelf = macroHullBoundsExcluding(currentMacro->id, uuid);
    if (hullExcludingSelf.isEmpty())
        return {};
    // Against the border frozen at press when this node's drag has one (see freezeHullsForDrag);
    // otherwise the excluding hull, e.g. a query with no drag behind it.
    const bool haveFrozen = frozenDragNode_ == draggedNodeId && !frozenDragHulls_.empty();
    const auto borderOf = [&](const juce::String& id) {
        if (haveFrozen)
            if (const auto frozen = frozenDragHull(id); !frozen.isEmpty())
                return frozen;
        return macroHullBoundsExcluding(id, uuid);
    };
    const bool stillInside = borderOf(currentMacro->id).contains(canvasCentre);

    const auto ancestors = macros.ancestorChain(currentMacro->id);
    const auto isAncestor = [&ancestors](const juce::String& id) {
        return std::find(ancestors.begin(), ancestors.end(), id) != ancestors.end();
    };
    const juce::String join = deepestMacroAt(macros, canvasCentre, [&](const synth::Macro& macro) {
        if (macro.id == currentMacro->id)
            return juce::Rectangle<int>();
        if (stillInside) {
            const auto chain = macros.ancestorChain(macro.id);
            if (std::find(chain.begin(), chain.end(), currentMacro->id) == chain.end())
                return juce::Rectangle<int>(); // only macros nested below the current one
        }
        return isAncestor(macro.id) ? borderOf(macro.id) : macroHullBounds(macro.id);
    });
    if (stillInside && join.isEmpty())
        return {};
    return {currentMacro->id, join};
}

juce::String MacroGroupController::macroHullAt(juce::Point<int> canvasPos) const {
    return macroHullAtExcluding(canvasPos, {});
}

// macroHullAt's body, with one macro left out of the scan (empty id: none). Private: macroHullAt
// itself stays the plain hit-test click/right-click use; only the drag query needs the exclusion.
juce::String MacroGroupController::macroHullAtExcluding(juce::Point<int> canvasPos,
                                                        const juce::String& excludedMacroId) const {
    return deepestMacroAt(host_.getMacros(), canvasPos, [&](const synth::Macro& macro) {
        if (excludedMacroId.isNotEmpty() && macro.id == excludedMacroId)
            return juce::Rectangle<int>();
        return paintedHullBounds(macro.id);
    });
}

juce::Rectangle<int> MacroGroupController::macroChipBounds(const juce::String& macroId) const {
    const auto hull = paintedHullBounds(macroId);
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
    return deepestMacroAt(host_.getMacros(), canvasPos,
                          [this](const synth::Macro& macro) { return macroChipBounds(macro.id); });
}

namespace {
// Size of the collapse button's square hit zone, and its margin from the hull's right edge —
// mirrors MacroCardComponent::getExpandButtonBounds' own fixed-size-plus-margin shape.
constexpr int kMacroCollapseButtonSize = 14;
constexpr int kMacroCollapseButtonMargin = 6;
} // namespace

juce::Rectangle<int> MacroGroupController::macroCollapseButtonBounds(const juce::String& macroId) const {
    const auto hull = paintedHullBounds(macroId);
    if (hull.isEmpty())
        return {};

    // Vertically centred in the same chip row macroChipBounds occupies; horizontally at the
    // row's RIGHT end, mirroring the chip's own left-end placement.
    return juce::Rectangle<int>(hull.getRight() - kMacroCollapseButtonMargin - kMacroCollapseButtonSize,
                                hull.getY() + (kMacroChipHeight - kMacroCollapseButtonSize) / 2,
                                kMacroCollapseButtonSize, kMacroCollapseButtonSize);
}

juce::String MacroGroupController::macroCollapseButtonAt(juce::Point<int> canvasPos) const {
    return deepestMacroAt(host_.getMacros(), canvasPos,
                          [this](const synth::Macro& macro) { return macroCollapseButtonBounds(macro.id); });
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

    // One 16px row per port from kMacroPortRowsTop down; the card grows to fit (macroCardHeightFor),
    // so N ports on one side never outgrow it. The label area spans the strip past the jack.
    const auto widths = macroCardStripWidths(macroId);
    auto placeSide = [&](const std::vector<const synth::MacroPort*>& side, int x, bool isInputSide) {
        int row = 0;
        auto emit = [&](const synth::MacroPort& p, const juce::String& name, int visibleJack) {
            const int rowTop = kMacroPortRowsTop + row * kMacroPortRowHeight;
            MacroCardPort port;
            port.nodeUuid = p.nodeUuid;
            port.isInput = p.isInput;
            port.kind = p.kind;
            port.name = name;
            port.row = row++;
            port.visibleJack = visibleJack;
            port.jackPos = {x, rowTop + kMacroPortRowHeight / 2};
            if (isInputSide)
                port.labelArea = {kMacroPortStripInset, rowTop, widths.first - kMacroPortStripInset,
                                  kMacroPortRowHeight};
            else
                port.labelArea = {synth::LayoutUtil::kSingleWidth - widths.second, rowTop,
                                  widths.second - kMacroPortStripInset, kMacroPortRowHeight};
            port.colour = p.colour; // MacroCardComponent falls back to the kind tint
            result.push_back(port);
        };
        for (const auto* p : side) {
            // A two-jack Stereo port (Left/Right jacks) gets one row per jack; every other shape is one row.
            const auto shape = p->kind == synth::MacroPortKind::AudioCV ? stereoPortShape(p->nodeUuid) : std::nullopt;
            if (shape == MacroPortShape::Stereo) {
                emit(*p, p->name + " L", 0);
                emit(*p, p->name + " R", 1);
            } else {
                emit(*p, p->name, -1);
            }
        }
    };
    placeSide(inputs, kMacroCardJackInsetX, true);
    placeSide(outputs, synth::LayoutUtil::kSingleWidth - kMacroCardJackInsetX, false);
    return result;
}

std::pair<int, int> MacroGroupController::macroCardStripWidths(const juce::String& macroId) const {
    (void)macroId;
    return macroStripWidths(false);
}

std::optional<MacroGroupController::MacroCardPort>
MacroGroupController::macroCardPortForPoint(const juce::String& macroId, juce::Point<int> cardLocalPos) const {
    // Rows are 16px apart but the hit radius is 10, so two jacks' zones overlap: the nearest wins.
    std::optional<MacroCardPort> best;
    float bestDistance = kMacroCardJackHitRadius;
    for (const auto& port : macroCardPortLayout(macroId)) {
        const float d = cardLocalPos.toFloat().getDistanceFrom(port.jackPos.toFloat());
        if (d < bestDistance) {
            bestDistance = d;
            best = port;
        }
    }
    return best;
}

MacroCardComponent* MacroGroupController::getMacroCard(const juce::String& macroId) {
    return getMacroCardForTest(macroId);
}

MacroCardComponent* MacroGroupController::getMacroCardForTest(const juce::String& macroId) {
    for (auto* card : host_.macroCards())
        if (card != nullptr && card->getMacroId() == macroId)
            return card;
    return nullptr;
}

std::pair<int, int> MacroGroupController::macroHullStripWidths(const juce::String& macroId) const {
    (void)macroId;
    return macroStripWidths(true);
}

juce::Rectangle<int> MacroGroupController::macroHullAddButtonBounds(const juce::String& macroId, bool isInput) const {
    const auto hull = paintedHullBounds(macroId);
    if (hull.isEmpty())
        return {};
    const int size = (int)kMacroPortFooterButtonSize;
    const int x =
        isInput ? hull.getX() + (int)kMacroPortAddButtonInset : hull.getRight() - (int)kMacroPortAddButtonInset - size;
    return {x, hull.getBottom() - (int)kMacroPortFooterButtonFromBottom, size, size};
}

juce::Rectangle<int> MacroGroupController::macroHullRemoveButtonBounds(const juce::String& macroId, bool isInput,
                                                                       float zoom) const {
    const auto* macro = host_.getMacros().find(macroId);
    const auto hull = paintedHullBounds(macroId);
    if (macro == nullptr || hull.isEmpty() || !macroPortRemoveClickableAtZoom(zoom))
        return {};
    const bool hasPort = std::any_of(macro->ports.begin(), macro->ports.end(),
                                     [isInput](const synth::MacroPort& p) { return p.isInput == isInput; });
    if (!hasPort)
        return {};
    const int size = (int)kMacroPortFooterButtonSize;
    const int x = isInput ? hull.getX() + (int)kMacroPortRemoveButtonInset
                          : hull.getRight() - (int)kMacroPortRemoveButtonInset - size;
    return {x, hull.getBottom() - (int)kMacroPortFooterButtonFromBottom, size, size};
}

std::optional<MacroGroupController::HullPortButtonHit>
MacroGroupController::macroHullPortButtonAt(juce::Point<int> canvasPos, float zoom) const {
    for (const auto& macro : host_.getMacros().getAll()) {
        if (host_.getMacros().isEffectivelyCollapsed(macro.id))
            continue;
        for (const bool isInput : {true, false}) {
            if (macroPortRemoveClickableAtZoom(zoom) && macroHullAddButtonBounds(macro.id, isInput).contains(canvasPos))
                return HullPortButtonHit{macro.id, isInput, true};
            if (macroHullRemoveButtonBounds(macro.id, isInput, zoom).contains(canvasPos))
                return HullPortButtonHit{macro.id, isInput, false};
        }
    }
    return std::nullopt;
}

// The layout against a card map the caller already holds: dockMacroPortWidgets docks every macro's ports in one pass
// and builds the map once for all of them.
namespace {
std::vector<MacroGroupController::MacroHullPort>
hullPortLayoutIn(GraphCanvasHost& host, const macro_geometry::CardsByNodeUid& cards, const juce::String& macroId) {
    using MacroHullPort = MacroGroupController::MacroHullPort;
    std::vector<MacroHullPort> result;
    const auto* macro = host.getMacros().find(macroId);
    const auto hull = macro_geometry::openHullBounds(host, cards, macroId);
    if (macro == nullptr || hull.isEmpty() || macro->ports.empty())
        return result;

    auto ports = macro->ports;
    std::sort(ports.begin(), ports.end(),
              [](const synth::MacroPort& a, const synth::MacroPort& b) { return a.order < b.order; });

    // Same widths computeMacroHullBounds widened the hull by, so strips and hull always agree.
    const auto [inW, outW] = macroStripWidths(true);
    const int firstRowTop = hull.getY() + kMacroChipTopMargin + kMacroPortRowsBelowChip;
    int nextRow[2] = {0, 0}; // [output, input]
    for (const auto& port : ports) {
        MacroHullPort entry;
        entry.nodeUuid = port.nodeUuid;
        entry.isInput = port.isInput;
        entry.rows = macroPortRowCountIn(host, port.nodeUuid);
        int& row = nextRow[port.isInput ? 1 : 0];
        entry.row = row;
        row += entry.rows;

        const int top = firstRowTop + entry.row * kMacroPortRowHeight;
        const int height = entry.rows * kMacroPortRowHeight;
        const int jackY = top + kMacroPortRowHeight / 2;
        // The widget overhangs the hull border by kMacroPortOverhang so its boundary jack (5px in from its own
        // edge) sits exactly on the border; the interior jack sits 5px inside the strip's inner edge.
        constexpr int kOverhang = synth::LayoutUtil::kMacroPortOverhang;
        if (port.isInput) {
            entry.widgetBounds = {hull.getX() - kOverhang, top, inW + kOverhang, height};
            entry.outerJack = {hull.getX(), jackY};
            entry.innerJack = {hull.getX() + inW - 5, jackY};
        } else {
            entry.widgetBounds = {hull.getRight() - outW, top, outW + 5, height};
            entry.outerJack = {hull.getRight(), jackY};
            entry.innerJack = {hull.getRight() - outW + 5, jackY};
        }
        result.push_back(entry);
    }
    return result;
}
} // namespace

std::vector<MacroGroupController::MacroHullPort>
MacroGroupController::macroHullPortLayout(const juce::String& macroId) const {
    return hullPortLayoutIn(host_, macro_geometry::cardsByNodeUid(host_), macroId);
}

void MacroGroupController::dockMacroPortWidgets() {
    const auto compByNodeUid = macro_geometry::cardsByNodeUid(host_);

    auto& graph = host_.graph();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.ports.empty() || host_.getMacros().isEffectivelyCollapsed(macro.id))
            continue; // hidden with the rest of its members; the collapsed CARD draws its jacks

        for (const auto& entry : hullPortLayoutIn(host_, compByNodeUid, macro.id)) {
            auto nodeId = resolveMemberNodeId(entry.nodeUuid);
            auto it = compByNodeUid.find(nodeId.uid);
            if (it == compByNodeUid.end())
                continue;

            it->second->setBounds(entry.widgetBounds);
            if (auto* node = graph.getNodeForId(nodeId)) {
                node->properties.set("x", entry.widgetBounds.getX());
                node->properties.set("y", entry.widgetBounds.getY());
            }
        }
    }
}
