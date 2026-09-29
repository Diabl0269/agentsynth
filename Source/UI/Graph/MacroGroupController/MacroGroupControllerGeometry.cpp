// MacroGroupControllerGeometry.cpp
//
// Macro hull/chip/card geometry and hit-testing, the collapse button, port-dock layout
// (dockMacroPortWidgets()), and general-purpose node-uuid plumbing. MacroGroupController is
// declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class. GraphEditor::syncMacroCards() is NOT here — see
// MacroGroupController.h's class comment for why it stays on GraphEditor.

#include "MacroGroupController.h"

#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

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

// {input, output} sidebar strip widths for a macro: the longest port name on each side plus the
// fixed padding, `innerJackRoom` extra for the open macro's inner jack. A side with no ports gets
// the bare minimum. Measured with a local font (no juce::Graphics), so paint, hit-testing and
// anchoring can all call it. Never depends on zoom.
std::pair<int, int> macroStripWidths(const synth::Macro& macro, int innerJackRoom) {
    const int empty = kMacroPortStripInset + kMacroPortStripPadding;
    const juce::Font font{juce::FontOptions(kMacroPortNameFontSize)};
    float longestIn = -1.0f, longestOut = -1.0f;
    for (const auto& p : macro.ports) {
        float& longest = p.isInput ? longestIn : longestOut;
        longest = juce::jmax(longest, font.getStringWidthFloat(p.name));
    }
    return {longestIn < 0.0f ? empty : macroPortStripWidthFor(longestIn, innerJackRoom),
            longestOut < 0.0f ? empty : macroPortStripWidthFor(longestOut, innerJackRoom)};
}
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

// Shared by macroHullBounds and macroHullBoundsExcluding — the latter is the former with
// one extra uuid left out of the union, needed because the plain hull is a LIVE union of member
// bounds: the member being dragged OUT of it keeps inflating its own hull, so it could never test
// as "outside" without excluding itself first (see macroDragJoinOrLeaveTarget's own comment).
juce::Rectangle<int> computeMacroHullBounds(GraphCanvasHost& host, const synth::Macro& macro,
                                            const juce::String& extraExcludedUuid) {
    // Port members are EXCLUDED from the union: they dock to this hull's own edge
    // (dockMacroPortWidgets), and if they also counted toward the bounds that
    // DEFINE the hull, docking one would grow the hull, which would push it out again, forever.
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

    // The two port strips sit INSIDE the hull, outside the members' own margin, so the hull grows
    // outward by each strip's width and members never move when a port is added. When the rows
    // outrun the members the hull grows down to hold them and the '+'/'-' footer.
    const auto [inW, outW] = macroStripWidths(macro, kMacroHullStripInnerJackRoom);
    expanded.setLeft(expanded.getX() - inW);
    expanded.setRight(expanded.getRight() + outW);
    int inputRows = 0, outputRows = 0;
    for (const auto& p : macro.ports)
        (p.isInput ? inputRows : outputRows) += macroPortRowCountIn(host, p.nodeUuid);
    expanded.setBottom(juce::jmax(expanded.getBottom(), expanded.getY() + kMacroChipTopMargin + 6 +
                                                            juce::jmax(inputRows, outputRows) * kMacroPortRowHeight +
                                                            kMacroPortStripFooter));
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

// The ONE query behind the Cmd-drag-across-a-hull gesture (docs/macros/menu-and-membership.md).
//
// LEAVE test: the plain hull is a LIVE union of member bounds, so the member being dragged OUT
// keeps inflating its own macro's hull and would never test as outside it. The test is therefore
// against macroHullBoundsExcluding(current, uuid). An EMPTY excluding hull (the dragged node is
// the macro's only ordinary member, so there is nothing left to union) also means "staying": with
// no remaining body to leave, the gesture is a plain move.
//
// JOIN test: the same live-union trap bites from the other side. The dragged module is one of
// A's members, so A's live hull always contains the module's own centre, and macroHullAt (smallest
// hull under the centre) would answer A itself (or nothing smaller than it) every time. The JOIN
// scan therefore skips the macro being left (macroHullAtExcluding) and only considers macros the
// node is NOT a member of, where the live hull carries no contribution from it. That is what
// makes `leave` + `join` a transfer.
MacroGroupController::MacroDragTargets
MacroGroupController::macroDragJoinOrLeaveTarget(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                                 juce::Point<int> canvasCentre) const {
    const juce::String uuid = nodeUuidFor(draggedNodeId);
    if (uuid.isEmpty())
        return {};

    const auto* currentMacro = host_.getMacros().findByMember(uuid);
    if (currentMacro == nullptr)
        return {{}, macroHullAt(canvasCentre)};

    const auto hullExcludingSelf = macroHullBoundsExcluding(currentMacro->id, uuid);
    if (hullExcludingSelf.isEmpty() || hullExcludingSelf.contains(canvasCentre))
        return {};
    return {currentMacro->id, macroHullAtExcluding(canvasCentre, currentMacro->id)};
}

juce::String MacroGroupController::macroHullAt(juce::Point<int> canvasPos) const {
    return macroHullAtExcluding(canvasPos, {});
}

// macroHullAt's body, with one macro left out of the scan (empty id: none). Private: macroHullAt
// itself stays the plain hit-test click/right-click use; only the drag query needs the exclusion.
juce::String MacroGroupController::macroHullAtExcluding(juce::Point<int> canvasPos,
                                                        const juce::String& excludedMacroId) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed || (excludedMacroId.isNotEmpty() && macro.id == excludedMacroId))
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

    // One 16px row per port from kMacroPortRowsTop down; the card grows to fit (macroCardHeightFor),
    // so N ports on one side never outgrow it. The label area spans the strip past the jack.
    const auto widths = macroCardStripWidths(macroId);
    auto placeSide = [&](const std::vector<const synth::MacroPort*>& side, int x, bool isInputSide) {
        const int n = (int)side.size();
        for (int i = 0; i < n; ++i) {
            const int rowTop = kMacroPortRowsTop + i * kMacroPortRowHeight;
            MacroCardPort port;
            port.nodeUuid = side[i]->nodeUuid;
            port.isInput = side[i]->isInput;
            port.kind = side[i]->kind;
            port.name = side[i]->name;
            port.row = i;
            port.jackPos = {x, rowTop + kMacroPortRowHeight / 2};
            if (isInputSide)
                port.labelArea = {kMacroPortStripInset, rowTop, widths.first - kMacroPortStripInset,
                                  kMacroPortRowHeight};
            else
                port.labelArea = {synth::LayoutUtil::kSingleWidth - widths.second, rowTop,
                                  widths.second - kMacroPortStripInset, kMacroPortRowHeight};
            port.colour = side[i]->colour; // MacroCardComponent falls back to the kind tint
            result.push_back(port);
        }
    };
    placeSide(inputs, kMacroCardJackInsetX, true);
    placeSide(outputs, synth::LayoutUtil::kSingleWidth - kMacroCardJackInsetX, false);
    return result;
}

std::pair<int, int> MacroGroupController::macroCardStripWidths(const juce::String& macroId) const {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr) {
        const int empty = kMacroPortStripInset + kMacroPortStripPadding;
        return {empty, empty};
    }
    return macroStripWidths(*macro, 0);
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
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr) {
        const int empty = kMacroPortStripInset + kMacroPortStripPadding;
        return {empty, empty};
    }
    return macroStripWidths(*macro, kMacroHullStripInnerJackRoom);
}

juce::Rectangle<int> MacroGroupController::macroHullAddButtonBounds(const juce::String& macroId, bool isInput) const {
    const auto hull = macroHullBounds(macroId);
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
    const auto hull = macroHullBounds(macroId);
    if (macro == nullptr || hull.isEmpty() || !macroPortNamesVisibleAtZoom(zoom))
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
        if (macro.collapsed)
            continue;
        for (const bool isInput : {true, false}) {
            if (macroHullAddButtonBounds(macro.id, isInput).contains(canvasPos))
                return HullPortButtonHit{macro.id, isInput, true};
            if (macroHullRemoveButtonBounds(macro.id, isInput, zoom).contains(canvasPos))
                return HullPortButtonHit{macro.id, isInput, false};
        }
    }
    return std::nullopt;
}

std::vector<MacroGroupController::MacroHullPort>
MacroGroupController::macroHullPortLayout(const juce::String& macroId) const {
    std::vector<MacroHullPort> result;
    const auto* macro = host_.getMacros().find(macroId);
    const auto hull = macroHullBounds(macroId);
    if (macro == nullptr || hull.isEmpty() || macro->ports.empty())
        return result;

    auto ports = macro->ports;
    std::sort(ports.begin(), ports.end(),
              [](const synth::MacroPort& a, const synth::MacroPort& b) { return a.order < b.order; });

    // Same widths computeMacroHullBounds widened the hull by, so strips and hull always agree.
    const auto [inW, outW] = macroStripWidths(*macro, kMacroHullStripInnerJackRoom);
    const int firstRowTop = hull.getY() + kMacroChipTopMargin + 6;
    int nextRow[2] = {0, 0}; // [output, input]
    for (const auto& port : ports) {
        MacroHullPort entry;
        entry.nodeUuid = port.nodeUuid;
        entry.isInput = port.isInput;
        entry.rows = macroPortRowCountIn(host_, port.nodeUuid);
        int& row = nextRow[port.isInput ? 1 : 0];
        entry.row = row;
        row += entry.rows;

        const int top = firstRowTop + entry.row * kMacroPortRowHeight;
        const int height = entry.rows * kMacroPortRowHeight;
        const int jackY = top + kMacroPortRowHeight / 2;
        // The widget overhangs the hull border by 5px so its boundary jack (5px in from its own
        // edge) sits exactly on the border; the interior jack sits 5px inside the strip's inner edge.
        if (port.isInput) {
            entry.widgetBounds = {hull.getX() - 5, top, inW + 5, height};
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

void MacroGroupController::dockMacroPortWidgets() {
    std::unordered_map<uint32_t, ModuleComponent*> compByNodeUid;
    for (auto* comp : host_.modules())
        if (comp != nullptr)
            compByNodeUid[comp->getNodeId().uid] = comp;

    auto& graph = host_.graph();
    for (const auto& macro : host_.getMacros().getAll()) {
        if (macro.collapsed || macro.ports.empty())
            continue; // hidden with the rest of its members; the collapsed CARD draws its jacks

        for (const auto& entry : macroHullPortLayout(macro.id)) {
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
