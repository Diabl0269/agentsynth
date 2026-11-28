// TimelineSectionStripMotion.cpp
//
// What the Sections strip animates and when it starts each effect: a new block grows in, a deleted one shrinks away, a
// dragged one lifts and settles, and the placements an edit rewrote flash an outline. The effects themselves
// (durations, Reduce Motion, nothing running at rest) are SectionStripMotion's.

#include "AppUndoManager.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropHintLook.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropWords.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kMaxGrowingBlocks = 8; // more new blocks than this at once is a load, not an edit
constexpr float kLiftRaisePx = 2.0f;
constexpr float kGhostAlpha = 0.85f;
constexpr float kDropLineWidth = 2.0f;
constexpr float kHintPadX = 7.0f;
using sectionstrip::kHintGap;
using sectionstrip::kHintPillH;
constexpr float kHintBand = kHintPillH + kHintGap + 2.0f; // the room the overlay keeps above and below the strip
juce::Font hintFont() { return juce::Font(juce::FontOptions(10.5f)); }
} // namespace

// Called by refresh() after every doc notification. A placement that was not there before grows in, and one that is
// gone shrinks away from where it was drawn at the last change (unless the doc was replaced wholesale with nothing
// surviving, as a load does, or more than a handful at once: those land at once); a new stamp revision outlines the
// placements it rewrote. A placement whose start changed (an undo or redo of a move, a key move, a menu's make-room)
// glides from where it was drawn to its new start; a drop being committed skips that, since its plan's glides already
// head for the same starts. This is the one place a block leaves: every delete, cover drop, undo and redo goes
// through it, so each removed block exits exactly once. A notification between the steps of one section edit is not
// a change of its own: the edit's last step is diffed against the state before its first. Each block grows from and
// shrinks toward the origin SectionMotionOrigins gives it (the clip lane asks its own copy the same question, so the
// clips inside a block move with it), and when blocks leave, the blocks closing up behind them glide over the exit's
// own time, so a shrinking copy's far edge and the neighbour coming back stay one line.
juce::Colour TimelineSectionStrip::getDrawnSectionColour(synth::SectionId id) const {
    const auto* section = doc_ != nullptr ? doc_->getSection(id) : nullptr;
    return section != nullptr ? colourGlide_.drawn(id.value, juce::Colour(section->colourArgb)) : juce::Colour();
}

// An undo or redo eases a section's colour (blocks and lane edge lines) from the one drawn to the restored one; a
// picker preview or any other edit lands at once. A section that is gone is forgotten.
void TimelineSectionStrip::syncColourGlide(bool restored) {
    std::map<std::int64_t, juce::Colour> now;
    for (const auto& section : doc_->getSections()) {
        const juce::Colour colour(section.colourArgb);
        const auto known = knownColours_.find(section.id.value);
        if (known != knownColours_.end())
            colourGlide_.note(section.id.value, known->second, colour, restored);
        now[section.id.value] = colour;
    }
    for (const auto& [id, colour] : knownColours_)
        if (now.find(id) == now.end())
            colourGlide_.forget(id);
    knownColours_ = std::move(now);
}

void TimelineSectionStrip::trackChangesForMotion() {
    if (doc_ == nullptr) {
        knownColours_.clear();
        knownPlacements_.clear();
        knownSpans_.clear();
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
    syncColourGlide(colourGlide_.takeRestore());
    const auto stampRevision = doc_->getSectionStampRevision();
    std::map<std::int64_t, double> now;
    for (const auto& placement : doc_->getPlacements())
        now[placement.id.value] = placement.startBeat;
    std::vector<std::int64_t> removed;
    for (const auto& [id, start] : knownPlacements_)
        if (now.find(id) == now.end())
            removed.push_back(id);
    const double glideMs = removed.empty() ? KeyedPointTween::kMs : SectionStripMotion::kExitMs;
    std::vector<synth::PlacementId> added;
    int survivors = 0;
    for (const auto& placement : doc_->getPlacements()) {
        const auto known = knownPlacements_.find(placement.id.value);
        if (known == knownPlacements_.end()) {
            added.push_back(placement.id);
            continue;
        }
        ++survivors;
        // A section made longer (an Option drop or paste) or put back by an undo: the block's right edge glides to its
        // new length, the same time and curve as a block gliding to a new start.
        if (motionPrimed_ && !(replaceCount != lastReplaceCount_ && !restored)) { // never for a load
            const auto before = std::find_if(knownSpans_.begin(), knownSpans_.end(),
                                             [&](const auto& span) { return span.id == placement.id.value; });
            const auto* section = doc_->getSection(placement.section);
            if (before != knownSpans_.end() && section != nullptr) {
                const double was = before->end - before->start;
                if (std::abs(was - section->lengthBeats) > sectionstrip::kBarEpsilon)
                    lengthGlide_->retarget(placement.id.value,
                                           lengthGlide_->drawnAt(placement.id.value, {(float)was, 0.0f}),
                                           {(float)section->lengthBeats, 0.0f}, false, glideMs);
            }
        }
        if (motionPrimed_ && !hasDragPlan_ && std::abs(known->second - placement.startBeat) > sectionstrip::kBarEpsilon)
            dropGlide_->retarget(placement.id.value,
                                 dropGlide_->drawnAt(placement.id.value, {(float)known->second, 0.0f}),
                                 {(float)placement.startBeat, 0.0f}, false, glideMs);
    }
    // A load replaces the doc and nothing in it moves, even when its ids happen to match the old song's. An undo or
    // redo replaces it too, but most of the placements it leaves are the ones already shown, and the ones it brings
    // back or takes away move like edits.
    const bool replaced = replaceCount != lastReplaceCount_;
    const bool loaded = replaced && !restored;
    const bool grows =
        motionPrimed_ && !loaded && (!replaced || (survivors > 0 && (int)added.size() <= kMaxGrowingBlocks));
    auto spans = placementSpansOf(*doc_);
    SectionMotionOrigins::Moves moves;
    if (loaded)
        origins_.clear();
    else if (motionPrimed_)
        moves = origins_.diff(knownSpans_, spans, restored, makeOrigin_);
    makeOrigin_.reset();
    if (grows)
        for (const auto id : added) {
            const auto anchor = moves.grows.find(id.value);
            if (anchor != moves.grows.end())
                motion_.startGrow(id, anchor->second.origin, anchor->second.beat);
            else
                motion_.startGrow(id, MotionOrigin::Left, now[id.value]);
        }
    const bool exits =
        motionPrimed_ && !loaded && (!replaced || survivors > 0) && (int)removed.size() <= kMaxGrowingBlocks;
    if (exits)
        for (const auto id : removed)
            if (const auto ghost = lastGhosts_.find(id); ghost != lastGhosts_.end()) {
                const auto anchor = moves.exits.find(id);
                if (anchor != moves.exits.end())
                    motion_.startExit(synth::PlacementId{id}, ghost->second, anchor->second.origin,
                                      (float)xAtBeat(anchor->second.beat));
                else
                    motion_.startExit(synth::PlacementId{id}, ghost->second, MotionOrigin::Centre,
                                      ghost->second.bounds.getCentreX());
            }
    if (motionPrimed_ && stampRevision != lastStampRevision_)
        motion_.startOutline(doc_->getLastStampedPlacements());
    knownPlacements_ = std::move(now);
    knownSpans_ = std::move(spans);
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
// A dragged ghost is see-through; a settling one firms up as it lands, so the block it hands over to is the same
// picture (fading it out instead left the dropped block blank for the settle, then popped it in).
float TimelineSectionStrip::dragGhostAlpha() const {
    return blockDragging_ ? kGhostAlpha : 1.0f - (1.0f - kGhostAlpha) * motion_.lift();
}

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
    const float alpha = dragGhostAlpha();
    // While it settles, the ghost turns into the block it hands over to: the lifted look (shadow, raise, outline) goes
    // with the lift, and the at-rest look (the selection outline, the focus ring on the anchor) comes in with it, so
    // the frame where the real block takes over is the same picture.
    const float rest = blockDragging_ ? 0.0f : 1.0f - lift;
    const bool landsSelected = !settlingIds_.empty();
    // The ring goes on the ghost of the block that will be the anchor: a copy's anchor is the copy of the pressed
    // block.
    const auto anchorId = getSelectedPlacement();
    const auto isAnchor = [&](const Block& b) {
        return dragCopy_ && landsSelected ? b.id == dragBlock_.id : b.id == anchorId;
    };
    const auto paintGhost = [&](const Block& block, juce::Rectangle<float> at, bool anchor) {
        const auto rect = at.translated(0.0f, -kLiftRaisePx * lift);
        juce::DropShadow(juce::Colours::black.withAlpha(0.35f * lift), 4, {0, 2})
            .drawForRectangle(g, rect.toNearestInt());
        paintBlock(g, block, rect, alpha, nameFont, countFont);
        g.setColour((dragCopy_ ? colors.accent : colors.border).withAlpha(alpha * (blockDragging_ ? 1.0f : lift)));
        g.drawRect(rect, 1.0f);
        if (rest <= 0.0f || !(landsSelected || isPlacementSelected(block.id)))
            return;
        g.beginTransparencyLayer(rest);
        g.setColour(colors.accent);
        g.drawRect(rect.expanded(1.0f), 2.0f);
        if (anchor)
            paintFocusRing(g, rect.expanded(3.0f), *this);
        g.endTransparencyLayer();
    };
    // The other members of a group drag ride beside the pressed one, which is drawn last, on top while it is held.
    // Once let go they are drawn left to right like the real blocks, so the anchor's ring sits under its right-hand
    // neighbour on both sides of the handover.
    const float x = blockDragging_ ? dragGhostX_ : dragSettleX_ + (dragGhostX_ - dragSettleX_) * lift;
    std::vector<std::pair<const Block*, juce::Rectangle<float>>> ghosts;
    for (std::size_t i = 0; i < dragMembers_.size(); ++i)
        if (dragMembers_[i].block.id != dragBlock_.id)
            ghosts.emplace_back(&dragMembers_[i].block, memberGhostRect(i));
    ghosts.emplace_back(&dragBlock_, dragBlock_.bounds.withX(x));
    if (!blockDragging_)
        std::stable_sort(ghosts.begin(), ghosts.end(),
                         [](const auto& a, const auto& b) { return a.second.getX() < b.second.getX(); });
    for (const auto& [block, at] : ghosts)
        paintGhost(*block, at, isAnchor(*block));
}

// What a release now would do, in words, in a pill under the strip (above it when the window has no room below), at the
// drop line's x, in the theme's inverse colours so it never reads as a section block. Its opacity follows the lift so
// it fades in and out with the drag; when Shift flips the mode the old words fade into the new. The pill never sits on
// a section, so no block, ghost or drop line is ever hidden by it.
void TimelineSectionStrip::updateDropHint() {
    const juce::String words =
        SectionDropWords::hint(dragMakeRoom_, dragCopy_, dropMovesOthers(), (int)dragPlan_.carried.size());
    if (words != dropHint_) {
        const bool swap = dropHint_.isNotEmpty() && blockDragging_;
        dropHintFrom_ = dropHint_;
        dropHint_ = words;
        if (swap)
            motion_.startHintSwap();
    }
    syncDropHintOverlay();
}

// The blocks as the drop leaves them, which the words must not cover: every other block at the start the plan gives it
// (a covered one stays where it is, dimmed), as the strip draws it.
std::vector<juce::Rectangle<float>> TimelineSectionStrip::plannedBlockRects() const {
    std::vector<juce::Rectangle<float>> rects;
    if (doc_ == nullptr)
        return rects;
    for (const auto& other : dragPlan_.others) {
        const auto block = blockOf(synth::PlacementId{other.id});
        if (!block)
            continue;
        const double start = other.covered ? block->startBeat : other.start;
        rects.push_back(
            sectionstrip::blockRect((float)xAtBeat(start), (float)xAtBeat(start + block->lengthBeats), getHeight()));
    }
    return rects;
}

// Where the words sit, in strip coordinates: centred on the drop line and kept inside the strip's width, in the band
// just below the strip, or just above it when the window ends before the pill would fit below. Sized by the longest
// phrase this drag can show, so a Shift cross-fade never resizes it.
juce::Rectangle<float> TimelineSectionStrip::dropHintPillRect() const {
    const bool swapping = motion_.hintSwap() < 1.0f && dropHintFrom_.isNotEmpty();
    const auto font = hintFont();
    const auto widthOf = [&](const juce::String& s) {
        return (float)juce::GlyphArrangement::getStringWidthInt(font, s);
    };
    float textW = std::max(widthOf(dropHint_), swapping ? widthOf(dropHintFrom_) : 0.0f);
    for (const auto& phrase : SectionDropWords::hintsFor(dragCopy_, dragBetweenCount()))
        textW = std::max(textW, widthOf(phrase));
    bool below = true;
    if (const auto* top = getTopLevelComponent(); top != nullptr && top != this) {
        const float bottom = top->getLocalPoint(this, juce::Point<float>(0.0f, (float)getHeight())).y;
        below = bottom + kHintGap + kHintPillH <= (float)top->getHeight();
    }
    return sectionstrip::dropHintPillRect((float)xAtBeat(dragPlan_.landing), textW + 2.0f * kHintPadX,
                                          (float)getWidth(), getHeight(), below);
}

// The click-through layer the pill is painted on: the strip clips its own children and paint to its bounds, and the
// pill lives outside them. A child of the window's top-level component, sized to the strip's width and the bands above
// and below it, so it needs no hit-testing of its own.
class SectionDropHintOverlay : public juce::Component {
public:
    explicit SectionDropHintOverlay(std::function<void(juce::Graphics&)> painter)
        : painter_(std::move(painter)) {
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    void paint(juce::Graphics& g) override { painter_(g); }

private:
    std::function<void(juce::Graphics&)> painter_;
};

void TimelineSectionStrip::syncDropHintOverlay() {
    auto* top = getTopLevelComponent();
    const bool want =
        top != nullptr && top != this && dropHint_.isNotEmpty() && (dropHintShown() || motion_.lift() > 0.0f);
    if (!want) {
        if (dropHintOverlay_ != nullptr)
            dropHintOverlay_->setVisible(false);
        return;
    }
    if (dropHintOverlay_ == nullptr)
        dropHintOverlay_ = std::make_unique<SectionDropHintOverlay>([this](juce::Graphics& g) {
            g.addTransform(juce::AffineTransform::translation(0.0f, kHintBand));
            paintDropHint(g, motion_.lift());
        });
    if (dropHintOverlay_->getParentComponent() != top)
        top->addChildComponent(*dropHintOverlay_);
    const juce::Rectangle<float> region(0.0f, -kHintBand, (float)getWidth(), (float)getHeight() + 2.0f * kHintBand);
    dropHintOverlay_->setBounds(top->getLocalArea(this, region.toNearestInt()));
    dropHintOverlay_->setVisible(true);
    dropHintOverlay_->toFront(false);
    dropHintOverlay_->repaint();
}

void TimelineSectionStrip::paintDropHint(juce::Graphics& g, float lift) const {
    if (dropHint_.isEmpty() || lift <= 0.0f)
        return;
    const auto look = sectionstrip::dropHintColours(synth::theme::themeOf(*this).colors);
    const float swap = motion_.hintSwap();
    const bool swapping = swap < 1.0f && dropHintFrom_.isNotEmpty();
    const auto pill = dropHintPillRect();
    g.setFont(hintFont());
    g.setColour(look.fill.withAlpha(lift));
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    g.setColour(look.rim.withAlpha(lift));
    g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);
    const auto text = pill.reduced(kHintPadX, 0.0f).toNearestInt();
    if (swapping) {
        g.setColour(look.text.withAlpha(lift * (1.0f - swap)));
        g.drawText(dropHintFrom_, text, juce::Justification::centred, false);
    }
    g.setColour(look.text.withAlpha(lift * (swapping ? swap : 1.0f)));
    g.drawText(dropHint_, text, juce::Justification::centred, false);
}

} // namespace synth::ui
