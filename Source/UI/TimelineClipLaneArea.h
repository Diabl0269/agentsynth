#pragma once

#include "../Timeline/PeaksFile.h"
#include "../Timeline/TimelineDoc.h"
#include "ClipSelectionModel.h"
#include "TimelineViewState.h"
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class RecordTapModule; // Forward declaration (Source/Modules/RecordTapModule.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// TimelineClipLaneArea — the clip-lane region of the timeline panel (below the ruler, filling
// TimelinePanelComponent::getLanesBounds() minus the ruler strip), with drag/trim/split/duplicate
// and marquee selection.
//
// Deliberately NOT a TimelineDoc::Listener itself: TimelinePanelComponent is already the doc's one
// listener, and its timelineChanged() routes a refreshFromDoc() call in here. setTimelineDoc()
// only stores the pointer and runs that same refresh once, so constructing this directly against
// a doc (as tests do, with no panel at all) is also fully functional.
//
// Non-owning refs/pointers, same null-safety contract as every other timeline sub-component:
// TimelineViewState&/ClipSelectionModel& are owned by TimelinePanelComponent and shared by
// reference; TimelineDoc*/AppUndoManager*/TransportService* are setters that may be null, and every
// interaction that needs one degrades to "read but don't mutate" rather than crashing.
//
// Positioned as a child directly over the rect TimelinePanelComponent::paint() paints the bar/beat
// grid into — JUCE paints a parent before its children, so clips land above the grid for free;
// playhead_ is added AFTER this component, so it stays topmost. No second place ever paints the
// grid.
//
// Every edit previews locally (a member offset/length, read back in paint() via
// effectiveGeometryFor()) and commits to the doc exactly once on mouseUp, through
// AppUndoManager::recordTimelineChange — so a multi-clip move or Delete is ONE undo step however
// many clips it touches (same contract GraphEditor::deleteSelection()/dragSelectionBy() keep for
// modules). A plain (non-Shift) drag-from-empty becomes a marquee, using the same
// deferred-empty-click trick as GraphEditor.cpp's pendingEmptyCanvasClick.
//
// Waveform/live-recording painting is driven off assetRef (the MIDI-vs-audio discriminator — see
// synth::Clip). A committed audio clip's waveform is lazily resolved and cached by assetRef
// (peaksCache_) via a host-supplied peaksResolver_; the cache is cleared wholesale on any doc
// change rather than diffed. A live take paints a growing strip sourced from
// RecordTapModule::copyLivePeaks(), repainted only when new buckets actually arrived.
//
// Authoring gestures (double-click empty space, OS file drop) create content directly: a MIDI row
// gets a one-bar clip and opens the piano roll on it; an audio row asks for a file and hands it
// OUTWARDS through onAudioFileDropped. This class never imports anything itself — it owns no
// AssetManager and no bundle knowledge, exactly like onRelinkAudioRequested.
namespace synth::ui {

class TimelineClipLaneArea
    : public juce::Component
    , public juce::FileDragAndDropTarget {
public:
    TimelineClipLaneArea(TimelineViewState& viewState, ClipSelectionModel& selection);
    ~TimelineClipLaneArea() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    // Double-clicking a clip opens the piano roll for it (onClipDoubleClicked with the hit clip's
    // id). Double-clicking EMPTY lane space authors content on the row under the pointer instead:
    // a Midi track gets a one-bar clip at the floor-snapped beat, selected, and fires
    // onClipDoubleClicked for it too (so "double-click empty space" lands straight in the note
    // editor); an Audio track asks for a file through audioFileChooser_ and reports the choice as
    // onAudioFileDropped, the same seam a file drop uses. An Automation row, and a double-click
    // below the last row, do nothing.
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Non-owning callback; may be unset. TimelinePanelComponent wires this to
    // PianoRollComponent::openClip via its own openPianoRoll(ClipId).
    std::function<void(synth::ClipId)> onClipDoubleClicked;

    // "Loop the selection" (the P key — Cubase's locators-to-selection), fired with the selected
    // clips' [min startBeat, max endBeat] span in absolute doc beats. The OWNER points the transport
    // at it (MainComponent: setLoop(start, end, true)); this class holds a TransportService only to
    // read its time signature and must never command it. Non-owning; may be unset, in which case P
    // falls through like any other unhandled key.
    std::function<void(double startBeat, double endBeat)> onLoopRangeRequested;

    // ---- Authoring: audio files (double-click an audio row, or drop files on one) ----

    // Fired with (audio track, floor-snapped start beat, the chosen/dropped file) — the OWNER
    // imports it and creates the clip (MainComponent::importAudioFileToClip), because importing
    // needs an AssetManager and the current bundle root, neither of which this class has (same
    // division as onRelinkAudioRequested). Non-owning; may be unset, in which case both gestures
    // are inert.
    std::function<void(synth::TrackId, double startBeat, juce::File)> onAudioFileDropped;

    // How a double-click on an empty AUDIO row asks for a file. Defaults to a real async
    // juce::FileChooser filtered to the formats a juce::AudioFormatManager can read; a test injects
    // a lambda that calls `onChosen` synchronously with a fixture file (juce::FileChooser, like
    // juce::PopupMenu::showMenuAsync, never runs in a test process). `onChosen` must not be called
    // for a cancelled dialog.
    using AudioFileChooser = std::function<void(std::function<void(const juce::File&)> onChosen)>;
    void setAudioFileChooser(AudioFileChooser chooser) { audioFileChooser_ = std::move(chooser); }

    // ---- juce::FileDragAndDropTarget: dropping audio files from the OS ----
    // Interested when at least one dragged file has an extension a juce::AudioFormatManager can
    // read (extension only — nothing here opens a dragged file).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    // Highlights the AUDIO row under the cursor, repainting ONLY on a row change (and only the two
    // rows involved) — no row is highlighted over a Midi/Automation row or below the last row, and
    // a drop there is refused.
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    // The FIRST readable audio file only, reported through onAudioFileDropped. Multi-file drops
    // deliberately import one clip rather than fanning out.
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The track row currently highlighted for a file drop, or -1. Test hook (the drop highlight is
    // otherwise only observable as pixels).
    int getFileDropRowForTest() const noexcept { return fileDropRow_; }

    // ---- Empty-row hint ----
    // The hint line an EMPTY row of this kind paints (empty String for Automation — nothing to
    // author there). Pure: the exact strings paint() draws, so a test pins the text without
    // decoding pixels.
    static juce::String emptyRowHintFor(synth::TrackKind kind);
    // Same, resolved against the doc: the hint the row at `trackIndex` paints, or an empty String
    // when that row has clips (or does not exist).
    juce::String getEmptyRowHintForTest(int trackIndex) const;

    // Fired when the user picks "Relink audio…" from an audio clip's context menu (visible
    // whenever assetRef is non-empty, whether the current asset is missing or present). Non-owning;
    // may be unset, in which case the menu item is simply never offered (see showClipContextMenu) —
    // relinking needs a host FileChooser and AssetManager import, neither of which this class has,
    // so unlike Split/Duplicate/Delete this is a callback rather than a ClipContextChoice: the
    // production path is MainComponent opening an async FileChooser and then calling its own
    // relinkClipAsset(); the headless path is MainComponent::relinkClipAssetForTest(), which never
    // goes through this callback (or a menu) at all.
    std::function<void(synth::ClipId)> onRelinkAudioRequested;

    // Panel-scoped Delete/Escape/P (see GraphEditor's identical idiom). Grabs focus on mouseDown, so
    // pressing Delete right after a click lands here rather than on whichever panel had focus
    // before. Returns false when there is nothing to act on so the key falls through — this is
    // only the local half of cross-panel key arbitration.
    bool keyPressed(const juce::KeyPress& key) override;

    // The selected clips' [min startBeat, max endBeat] span in absolute doc beats, or nullopt when
    // nothing selected resolves to a clip. What P hands to onLoopRangeRequested.
    std::optional<std::pair<double, double>> getSelectedClipSpan() const;

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

    // ---- Waveform peaks (committed clips) ----

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

    // ---- Missing-asset placeholder ----

    // Non-owning; may be unset (paint() then assumes every non-empty assetRef resolves — no
    // placeholder is ever drawn without a resolver installed, the same degrade-gracefully contract
    // every other host seam here has). MainComponent wires this to
    // `AudioClipStreamer::resolveAssetRef(assetRef) != juce::File()` — the SAME resolution
    // playback and the peaks resolver (above) use, just answering "does it exist" instead of
    // handing back a File. The answer is cached per assetRef right alongside peaksCache_ so a
    // repeated paint of the same (still missing) clip never re-stats the filesystem; installing a
    // new resolver invalidates both caches, same as setPeaksResolver.
    void setAssetExistsResolver(std::function<bool(const juce::String& assetRef)> resolver);

    // ---- The live-recording strip ----

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

    // ---- Context-menu hook ("showMenuAsync doesn't run headless" idiom) ----
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

    // ---- Waveform bucket geometry — pure, no doc/component/LookAndFeel state ----
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
    // The snap grid line at or BEFORE `rawBeat` (never after it), clamped to >= 0 — what a
    // created clip starts on, so a double-click always lands inside the bar/beat cell it was
    // aimed at rather than the next one. Same grid every drag uses (TimelineViewState::
    // divisionBeats); Snap::Off passes the raw beat through.
    double floorSnappedBeatAt(double rawBeat) const;

    // The track row `pos.y` falls on, or nullopt when there is no doc or it is below the last row.
    std::optional<int> trackIndexAt(juce::Point<int> pos) const;
    // The row's full-width rect (the same y/height computeClipRect gives that row).
    juce::Rectangle<int> rowBounds(int trackIndex, int rowHeight) const;

    // ---- Authoring (double-click on empty lane space) ----
    // One-bar clip on `track` at `startBeat`, as ONE recordTimelineChange, selected, then
    // onClipDoubleClicked so the caller opens the piano roll on it.
    void createMidiClipAt(synth::TrackId track, double startBeat);
    // Asks audioFileChooser_ for a file and reports it through onAudioFileDropped.
    void requestAudioFileFor(synth::TrackId track, double startBeat);
    // The real async chooser audioFileChooser_ defaults to (see setAudioFileChooser).
    void launchAudioFileChooser(std::function<void(const juce::File&)> onChosen);
    // True when `file`'s EXTENSION is one audioFormats_ can read. No file is ever opened.
    bool isReadableAudioFile(const juce::File& file) const;
    // The first file in `files` isReadableAudioFile() accepts, or an invalid File.
    juce::File firstAudioFileIn(const juce::StringArray& files) const;
    // The audio row `x, y` would drop onto, or -1 (no audio row there, or nothing droppable).
    int dropRowFor(const juce::StringArray& files, int x, int y) const;
    void setFileDropRow(int row);

    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    void showClipContextMenu(synth::ClipId id, juce::Point<int> localPos);
    void paintClip(juce::Graphics& g, const synth::Clip& clip, const synth::Track& track, int trackIndex,
                   int rowHeight);
    void paintMarquee(juce::Graphics& g);

    // ---- Waveform + live-recording-strip painting ----
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
    // Cache lookup/lazy-resolve for one assetRef's existence, mirroring findPeaksData's
    // cache shape exactly (a miss is cached too, so a still-missing clip never re-triggers the
    // resolver on the next paint). No resolver installed -> true (see setAssetExistsResolver).
    bool assetExists(const juce::String& assetRef);
    // The diagonal-hatch / dimmed-fill treatment for a clip whose assetRef does not
    // resolve, plus a "missing: <name>" label when the clip is wide enough (same threshold
    // paintClip's own name label uses). Theme-token colours via the same
    // dynamic_cast<AppLookAndFeel*> pattern getRowHeight() uses, with a hardcoded fallback when
    // headless.
    void paintMissingAssetPlaceholder(juce::Graphics& g, const synth::Clip& clip, juce::Rectangle<int> rect);
    void paintLiveRecordingStrip(juce::Graphics& g);
    // The dim "how do I put something here" line for a row with no clips (see emptyRowHintFor).
    // Static paint straight from doc state — no timer, no animation. Skipped when the row is too
    // short or too narrow to render the line legibly.
    void paintEmptyRowHint(juce::Graphics& g, const synth::Track& track, juce::Rectangle<int> bounds);
    // The accent wash marking the audio row an OS file drop would land on.
    void paintFileDropHighlight(juce::Graphics& g, juce::Rectangle<int> bounds);
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

    // ---- Waveform peaks cache ----
    std::function<juce::File(const juce::String& assetRef)> peaksResolver_;
    std::map<juce::String, synth::PeaksFile::Data> peaksCache_;

    // ---- Asset-existence cache (see setAssetExistsResolver) ----
    std::function<bool(const juce::String& assetRef)> assetExistsResolver_;
    std::map<juce::String, bool> assetExistsCache_;

    // ---- The live-recording strip ----
    LiveRecordingInfo liveRecording_;
    std::vector<std::pair<float, float>> livePeaks_;
    juce::Rectangle<int> liveStripRect_;
    int liveStripRepaintCount_ = 0;

    // ---- Authoring / file drop ----
    // Registered with the basic formats once, in the constructor: it answers "can this extension be
    // read" for a drag and supplies the chooser's filter. Never used to open a file.
    juce::AudioFormatManager audioFormats_;
    AudioFileChooser audioFileChooser_;
    std::unique_ptr<juce::FileChooser> fileChooser_; // kept alive for the duration of launchAsync
    int fileDropRow_ = -1;                           // -1 = nothing highlighted

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineClipLaneArea)
};

} // namespace synth::ui
