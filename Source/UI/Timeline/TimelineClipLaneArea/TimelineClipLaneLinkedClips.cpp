// TimelineClipLaneLinkedClips.cpp
//
// Linked clips in the clip lane: the linked duplicate of the selection (the rebindable
// "timelineClipDuplicateLinked" key), the link mark a linked clip paints in its name strip and that
// mark's tooltip. The model half (what a link shares, how edits reach the group) is
// synth::TimelineDoc's, in TimelineDocLinkedClips.cpp. TimelineClipLaneArea is declared in
// TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files hold the rest of the class.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"

#include "AppUndoManager.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kLinkMarkSize = 10;
constexpr int kLinkMarkInset = 3;
} // namespace

using namespace detail;

// Top-right corner of the clip, in the strip the name is drawn in. A clip too narrow to show its
// name has no mark either: the screen-reader text still says it is linked.
juce::Rectangle<int> TimelineClipLaneArea::linkMarkBounds(juce::Rectangle<int> clipRect) {
    if (clipRect.getWidth() <= kMinWidthForName || clipRect.getHeight() < kLinkMarkSize + 2 * kLinkMarkInset)
        return {};
    return {clipRect.getRight() - kLinkMarkInset - kLinkMarkSize, clipRect.getY() + kLinkMarkInset, kLinkMarkSize,
            kLinkMarkSize};
}

// Two interlocked rings (a chain link), drawn as paths so no glyph or font is involved. Fixed
// black like the clip's name label, for the same reason: it sits on the track's own hue.
void TimelineClipLaneArea::paintLinkMark(juce::Graphics& g, juce::Rectangle<int> clipRect, bool dimmed) const {
    const auto bounds = linkMarkBounds(clipRect).toFloat();
    if (bounds.isEmpty())
        return;
    const float ringW = bounds.getWidth() * 0.62f;
    const float ringH = bounds.getHeight() * 0.42f;
    const juce::Rectangle<float> first(bounds.getX(), bounds.getY(), ringW, ringH);
    const juce::Rectangle<float> second(bounds.getRight() - ringW, bounds.getBottom() - ringH, ringW, ringH);
    g.setColour(juce::Colours::black.withAlpha(dimmed ? 0.35f : 0.8f));
    g.drawRoundedRectangle(first, ringH * 0.5f, 1.4f);
    g.drawRoundedRectangle(second, ringH * 0.5f, 1.4f);
}

juce::String TimelineClipLaneArea::getTooltip() { return linkTooltipAt(getMouseXYRelative()); }

juce::String TimelineClipLaneArea::linkTooltipAt(juce::Point<int> pos) const {
    if (doc_ == nullptr)
        return {};
    const auto hit = hitTestClip(pos);
    if (!hit.has_value() || !linkMarkBounds(hit->rect).contains(pos))
        return {};
    const int others = doc_->countLinkedClips(hit->id);
    if (others <= 0)
        return {};
    return "Linked with " + juce::String(others) + (others == 1 ? " other clip" : " other clips") +
           ": editing the notes of one changes them all. Right-click > Make unique to edit this one alone.";
}

// Duplicate's tiling (TimelinePanelComponent::repeatSelectedClips), with linked copies: the copies
// land as ONE block after the selection's end, so two adjacent clips are not covered by each other's
// copy, and each copy is linked to its own source. Audio clips are skipped (they never link).
bool TimelineClipLaneArea::duplicateLinkedSelection() {
    if (doc_ == nullptr)
        return false;

    struct Source {
        synth::ClipId id;
        double startBeat = 0.0;
    };
    std::vector<Source> sources;
    double spanStart = 0.0, spanEnd = 0.0;
    for (auto id : selection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr || clip->assetRef.isNotEmpty())
            continue;
        const double end = clip->startBeat + clip->lengthBeats;
        spanStart = sources.empty() ? clip->startBeat : std::min(spanStart, clip->startBeat);
        spanEnd = sources.empty() ? end : std::max(spanEnd, end);
        sources.push_back({id, clip->startBeat});
    }
    if (sources.empty())
        return false;

    const double blockLength = spanEnd - spanStart;
    std::vector<synth::ClipId> newIds;
    auto mutate = [this, &sources, blockLength, &newIds] {
        for (const auto& source : sources) {
            const auto dup = doc_->duplicateClipLinked(source.id);
            if (!dup.isValid())
                continue;
            doc_->moveClip(dup, source.startBeat + blockLength);
            newIds.push_back(dup);
        }
    };
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;
    clearRange(); // a selection never coexists with a range
    selection_.setSelection(newIds);
    repaint();
    return true;
}

} // namespace synth::ui
