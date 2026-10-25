// TimelineClipLaneSizing.cpp
//
// Clip edge sizing during a drag: which sizing mode the drag uses (the Sizing field's, or Stretch while Option is
// held), the preview of the dragged clip and of its linked copies, and the readout drawn on the clip. The preview
// is synth::clipEdgeResult, the same function TimelineDoc::resizeClipEdge commits with. TimelineClipLaneArea is
// declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files in this directory hold the rest.

#include "TimelineClipLaneArea.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kReadoutFontHeight = 11.0f;
constexpr int kReadoutPaddingPx = 4;
} // namespace

synth::ClipSizingMode TimelineClipLaneArea::dragSizingMode() const noexcept {
    return stretchOnce_ ? synth::ClipSizingMode::Stretch : clipSizingMode_;
}

// The dragged clip itself, and every clip linked to it: a linked group shares notes and length, so the drop changes
// them all alike. The one exception is a left-edge Trim, which takes the dragged clip out of its group and leaves
// the others untouched (see TimelineDoc::resizeClipEdge).
std::optional<synth::ClipEdgeResult> TimelineClipLaneArea::resizePreviewFor(const synth::Clip& clip) const {
    const bool left = dragMode_ == DragMode::ResizeLeft;
    if ((!left && dragMode_ != DragMode::ResizeRight) || doc_ == nullptr)
        return std::nullopt;
    const auto* active = doc_->getClip(activeClip_);
    if (active == nullptr)
        return std::nullopt;
    const auto edge = left ? synth::ClipEdge::Left : synth::ClipEdge::Right;
    auto result = synth::clipEdgeResult(*active, edge, dragSizingMode(), left ? previewStart_ : active->startBeat,
                                        previewLength_);
    if (clip.id == active->id)
        return result;

    const bool leavesGroup =
        left && synth::effectiveSizingMode(*active, dragSizingMode()) == synth::ClipSizingMode::Trim;
    if (clip.linkGroup == 0 || clip.linkGroup != active->linkGroup || leavesGroup)
        return std::nullopt;
    result.startBeat = clip.startBeat; // a copy keeps its own place; only the shared content follows
    return result;
}

// Only Stretch says anything: the new length as a share of the old one, or, for an audio clip, why it trims instead.
juce::String TimelineClipLaneArea::sizingReadout() const {
    if (!isDragInProgress() || dragMode_ == DragMode::Move || dragSizingMode() != synth::ClipSizingMode::Stretch ||
        doc_ == nullptr)
        return {};
    const auto* clip = doc_->getClip(activeClip_);
    if (clip == nullptr || resizeOriginalLength_ <= 0.0)
        return {};
    if (clip->assetRef.isNotEmpty())
        return "Audio trims: no stretch yet";
    return juce::String(juce::roundToInt(previewLength_ / resizeOriginalLength_ * 100.0)) + "%";
}

// A small dark pill at the clip's top right. Fixed black and white rather than theme tokens, like the clip's own
// label: it sits on the clip's arbitrary track hue, whatever the app theme. Dropped when the clip is too narrow.
void TimelineClipLaneArea::paintSizingReadout(juce::Graphics& g, juce::Rectangle<int> clipRect) const {
    const auto text = sizingReadout();
    if (text.isEmpty())
        return;
    const juce::Font font{juce::FontOptions(kReadoutFontHeight)};
    const int width = (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, text)) + 2 * kReadoutPaddingPx;
    const int height = (int)kReadoutFontHeight + 4;
    if (width + 4 > clipRect.getWidth() || height + 4 > clipRect.getHeight())
        return;
    const auto pill = juce::Rectangle<int>(clipRect.getRight() - width - 2, clipRect.getY() + 2, width, height);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(pill.toFloat(), 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(text, pill, juce::Justification::centred, false);
}

} // namespace synth::ui
