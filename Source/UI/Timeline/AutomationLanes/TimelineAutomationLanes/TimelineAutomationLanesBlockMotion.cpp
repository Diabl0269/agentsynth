// Concern: TimelineAutomationLanes' motion when something in an open track's fold-out comes or goes. A lane block that
// arrives (added, duplicated, moved here, an undo bringing it back) grows from nothing to its height; one that leaves
// (deleted, moved to another track, an undo taking it away) shrinks to nothing in its old place, drawn as a picture
// taken just before it went. A modulator row that arrives (added, moved here from another lane) grows the same way,
// and one that leaves fades out in a gap that closes. Every height feeds the track layout (extraHeights), so the
// tracks below glide through the usual onLayoutChanged path. One AnimationDriver steps every tween (160 ms
// easeOutCubic, retargeted from where each one is when another starts); it lands at once off screen, under Reduce
// Motion and with Animations off. docs/layout/animation.md "Lane blocks and modulator rows".
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "AppUndoManager.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kBlockMs = 160.0;

int scaled(int height, float presence) { return std::max(0, (int)std::lround((double)height * (double)presence)); }
} // namespace

// Painted at its own resolution from the top-left corner, so a shrinking gap clips it instead of squashing it.
TimelineAutomationLanes::BlockPicture::BlockPicture(juce::Image image, float scale)
    : image_(std::move(image))
    , scale_(scale > 0.0f ? scale : 1.0f) {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
}

void TimelineAutomationLanes::BlockPicture::paint(juce::Graphics& g) {
    if (image_.isValid())
        g.drawImageTransformed(image_, juce::AffineTransform::scale(1.0f / scale_));
}

// The same test every other timeline motion asks: on screen (or forced by a test), and neither Reduce Motion nor Off.
bool TimelineAutomationLanes::canAnimateBlocks() const {
    return FadeVisibility::canAnimateIn(&headerParent_) && !prefersReducedMotion();
}

juce::String TimelineAutomationLanes::laneMotionKey(synth::LaneId lane) {
    return "lane:" + juce::String((juce::int64)lane.value);
}

// Per lane: a routing moved between two lanes of the same jack keeps its key, and may leave one lane as it arrives in
// the other.
juce::String TimelineAutomationLanes::rowMotionKey(synth::LaneId lane, const juce::String& routingKey) {
    return juce::String((juce::int64)lane.value) + "|" + routingKey;
}

float TimelineAutomationLanes::arrivingPresence(const juce::String& key) const {
    const auto it = arriving_.find(key);
    return it != arriving_.end() ? it->second.now : 1.0f;
}

void TimelineAutomationLanes::growIn(const juce::String& key) {
    if (!canAnimateBlocks())
        return;
    arriving_[key] = {0.0f, 1.0f, 0.0f};
    startBlockMotion();
}

// A restart retargets every tween in flight from where it stands, so a second change mid-motion never jumps.
void TimelineAutomationLanes::startBlockMotion() {
    for (auto& [key, tween] : arriving_)
        tween.from = tween.now;
    for (auto& leaving : leavingLanes_)
        leaving.presence.from = leaving.presence.now;
    for (auto& leaving : leaving_)
        leaving->presence.from = leaving->presence.now;
    blockDriverRunning_ = true;
    blockDriver_.start(
        foldUpdater_, kBlockMs, easeOutCubic, [this](float t) { frameBlockMotion(t); }, [this] { landBlockMotion(); });
}

void TimelineAutomationLanes::frameBlockMotion(float t) {
    const auto step = [t](FoldTween& tween) { tween.now = tween.from + (tween.to - tween.from) * t; };
    for (auto& [key, tween] : arriving_)
        step(tween);
    for (auto& leaving : leavingLanes_)
        step(leaving.presence);
    for (auto& leaving : leaving_)
        step(leaving->presence);
    if (onLayoutChanged)
        onLayoutChanged();
}

// Everything that arrived is whole and everything that left is gone; the pictures and the faded rows go with it.
void TimelineAutomationLanes::landBlockMotion() {
    blockDriver_.stop(foldUpdater_);
    blockDriverRunning_ = false;
    arriving_.clear();
    leavingLanes_.clear();
    leaving_.clear();
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::stepBlockMotionForTest(float t) {
    if (!blockDriverRunning_)
        return;
    if (t >= 1.0f)
        landBlockMotion();
    else
        frameBlockMotion(t);
}

// The track's blocks top to bottom: its lanes (an amount lane is a block of no height) with every leaving block right
// under the lane it followed. One whose lane above has gone too closes the list.
std::vector<TimelineAutomationLanes::BlockRef> TimelineAutomationLanes::blocksOf(const synth::Track& track) const {
    std::vector<BlockRef> blocks;
    const auto leavingAfter = [this, &blocks, &track](synth::LaneId after) {
        for (const auto& leaving : leavingLanes_)
            if (leaving.track == track.id && leaving.after == after)
                blocks.push_back({nullptr, &leaving, scaled(leaving.height, leaving.presence.now)});
    };
    leavingAfter({});
    for (const auto& lane : track.lanes) {
        blocks.push_back({&lane, nullptr, laneBlockHeight(lane)});
        leavingAfter(lane.id);
    }
    for (const auto& leaving : leavingLanes_) {
        const bool orphan = leaving.track == track.id && leaving.after.isValid() &&
                            std::none_of(track.lanes.begin(), track.lanes.end(),
                                         [&leaving](const synth::AutomationLane& l) { return l.id == leaving.after; });
        if (orphan)
            blocks.push_back({nullptr, &leaving, scaled(leaving.height, leaving.presence.now)});
    }
    return blocks;
}

// Called by syncPools with the lane blocks each open track shows now, before the headers of the lanes that went are
// dropped. Only a track that was open before and still is moves: opening or folding a track, or a project load (the
// whole doc replaced outside an undo or redo), lands at once. A lane that turned into a modulator's amount band, or
// one that came and went within one change (an edit in several steps, never drawn), has no block to shrink.
void TimelineAutomationLanes::noteLaneBlockChanges(const std::map<synth::TrackId, std::vector<synth::LaneId>>& shown) {
    const bool loaded = doc_->getReplaceCount() != replaceCountSeen_ && (undo_ == nullptr || !undo_->isRestoring());
    replaceCountSeen_ = doc_->getReplaceCount();
    const bool animate = canAnimateBlocks() && !loaded;
    for (const auto& [track, before] : shownBlocks_) {
        const auto now = shown.find(track);
        if (now == shown.end())
            continue;
        const auto has = [](const std::vector<synth::LaneId>& ids, synth::LaneId id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        };
        for (size_t i = 0; i < before.size(); ++i) {
            const auto lane = before[i];
            if (has(now->second, lane))
                continue;
            const auto arriving = arriving_.find(laneMotionKey(lane));
            if (arriving != arriving_.end() && arriving->second.now <= 0.0f) {
                arriving_.erase(arriving);
                continue;
            }
            if (!animate || (doc_->getLane(lane) != nullptr && isAmountLane(lane)))
                continue;
            synth::LaneId after;
            for (size_t j = i; j-- > 0;)
                if (has(now->second, before[j])) {
                    after = before[j];
                    break;
                }
            retireLaneBlock(lane, track, after);
        }
        for (const auto lane : now->second)
            if (!has(before, lane))
                growIn(laneMotionKey(lane));
    }
    shownBlocks_ = shown;
}

// A picture of the block as it stands now (its header and modulator rows; its curve and bands), shrinking where it was.
void TimelineAutomationLanes::retireLaneBlock(synth::LaneId lane, synth::TrackId track, synth::LaneId after) {
    auto* header = headerFor(lane);
    const auto placed = placedBlockHeights_.find(lane);
    if (header == nullptr || placed == placedBlockHeights_.end() || placed->second <= 0)
        return;
    LeavingLane leaving;
    leaving.lane = lane;
    leaving.track = track;
    leaving.after = after;
    leaving.height = placed->second;
    const float scale = juce::Component::getApproximateScaleFactorForComponent(&headerParent_);
    const juce::Rectangle<int> area(0, header->getY(), header->getWidth(), leaving.height);
    leaving.header = std::make_unique<BlockPicture>(headerParent_.createComponentSnapshot(area, true, scale), scale);
    headerParent_.addAndMakeVisible(*leaving.header);
    if (auto* editor = editorFor(lane)) {
        const juce::Rectangle<int> bodyArea(0, editor->getY(), bodies_->getWidth(), leaving.height);
        leaving.body = std::make_unique<BlockPicture>(bodies_->createComponentSnapshot(bodyArea, true, scale), scale);
        bodies_->addAndMakeVisible(*leaving.body);
    }
    leavingLanes_.push_back(std::move(leaving));
    startBlockMotion();
}

void TimelineAutomationLanes::placeLeavingLaneHeader(const LeavingLane& leaving, int y, int width) {
    leaving.header->setBounds(0, y, width, scaled(leaving.height, leaving.presence.now));
    leaving.header->setAlpha(leaving.presence.now);
}

// The lanes-region half of each leaving block, in the same content rows the header column gives it.
void TimelineAutomationLanes::placeLeavingLaneBodies(const TimelineRowLayout& layout) {
    if (leavingLanes_.empty())
        return;
    const int scroll = (int)std::llround(viewState_.trackScrollY);
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto& track = tracks[(size_t)i];
        if (!isVisibleLane(track))
            continue;
        int y = layout.trackTop(i) + layout.trackRowHeight(i);
        for (const auto& block : blocksOf(track)) {
            if (block.leaving != nullptr && block.leaving->body != nullptr) {
                block.leaving->body->setBounds(0, y - scroll, bodies_->getWidth(), block.height);
                block.leaving->body->setAlpha(block.leaving->presence.now);
            }
            y += block.height;
        }
    }
}

//==============================================================================
// Leaving modulator rows

// What a leaving row still takes of its lane's block: its shrinking gap, while its lane is on screen.
int TimelineAutomationLanes::leavingRowHeight(const LeavingModulator& leaving) const {
    if (leaving.presence.now <= 0.0f || modulators_.count(leaving.lane) == 0)
        return 0;
    return scaled(leaving.height, leaving.presence.now);
}

// The leaving rows standing just before the lane's `slot`th row (`slot` == count: after the last); a slot past the
// rows the lane has now closes the list.
int TimelineAutomationLanes::leavingRowsHeightAt(synth::LaneId lane, int slot) const {
    const int count = modulatorCount(lane);
    int height = 0;
    for (const auto& leaving : leaving_)
        if (leaving->lane == lane && std::min(leaving->slot, count) == slot)
            height += leavingRowHeight(*leaving);
    return height;
}

// Where the leaving row's gap starts under its lane row, px: the rows before its slot and the leaving rows ahead of it
// in that slot.
int TimelineAutomationLanes::leavingRowOffset(const LeavingModulator& leaving) const {
    const int count = modulatorCount(leaving.lane);
    const int slot = std::min(leaving.slot, count);
    int offset = slot > 0 ? modulatorOffset(leaving.lane, slot - 1) + modulatorRowHeightFor(leaving.lane, slot - 1) +
                                trayExtentOf(leaving.lane, slot - 1)
                          : trayExtentOf(leaving.lane);
    for (const auto& other : leaving_) {
        if (other.get() == &leaving)
            break;
        if (other->lane == leaving.lane && std::min(other->slot, count) == slot)
            offset += leavingRowHeight(*other);
    }
    return offset;
}

// Places the lane's leaving rows of `slot` from `y` down, returning the height they take. A row whose gap has closed
// (or never opened, under Reduce Motion) stays where it stood while its fade ends.
int TimelineAutomationLanes::placeLeavingRows(synth::LaneId lane, int slot, int y, int width) {
    const int count = modulatorCount(lane);
    int taken = 0;
    for (const auto& leaving : leaving_) {
        if (leaving->lane != lane || std::min(leaving->slot, count) != slot)
            continue;
        const int height = leavingRowHeight(*leaving);
        if (height > 0)
            leaving->row->setBounds(0, y + taken, width, height);
        taken += height;
    }
    return taken;
}

void TimelineAutomationLanes::placeLeavingBands(const TimelineRowLayout& layout, int scroll) {
    for (const auto& leaving : leaving_) {
        const int height = leavingRowHeight(*leaving);
        if (height <= 0)
            continue;
        const auto row = laneRowContentBounds(leaving->lane, layout);
        if (row.isEmpty())
            continue;
        const int top = row.getY() + laneRowHeight(leaving->lane) + leavingRowOffset(*leaving);
        leaving->band->setBounds(0, top - scroll, row.getWidth(), height);
    }
}

} // namespace synth::ui
