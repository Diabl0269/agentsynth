// TimelineSectionStripMotion.cpp
//
// What the Sections strip animates and when it starts each effect: a new block grows in, a deleted one shrinks away, a
// dragged one lifts and settles, and the placements an edit rewrote flash an outline. The effects themselves
// (durations, Reduce Motion, nothing running at rest) are SectionStripMotion's.

#include "AppUndoManager.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

namespace {
constexpr int kMaxGrowingBlocks = 8; // more new blocks than this at once is a load, not an edit
constexpr float kLiftRaisePx = 2.0f;
constexpr float kGhostAlpha = 0.85f;
constexpr float kDropLineWidth = 2.0f;
constexpr float kHintPadX = 7.0f;
constexpr float kHintGap = 6.0f;
constexpr float kHintPillH = 16.0f;
juce::Font hintFont() { return juce::Font(juce::FontOptions(10.5f)); }
} // namespace

// Called by refresh() after every doc notification. A placement that was not there before grows in, and one that is
// gone shrinks away from where it was drawn at the last change (unless the doc was replaced wholesale with nothing
// surviving, as a load does, or more than a handful at once: those land at once); a new stamp revision outlines the
// placements it rewrote. A placement whose start changed (an undo or redo of a move, a key move, a menu's make-room)
// glides from where it was drawn to its new start; a drop being committed skips that, since its plan's glides already
// head for the same starts. This is the one place a block leaves: every delete, cover drop, undo and redo goes
// through it, so each removed block exits exactly once. A notification between the steps of one section edit is not
// a change of its own: the edit's last step is diffed against the state before its first.
void TimelineSectionStrip::trackChangesForMotion() {
    if (doc_ == nullptr) {
        knownPlacements_.clear();
        lastGhosts_.clear();
        motionPrimed_ = false;
        return;
    }
    const auto editRevision = doc_->getSectionEditRevision();
    if (motionPrimed_ && doc_->isSectionEditUnfinished() && editRevision == lastSectionEditRevision_)
        return;
    const auto replaceCount = doc_->getReplaceCount();
    bool restored = false;
    if (undoManager_ != watchedUndo_) {
        watchedUndo_ = undoManager_;
        restoreWatch_ = RestoreWatch(undoManager_ != nullptr ? undoManager_->getRestoreSerial() : 0);
    } else if (undoManager_ != nullptr) {
        restored = restoreWatch_.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    }
    const auto stampRevision = doc_->getSectionStampRevision();
    std::map<std::int64_t, double> now;
    std::vector<synth::PlacementId> added;
    int survivors = 0;
    for (const auto& placement : doc_->getPlacements()) {
        now[placement.id.value] = placement.startBeat;
        const auto known = knownPlacements_.find(placement.id.value);
        if (known == knownPlacements_.end()) {
            added.push_back(placement.id);
            continue;
        }
        ++survivors;
        if (motionPrimed_ && !hasDragPlan_ && std::abs(known->second - placement.startBeat) > sectionstrip::kBarEpsilon)
            dropGlide_->retarget(placement.id.value,
                                 dropGlide_->drawnAt(placement.id.value, {(float)known->second, 0.0f}),
                                 {(float)placement.startBeat, 0.0f});
    }
    std::vector<std::int64_t> removed;
    for (const auto& [id, start] : knownPlacements_)
        if (now.find(id) == now.end())
            removed.push_back(id);
    // A load replaces the doc and nothing in it moves, even when its ids happen to match the old song's. An undo or
    // redo replaces it too, but most of the placements it leaves are the ones already shown, and the ones it brings
    // back or takes away move like edits.
    const bool replaced = replaceCount != lastReplaceCount_;
    const bool loaded = replaced && !restored;
    const bool grows =
        motionPrimed_ && !loaded && (!replaced || (survivors > 0 && (int)added.size() <= kMaxGrowingBlocks));
    if (grows)
        for (const auto id : added)
            motion_.startGrow(id, growFromRight_);
    growFromRight_ = false;
    const bool exits =
        motionPrimed_ && !loaded && (!replaced || survivors > 0) && (int)removed.size() <= kMaxGrowingBlocks;
    if (exits)
        for (const auto id : removed)
            if (const auto ghost = lastGhosts_.find(id); ghost != lastGhosts_.end())
                motion_.startExit(synth::PlacementId{id}, ghost->second);
    if (motionPrimed_ && stampRevision != lastStampRevision_)
        motion_.startOutline(doc_->getLastStampedPlacements());
    knownPlacements_ = std::move(now);
    lastGhosts_.clear();
    for (const auto& block : buildBlocks())
        lastGhosts_[block.id.value] = {block.bounds, block.colour, block.name};
    lastReplaceCount_ = replaceCount;
    lastStampRevision_ = stampRevision;
    lastSectionEditRevision_ = editRevision;
    motionPrimed_ = true;
}

// The lifted block (a ghost that follows the pointer at the grab offset while dragging, and glides to where it lands
// or back to where it was when it settles) and the accent line at the start a release would give it. The line, the
// gap the other blocks open and the settle's end are all the plan's landing.
void TimelineSectionStrip::paintBlockDrag(juce::Graphics& g) const {
    const float lift = motion_.lift();
    if (!blockDragging_ && lift <= 0.0f)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const bool dragging = blockDragging_ && !dragCancelled_;

    if (dragging && !dragOutside_) {
        g.setColour(colors.accent);
        g.fillRect((float)xAtBeat(dragPlan_.landing) - kDropLineWidth * 0.5f, 0.0f, kDropLineWidth, (float)getHeight());
    }

    const auto nameFont = synth::theme::AppLookAndFeel::uiSemiBoldFont(11.0f);
    const juce::Font countFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    const float alpha = blockDragging_ ? kGhostAlpha : kGhostAlpha * lift;
    const auto paintGhost = [&](const Block& block, juce::Rectangle<float> at) {
        const auto rect = at.translated(0.0f, -kLiftRaisePx * lift);
        juce::DropShadow(juce::Colours::black.withAlpha(0.35f * lift), 4, {0, 2})
            .drawForRectangle(g, rect.toNearestInt());
        paintBlock(g, block, rect, alpha, nameFont, countFont);
        g.setColour((dragCopy_ ? colors.accent : colors.border).withAlpha(alpha));
        g.drawRect(rect, 1.0f);
    };
    // The other members of a group drag ride back to back beside the pressed one, which is drawn last, on top.
    for (std::size_t i = 0; i < dragMembers_.size(); ++i)
        if (dragMembers_[i].block.id != dragBlock_.id)
            paintGhost(dragMembers_[i].block, memberGhostRect(i));
    const float x = blockDragging_ ? dragGhostX_ : dragSettleX_ + (dragGhostX_ - dragSettleX_) * lift;
    paintGhost(dragBlock_, dragBlock_.bounds.withX(x));
    if (dragging && !dragOutside_)
        paintDropHint(g, lift);
}

// What a release now would do, in words, on the side of the drop line away from the ghost. Its opacity follows the lift
// so it fades in and out with the drag; when Shift flips the mode the old words fade into the new.
void TimelineSectionStrip::updateDropHint() {
    const juce::String words = dragMakeRoom_ ? "Release to push the music later" : "Release to cover it";
    if (words == dropHint_)
        return;
    const bool swap = dropHint_.isNotEmpty() && blockDragging_;
    dropHintFrom_ = dropHint_;
    dropHint_ = words;
    if (swap)
        motion_.startHintSwap();
}

// Where the words sit: on the side of the drop line the ghost is not on (past the ghost when it straddles the line), or
// the other side when that one has no room, so they never sit on the line or on the block being dragged.
juce::Rectangle<float> TimelineSectionStrip::dropHintPillRect() const {
    const bool swapping = motion_.hintSwap() < 1.0f && dropHintFrom_.isNotEmpty();
    const auto font = hintFont();
    const auto widthOf = [&](const juce::String& s) {
        return (float)juce::GlyphArrangement::getStringWidthInt(font, s);
    };
    const float textW = std::max(widthOf(dropHint_), swapping ? widthOf(dropHintFrom_) : 0.0f);
    const float pillW = textW + 2.0f * kHintPadX;
    const float lineX = (float)xAtBeat(dragPlan_.landing);
    // A group drag's other ghosts ride beside the pressed one; the words keep clear of all of them.
    float ghostLeft = dragGhostX_;
    float ghostRight = dragGhostX_ + dragBlock_.bounds.getWidth();
    for (std::size_t i = 0; i < dragMembers_.size(); ++i)
        if (dragMembers_[i].block.id != dragBlock_.id) {
            const auto member = memberGhostRect(i);
            ghostLeft = std::min(ghostLeft, member.getX());
            ghostRight = std::max(ghostRight, member.getRight());
        }
    const bool ghostOnRight = ghostLeft + (ghostRight - ghostLeft) * 0.5f >= lineX;
    const auto fits = [&](float left) { return left >= 0.0f && left + pillW <= (float)getWidth(); };
    // Clear of both the line and the ghost: before whichever starts first, or after whichever ends last.
    const float before = std::min(lineX, ghostLeft) - kHintGap - pillW;
    const float after = std::max(lineX, ghostRight) + kHintGap;
    float left = ghostOnRight ? before : after;
    if (!fits(left))
        left = ghostOnRight ? after : before;
    left = std::clamp(left, 0.0f, std::max(0.0f, (float)getWidth() - pillW));
    return juce::Rectangle<float>(left, ((float)getHeight() - kHintPillH) * 0.5f, pillW, kHintPillH);
}

void TimelineSectionStrip::paintDropHint(juce::Graphics& g, float lift) const {
    if (dropHint_.isEmpty() || lift <= 0.0f)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const float swap = motion_.hintSwap();
    const bool swapping = swap < 1.0f && dropHintFrom_.isNotEmpty();
    const auto pill = dropHintPillRect();
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(getLocalBounds());
    g.setColour(colors.surfaceHi.withAlpha(0.95f * lift));
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    g.setColour(colors.border.withAlpha(lift));
    g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);
    g.setFont(hintFont());
    const auto text = pill.reduced(kHintPadX, 0.0f).toNearestInt();
    if (swapping) {
        g.setColour(colors.textPrimary.withAlpha(lift * (1.0f - swap)));
        g.drawText(dropHintFrom_, text, juce::Justification::centred, false);
    }
    g.setColour(colors.textPrimary.withAlpha(lift * (swapping ? swap : 1.0f)));
    g.drawText(dropHint_, text, juce::Justification::centred, false);
}

} // namespace synth::ui
