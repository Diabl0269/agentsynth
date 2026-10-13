// CableRetractAnimator.cpp -- which cables become ghosts or grow, and where their ends are drawn as they move.

#include "CableRetractAnimator.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <tuple>

namespace {
using Key = std::tuple<uint32_t, int, uint32_t, int, uint32_t>;
Key keyOf(const graph_editor_types::CableId& id) {
    return Key{id.srcUid, id.srcPort, id.dstUid, id.dstPort, id.attenUid};
}

std::vector<Key> sortedKeys(const std::vector<graph_editor_types::VisibleCable>& cables) {
    std::vector<Key> keys;
    keys.reserve(cables.size());
    for (const auto& cable : cables)
        keys.push_back(keyOf(cable.id));
    std::sort(keys.begin(), keys.end());
    return keys;
}
} // namespace

// The cables on the other side of the change are looked up in a set built once, so the diff costs what the two lists
// hold, never the product of their sizes: on a project with thousands of cables a scan of `after` per cable in
// `before` made every undo's diff grow with the square of the project.
bool CableRetractAnimator::arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after,
                               Options options) {
    ghosts_.clear();
    growing_.clear();
    splitFrom_.clear();
    joinHalves_.clear();
    joinHidden_.clear();
    progress_ = 0.0f;
    reduced_ = options.reduceMotion;
    const auto stillDrawn = sortedKeys(after);
    for (const auto& was : before)
        if (!std::binary_search(stillDrawn.begin(), stillDrawn.end(), keyOf(was.id)))
            ghosts_.push_back(was);
    if (options.growAdded) {
        const auto wasDrawn = sortedKeys(before);
        for (const auto& now : after)
            if (!std::binary_search(wasDrawn.begin(), wasDrawn.end(), keyOf(now.id)))
                growing_.push_back(now);
        if (growing_.size() > kMaxGrowing)
            growing_.clear();
    }
    if (options.morphChains) {
        const auto involved = armChains(before, after);
        if (options.chainsOnly) { // nothing but the cables of a chain moves; under Reduce Motion those fade
            const auto notInvolved = [&involved](const VisibleCable& c) {
                return std::find(involved.begin(), involved.end(), c.id) == involved.end();
            };
            ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), notInvolved), ghosts_.end());
            growing_.erase(std::remove_if(growing_.begin(), growing_.end(), notInvolved), growing_.end());
        }
    } else if (options.chainsOnly) {
        ghosts_.clear();
        growing_.clear();
    }
    return isLive();
}

// A split and a join are told apart by which side has the single cable. Cables are matched on the jacks they share:
// the new halves of a split keep the old cable's source jack and destination jack and meet at the new card; the old
// halves of a join do the same around the deleted card. A cable that fits neither shape keeps its ordinary retract or
// grow.
std::vector<graph_editor_types::CableId> CableRetractAnimator::armChains(const std::vector<VisibleCable>& before,
                                                                         const std::vector<VisibleCable>& after) {
    std::vector<graph_editor_types::CableId> involved; // every cable that is part of a split or a join
    const auto wasDrawn = sortedKeys(before);
    const auto stillDrawn = sortedKeys(after);
    const auto isGone = [&stillDrawn](const VisibleCable& c) {
        return !std::binary_search(stillDrawn.begin(), stillDrawn.end(), keyOf(c.id));
    };
    const auto isNew = [&wasDrawn](const VisibleCable& c) {
        return !std::binary_search(wasDrawn.begin(), wasDrawn.end(), keyOf(c.id));
    };
    const auto direct = [](const VisibleCable& c) {
        return c.id.attenUid == 0 && c.kind == VisibleCable::Kind::Direct;
    };
    std::vector<const VisibleCable*> gone, added;
    for (const auto& c : before)
        if (direct(c) && isGone(c))
            gone.push_back(&c);
    for (const auto& c : after)
        if (direct(c) && isNew(c))
            added.push_back(&c);
    if (gone.empty() || added.empty())
        return involved;

    std::vector<const VisibleCable*> usedGone, usedAdded;
    const auto used = [](const std::vector<const VisibleCable*>& list, const VisibleCable* c) {
        return std::find(list.begin(), list.end(), c) != list.end();
    };
    const auto dropFrom = [](auto& list, const VisibleCable& c) {
        list.erase(std::remove_if(list.begin(), list.end(), [&c](const VisibleCable& o) { return o.id == c.id; }),
                   list.end());
    };

    const auto addGrowing = [this](const VisibleCable& c) {
        if (std::none_of(growing_.begin(), growing_.end(), [&c](const VisibleCable& g) { return g.id == c.id; }))
            growing_.push_back(c);
    };

    // Splits: gone A -> C, added A -> B and B -> C.
    for (const auto* old : gone) {
        if (used(usedGone, old))
            continue;
        for (const auto* first : added) {
            if (used(usedAdded, first) || first->id.srcUid != old->id.srcUid || first->id.srcPort != old->id.srcPort ||
                first->id.dstUid == old->id.dstUid)
                continue;
            const auto* second = static_cast<const VisibleCable*>(nullptr);
            for (const auto* c : added)
                if (c != first && !used(usedAdded, c) && c->id.srcUid == first->id.dstUid &&
                    c->id.dstUid == old->id.dstUid && c->id.dstPort == old->id.dstPort) {
                    second = c;
                    break;
                }
            if (second == nullptr)
                continue;
            usedGone.push_back(old);
            usedAdded.push_back(first);
            usedAdded.push_back(second);
            for (const auto* c : {old, first, second})
                involved.push_back(c->id);
            if (reduced_) { // a plain fade: the old cable out, the two new ones in
                addGrowing(*first);
                addGrowing(*second);
                break;
            }
            splitFrom_.push_back({*first, old->p1, old->p2, {}, {}});
            splitFrom_.push_back({*second, old->p1, old->p2, {}, {}});
            dropFrom(ghosts_, *old);
            dropFrom(growing_, *first);
            dropFrom(growing_, *second);
            break;
        }
    }

    // Joins: gone A -> B and B -> C, added A -> C.
    for (const auto* joined : added) {
        if (used(usedAdded, joined))
            continue;
        const VisibleCable* first = nullptr;
        const VisibleCable* second = nullptr;
        for (const auto* c : gone)
            if (!used(usedGone, c) && c->id.srcUid == joined->id.srcUid && c->id.srcPort == joined->id.srcPort &&
                c->id.dstUid != joined->id.dstUid) {
                first = c;
                break;
            }
        if (first == nullptr)
            continue;
        for (const auto* c : gone)
            if (c != first && !used(usedGone, c) && c->id.srcUid == first->id.dstUid &&
                c->id.dstUid == joined->id.dstUid && c->id.dstPort == joined->id.dstPort) {
                second = c;
                break;
            }
        if (second == nullptr)
            continue;
        for (const auto* half : {first, second}) {
            usedGone.push_back(half);
            involved.push_back(half->id);
        }
        usedAdded.push_back(joined);
        involved.push_back(joined->id);
        if (reduced_) {
            addGrowing(*joined);
            continue;
        }
        for (const auto* half : {first, second}) {
            joinHalves_.push_back({*half, half->p1, half->p2, joined->p1, joined->p2});
            dropFrom(ghosts_, *half);
        }
        joinHidden_.push_back(joined->id);
        dropFrom(growing_, *joined);
    }
    return involved;
}

float CableRetractAnimator::chainProgress() const noexcept { return synth::ui::easeOutCubic(progress_); }

bool CableRetractAnimator::isMorphing(const graph_editor_types::CableId& id) const noexcept {
    return std::any_of(splitFrom_.begin(), splitFrom_.end(), [&id](const Run& r) { return r.cable.id == id; });
}

// Both ends ease out from where the old cable was to where the live cable is (live, so a card still gliding is
// followed).
CableRetractAnimator::VisibleCable CableRetractAnimator::morphed(const VisibleCable& live) const noexcept {
    auto drawn = live;
    const auto it =
        std::find_if(splitFrom_.begin(), splitFrom_.end(), [&live](const Run& r) { return r.cable.id == live.id; });
    if (it == splitFrom_.end())
        return drawn;
    const float t = chainProgress();
    drawn.p1 = it->from1 + (live.p1 - it->from1) * t;
    drawn.p2 = it->from2 + (live.p2 - it->from2) * t;
    return drawn;
}

bool CableRetractAnimator::isHidden(const graph_editor_types::CableId& id) const noexcept {
    return std::find(joinHidden_.begin(), joinHidden_.end(), id) != joinHidden_.end();
}

std::vector<CableRetractAnimator::VisibleCable> CableRetractAnimator::joinHalves() const {
    std::vector<VisibleCable> drawn;
    const float t = chainProgress();
    for (const auto& run : joinHalves_) {
        auto cable = run.cable;
        cable.p1 = run.from1 + (run.to1 - run.from1) * t;
        cable.p2 = run.from2 + (run.to2 - run.from2) * t;
        drawn.push_back(cable);
    }
    return drawn;
}

void CableRetractAnimator::applyTweenAt(float t) noexcept { progress_ = juce::jlimit(0.0f, 1.0f, t); }

void CableRetractAnimator::finish() noexcept {
    ghosts_.clear();
    growing_.clear();
    splitFrom_.clear();
    joinHalves_.clear();
    joinHidden_.clear();
    progress_ = 1.0f;
}

// Ease in and out: the cable gathers itself, pulls in, and settles into the jack (docs/layout/animation.md).
std::vector<CableRetractAnimator::VisibleCable> CableRetractAnimator::ghosts() const {
    auto drawn = ghosts_;
    if (reduced_)
        return drawn;
    const float pulled = synth::ui::easeInOutCubic(progress_);
    for (auto& cable : drawn)
        cable.p2 += (cable.p1 - cable.p2) * pulled;
    return drawn;
}

float CableRetractAnimator::opacity() const noexcept {
    if (reduced_)
        return 1.0f - progress_;
    return progress_ <= kRetractHoldUntil ? 1.0f : 1.0f - (progress_ - kRetractHoldUntil) / (1.0f - kRetractHoldUntil);
}

bool CableRetractAnimator::isGrowing(const graph_editor_types::CableId& id) const noexcept {
    return std::any_of(growing_.begin(), growing_.end(), [&id](const VisibleCable& c) { return c.id == id; });
}

// Ease out: it shoots out of the jack and settles onto the destination.
CableRetractAnimator::VisibleCable CableRetractAnimator::grown(const VisibleCable& live) const noexcept {
    auto drawn = live;
    if (!reduced_)
        drawn.p2 = live.p1 + (live.p2 - live.p1) * synth::ui::easeOutCubic(progress_);
    return drawn;
}

float CableRetractAnimator::growOpacity() const noexcept {
    return reduced_ ? progress_ : juce::jmin(1.0f, progress_ / kGrowFadeInUntil);
}

juce::Rectangle<int> CableRetractAnimator::paintArea() const {
    juce::Rectangle<float> area;
    for (const auto& ghost : ghosts())
        area = area.getUnion(synth::ui::cablePaintBounds(ghost.p1, ghost.p2));
    for (const auto& cable : growing_) {
        const auto now = grown(cable);
        area = area.getUnion(synth::ui::cablePaintBounds(now.p1, now.p2));
    }
    for (const auto& run : splitFrom_) {
        const auto now = morphed(run.cable);
        area = area.getUnion(synth::ui::cablePaintBounds(now.p1, now.p2));
        area = area.getUnion(synth::ui::cablePaintBounds(run.cable.p1, run.cable.p2));
    }
    for (const auto& half : joinHalves())
        area = area.getUnion(synth::ui::cablePaintBounds(half.p1, half.p2));
    for (const auto& run : joinHalves_)
        area = area.getUnion(synth::ui::cablePaintBounds(run.to1, run.to2));
    return area.getSmallestIntegerContainer();
}
