// TimelineSectionStripMotion.cpp
//
// What the Sections strip animates and when it starts each effect: a new block grows in, a deleted one shrinks away, a
// dragged one lifts and settles, and the placements an edit rewrote flash an outline. The effects themselves
// (durations, Reduce Motion, nothing running at rest) are SectionStripMotion's.

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

namespace {
constexpr int kMaxGrowingBlocks = 8; // more new blocks than this at once is a load, not an edit
constexpr float kLiftRaisePx = 2.0f;
constexpr float kGhostAlpha = 0.85f;
constexpr float kDropLineWidth = 2.0f;
} // namespace

// Called by refresh() after every doc notification. A placement that was not there before grows in (unless the doc
// was replaced wholesale, as a load or an undo does: those land at once); a new stamp revision outlines the
// placements it rewrote.
void TimelineSectionStrip::trackChangesForMotion() {
    if (doc_ == nullptr) {
        knownPlacements_.clear();
        motionPrimed_ = false;
        return;
    }
    const auto replaceCount = doc_->getReplaceCount();
    const auto stampRevision = doc_->getSectionStampRevision();
    std::set<std::int64_t> now;
    std::vector<synth::PlacementId> added;
    int survivors = 0;
    for (const auto& placement : doc_->getPlacements()) {
        now.insert(placement.id.value);
        if (knownPlacements_.count(placement.id.value) == 0)
            added.push_back(placement.id);
        else
            ++survivors;
    }
    // A load replaces the doc: nearly every id is new and nothing grows. An undo or redo replaces it too, but most of
    // the placements it leaves are the ones already shown, and the one it brings back grows like a new one.
    const bool replaced = replaceCount != lastReplaceCount_;
    const bool grows = motionPrimed_ && (!replaced || (survivors > 0 && (int)added.size() <= kMaxGrowingBlocks));
    if (grows) {
        for (const auto id : added)
            motion_.startGrow(id, growFromRight_);
        growFromRight_ = false;
    }
    if (motionPrimed_ && stampRevision != lastStampRevision_)
        motion_.startOutline(doc_->getLastStampedPlacements());
    knownPlacements_ = std::move(now);
    lastReplaceCount_ = replaceCount;
    lastStampRevision_ = stampRevision;
    motionPrimed_ = true;
}

std::optional<SectionStripMotion::Ghost> TimelineSectionStrip::ghostOf(synth::PlacementId id) const {
    for (const auto& block : buildBlocks())
        if (block.id == id)
            return SectionStripMotion::Ghost{block.bounds, block.colour, block.name};
    return std::nullopt;
}

// The lifted block (a ghost that follows the pointer at the grab offset while dragging, and glides to where it lands
// or back to where it was when it settles) and the accent line where a release would drop it.
void TimelineSectionStrip::paintBlockDrag(juce::Graphics& g) const {
    const float lift = motion_.lift();
    if (!blockDragging_ && lift <= 0.0f)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const bool dragging = blockDragging_ && !dragCancelled_;

    if (dragging && !dragOutside_) {
        g.setColour(colors.accent);
        g.fillRect((float)xAtBeat(dragTargetBeat_) - kDropLineWidth * 0.5f, 0.0f, kDropLineWidth, (float)getHeight());
    }

    const float x = blockDragging_ ? dragGhostX_ : dragSettleX_ + (dragGhostX_ - dragSettleX_) * lift;
    auto rect = dragBlock_.bounds.withX(x).translated(0.0f, -kLiftRaisePx * lift);
    juce::DropShadow(juce::Colours::black.withAlpha(0.35f * lift), 4, {0, 2}).drawForRectangle(g, rect.toNearestInt());
    const auto nameFont = synth::theme::AppLookAndFeel::uiSemiBoldFont(11.0f);
    const juce::Font countFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    const float alpha = blockDragging_ ? kGhostAlpha : kGhostAlpha * lift;
    paintBlock(g, dragBlock_, rect, alpha, nameFont, countFont);
    g.setColour((dragCopy_ ? colors.accent : colors.border).withAlpha(alpha));
    g.drawRect(rect, 1.0f);
}

} // namespace synth::ui
