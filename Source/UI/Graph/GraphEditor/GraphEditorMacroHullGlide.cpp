// GraphEditorMacroHullGlide.cpp -- the glide of expanded macro borders (MacroHullGlide.h): what is drawn
// for a border while it glides, the snapshot a change site takes before it moves borders, and the driver.
// GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest.

#include "GraphEditor.h"
#include "GraphEditorPaintMemo.h"
#include "MacroOwnerIndex.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace graph_editor_paint {
namespace {
HullMemoScope* activeMemo = nullptr; // innermost open scope; message thread only, like paint itself
CardMapScope* activeCardMap = nullptr;
MacroOwnerScope* activeOwners = nullptr;
} // namespace

// A pass that measures every border (the snapshot before a restore and the glide armed after it) used to build a
// map of every card per border, so it cost macros x cards; it opens one of these and every border reads one map.
CardMapScope::CardMapScope(GraphCanvasHost& host)
    : host_(host)
    , previous_(activeCardMap) {
    for (auto* comp : host.modules())
        if (comp != nullptr)
            cards_[comp->getNodeId().uid] = comp;
    activeCardMap = this;
}

CardMapScope::~CardMapScope() { activeCardMap = previous_; }

const std::unordered_map<uint32_t, ModuleComponent*>* CardMapScope::cardsFor(const GraphCanvasHost& host) {
    return activeCardMap != nullptr && &activeCardMap->host_ == &host ? &activeCardMap->cards_ : nullptr;
}

// The cable rebuild places every cable end on its card's jack, and a macro port card's jack asks which macro owns the
// port: MacroSet::findByMember searches every macro's members, so asking it per cable end made the rebuild cost
// cables x macro members. The rebuild opens one of these and every end reads one map.
MacroOwnerScope::MacroOwnerScope(GraphCanvasHost& host)
    : host_(host)
    , previous_(activeOwners)
    , owners_(std::make_unique<synth::MacroOwnerIndex>(host.getMacros())) {
    activeOwners = this;
}

MacroOwnerScope::~MacroOwnerScope() { activeOwners = previous_; }

const synth::MacroOwnerIndex* MacroOwnerScope::ownersFor(const GraphCanvasHost& host) {
    return activeOwners != nullptr && &activeOwners->host_ == &host ? activeOwners->owners_.get() : nullptr;
}

// A paint pass asks for the same border many times (outline, chip, collapse button, '+'/'-', each port strip
// row), and each ask unions the members' live bounds. Nothing moves a card during one paint(), so the first
// answer holds for the rest of the pass; the scope is opened in GraphContentComponent::paint and nowhere else.
HullMemoScope::HullMemoScope(const GraphEditor& editor)
    : editor_(editor)
    , previous_(activeMemo) {
    activeMemo = this;
}

HullMemoScope::~HullMemoScope() { activeMemo = previous_; }

WorkCounters& workCounters() noexcept {
    static WorkCounters counters;
    return counters;
}
} // namespace graph_editor_paint

// Everything that draws or docks against a border (the dashed outline, the port strips, the chip and buttons,
// the docked port widgets) reads this, so they all glide together. Inside a paint pass it is memoized per
// macro (HullMemoScope above): computing it afresh per ask made a many-macro canvas spend most of each frame
// re-unioning the same borders.
juce::Rectangle<int> GraphEditor::paintedMacroHullBounds(const juce::String& macroId) const {
    auto* memo = graph_editor_paint::activeMemo;
    if (memo != nullptr && &memo->editor_ == this)
        if (const auto it = memo->hulls_.find(macroId); it != memo->hulls_.end())
            return it->second;
    const auto hull = hullGlide_.apply(macroId, macroHullTargetBounds(macroId));
    if (memo != nullptr && &memo->editor_ == this)
        memo->hulls_.emplace(macroId, hull);
    return hull;
}

// Collapsed macros draw a card, not a border, so they never glide.
MacroHullGlide::Hulls GraphEditor::snapshotPaintedHulls() const {
    const graph_editor_paint::CardMapScope oneCardMap(const_cast<GraphEditor&>(*this));
    MacroHullGlide::Hulls hulls;
    for (const auto& macro : macros.getAll())
        if (!macros.isEffectivelyCollapsed(macro.id))
            hulls[macro.id] = paintedMacroHullBounds(macro.id);
    return hulls;
}

// Call after a change that may have moved borders, with a snapshotPaintedHulls() taken before it. A border
// already gliding is restarted from where it is drawn now, so a second change never makes it jump. The borders measured
// before the change are dropped first: a membership change moves no card, so nothing else told the memo, and the
// border a paint measured during the drag would read as unmoved and snap instead of gliding.
void GraphEditor::glideHullsFrom(const MacroHullGlide::Hulls& before) {
    canvasMemo_->layoutChanged();
    MacroHullGlide::Hulls after;
    {
        const graph_editor_paint::CardMapScope oneCardMap(*this);
        for (const auto& [id, rect] : before)
            if (macros.find(id) != nullptr && !macros.isEffectivelyCollapsed(id))
                after[id] = macroHullTargetBounds(id);
    }
    if (!hullGlide_.arm(before, after))
        return;

    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    const auto frame = [safeEditor] {
        if (safeEditor == nullptr)
            return;
        safeEditor->macroController_.dockMacroPortWidgets();
        safeEditor->repaintCanvas();
    };
    hullGlideDriverAnim_.start(
        vblankUpdater, 220.0, synth::ui::easeOutCubic,
        [safeEditor, frame](float t) {
            if (safeEditor != nullptr)
                safeEditor->hullGlide_.applyTweenAt(t);
            frame();
        },
        [safeEditor, frame] {
            if (safeEditor != nullptr)
                safeEditor->hullGlide_.finish();
            frame();
        });
    macroController_.dockMacroPortWidgets();
    repaintCanvas();
}

void GraphEditor::finishHullGlideForTest() {
    hullGlide_.finish();
    macroController_.dockMacroPortWidgets();
    repaintCanvas();
}
