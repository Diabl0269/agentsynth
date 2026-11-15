// ClipLaneEnterExit.cpp
//
// A section edit runs as several doc steps, each with its own notification (delete the old bars, open the new ones,
// stamp the music); only the last step bumps getSectionEditRevision(), so the steps before it are skipped and the
// lane diffs the finished edit against the state before it. Exits follow docs/layout/animation.md: they ask whether
// the lane can animate once when they start, run on one AnimationDriver and stop by themselves.

#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth::ui {

ClipLaneEnterExit::ClipLaneEnterExit(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

ClipLaneEnterExit::~ClipLaneEnterExit() { driver_.stop(); }

bool ClipLaneChange::covers(double beat) const {
    if (spans.empty())
        return true;
    for (const auto& [start, end] : spans)
        if (beat >= start && beat < end)
            return true;
    return false;
}

// A restore and a section edit move every clip that changed; a repeat stamp only the clips in the placements it
// rewrote, so the clip the user is editing (in the placement that was the source) never glides back to where it was.
ClipLaneEnterExit::Change ClipLaneEnterExit::classify(const synth::TimelineDoc& doc, bool restored) {
    Change change;
    leavingScale_.clear(); // noted afresh by this refresh, before its exits start
    const auto edit = doc.getSectionEditRevision();
    if (primed_ && doc.isSectionEditUnfinished() && edit == seenEdit_) {
        change.intermediate = true;
        return change;
    }
    const auto stamp = doc.getSectionStampRevision();
    const auto replace = doc.getReplaceCount();
    change.replaced = primed_ && replace != seenReplace_;
    change.restored = restored;
    const bool edited = primed_ && edit != seenEdit_;
    const bool stamped = primed_ && stamp != seenStamp_;
    if (change.replaced && !restored)
        origins_.clear(); // a load
    else if (primed_)
        change.sections = origins_.diff(placements_, placementSpansOf(doc), restored);
    seenEdit_ = edit;
    seenStamp_ = stamp;
    seenReplace_ = replace;
    primed_ = true;
    if (restored || edited) {
        change.animates = true;
    } else if (stamped) {
        for (const auto id : doc.getLastStampedPlacements())
            if (const auto* placement = doc.getPlacement(id))
                if (const auto* section = doc.getSection(placement->section))
                    change.spans.emplace_back(placement->startBeat, placement->startBeat + section->lengthBeats);
        change.animates = !change.spans.empty();
    }
    return change;
}

void ClipLaneEnterExit::remember(const synth::TimelineDoc& doc) {
    clips_.clear();
    tracks_.clear();
    placements_ = placementSpansOf(doc);
    const auto& tracks = doc.getTracks();
    for (int row = 0; row < (int)tracks.size(); ++row) {
        const auto& track = tracks[(size_t)row];
        tracks_.insert(track.id.value);
        for (const auto& clip : track.clips)
            clips_[clip.id.value] = Ghost{clip, row, track.id, {}};
    }
}

const ClipLaneEnterExit::Ghost* ClipLaneEnterExit::remembered(std::int64_t id) const {
    const auto it = clips_.find(id);
    return it != clips_.end() ? &it->second : nullptr;
}

void ClipLaneEnterExit::clear() {
    driver_.stop();
    if (!ghosts_.empty())
        repaintGhosts();
    ghosts_.clear();
    clips_.clear();
    tracks_.clear();
    placements_.clear();
    origins_.clear();
    returning_.clear();
    leavingScale_.clear();
    primed_ = false;
}

bool ClipLaneEnterExit::canAnimate() const {
    return forceAnimateForTest_ ? !animationsOff() : FadeVisibility::canAnimateIn(&owner_);
}

// A picture stays between the rect it rests in and its anchor (it shrinks toward its block's origin, which can lie
// outside the clip, or fades), so a frame repaints that span and a little around it for the clip outline.
void ClipLaneEnterExit::repaintGhosts() const {
    juce::Rectangle<int> area;
    for (const auto& ghost : ghosts_) {
        const auto x = (int)std::floor(ghost.anchor());
        const auto span = ghost.rect.getUnion({x, ghost.rect.getY(), 1, ghost.rect.getHeight()});
        area = area.isEmpty() ? span : area.getUnion(span);
    }
    if (!area.isEmpty())
        repaintMotionArea(owner_, area.expanded(3));
}

void ClipLaneEnterExit::startExits(std::vector<Ghost> leaving) {
    leavingScale_.clear();
    driver_.stop();
    if (!ghosts_.empty())
        repaintGhosts();
    ghosts_.clear();
    if (leaving.empty() || !canAnimate())
        return; // nothing to show: the clips are simply gone
    ghosts_ = std::move(leaving);
    fade_ = prefersReducedMotion();
    t_ = 0.0f;
    repaintGhosts();
    // Linear time: each picture eases it by its origin (scaleOf), like the strip's block above it.
    driver_.start(
        vblank_, motionMs(kExitMs, kReducedMs), [](float t) { return t; },
        [this](float t) {
            t_ = t;
            repaintGhosts();
        },
        [this] {
            repaintGhosts();
            ghosts_.clear();
        });
}

} // namespace synth::ui
