// GraphEditorMacroGeometry.cpp
//
// Macro hull/chip/card geometry and hit-testing, the collapse button, port-dock layout
// constants, and syncMacroCards()/dockMacroPortWidgets(). GraphEditor is declared in
// GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "../MacroCardComponent.h"
#include "../ModuleComponent.h"

using namespace detail;

// ---- Macros (P8-12) ----------------------------------------------------------------------
// (kMacroCardHeight / the card-jack constants live in GraphEditorInternal.h now — used by
// several GraphEditor*.cpp units, see the comment there.)

juce::String GraphEditor::nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["uuid"].toString();
    return {};
}

juce::AudioProcessorGraph::NodeID GraphEditor::resolveMemberNodeId(const juce::String& memberUuid) const {
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node->properties["uuid"].toString() == memberUuid)
            return node->nodeID;
    }
    return {};
}

GraphEditor::MacroPortOwner GraphEditor::macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return {};
    const auto* macro = macros.findByMember(uuid);
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

juce::Rectangle<int> GraphEditor::macroHullBounds(const juce::String& macroId) const {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || macro->collapsed)
        return {};

    // Port members are EXCLUDED from the union: they dock to this hull's own edge
    // (dockMacroPortWidgets, P8-15 fix F2), and if they also counted toward the bounds that
    // DEFINE the hull, docking one would grow the hull, which would push it out again, forever —
    // the exact feedback loop the fix's own review called out. See this method's header doc.
    std::set<juce::String> portNodeUuids;
    for (const auto& p : macro->ports)
        portNodeUuids.insert(p.nodeUuid);

    std::unordered_map<uint32_t, ModuleComponent*> compByNodeUid;
    for (auto* comp : const_cast<GraphContentComponent&>(content).getModules())
        if (comp != nullptr)
            compByNodeUid[comp->getNodeId().uid] = comp;

    juce::Rectangle<int> hull;
    for (const auto& uuid : macro->members) {
        if (portNodeUuids.count(uuid) > 0)
            continue; // a port's own fronting node — presentation-docked OUTSIDE the hull
        auto nodeId = resolveMemberNodeId(uuid);
        auto it = compByNodeUid.find(nodeId.uid);
        if (it == compByNodeUid.end())
            continue;
        hull = hull.isEmpty() ? it->second->getBounds() : hull.getUnion(it->second->getBounds());
    }
    if (hull.isEmpty()) {
        // A macro made ENTIRELY of ports (no ordinary member) has nothing left to union. Fall
        // back to the macro's own persisted `bounds` — the same footprint its collapsed card uses
        // — so its ports still have an edge to dock against rather than piling up at the canvas
        // origin. Genuinely empty `bounds` (shouldn't happen: every macro is created with a real
        // groupBounds by groupSelectionIntoMacro) means there is truly nothing to draw, same as
        // before this fallback existed.
        if (macro->bounds.isEmpty())
            return {};
        hull = macro->bounds;
    }

    // The top margin is DEEPER than the other three, and that asymmetry is load-bearing: the name
    // chip is drawn at the hull's top-left and doubles as the macro's drag handle, but it is
    // PAINTED, not a component, so it has no z-order of its own. Wherever it overlapped a member's
    // ModuleComponent, that component won the click and dragged itself instead - the chip showed a
    // grab cursor and then did nothing, which is exactly the bug
    // MacroChipDrag.ChipRectNeverOverlapsAMemberModule pins. Reserving kMacroChipTopMargin above
    // the member row keeps the whole chip on empty canvas, where GraphEditor's own mouse handlers
    // get it, while still sitting INSIDE the hull so a macro near the top of the canvas cannot
    // clip its own label off-screen.
    auto expanded = hull.expanded(kMacroHullMargin);
    expanded.setTop(hull.getY() - kMacroChipTopMargin);
    return expanded;
}

juce::String GraphEditor::macroHullAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : macros.getAll()) {
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

juce::Rectangle<int> GraphEditor::macroChipBounds(const juce::String& macroId) const {
    const auto hull = macroHullBounds(macroId);
    if (hull.isEmpty())
        return {};

    // Measured with a LOCAL font rather than a juce::Graphics context, so this can be called from
    // hit-testing (mouseDown/mouseMove) as well as paint - GraphContentComponent::paint uses this
    // exact same font when it draws the chip, so the drawn rect and the hit rect never diverge.
    const auto* macro = macros.find(macroId);
    const juce::String label = (macro != nullptr && macro->name.isNotEmpty()) ? macro->name : juce::String("Macro");
    juce::Font font(juce::FontOptions(11.0f, juce::Font::bold));
    // +28, not the original card's +16: paint reserves the left ~12px for the grip-line affordance
    // (see GraphContentComponent::paint), so the label needs the extra room to not look crowded.
    // This is the one place that width is computed - paint reads this same rect.
    const int labelW = (int)font.getStringWidthFloat(label) + 28;
    return juce::Rectangle<int>(hull.getX() + 8, hull.getY(), labelW, kMacroChipHeight);
}

juce::String GraphEditor::macroChipAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : macros.getAll()) {
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
// mirrors MacroCardComponent::getExpandButtonBounds' own fixed-size-plus-margin shape. Smaller
// than the card's 20px chevron (kMacroChipHeight is only 18, the full row height available here),
// so it fits the chip row without growing kMacroChipTopMargin.
constexpr int kMacroCollapseButtonSize = 14;
constexpr int kMacroCollapseButtonMargin = 6;
} // namespace

juce::Rectangle<int> GraphEditor::macroCollapseButtonBounds(const juce::String& macroId) const {
    const auto hull = macroHullBounds(macroId);
    if (hull.isEmpty())
        return {};

    // Vertically centred in the same chip row macroChipBounds occupies (hull.getY() ..
    // hull.getY() + kMacroChipHeight); horizontally at the row's RIGHT end, mirroring the chip's
    // own left-end placement so the pair reads as one control spanning the row. The two can never
    // overlap for any real macro: the chip's width is a short label plus a fixed pad
    // (macroChipBounds), and macroHullBounds' own margin guarantees at least one member's width of
    // clearance between the hull's left and right edges.
    return juce::Rectangle<int>(hull.getRight() - kMacroCollapseButtonMargin - kMacroCollapseButtonSize,
                                hull.getY() + (kMacroChipHeight - kMacroCollapseButtonSize) / 2,
                                kMacroCollapseButtonSize, kMacroCollapseButtonSize);
}

juce::String GraphEditor::macroCollapseButtonAt(juce::Point<int> canvasPos) const {
    juce::String best;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& macro : macros.getAll()) {
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

juce::Rectangle<int> GraphEditor::macroCableAnchorBounds(const synth::Macro& macro) const {
    for (auto* card : const_cast<GraphContentComponent&>(content).getMacroCards())
        if (card != nullptr && card->getMacroId() == macro.id)
            return card->getBounds();
    return macro.bounds;
}

std::vector<GraphEditor::MacroCardPort> GraphEditor::macroCardPortLayout(const juce::String& macroId) const {
    std::vector<MacroCardPort> result;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || macro->ports.empty())
        return result;

    auto ports = macro->ports;
    std::sort(ports.begin(), ports.end(),
              [](const synth::MacroPort& a, const synth::MacroPort& b) { return a.order < b.order; });

    std::vector<const synth::MacroPort*> inputs, outputs;
    for (const auto& p : ports)
        (p.isInput ? inputs : outputs).push_back(&p);

    // Evenly spaced within the fixed jack band regardless of count, so N ports on one side never
    // outgrow the card's fixed footprint — the same "the card stays a fixed size" reasoning
    // kMacroCardHeight's own comment states for the collapsed card as a whole (§5.4).
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

std::optional<GraphEditor::MacroCardPort> GraphEditor::macroCardPortForPoint(const juce::String& macroId,
                                                                             juce::Point<int> cardLocalPos) const {
    for (const auto& port : macroCardPortLayout(macroId))
        if (cardLocalPos.toFloat().getDistanceFrom(port.jackPos.toFloat()) < kMacroCardJackHitRadius)
            return port;
    return std::nullopt;
}

MacroCardComponent* GraphEditor::getMacroCardForTest(const juce::String& macroId) {
    for (auto* card : content.getMacroCards())
        if (card != nullptr && card->getMacroId() == macroId)
            return card;
    return nullptr;
}

void GraphEditor::syncMacroCards() {
    auto& cards = content.getMacroCards();

    // 1. Remove cards for macros that no longer exist.
    for (int i = cards.size(); --i >= 0;) {
        auto* card = cards.getUnchecked(i);
        if (macros.find(card->getMacroId()) == nullptr) {
            content.removeChildComponent(card);
            cards.remove(i);
        }
    }

    // 2. Add cards for new macros; every card's bounds/visibility follow its macro's collapsed
    //    state (bounds are meaningless while expanded — see synth::Macro's comment).
    for (const auto& macro : macros.getAll()) {
        MacroCardComponent* card = nullptr;
        for (auto* c : cards) {
            if (c->getMacroId() == macro.id) {
                card = c;
                break;
            }
        }
        if (card == nullptr) {
            card = cards.add(new MacroCardComponent(*this, macro.id));
            content.addAndMakeVisible(card);
        }
        card->setBounds(macro.bounds);
        card->setVisible(macro.collapsed);
    }

    // 3. A member's own ModuleComponent is hidden exactly while its macro is collapsed — kept
    //    alive (not removed), so its position keeps tracking a card drag underneath.
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const juce::String uuid = nodeUuidFor(comp->getNodeId());
        const auto* macro = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
        comp->setVisible(macro == nullptr || !macro->collapsed);
    }
}

namespace {
// Docked macro-port widget layout (P8-15 founder-review fix F2, docs/macros.md §5.4). Small and
// fixed regardless of anything else on the canvas — the widget's own getWidth()/getHeight() (set
// by ModuleComponent::layoutMacroPortWidget, called from its own updateLayout() before this ever
// runs) decide how big; this only decides WHERE.
constexpr int kMacroPortDockGap = 6;     // clearance between a widget's inner edge and the hull
constexpr int kMacroPortDockMarginY = 8; // clearance below the hull's own top edge for port #0
constexpr int kMacroPortDockSpacing = 6; // vertical gap between two stacked ports on one side
} // namespace

void GraphEditor::dockMacroPortWidgets() {
    std::unordered_map<uint32_t, ModuleComponent*> compByNodeUid;
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            compByNodeUid[comp->getNodeId().uid] = comp;

    auto& graph = audioEngine.getGraph();
    for (const auto& macro : macros.getAll()) {
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
