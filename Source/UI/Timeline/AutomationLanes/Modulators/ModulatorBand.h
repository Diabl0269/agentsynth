#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService;
}

namespace synth::ui {

// What a drag on a band does. The band follows the timeline's edit tool like the lane editors do:
// Draw turns the dragged span on, Erase turns it off, and every other tool is the pointer (move a
// block, drag an edge to resize it, click to select).
enum class SectionTool { Select, Draw, Erase };

constexpr SectionTool sectionToolFor(EditTool tool) noexcept {
    switch (tool) {
    case EditTool::Draw:
        return SectionTool::Draw;
    case EditTool::Erase:
        return SectionTool::Erase;
    default:
        return SectionTool::Select;
    }
}

// The lanes-region half of a modulator row: where along the song the modulator is on. An LFO's band
// shows its sections as blocks in the mod-wire colour and edits them (ModulatorSections.h: the blocks
// live in an ordinary lane on the LFO's level parameter); with no sections lane the whole band is the
// faint "on everywhere" fill. Any other source's band is a decoration that takes no clicks, so the clip
// lanes underneath still decide.
//
// Reads its blocks from the doc on every paint and keeps none of its own, apart from a drag's preview:
// like AutomationLaneEditor, a gesture is previewed locally and written ONCE on mouse-up, as one doc
// mutation inside one undo step. The x mapping is the shared TimelineViewState, so blocks line up with
// the clip lanes and the playhead pixel for pixel. Message thread only.
class ModulatorBand
    : public juce::Component
    , public juce::TooltipClient {
public:
    static constexpr float kBandAlpha = 0.28f;
    static constexpr int kEdgeHitPx = 6; // either side of a block's edge, where a Select drag resizes

    explicit ModulatorBand(TimelineViewState& viewState);

    // Non-owning, null-safe, like every other timeline sub-component's.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    void setUndoManager(AppUndoManager* undo) noexcept { undo_ = undo; }
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    void setEditTool(EditTool tool) noexcept { tool_ = sectionToolFor(tool); }

    /** The routing this band stands for, under `ownerLane` (whose track a new sections lane goes on). */
    void setModulator(const ModulatorInfo& info, synth::LaneId ownerLane, const juce::String& parameterName);
    /** Re-reads the doc: the accessible description, the selection, the picture. Call on every doc change. */
    void refreshFromDoc();

    juce::Colour getBandColour() const noexcept { return info_.colour; }
    /** True for an LFO's band; any other source's band is a plain decoration. */
    bool isEditable() const noexcept { return info_.isLfo; }
    /** The blocks as they are in the doc now (empty with no sections lane). */
    SectionBlocks currentBlocks() const;
    bool hasSectionsLane() const;
    /** The start beat of the selected block, if any. */
    std::optional<double> getSelectedStart() const noexcept { return selectedStart_; }
    bool isDragActive() const noexcept { return drag_ != Drag::None; }

    juce::String getTooltip() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    enum class Drag { None, Paint, Erase, ResizeStart, ResizeEnd, Move };

    struct EdgeHit {
        int index = -1;
        bool startEdge = true;
    };

    double beatsPerBar() const;
    double snappedBeat(double rawBeat) const;
    double snappedBeatAt(int x) const;
    double minBlockLength() const;
    std::optional<EdgeHit> hitEdge(const SectionBlocks& blocks, int x) const;
    synth::TrackId ownerTrack() const;
    void applyNames();

    // Gestures (ModulatorBandGestures.cpp)
    void beginSelectDrag(int x);
    void updatePreview(int x);
    void finishDrag();
    void cancelDrag();
    /** Writes `blocks` as one undo step and selects the block containing `selectBeat`, if any. */
    void commit(SectionBlocks blocks, std::optional<double> selectBeat);
    void addBarAt(double start);

    // Keyboard (ModulatorBandKeys.cpp)
    bool moveSelection(int direction);
    bool removeSelected();
    bool addBarAtPlayhead();

    // Painting
    void paintBlock(juce::Graphics& g, const SectionBlock& block, bool selected);

    TimelineViewState& viewState_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undo_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    SectionTool tool_ = SectionTool::Select;

    ModulatorInfo info_;
    synth::LaneId ownerLane_;
    juce::String parameterName_;

    std::optional<double> selectedStart_;
    bool committing_ = false; // a write is in flight: the doc is briefly between its two halves (lane, then points)
    Drag drag_ = Drag::None;
    SectionBlocks original_;               // what the drag edits: the blocks when it began
    std::optional<SectionBlocks> preview_; // what the drag would write; painted instead of the doc's
    double downBeat_ = 0.0;                // Draw/Erase: the snapped beat the press landed on
    int dragIndex_ = -1;                   // Resize/Move: the block being dragged
    double grabOffset_ = 0.0;              // Move: raw beats from the block's start to the press
    double selectBeat_ = 0.0;              // a beat inside the dragged block, to reselect it on release

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorBand)
};

} // namespace synth::ui
