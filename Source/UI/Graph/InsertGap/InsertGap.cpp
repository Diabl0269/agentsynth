// InsertGap.cpp -- the live insert-between gap; see InsertGap.h and
// docs/layout/layout.md#making-room-for-a-module-dropped-between-others.

#include "InsertGap.h"

#include "Mixer/MasterSplice.h"
#include "UI/Graph/GraphCanvasHost.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"

#include <algorithm>
#include <map>

namespace {
using synth::LayoutUtil::LayoutUnit;
namespace ig = synth::insert_gap;

juce::String nodeKey(juce::AudioProcessorGraph::NodeID id) { return "n:" + juce::String((juce::int64)id.uid); }

juce::AudioProcessorGraph::NodeID nodeIdOfKey(const juce::String& key) {
    if (!key.startsWith("n:"))
        return {};
    return juce::AudioProcessorGraph::NodeID(
        static_cast<juce::uint32>(key.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue()));
}

// The open gap is kept while the pointer stays over the slot or the spacing on either side of it, so the gap it just
// opened (which is where the pointer is) never reads as "no gap here" and closes again.
juce::Rectangle<int> holdZone(const ig::Plan& plan) {
    return plan.target.axis == ig::Axis::Row ? plan.slot.expanded(plan.spacing, synth::LayoutUtil::kCollisionGap)
                                             : plan.slot.expanded(synth::LayoutUtil::kCollisionGap, plan.spacing);
}
} // namespace

InsertGap::InsertGap(GraphCanvasHost& host, MacroGroupController& macros)
    : host_(host)
    , macros_(macros) {}

InsertGap::~InsertGap() { escape_.disarm(); }

void InsertGap::setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

std::vector<LayoutUnit> InsertGap::unitsAt(const juce::String& container, const juce::String& without) const {
    auto units = macros_.buildLayoutUnits(container);
    units.erase(
        std::remove_if(units.begin(), units.end(), [&without](const LayoutUnit& u) { return u.key == without; }),
        units.end());
    return units;
}

// A dock card (it only moves up and down) and a macro port widget (docked to its border) never insert.
bool InsertGap::selfCanInsert(const juce::String& selfKey) const {
    const auto id = nodeIdOfKey(selfKey);
    if (id.uid == 0)
        return true;
    auto* node = host_.graph().getNodeForId(id);
    return node != nullptr && !synth::isOutputDockProcessor(node->getProcessor()) && !macros_.nodeIsMacroPort(id);
}

// The gap is picked on the cards as they are drawn now, with the open gap's own slot as a hold zone (see holdZone), and
// only re-made when the target changes, so a pointer moving inside one gap moves nothing.
void InsertGap::hover(const Hover& request) {
    auto hover = request;
    if (const auto self = nodeIdOfKey(hover.selfKey); self.uid != 0) {
        const auto* owner = macros_.macroForNode(self);
        hover.container = owner != nullptr ? owner->id : juce::String();
    }
    if (committing_ || suppressed_ || hover.size.x <= 0 || hover.size.y <= 0 || !selfCanInsert(hover.selfKey)) {
        if (!committing_ && !suppressed_ && isOpen())
            close();
        return;
    }
    const bool sameLevel = isOpen() && hover.container == container_ && hover.selfKey == selfKey_;
    if (sameLevel && hold_.contains(hover.pointer))
        return;
    // A card nudged within its own old place is no insert: nothing is pushed until it has really gone somewhere.
    const auto target = hover.selfStart.contains(hover.pointer)
                            ? std::nullopt
                            : ig::pickTarget(unitsAt(hover.container, hover.selfKey), hover.pointer, hover.size);
    if (sameLevel && target.has_value() && *target == plan_->target)
        return;
    if (!isOpen()) {
        // A target whose card already fits moves nothing; it is only planned again once the target changes.
        const auto idle = target.has_value() ? hover.container + "|" + hover.selfKey + "|" + target->anchorKey +
                                                   (target->axis == ig::Axis::Row ? "|r" : "|c")
                                             : juce::String();
        if (idle == idle_)
            return;
        idle_ = idle;
    }
    retarget(hover, target);
}

// Everything the open gap moved goes home, then the new gap (planned on that home layout) is made, in one glide Scope
// so each card slides from where it is drawn straight to where it ends. Reduce Motion lands at once.
void InsertGap::retarget(const Hover& hover, const std::optional<ig::Target>& target) {
    const bool reduced = synth::ui::prefersReducedMotion();
    auto& glide = host_.cardGlide();
    if (reduced && glide.isLive())
        glide.finish();
    std::optional<CardGlideAnimator::Scope> scope;
    if (!reduced)
        scope.emplace(glide);
    const auto borders = !reduced && hooks_.beginBorderGlide ? hooks_.beginBorderGlide() : std::function<void()>();

    bool movedAny = revert();
    plan_.reset();
    if (target.has_value()) {
        const auto plan = ig::planBefore(unitsAt(hover.container, hover.selfKey), *target, hover.size);
        if (plan.has_value() && !plan->moves.empty()) {
            if (dock_.empty())
                snapshotDock();
            apply(*plan, hover.container, hover.selfKey);
            plan_ = plan;
            container_ = hover.container;
            selfKey_ = hover.selfKey;
            size_ = hover.size;
            hold_ = holdZone(*plan);
            movedAny = true;
            idle_.clear();
            armEscape();
        }
    }
    if (movedAny)
        macros_.refreshAfterUnitMoves();
    if (!plan_.has_value()) {
        restoreDock();
        escape_.disarm();
    }
    if (movedAny && hooks_.afterMove)
        hooks_.afterMove();
    if (borders)
        borders();
}

void InsertGap::close() {
    if (!isOpen() && applied_.empty())
        return;
    retarget({}, std::nullopt);
}

void InsertGap::endSession() {
    if (!committing_)
        close();
    suppressed_ = false;
    idle_.clear();
}

bool InsertGap::cancel() {
    if (!isOpen() || committing_)
        return false;
    close();
    suppressed_ = true;
    return true;
}

void InsertGap::forget() noexcept {
    escape_.disarm();
    plan_.reset();
    applied_.clear();
    dock_.clear();
    suppressed_ = false;
    idle_.clear();
    pending_.reset();
}

// Moves the plan's units at `container`, then, level by level outwards, lets each enclosing macro whose border grew
// push what is ahead of it at its own level (in chain, the same direction), so a gap opened inside a macro never leaves
// its border on top of a neighbour. Every move is remembered so the gap can be taken back exactly.
void InsertGap::apply(const ig::Plan& plan, const juce::String& container, const juce::String& without) {
    auto& macros = host_.getMacros();
    std::map<juce::String, juce::Rectangle<int>> homeHulls;
    for (auto level = container; level.isNotEmpty();) {
        homeHulls[level] = macros_.macroHullBounds(level);
        const auto* macro = macros.find(level);
        level = macro != nullptr ? macro->parentId : juce::String();
    }
    for (const auto& move : plan.moves) {
        macros_.moveUnitBy(move.key, move.delta);
        applied_.push_back({move.key, move.delta, container});
    }
    for (auto level = container; level.isNotEmpty();) {
        const auto* macro = macros.find(level);
        if (macro == nullptr)
            break;
        const auto parent = macro->parentId;
        auto units = unitsAt(parent, without);
        units.erase(
            std::remove_if(units.begin(), units.end(), [&level](const LayoutUnit& u) { return u.key == "m:" + level; }),
            units.end());
        for (const auto& move :
             ig::pushAhead(units, homeHulls[level], macros_.macroHullBounds(level), plan.target.axis)) {
            macros_.moveUnitBy(move.key, move.delta);
            applied_.push_back({move.key, move.delta, parent});
        }
        level = parent;
    }
}

bool InsertGap::revert() {
    if (applied_.empty())
        return false;
    for (auto it = applied_.rbegin(); it != applied_.rend(); ++it)
        macros_.moveUnitBy(it->key, -it->delta);
    applied_.clear();
    return true;
}

// The output dock is re-derived to the right of whatever moved (refreshAfterUnitMoves), which is only right while the
// gap is open: closing it puts the dock back exactly where it stood, even if that was not where it would be derived.
void InsertGap::snapshotDock() {
    dock_.clear();
    for (auto* node : synth::outputDockNodes(host_.graph()))
        dock_.emplace_back(node->nodeID, juce::Point<int>((int)node->properties.getWithDefault("x", 0),
                                                          (int)node->properties.getWithDefault("y", 0)));
}

void InsertGap::restoreDock() {
    for (const auto& [id, pos] : dock_) {
        auto* node = host_.graph().getNodeForId(id);
        if (node == nullptr)
            continue;
        node->properties.set("x", pos.x);
        node->properties.set("y", pos.y);
        if (auto* comp = host_.moduleComponentFor(id))
            comp->setTopLeftPosition(pos);
    }
    if (!dock_.empty())
        host_.repaintCanvas();
    dock_.clear();
}

void InsertGap::armEscape() {
    if (hooks_.keyHost != nullptr && !escape_.isArmed())
        escape_.arm(*hooks_.keyHost, [this] { cancel(); });
}

bool InsertGap::armAfter(const juce::String& container, const juce::String& afterKey, juce::Point<int> size) {
    if (committing_ || isOpen())
        return false;
    const auto plan = ig::planAfter(unitsAt(container, {}), afterKey, size);
    if (!plan.has_value())
        return false;
    pending_ = plan->target;
    pendingContainer_ = container;
    pendingSlot_ = plan->slot.getPosition();
    pendingSize_ = size;
    return true;
}

void InsertGap::clearPending() noexcept {
    if (!committing_)
        pending_.reset();
}

// The pushes go on the landed card as if its own growth had made them, so deleting it (or shrinking it) offers them
// their way back. Newest first is how they return, so they are listed farthest first: the nearest returns first and
// clears the way for the next.
std::optional<juce::Point<int>> InsertGap::land(juce::AudioProcessorGraph::NodeID nodeId, juce::Point<int> size) {
    if (!committing_ || !pending_.has_value())
        return std::nullopt;
    const auto key = nodeKey(nodeId);
    const auto plan = ig::planBefore(unitsAt(pendingContainer_, key), *pending_, size);
    pending_.reset();
    if (!plan.has_value())
        return std::nullopt;
    apply(*plan, pendingContainer_, key);
    if (!applied_.empty())
        macros_.refreshAfterUnitMoves();
    std::vector<synth::Macro::DisplacedNeighbour> records;
    for (auto it = applied_.rbegin(); it != applied_.rend(); ++it) {
        const auto units = macros_.buildLayoutUnits(it->container);
        const auto unit =
            std::find_if(units.begin(), units.end(), [&it](const LayoutUnit& u) { return u.key == it->key; });
        if (unit != units.end())
            records.push_back({it->key, it->delta, unit->rect.getPosition()});
    }
    applied_.clear();
    macros_.notePushesBy(key, records);
    return plan->slot.getPosition();
}

// The gap goes home before the record so that undo returns to the layout without it; land() re-makes it inside. The
// glide Scope opens while the gap is made, so a card that ends where the hover put it does not move at all. A keyboard
// insert has no gap yet: under Reduce Motion it is made here first, so its cards land at once rather than glide.
InsertGap::Commit::Commit(InsertGap& gap)
    : gap_(gap) {
    if (gap_.committing_ || (!gap_.isOpen() && !gap_.pending_.has_value()))
        return;
    const bool reduced = synth::ui::prefersReducedMotion();
    if (!gap_.isOpen() && reduced)
        if (const auto plan =
                ig::planBefore(gap_.unitsAt(gap_.pendingContainer_, {}), *gap_.pending_, gap_.pendingSize_);
            plan.has_value() && !plan->moves.empty()) {
            gap_.snapshotDock();
            gap_.apply(*plan, gap_.pendingContainer_, {});
            gap_.macros_.refreshAfterUnitMoves();
        }
    glide_.emplace(gap_.host_.cardGlide());
    if (!reduced && gap_.hooks_.beginBorderGlide)
        borders_ = gap_.hooks_.beginBorderGlide();
    if (gap_.isOpen()) {
        gap_.pending_ = gap_.plan_->target;
        gap_.pendingContainer_ = gap_.container_;
        gap_.pendingSlot_ = gap_.plan_->slot.getPosition();
        gap_.plan_.reset();
    }
    if (gap_.revert())
        gap_.macros_.refreshAfterUnitMoves();
    gap_.restoreDock();
    gap_.escape_.disarm();
    gap_.committing_ = true;
    owns_ = true;
}

InsertGap::Commit::~Commit() {
    if (!owns_)
        return;
    gap_.committing_ = false;
    gap_.pending_.reset();
    gap_.applied_.clear();
    gap_.dock_.clear();
    if (borders_)
        borders_();
    glide_.reset();
}
