// MacroCrossingAnimator.cpp
//
// The matching rule behind arm(): see MacroCrossingAnimator.h for the class's overall shape.

#include "MacroCrossingAnimator.h"

#include <algorithm>

namespace {

using graph_editor_types::CableId;
using VisibleCable = MacroCrossingAnimator::VisibleCable;

// One endpoint's stable identity: which node/channel/side of a CableId, deliberately ignoring
// attenUid (an attenuverter chain's own hidden node id, not one of the two REAL endpoints) — this
// is what lets a match survive a mod-routed crossing the same as a plain one, and keeps this class
// off MacroGroupController's own port-node bookkeeping entirely (match on
// the real endpoints, never the port NodeID).
struct EndpointKey {
    uint32_t uid = 0;
    int channel = 0;
    bool isSrc = false;

    bool operator==(const EndpointKey& o) const noexcept {
        return uid == o.uid && channel == o.channel && isSrc == o.isSrc;
    }
};

EndpointKey srcKey(const CableId& id) noexcept { return {id.srcUid, id.srcPort, true}; }
EndpointKey dstKey(const CableId& id) noexcept { return {id.dstUid, id.dstPort, false}; }

} // namespace

namespace {
bool touchesNode(const graph_editor_types::CableId& id, uint32_t uid) noexcept {
    return id.srcUid == uid || id.dstUid == uid;
}
} // namespace

bool MacroCrossingAnimator::arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after,
                                uint32_t crossingNodeUid, juce::Rectangle<int> flashBounds) {
    tweens_.clear();
    progress_ = 0.0f;
    live_ = false;

    // Only a cable touching the crossing module itself is a candidate — see arm()'s own doc
    // comment for why a wholly-interior port removal between two OTHER members (which can also
    // change CableIds in this same splice) must never enter this matching pool: it has no single
    // well-defined "old anchor" to slide from, and matching it can steal the pairing a genuine
    // crossing cable needed.
    std::vector<VisibleCable> vanished;
    for (const auto& b : before)
        if (touchesNode(b.id, crossingNodeUid)) {
            bool stillThere = false;
            for (const auto& a : after)
                if (a.id == b.id) {
                    stillThere = true;
                    break;
                }
            if (!stillThere)
                vanished.push_back(b);
        }

    std::vector<VisibleCable> appeared;
    for (const auto& a : after)
        if (touchesNode(a.id, crossingNodeUid)) {
            bool existedBefore = false;
            for (const auto& b : before)
                if (a.id == b.id) {
                    existedBefore = true;
                    break;
                }
            if (!existedBefore)
                appeared.push_back(a);
        }

    std::vector<bool> appearedUsed(appeared.size(), false);
    for (const auto& v : vanished) {
        const auto vSrc = srcKey(v.id);
        const auto vDst = dstKey(v.id);
        for (size_t i = 0; i < appeared.size(); ++i) {
            if (appearedUsed[i])
                continue;
            const auto& a = appeared[i];
            // Exactly one endpoint must match (the OTHER one is what changed — a port spliced in
            // or out); both matching would mean `a` and `v` are the same cable, which the
            // still-there filter above already excluded.
            if (!(srcKey(a.id) == vSrc) && !(dstKey(a.id) == vDst))
                continue;

            CableTween tween;
            tween.afterId = a.id;
            tween.fromP1 = v.p1;
            tween.fromP2 = v.p2;
            tween.toP1 = a.p1;
            tween.toP2 = a.p2;
            tweens_.push_back(tween);
            appearedUsed[i] = true;
            break;
        }
    }

    flashBounds_ = flashBounds;
    live_ = !tweens_.empty() || !flashBounds_.isEmpty();
    return live_;
}

// A cable with no earlier self has no "before" to match, so the slide's start is the drop point itself:
// the port end emerges from where the cable was released (docs/layout/animation.md "Motion rules").
bool MacroCrossingAnimator::armSlideFrom(const std::vector<VisibleCable>& before,
                                         const std::vector<VisibleCable>& after, juce::Point<float> dropPoint,
                                         const std::function<bool(uint32_t)>& isPortUid) {
    tweens_.clear();
    flashBounds_ = {};
    progress_ = 0.0f;
    live_ = false;

    for (const auto& a : after) {
        const bool existedBefore =
            std::any_of(before.begin(), before.end(), [&a](const auto& b) { return b.id == a.id; });
        const bool srcOnPort = isPortUid(a.id.srcUid);
        const bool dstOnPort = isPortUid(a.id.dstUid);
        if (existedBefore || (!srcOnPort && !dstOnPort))
            continue;
        CableTween tween;
        tween.afterId = a.id;
        tween.toP1 = a.p1;
        tween.toP2 = a.p2;
        tween.fromP1 = srcOnPort ? dropPoint : a.p1;
        tween.fromP2 = dstOnPort ? dropPoint : a.p2;
        tweens_.push_back(tween);
    }
    live_ = !tweens_.empty();
    return live_;
}

void MacroCrossingAnimator::applyTweenAt(float t) noexcept { progress_ = juce::jlimit(0.0f, 1.0f, t); }

void MacroCrossingAnimator::finish() noexcept {
    tweens_.clear();
    progress_ = 1.0f;
    live_ = false;
}

void MacroCrossingAnimator::applyTo(std::vector<VisibleCable>& cables) const {
    if (!live_ || tweens_.empty())
        return;

    for (auto& cable : cables) {
        for (const auto& tween : tweens_) {
            if (!(cable.id == tween.afterId))
                continue;
            // Offset from the live anchors rather than lerped between fixed ones: a crossing applied mid-drag
            // keeps moving its module, and the cable end on that card must stay on it.
            cable.p1 += (tween.fromP1 - tween.toP1) * (1.0f - progress_);
            cable.p2 += (tween.fromP2 - tween.toP2) * (1.0f - progress_);
            break;
        }
    }
}

std::optional<std::pair<juce::Rectangle<int>, float>> MacroCrossingAnimator::flashState() const noexcept {
    if (!live_ || flashBounds_.isEmpty())
        return std::nullopt;
    return std::make_pair(flashBounds_, progress_);
}
