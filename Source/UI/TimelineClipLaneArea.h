#pragma once

#include "../Timeline/PeaksFile.h"
#include "../Timeline/TimelineDoc.h"
#include "ClipSelectionModel.h"
#include "EditTool.h"
#include "TimelineViewState.h"
#include <array>
#include <cmath>
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
class ShortcutManager; // Forward declaration (Source/ShortcutManager.h)

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

//==============================================================================
// ---- The vertical time grid's THREE-LEVEL colour policy (shared, pure) ----
//
// Cubase's hierarchy: a bar line is unmistakable, a beat line is clearly there, and the current
// snap subdivision is a quiet-but-readable hint. Before this existed each surface picked its own
// alphas independently and the sub-beat level ended up a near-invisible hairline on every dark
// theme — the whole reason it is a shared, testable function rather than three magic numbers at
// three paint sites.
//
// It lives HERE, in the lane header, because the timeline's own vertical grid is painted by
// TimelinePanelComponent::paint() (this component is a transparent child sitting directly over
// that rect — see the class comment above; "no second place ever paints the grid" still holds).
// synth::ui::PianoRollComponent, which owns its own mapping and therefore genuinely does paint its
// own grid, includes this header for the same policy so the two surfaces can never drift.
//
// Nothing here is per-frame work: both painters already walk their visible line ranges from view
// state alone, and these are constant-time colour choices made inside that existing walk.

/** Which level of the time grid a vertical line belongs to, weakest to strongest. Ordered so the
 *  enum's own ordering IS the visual hierarchy — a test can assert monotonicity by walking it. */
enum class GridLineLevel { Subdivision = 0, Beat, Bar };

/** Below this spacing a whole LEVEL of gridlines is dropped rather than drawn as a wall of
 *  touching pixels — Cubase does exactly the same, and the alternative (drawing them anyway) is
 *  strictly worse than no grid at all, because a solid block of subdivision lines swamps the beat
 *  and bar lines it is supposed to sit under. 3 px is the point where two adjacent lines still read
 *  as two lines on a 1x display. */
constexpr double kMinGridLinePixels = 3.0;

/** True when lines `spacingBeats` apart at `pixelsPerBeat` are still readable — the density guard
 *  above. A non-positive or non-finite spacing (Snap::Off has no subdivision at all) never
 *  draws. */
inline bool gridLevelIsReadable(double spacingBeats, double pixelsPerBeat) noexcept {
    if (!std::isfinite(spacingBeats) || !std::isfinite(pixelsPerBeat) || spacingBeats <= 0.0)
        return false;
    return spacingBeats * pixelsPerBeat >= kMinGridLinePixels;
}

/** One level's opacity. Monotonic by construction (Bar >= Beat >= Subdivision) and deliberately
 *  well clear of zero at the bottom: the subdivision level is a HINT, not a hairline, and the bug
 *  this replaced was a 0.14 alpha applied to a token that is itself only a shade off the
 *  background. */
inline float gridLineAlphaFor(GridLineLevel level) noexcept {
    switch (level) {
    case GridLineLevel::Subdivision:
        return 0.28f;
    case GridLineLevel::Beat:
        return 0.50f;
    case GridLineLevel::Bar:
        return 0.85f;
    }
    return 0.28f;
}

/** How far a grid line is lifted from its theme token towards the background's contrasting end.
 *  Raising alpha alone cannot fix a dark theme: `border` there is a dark grey a shade or two off
 *  bg0/bg1, so even at alpha 1.0 it is nearly the background. Mixing halfway to the contrasting
 *  extreme (white on a dark theme, black on a light one) is what makes the line a LINE, while
 *  keeping the theme's own hue in it rather than introducing a new token nobody can re-skin. */
constexpr float kGridLineContrastMix = 0.5f;

/** The exact colour a grid line of `level` is drawn in.
 *  @param base       the theme token the surface already uses for its grid (Theme::Colors::border
 *                    on both the piano roll and the lanes) — no new token is introduced.
 *  @param background what that surface fills behind the grid (bg0 on both), consulted only to
 *                    decide which way "more contrast" points. */
inline juce::Colour gridLineColourFor(GridLineLevel level, juce::Colour base, juce::Colour background) noexcept {
    // contrasting(1.0f) is JUCE's "clearly visible against this colour" — black or white.
    return base.interpolatedWith(background.contrasting(1.0f), kGridLineContrastMix).withAlpha(gridLineAlphaFor(level));
}

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
    // Both exist only for the tool layer: mouseEnter re-applies the active tool's cursor (it is
    // NOT set per mouse-move — see setActiveTool), and mouseExit drops the Split tool's hover
    // preview so a line never survives the pointer leaving the lanes.
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    // Rebuilds the six cached tool cursors from the (re-tinted) icons of the new LookAndFeel and
    // re-applies the active one — a theme switch is the only thing that changes what a tool
    // cursor looks like, so it is the only thing that pays for rebuilding them.
    void lookAndFeelChanged() override;
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

    // ---- Edit tools (synth::ui::EditTool — the Cubase-style tool row) ----

    // THE tool every pointer gesture in the lanes is interpreted through. TimelinePanelComponent
    // owns the one active tool for the whole timeline and pushes it in here (and into the piano
    // roll, which shares this rect); nothing in this class ever changes it by itself.
    //
    // EditTool::Select is the tool this component grew up with and keeps ALL of its behaviour:
    // click/shift-click select, drag-move (now cross-track and Alt-copy capable), edge trims with
    // their resize cursors, marquee, and both double-click authoring gestures. The other five are
    // deliberately click-only — a drag with Split/Glue/Erase/Mute held would be a second,
    // undiscoverable gesture on a tool whose whole point is that one click does one thing — with
    // Draw the single exception (its drag IS the clip's length).
    //
    // Switching tools cancels whatever gesture and preview are in flight rather than trying to
    // reinterpret them: a half-finished drag has no meaning under a different tool.
    void setActiveTool(EditTool tool);
    EditTool getActiveTool() const noexcept { return activeTool_; }

    // The clip the Glue tool (and the "Glue with next" menu item) would join `id` into: the clip
    // on the SAME track with the smallest startBeat at or after `id`'s end. TimelineDoc::joinClips
    // treats a gap as legal (it becomes silence) and rejects an overlap, so "the next clip that
    // does not overlap" is exactly the set of legal targets — picking the abutting clip only would
    // make the tool silently inert on the very arrangement (clips with gaps) where gluing is most
    // useful. Returns an invalid ClipId when there is no doc, no such clip, or nothing after it.
    synth::ClipId findGlueTarget(synth::ClipId id) const;

    // The commit half of the inline rename editor, and the headless seam for it (a live
    // juce::TextEditor is no more testable than a juce::PopupMenu). One recordTimelineChange;
    // TimelineDoc::setClipName trims and REJECTS a blank name, so a cleared field keeps the old
    // one instead of erasing it — that rejection is the whole reason this is a doc call rather
    // than a local string assignment.
    void renameClip(synth::ClipId id, const juce::String& newName);

    // Opens the inline editor over `id`'s name area, pre-filled and selected. Return commits
    // (through renameClip), Escape cancels, and losing focus commits — the same three outcomes
    // every other in-place rename in this app has. A no-op when `id` does not resolve.
    void beginRenameClip(synth::ClipId id);
    // The live editor, or nullptr when no rename is in flight (test hook: a rename is otherwise
    // only observable as pixels).
    juce::TextEditor* getRenameEditorForTest() const noexcept { return renameEditor_.get(); }

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

    /** The user's binding for the one rebindable key this component owns: P, loop the selection
     *  ("timelineLoopSelection" — the SAME action id TimelinePanelComponent's own P fallback
     *  resolves, so the two can never end up on different keys). Non-owning and may stay null, in
     *  which case P is the hardcoded default; installed, resolution is strict (an unset binding
     *  means no key), exactly as on PianoRollComponent::setShortcutManager.
     *
     *  Delete/Backspace and Escape stay FIXED and are deliberately absent from the shortcut table —
     *  they are platform conventions every surface in the app answers identically. */
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }
    const ShortcutManager* getShortcutManager() const noexcept { return shortcuts_; }

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
    // Every tool action is ALSO a menu item, so a user who never touches the tool row can still
    // split, glue, mute and rename — the tools are an accelerator for the menu, not a second set
    // of capabilities. Rename is the one entry with no headless meaning (it opens a text editor
    // rather than mutating), so applyClipContextChoice treats it as a no-op and the commit path is
    // renameClip() — see that method.
    enum class ClipContextChoice { SplitAtPointer, Duplicate, Delete, ToggleMute, GlueWithNext, Rename };

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

    // ---- Tool-gesture test hooks (preview state is otherwise only observable as pixels) ----

    // The Split tool's hover preview: the hovered clip and the SNAPPED beat the line is drawn at,
    // or nullopt when nothing is previewed. The pair is the repaint gate — see
    // requestToolPreviewRepaint.
    struct SplitPreview {
        synth::ClipId clip;
        double beat = 0.0;
    };
    std::optional<SplitPreview> getSplitPreviewForTest() const;
    // The Draw tool's ghost rect while a drag is in flight, or an empty rect.
    juce::Rectangle<int> getDrawGhostRectForTest() const;
    // Whether the in-flight Select-tool move drag is an Alt copy-drag (originals stay put, ghosts
    // move) rather than a plain move.
    bool isCopyDragForTest() const noexcept { return dragMode_ == DragMode::Move && copyDrag_; }
    // The destination rects the copy-drag ghosts occupy right now, in dragClips_ order — empty
    // unless a copy-drag is in flight. Computed by the SAME helper paintDragGhosts() draws from,
    // so an assertion about a ghost cannot pass while the drawn ghost sits somewhere else.
    std::vector<juce::Rectangle<int>> getDragGhostRectsForTest() const;
    // The (startBeat, lengthBeats) a clip PAINTS at this instant — its doc geometry except while
    // its own move/trim drag is previewing (see effectiveGeometryFor). The pair a copy-drag test
    // asserts is UNCHANGED mid-drag, while getDragGhostRectsForTest() shows the delta. nullopt
    // when the id does not resolve.
    std::optional<std::pair<double, double>> getEffectiveGeometryForTest(synth::ClipId id) const;
    // The track-row offset the in-flight move/copy drag would apply to the whole selection — 0
    // whenever the drop would be illegal for any clip in it (see mouseDrag's kind check).
    int getPreviewRowDeltaForTest() const noexcept { return previewRowDelta_; }

protected:
    // THE paint-count seam for the tool previews (the Split tool's hover line and the Draw tool's
    // ghost), mirroring PianoRollComponent::requestRepaintStrip / TimelinePlayheadOverlay::
    // requestRepaintStrip exactly — a test subclasses this and counts. Every repaint a preview
    // costs goes through it and nowhere else, and it is only ever reached when the previewed STATE
    // changed (a new snapped beat, a new hovered clip, a new ghost rect): pointer movement inside
    // one snap cell repaints nothing at all.
    virtual void requestToolPreviewRepaint(juce::Rectangle<int> region);

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, Marquee, Draw };

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
    // frame (same reasoning as GraphEditor::dragSelectionBy's comment). `trackIndex` is captured
    // for the same reason the start beat is: a cross-track drag applies ONE shared row delta to
    // every clip, and re-deriving each clip's row mid-drag would read rows the preview has already
    // moved.
    struct DragOrigin {
        synth::ClipId id;
        double originalStart = 0.0;
        double lengthBeats = 0.0;
        int trackIndex = 0;
    };

    std::optional<ClipHit> hitTestClip(juce::Point<int> pos) const;
    std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> collectClipRects() const;
    Geometry effectiveGeometryFor(const synth::Clip& clip) const;
    // The row a clip PAINTS in right now: its own, except while a plain (non-copy) move drag is
    // previewing a cross-track drop, in which case the whole dragged set previews one row delta
    // down/up. A copy-drag deliberately does NOT move the original — its destination is drawn as a
    // separate ghost (see paintDragGhosts).
    int effectiveRowFor(synth::ClipId id, int trackIndex) const;
    double currentBeatsPerBar() const;
    double snappedBeatAt(double rawBeat) const;
    // The snap grid line at or AFTER `rawBeat` — floorSnappedBeatAt's mirror, and what the Draw
    // tool's drag end uses so a drag that has crossed into a cell always includes that whole cell.
    double ceilSnappedBeatAt(double rawBeat) const;
    // The smallest length the Draw tool will create: one snap division, or kMinClipLengthBeats
    // when the grid is off (there is no cell to fill, so this falls back to the same floor every
    // trim already uses).
    double minDrawLengthBeats() const;
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

    // ---- Tool gestures (everything below is inert while EditTool::Select is active) ----
    // One press with a non-Select tool. Split/Glue/Erase/Mute act immediately on press (a DAW's
    // tool click is expected to land under the finger, not on release); Draw anchors a drag.
    // Split/Glue/Erase/Mute all route straight into applyClipContextChoice — the tool and the
    // menu item are literally the same code path, which is what keeps "the tools are an
    // accelerator for the menu" true rather than aspirational (and gives each one the same single
    // recordTimelineChange, wrapped unconditionally: a refused mutation makes the lambda a no-op
    // and AppUndoManager pushes nothing for one).
    void handleToolMouseDown(const juce::MouseEvent& e);
    // Draw: press anchors on the floor-snapped beat of a Midi row (other kinds are inert), drag
    // grows the ghost, release creates the clip. A press that never dragged falls back to
    // createMidiClipAt — the same one-bar clip the empty-lane double-click authors.
    void beginDrawGesture(const juce::MouseEvent& e);
    void updateDrawGesture(const juce::MouseEvent& e);
    void commitDrawGesture();
    // Both preview writers: they compute the new state, compare it with the old, and only then
    // ask for a repaint (of the union of the two regions, so one change costs exactly one
    // repaint) — see requestToolPreviewRepaint.
    void updateSplitPreview(juce::Point<int> pos);
    void clearToolPreviews();
    // The rect a split line at (clip, beat) occupies — a few pixels wide so the repaint region
    // covers the stroke rather than a zero-width column.
    juce::Rectangle<int> splitPreviewBounds(synth::ClipId clip, double beat) const;
    void paintSplitPreview(juce::Graphics& g);
    // The ghosted destinations of a copy-drag (the originals keep painting in place, unmoved on
    // both axes — see effectiveGeometryFor/effectiveRowFor).
    void paintDragGhosts(juce::Graphics& g);
    // ONE dragged clip's ghost rect. The single geometry source shared by paintDragGhosts() and
    // getDragGhostRectsForTest() — computing them separately is how a drawn affordance drifts
    // from the one a test pins (the same reasoning GraphEditor::buildVisibleCables() states).
    juce::Rectangle<int> dragGhostRectFor(const DragOrigin& origin, int rowHeight) const;
    void paintDrawGhost(juce::Graphics& g);

    // ---- Tool cursors (built once per theme — never per mouse move) ----
    void rebuildToolCursors();
    void applyToolCursor();

    // ---- Inline rename ----
    // Tears the editor down and, when `commit`, pushes its text through renameClip(). Detaches
    // the editor BEFORE doing either, so the focus-loss callback that deleting it fires re-enters
    // to a null editor and stops.
    void finishRename(bool commit);

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
    // Non-owning, may stay null (see setShortcutManager). const — this component only reads.
    const ShortcutManager* shortcuts_ = nullptr;

    DragMode dragMode_ = DragMode::None;
    synth::ClipId activeClip_;
    juce::Point<int> mouseDownPos_;

    // Deferred-deselect (see class comment): a plain press on empty lane space that never becomes
    // a drag clears the selection on mouseUp; one that DOES move promotes to a marquee instead.
    bool pendingEmptyClick_ = false;

    // ---- Move preview (one or many clips, one shared snapped delta) ----
    std::vector<DragOrigin> dragClips_;
    double previewDeltaBeats_ = 0.0;
    // The whole selection's shared row offset, 0 unless EVERY dragged clip's destination row
    // exists and accepts its payload (TimelineDoc::moveClipToTrack's kind rule). Clamping the
    // group rather than dropping the clips that would fit is deliberate: a partial drop silently
    // tears a selection apart.
    int previewRowDelta_ = 0;
    // Alt held at mouseDown: the drag previews COPIES (originals paint in place, ghosts move) and
    // commits duplicateClip + moveClipToTrack per clip instead of moving anything.
    bool copyDrag_ = false;

    // ---- Edit tool ----
    EditTool activeTool_ = EditTool::Select;
    // Six cached cursors, rebuilt only on a theme change (see rebuildToolCursors) — building one
    // renders an icon into an Image, which is far too much work for a mouse-move.
    std::array<juce::MouseCursor, kAllEditTools.size()> toolCursors_;
    bool toolCursorsBuilt_ = false;

    // ---- Split tool hover preview (the (clip, snapped beat) pair IS the repaint gate) ----
    synth::ClipId splitPreviewClip_;
    double splitPreviewBeat_ = 0.0;

    // ---- Draw tool gesture ----
    synth::TrackId drawTrack_;
    int drawRow_ = -1;
    double drawAnchorBeat_ = 0.0;
    double drawEndBeat_ = 0.0;
    bool drawDragged_ = false;

    // ---- Inline rename ----
    std::unique_ptr<juce::TextEditor> renameEditor_;
    synth::ClipId renamingClip_;

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
