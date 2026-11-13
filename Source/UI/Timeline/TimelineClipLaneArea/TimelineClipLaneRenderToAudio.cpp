// TimelineClipLaneRenderToAudio.cpp
//
// "Render to audio track" as the clip lanes offer it: the spans a range or a clip selection stands for, the menu item
// both menus share, and the shortcut. The lane area never sees the graph, so which tracks have a mixer channel is
// asked of the host (renderableTracks) and the render itself is handed to it (onRenderToAudioRequested).

#include "TimelineClipLaneArea.h"

#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr const char* kRenderActionId = "timelineRenderToAudioTrack";

// Widens the entry for `track` in `spans` to include [start, end), adding it when the track has none yet.
void mergeSpan(std::vector<RenderSpan>& spans, synth::TrackId track, double start, double end) {
    const auto found =
        std::find_if(spans.begin(), spans.end(), [track](const RenderSpan& s) { return s.track == track; });
    if (found == spans.end()) {
        spans.push_back({track, start, end});
        return;
    }
    found->startBeat = std::min(found->startBeat, start);
    found->endBeat = std::max(found->endBeat, end);
}
} // namespace

// The live range, one span per covered track.
std::vector<RenderSpan> TimelineClipLaneArea::renderSpansForRange() const {
    std::vector<RenderSpan> spans;
    if (const auto range = getRangeSpan())
        for (const auto track : range->tracks)
            spans.push_back({track, range->startBeat, range->endBeat});
    return spans;
}

// The selected clips when `clicked` is one of several selected, else just `clicked` (a clip menu acts on what was
// right-clicked unless the click landed inside a multi-selection). Each track gets the union of its clips' spans.
std::vector<RenderSpan> TimelineClipLaneArea::renderSpansForClips(synth::ClipId clicked) const {
    std::vector<RenderSpan> spans;
    if (doc_ == nullptr)
        return spans;
    std::vector<synth::ClipId> ids = selection_.getSelected();
    if (clicked.isValid() && (ids.size() < 2 || std::find(ids.begin(), ids.end(), clicked) == ids.end()))
        ids = {clicked};
    for (const auto id : ids) {
        const auto* clip = doc_->getClip(id);
        const auto* track = clip != nullptr ? doc_->getTrackForClip(id) : nullptr;
        if (track != nullptr)
            mergeSpan(spans, track->id, clip->startBeat, clip->startBeat + clip->lengthBeats);
    }
    return spans;
}

std::vector<synth::TrackId> TimelineClipLaneArea::renderableTracksIn(const std::vector<RenderSpan>& spans) const {
    std::vector<synth::TrackId> tracks;
    for (const auto& span : spans)
        tracks.push_back(span.track);
    return renderableTracks ? renderableTracks(tracks) : std::vector<synth::TrackId>{};
}

// Offered only when a host can render; greyed, with the reason in its text, when no span's track has a channel.
void TimelineClipLaneArea::addRenderToAudioItem(juce::PopupMenu& menu, const std::vector<RenderSpan>& spans) {
    if (!onRenderToAudioRequested || spans.empty())
        return;
    menu.addSeparator();
    const bool canRender = !renderableTracksIn(spans).empty();
    juce::PopupMenu::Item item(canRender ? "Render to audio track" : "Render to audio track (no mixer channel)");
    item.setEnabled(canRender);
    const juce::KeyPress fallback('r', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    const auto bound = shortcuts_ != nullptr ? shortcuts_->getBinding(kRenderActionId) : fallback;
    if (bound.isValid())
        item.shortcutKeyDescription = bound.getTextDescriptionWithIcons();
    item.action = [this, spans] { onRenderToAudioRequested(spans); };
    menu.addItem(item);
}

// The shortcut: the live range if there is one, else the selected clips. False (the key falls through) when neither.
bool TimelineClipLaneArea::renderSelectionToAudio() {
    if (!onRenderToAudioRequested)
        return false;
    auto spans = renderSpansForRange();
    if (spans.empty())
        spans = renderSpansForClips({});
    if (spans.empty())
        return false;
    onRenderToAudioRequested(spans);
    return true;
}

bool TimelineClipLaneArea::matchesRenderToAudioKey(const juce::KeyPress& key) const {
    const juce::KeyPress fallback('r', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    return matchesClipAction(key, kRenderActionId, fallback);
}

} // namespace synth::ui
