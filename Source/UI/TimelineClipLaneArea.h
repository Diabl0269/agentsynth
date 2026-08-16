#pragma once

#include "../Timeline/PeaksFile.h"
#include "../Timeline/TimelineDoc.h"
#include "ClipSelectionModel.h"
#include "TimelineViewState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class RecordTapModule; // Forward declaration (Source/Modules/RecordTapModule.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// TimelineClipLaneArea — TL5-7: the clip-lane region of the timeline panel (below the ruler,
// filling TimelinePanelComponent::getLanesBounds() minus the ruler strip), with drag/trim/split/
// duplicate and marquee selection.
//
// Deliberately NOT a TimelineDoc::Listener itself: TimelinePanelComponent is already the doc's one
// listener (TL5-3), and its timelineChanged() routes a refreshFromDoc() call in here — the same
// "one listener, several owners react" shape the header column uses. setTimelineDoc() only stores
// the pointer and runs that same refresh once, so constructing this directly against a doc (as the
// tests below do, with no panel at all) is also fully functional.
//
// Non-owning refs/pointers, same null-safety contract as every other timeline sub-component:
//   - TimelineViewState& — owned by TimelinePanelComponent (or a test), shared by reference so
//     beat<->pixel mapping agrees with the ruler and the grid.
//   - ClipSelectionModel& — owned by TimelinePanelComponent (getClipSelection()), same reasoning.
//   - TimelineDoc* / AppUndoManager* / TransportService* — setters, may be null (a
//     SYNTH_ENABLE_TIMELINE=OFF build, or before MainComponent finishes wiring); every interaction
//     that needs one degrades to "read but don't mutate" or "mutate without an undo step" rather
//     than crashing.
//
// Rendering, one paint() pass per repaint (never a per-tick timer — see CLAUDE.md): every track in
// doc order gets one row (Metrics::timelineTrackRowHeight, shared with TimelineTrackHeaderComponent
// so header rows and clip rows never drift apart — see that header's kRowHeight comment), each clip
// on it a rounded rect in its track's resolved colour, a name when wide enough, and a thin
// pitch-mapped note preview when wider still. The panel paints the bar/beat grid directly
// (TimelinePanelComponent::paint(), unchanged) and adds this component as a child positioned over
// exactly that same rect — JUCE paints a parent before its children, so clips land above the grid
// for free; playhead_ is added AFTER this component in the panel's constructor, so it stays
// topmost. That is the ONE relocation this task makes: no second place ever paints the grid.
//
// Interactions mirror two existing idioms rather than inventing new ones:
//   - The deferred-empty-click trick (GraphEditor.cpp's pendingEmptyCanvasClick, see the comment
//     above GraphEditor::mouseDown/mouseUp) for "click empty lane space deselects, but a press that
//     turns into a drag must not" — here a plain (non-Shift) drag-from-empty becomes a marquee
//     (there is no drag-to-pan in the lane area; scrolling is wheel-only per TL5-2).
//   - Every edit previews locally (a member offset/length, read back in paint() via
//     effectiveGeometryFor()) and commits to the doc exactly once on mouseUp, through
//     AppUndoManager::recordTimelineChange — so a multi-clip move or a multi-clip Delete is ONE
//     undo step however many clips it touches, the same contract GraphEditor::deleteSelection()
//     and dragSelectionBy()/finalizeSelectionDrag() keep for modules.
//
// TL6-5 adds two more paint concerns, both driven off assetRef (the MIDI-vs-audio discriminator —
// see synth::Clip's own comment):
//   - A COMMITTED audio clip (non-empty assetRef, wide enough — see kMinWidthForWaveform) paints a
//     min/max waveform from its `synth::PeaksFile::Data`, lazily resolved and cached by assetRef
//     (`peaksCache_`) via a host-supplied `peaksResolver_` — MainComponent wires this to the SAME
//     resolution `AudioClipStreamer::resolveAssetRef` uses for playback, just re-targeted at the
//     Peaks/ sidecar rather than the Audio/ file. The cache is intentionally coarse: ANY doc change
//     clears the whole thing (refreshFromDoc()) rather than diffing which assetRefs actually moved
//     — peaks files are small, and the alternative (per-ref dirty tracking) is not worth it yet.
//   - A LIVE take in flight (see updateLiveRecording()/LiveRecordingInfo below) paints a growing
//     translucent strip from the punch beat to the transport's current position, sourced from
//     RecordTapModule::copyLivePeaks() — a message-thread-safe snapshot of the SAME accumulator the
//     writer thread is still appending to. Repaints only when new buckets actually arrived (a
//     bucket-count diff), never merely because the transport tick moved — see updateLiveRecording's
//     own comment for why that is the correct "repaint on data arrival" reading of CLAUDE.md's rule
//     here (the strip's rect still grows every poll; only the REPAINT is gated).
namespace synth::ui {

class TimelineClipLaneArea : public juce::Component {
public:
    TimelineClipLaneArea(TimelineViewState& viewState, ClipSelectionModel& selection);
    ~TimelineClipLaneArea() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    // TL5-8: double-clicking a clip opens the piano roll for it. Fires onClipDoubleClicked (if
    // set) with the hit clip's id; a double-click on empty lane space is a no-op.
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Non-owning callback; may be unset. TimelinePanelComponent wires this to
    // PianoRollComponent::openClip via its own openPianoRoll(ClipId).
    std::function<void(synth::ClipId)> onClipDoubleClicked;

    // TL6-6: fired when the user picks "Relink audio…" from an audio clip's context menu (visible
    // whenever assetRef is non-empty, whether the current asset is missing or present). Non-owning;
    // may be unset, in which case the menu item is simply never offered (see showClipContextMenu) —
    // relinking needs a host FileChooser and AssetManager import, neither of which this class has,
    // so unlike Split/Duplicate/Delete this is a callback rather than a ClipContextChoice: the
    // production path is MainComponent opening an async FileChooser and then calling its own
    // relinkClipAsset(); the headless path is MainComponent::relinkClipAssetForTest(), which never
    // goes through this callback (or a menu) at all.
    std::function<void(synth::ClipId)> onRelinkAudioRequested;

    // Panel-scoped Delete/Escape (see GraphEditor's identical idiom). Grabs focus on mouseDown, so
    // pressing Delete right after a click lands here rather than on whichever panel had focus
    // before. Returns false when there is nothing to act on so the key falls through — TL5-10
    // formalises cross-panel key arbitration; this is only the local half of it.
    bool keyPressed(const juce::KeyPress& key) override;

    // Non-owning; may be null. Runs one refresh (see class comment) — the same thing
    // TimelinePanelComponent::timelineChanged() calls on every subsequent doc notification.
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning; may be null (no undo manager -> mutations apply directly, uncommitted to any
    // undo stack — the same degrade-gracefully contract TimelineTrackHeaderComponent::performEdit
    // uses when built without a host).
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }

    // Non-owning; may be null. Only consulted for its current time signature (beatsPerBar, for
    // Snap::Bar) — the same reasoning TimelinePanelComponent::paint()'s grid loop already uses.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

    // Re-derives the doc-backed truth: prunes the selection of any clip id that no longer exists
    // (synth::ui::ClipSelectionModel::retainOnly) and repaints. THE refresh seam — called once by
    // setTimelineDoc() and, thereafter, by TimelinePanelComponent::timelineChanged() on every
    // effective doc mutation. No timer anywhere in this class.
    void refreshFromDoc();

    // ---- TL6-5: waveform peaks (committed clips) ----

    // Non-owning; may be unset (paint() then simply never draws a waveform — same degrade-
    // gracefully contract every other host seam here has). MainComponent supplies this from the
    // SAME resolution `AudioClipStreamer::resolveAssetRef` uses, re-pointed at the Peaks/ sidecar
    // — see that method's comment and the class comment above. Installing a new resolver
    // invalidates the cache (a different resolver may resolve the same ref differently).
    void setPeaksResolver(std::function<juce::File(const juce::String& assetRef)> resolver);

    // Drops every cached synth::PeaksFile::Data and repaints. Called automatically by
    // refreshFromDoc() (the simplest-correct policy — see the class comment); public so a caller
    // that knows only a peaks FILE changed underneath an unchanged assetRef (the resolver's target
    // moved, not the doc) can still force a re-read without waiting for a doc mutation.
    void invalidatePeaksCache();

    // ---- TL6-6: missing-asset placeholder ----

    // Non-owning; may be unset (paint() then assumes every non-empty assetRef resolves — no
    // placeholder is ever drawn without a resolver installed, the same degrade-gracefully contract
    // every other host seam here has). MainComponent wires this to
    // `AudioClipStreamer::resolveAssetRef(assetRef) != juce::File()` — the SAME resolution
    // playback and the peaks resolver (above) use, just answering "does it exist" instead of
    // handing back a File. The answer is cached per assetRef right alongside peaksCache_ so a
    // repeated paint of the same (still missing) clip never re-stats the filesystem; installing a
    // new resolver invalidates both caches, same as setPeaksResolver.
    void setAssetExistsResolver(std::function<bool(const juce::String& assetRef)> resolver);

    // ---- TL6-5: the live-recording strip ----

    // What updateLiveRecording() needs to know about an in-flight audio take. A default-
    // constructed (or `active == false`) value means "nothing recording" — the strip (if any) is
    // cleared. `tap` is non-owning and read for the DURATION OF THE CALL ONLY (copyLivePeaks() is
    // called synchronously inside updateLiveRecording()); nothing here holds it across calls.
    struct LiveRecordingInfo {
        bool active = false;
        synth::TrackId track;     // the armed track the strip paints on
        double punchBeat = 0.0;   // the strip's fixed left edge
        double currentBeat = 0.0; // the strip's growing right edge — the transport's current position
        const RecordTapModule* tap = nullptr;
    };

    // THE 10 Hz update for an in-flight take (see LiveRecordingInfo) — MainComponent calls this
    // every tick alongside the panel's other polled updates, whether or not anything is actually
    // recording. Cheap when it isn't: `info.active == false` just clears any previous strip (one
    // repaint, once, on the falling edge) and returns. When it is, copies the tap's live peaks
    // (copyLivePeaks() — a lock held only for that copy, never on the audio thread) and repaints
    // ONLY the strip's dirty rect, and ONLY when the bucket count actually grew since the last
    // call — the repaint-on-data-arrival rule. The strip's rect itself is still updated every call
    // (so a later arrival's dirty-rect union is correct), just not necessarily repainted.
    void updateLiveRecording(const LiveRecordingInfo& info);

    // Test hook: how many times updateLiveRecording() has actually issued a repaint (as opposed to
    // being called) — the same idiom TimelineTransportBar::getReadoutRepaintCountForTest() uses.
    int getLiveStripRepaintCountForTest() const noexcept { return liveStripRepaintCount_; }

    // ---- Context-menu hook (TL5-3's "showMenuAsync doesn't run headless" idiom) ----
    enum class ClipContextChoice { SplitAtPointer, Duplicate, Delete };

    // Applies one context-menu choice. `pointerBeat` is in absolute (doc) beats, UNSNAPPED — the
    // split case snaps it internally against the current view-state snap + beatsPerBar, exactly
    // like showClipContextMenu()'s own enablement check, so a test driving this directly observes
    // the same snapping the real right-click menu would. One recordTimelineChange mutation, same
    // as every other doc-mutating gesture here.
    void applyClipContextChoice(synth::ClipId id, ClipContextChoice choice, double pointerBeat);

    // ---- Pure geometry (no doc, no component state) — what GeometryMapsBeatsAndRows tests ----
    // The clip rect for a known view state / track row / beat span. `rowHeight` is passed in
    // rather than read from a theme so this stays callable with no LookAndFeel installed at all.
    static juce::Rectangle<int> computeClipRect(const TimelineViewState& viewState, int trackIndex, double startBeat,
                                                double lengthBeats, int rowHeight);

    // ---- TL6-5: waveform bucket geometry — pure, no doc/component/LookAndFeel state ----
    // A half-open [firstBucket, firstBucket + bucketCount) range into `peaks.buckets` (bucket
    // INDICES, not raw pair indices — multiply by peaks.numChannels to reach a `buckets[]` slot).
    // Both fields are 0 when nothing in `peaks` overlaps the clip's span at all.
    struct BucketRange {
        int firstBucket = 0;
        int bucketCount = 0;
    };

    // Which buckets of `peaks` cover this clip's visible span, given where inside the asset it
    // starts reading (`sourceStartSeconds`, seconds — see synth::Clip::sourceStartSeconds) and the
    // beats<->seconds conversion (`bpm`). `sampleRate` is the ASSUMED source sample rate — the
    // peaks file itself does not carry one (see PeaksFile.h), so this is the same "engine rate,
    // no resampling" honesty AudioClipStreamer already states for playback; a caller with a live
    // transport passes its current sampleRate/bpm, exactly like currentBeatsPerBar() does for the
    // snap grid. Clamped to `[0, totalBuckets]` — a clip whose span starts past the end of the
    // peaks data (or `peaks` has no buckets at all) returns a zero-length range rather than an
    // out-of-bounds one.
    static BucketRange bucketRangeForClip(const synth::PeaksFile::Data& peaks, double lengthBeats,
                                          double sourceStartSeconds, double bpm, double sampleRate);

    // The row height this instance currently lays out at: themed Metrics::timelineTrackRowHeight
    // with TimelineTrackHeaderComponent::kRowHeight as the headless fallback (see that constant's
    // comment) — the same dynamic_cast<AppLookAndFeel*> pattern every other timeline component uses.
    int getRowHeight() const;

    // The live rect for a clip id, using its CURRENT doc geometry (never a mid-drag preview) —
    // what tests use to compute where to synthesize a mouse event. Returns an empty rect if the id
    // does not resolve (doc null, or no such clip).
    juce::Rectangle<int> getClipRect(synth::ClipId id) const;

    bool isMarqueeActiveForTest() const noexcept { return dragMode_ == DragMode::Marquee; }

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, Marquee };

    struct ClipHit {
        synth::ClipId id;
        juce::Rectangle<int> rect;
        enum class Zone { Body, LeftEdge, RightEdge } zone = Zone::Body;
    };

    struct Geometry {
        double start = 0.0;
        double length = 0.0;
    };

    // One dragged clip's ORIGIN (pre-drag) geometry — every preview/commit computation reads from
    // this, never from the accumulating pointer position, so rounding never accumulates frame to
    // frame (same reasoning as GraphEditor::dragSelectionBy's comment).
    struct DragOrigin {
        synth::ClipId id;
        double originalStart = 0.0;
        double lengthBeats = 0.0;
    };

    std::optional<ClipHit> hitTestClip(juce::Point<int> pos) const;
    std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> collectClipRects() const;
    Geometry effectiveGeometryFor(const synth::Clip& clip) const;
    double currentBeatsPerBar() const;
    double snappedBeatAt(double rawBeat) const;

    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    void showClipContextMenu(synth::ClipId id, juce::Point<int> localPos);
    void paintClip(juce::Graphics& g, const synth::Clip& clip, const synth::Track& track, int trackIndex,
                   int rowHeight);
    void paintMarquee(juce::Graphics& g);

    // ---- TL6-5: waveform + live-recording-strip painting ----
    // Resolves (lazily loading + caching via peaksResolver_/peaksCache_) and paints a committed
    // audio clip's waveform inside `rect`. A no-op below kMinWidthForWaveform or when nothing
    // resolves (no resolver set, unresolvable ref, or an unreadable/absent peaks file).
    void paintWaveform(juce::Graphics& g, const synth::Clip& clip, juce::Rectangle<int> rect);
    // The cheap per-column line-pair loop shared by paintWaveform() (a committed clip's peaks) and
    // paintLiveRecordingStrip() (the live accumulator's peaks) — one juce::Graphics::drawLine per
    // x column, sampling `buckets[firstBucket + column's fraction of bucketCount]` across every
    // channel (min of mins, max of maxes — a simple downmix; see the class comment's "keep it
    // lean" note). Assumes the caller already set the colour.
    static void paintWaveformColumns(juce::Graphics& g, juce::Rectangle<int> rect,
                                     const std::vector<std::pair<float, float>>& buckets, int numChannels,
                                     int firstBucket, int bucketCount);
    // Cache lookup/lazy-load for one assetRef. Returns nullptr for an empty ref, no resolver, an
    // unresolvable file, or a file that fails synth::PeaksFile::read() — a miss is cached too (as
    // a default-constructed, structurally-invalid Data) so a repeated paint of a still-missing
    // asset never re-touches disk; only invalidatePeaksCache()/refreshFromDoc() forget that.
    const synth::PeaksFile::Data* findPeaksData(const juce::String& assetRef);
    // TL6-6: cache lookup/lazy-resolve for one assetRef's existence, mirroring findPeaksData's
    // cache shape exactly (a miss is cached too, so a still-missing clip never re-triggers the
    // resolver on the next paint). No resolver installed -> true (see setAssetExistsResolver).
    bool assetExists(const juce::String& assetRef);
    // TL6-6: the diagonal-hatch / dimmed-fill treatment for a clip whose assetRef does not
    // resolve, plus a "missing: <name>" label when the clip is wide enough (same threshold
    // paintClip's own name label uses). Theme-token colours via the same
    // dynamic_cast<AppLookAndFeel*> pattern getRowHeight() uses, with a hardcoded fallback when
    // headless.
    void paintMissingAssetPlaceholder(juce::Graphics& g, const synth::Clip& clip, juce::Rectangle<int> rect);
    void paintLiveRecordingStrip(juce::Graphics& g);
    // Shared by updateLiveRecording()'s "nothing recording (any more)" branch and its
    // track-vanished branch: one repaint over wherever the strip used to be, then a clean reset.
    void clearLiveRecording();

    TimelineViewState& viewState_;
    ClipSelectionModel& selection_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    DragMode dragMode_ = DragMode::None;
    synth::ClipId activeClip_;
    juce::Point<int> mouseDownPos_;

    // Deferred-deselect (see class comment): a plain press on empty lane space that never becomes
    // a drag clears the selection on mouseUp; one that DOES move promotes to a marquee instead.
    bool pendingEmptyClick_ = false;

    // ---- Move preview (one or many clips, one shared snapped delta) ----
    std::vector<DragOrigin> dragClips_;
    double previewDeltaBeats_ = 0.0;

    // ---- Resize preview (always the single grabbed clip, even inside a wider selection) ----
    double resizeOriginalStart_ = 0.0;
    double resizeOriginalLength_ = 0.0;
    double previewStart_ = 0.0;  // left-edge trim only (end stays fixed)
    double previewLength_ = 0.0; // both edges

    // ---- Marquee ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::ClipId> marqueeBaseSelection_;

    // ---- TL6-5: waveform peaks cache ----
    std::function<juce::File(const juce::String& assetRef)> peaksResolver_;
    std::map<juce::String, synth::PeaksFile::Data> peaksCache_;

    // ---- TL6-6: asset-existence cache (see setAssetExistsResolver) ----
    std::function<bool(const juce::String& assetRef)> assetExistsResolver_;
    std::map<juce::String, bool> assetExistsCache_;

    // ---- TL6-5: the live-recording strip ----
    LiveRecordingInfo liveRecording_;
    std::vector<std::pair<float, float>> livePeaks_;
    juce::Rectangle<int> liveStripRect_;
    int liveStripRepaintCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineClipLaneArea)
};

} // namespace synth::ui
