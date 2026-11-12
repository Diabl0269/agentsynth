// TimelineClipLaneArea -- a live clip drag (an edge or a whole clip) with snap on: the preview jumps from grid line to
// grid line, and the drawn clip, with the notes inside it, glides onto each new line through clipGlide_, the tween an
// undo or redo uses. The preview (what hit tests and the drop read) is final at once; only the drawing trails it.
// The class is declared in TimelineClipLaneArea.h.

#include "TimelineClipLaneArea.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include <cmath>
#include <map>

namespace synth::ui {

namespace {

// With snap off the preview moves a pixel or less per pointer event; easing each of those would only lag the clip
// behind the pointer, so a step has to be a real jump to glide.
constexpr float kMinGlideStepPx = 1.0f;

bool isJump(juce::Rectangle<float> from, juce::Rectangle<float> to) {
    return std::abs(from.getX() - to.getX()) >= kMinGlideStepPx ||
           std::abs(from.getRight() - to.getRight()) >= kMinGlideStepPx ||
           std::abs(from.getY() - to.getY()) >= kMinGlideStepPx ||
           std::abs(from.getBottom() - to.getBottom()) >= kMinGlideStepPx;
}

synth::ClipNoteGlide noteGlideFor(synth::ClipSizingMode mode) {
    switch (mode) {
    case synth::ClipSizingMode::Trim:
        return synth::ClipNoteGlide::Stay;
    case synth::ClipSizingMode::CarryContents:
        return synth::ClipNoteGlide::Shift;
    case synth::ClipSizingMode::Stretch:
        return synth::ClipNoteGlide::Scale;
    }
    return synth::ClipNoteGlide::Stay;
}

} // namespace

// Moves the preview (stepDragPreview), then retargets the tween of every clip the drag shows moving whose resting
// rectangle jumped: it is drawn where it is drawn now (so a step mid-glide continues, never restarts from the line)
// and eases to the new rectangle. A move drag shifts its notes with the clip; an edge drag moves them the way the
// sizing rule does (Trim stays on its beats, Carry follows the left edge, Stretch scales). An Option copy-drag
// moves ghosts, not the clips, so nothing glides.
void TimelineClipLaneArea::updateDragPreviewFromLastPointer() {
    if (dragMode_ == DragMode::None || doc_ == nullptr)
        return;
    const auto restRects = [this] {
        std::map<std::int64_t, juce::Rectangle<float>> rects;
        const auto layout = getRowLayout();
        const auto& tracks = doc_->getTracks();
        for (int row = 0; row < (int)tracks.size(); ++row)
            for (const auto& clip : tracks[(size_t)row].clips) {
                bool shown = resizePreviewFor(clip).has_value();
                if (dragMode_ == DragMode::Move && !copyDrag_)
                    for (const auto& origin : dragClips_)
                        shown = shown || origin.id == clip.id;
                if (!shown)
                    continue;
                const auto geometry = effectiveGeometryFor(clip);
                rects[clip.id.value] =
                    computeClipRect(viewState_, layout, effectiveRowFor(clip.id, row), geometry.start, geometry.length)
                        .toFloat();
            }
        return rects;
    };
    const auto before = restRects();
    stepDragPreview();
    const auto noteGlide = dragMode_ == DragMode::Move ? synth::ClipNoteGlide::Shift : noteGlideFor(dragSizingMode());
    for (const auto& [id, rest] : restRects()) {
        const auto was = before.find(id);
        if (was == before.end() || !isJump(was->second, rest))
            continue;
        noteGlide_[id] = noteGlide;
        clipGlide_->retarget(id, clipGlide_->drawnAt(id, was->second), rest);
    }
}

} // namespace synth::ui
