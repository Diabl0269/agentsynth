#pragma once

#include "../Timeline/TimelineDoc.h"
#include "ColourPickerPopup.h"
#include "TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth {
class TransportService;
}

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

// The bar/beat ruler strip at the top of the timeline panel's lanes region. Owns nothing: it
// references a shared synth::ui::TimelineViewState (the beat<->pixel mapping — zoom/scroll/snap),
// an optional synth::TransportService (time signature + loop state for painting, and the target of
// drag-to-scrub / drag-to-loop) and an optional synth::TimelineDoc (the MARKERS it draws and
// edits). Every one of those pointers may be null (tests, or a build/flag state with no engine
// wired in yet): paint() then shows an empty ruler and the matching mouse interactions are no-ops.
//
// The strip is split horizontally into two interaction zones — top half = loop, bottom half =
// playhead — so both gestures are reachable without a modifier. See the Zone enum below. MARKER
// flags sit in the bottom band and are hit-tested BEFORE the zone split, so a click on a flag
// drags/renames the marker instead of scrubbing; anything outside a flag rect is unchanged.
//
// No timer, no animation of its own — repaint() is called after an interaction changes something
// this component paints (view-state zoom/scroll, the hovered zone, the hovered marker, or after
// posting a transport command), by TimelinePanelComponent::updateFromTransport's 10 Hz diff when
// the time signature / loop range changed from somewhere else, and by the panel's
// TimelineDoc::Listener callback when a marker was added, moved, renamed, recoloured or removed.
// There is deliberately NO polling of the doc: a marker only changes as the result of a mutation,
// and that mutation already notifies. The moving position line is a separate topmost overlay drawn
// over this strip and the lanes below it — see TimelinePlayheadOverlay.
namespace synth::ui {

// ---- Adaptive ruler density (pure; paint() and the tests below share it) ----
//
// Cubase's ruler shows three bands as you zoom in, and the SNAP division has no say in any of them
// — the ruler is a bars/beats reference, not a picture of the current grid (a 1/16 snap on a
// zoomed-out arrangement would otherwise turn the strip into a grey smear). The bands:
//   1. far out  — bar lines + bar numbers only (bar numbers themselves thin out by powers of two,
//                 see paint()'s labelEveryNBars);
//   2. mid      — short beat ticks appear between the bar lines;
//   3. close in — those beats also get a small dim "bar.beat" label ("80.2", "80.3").
//
// The two thresholds below are the band edges, in pixels per BEAT (a beat is always a quarter note
// here). Deterministic and font-independent — the same reason the bar-label stride is computed from
// a constant rather than from measured text: the guard tests must mean the same thing on every
// platform.
constexpr double kMinBeatTickSpacingPx = 8.0;   // under this, beat ticks are noise beside the bar lines
constexpr double kMinBeatLabelSpacingPx = 48.0; // "80.2" needs this much room before it earns its place

// ---- Marker flags (pure geometry; paint() and hit-testing share it) ----
//
// A flag is a small filled tab in the LOWER band of the strip, anchored at the marker's beat and
// running right, with a full-height 1 px stem at the beat itself so the exact position stays
// readable when the label is clipped.
//
// The flag's WIDTH is derived from the label's character COUNT, never from measured text: the
// clickable rect and the painted rect are the same rect (see markerFlagWidthFor), and a
// font-measured width would make a hit-test assertion mean something different on every platform —
// the same reason the bar-label stride above is computed from a constant.
constexpr float kMarkerFlagHeight = 11.0f;
constexpr float kMarkerStemWidth = 1.0f;
constexpr float kMarkerFlagPadX = 3.0f;
constexpr float kMarkerCharWidthPx = 5.5f; // nominal advance for the 9 pt label font
constexpr float kMarkerMinFlagWidth = 9.0f;
constexpr float kMarkerMaxFlagWidth = 140.0f;
constexpr float kMarkerFlagFontHeight = 9.0f;

/** The flag width for a label of `textLength` characters — clamped so an empty label still has a
 *  grabbable tab and a pathological one cannot swallow the strip. Pure and font-independent. */
inline float markerFlagWidthFor(int textLength) noexcept {
    const float wanted = 2.0f * kMarkerFlagPadX + (float)juce::jmax(0, textLength) * kMarkerCharWidthPx;
    return juce::jlimit(kMarkerMinFlagWidth, kMarkerMaxFlagWidth, wanted);
}

struct RulerTickPlan {
    bool drawBeatTicks = false;
    bool drawBeatLabels = false;
};

/** What the ruler draws BETWEEN bar lines at this zoom. `beatsPerBar` matters only for the
 *  degenerate one-beat-or-shorter bar: there are then no non-bar beats to mark at all, and drawing
 *  a "tick" on top of every bar line would just thicken it. Labels imply ticks by construction —
 *  a label with no tick to sit against would float. */
inline RulerTickPlan rulerTickPlanFor(double pixelsPerBeat, double beatsPerBar) noexcept {
    RulerTickPlan plan;
    if (!(pixelsPerBeat > 0.0) || !(beatsPerBar > 1.0))
        return plan;
    plan.drawBeatTicks = pixelsPerBeat >= kMinBeatTickSpacingPx;
    plan.drawBeatLabels = plan.drawBeatTicks && pixelsPerBeat >= kMinBeatLabelSpacingPx;
    return plan;
}

class TimelineRulerComponent : public juce::Component {
public:
    // Which interaction the pointer's height in the strip selects. Top half drives the loop
    // range, bottom half drives the playhead. Decided once from the mouseDown y and held for the
    // whole gesture — a drag is free to leave the band it started in.
    enum class Zone { Loop, Playhead };

    // How the loop brace paints. A RANGE exists whenever loopEnd > loopStart, independently of
    // whether looping is armed: disarming dims the brace instead of hiding it, so the locators stay
    // visible (and clickable — see mouseUp) once set.
    enum class BraceState { None, Inactive, Active };

    explicit TimelineRulerComponent(TimelineViewState& viewState);

    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // ---- Markers ----
    //
    // Non-owning, may be null (no doc wired yet — the strip then draws no flags and every marker
    // gesture is inert). Set/cleared by TimelinePanelComponent::setTimelineDoc, which also repaints
    // this strip from its TimelineDoc::Listener callback: this component never listens to the doc
    // itself and never polls it.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept {
        doc_ = doc;
        cancelMarkerRename(); // an in-flight rename names a marker in the OLD doc
        draggingMarker_ = {};
        hoveredMarker_ = {};
        repaint();
    }
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    /** Non-owning, may stay null — every marker mutation then applies directly, off the undo
     *  stack. Same degrade-gracefully contract TimelineClipLaneArea::setUndoManager has. */
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }

    /** Where the marker colour picker's favourites shelf persists to. Null means "in-memory only
     *  for this popup instance", which is what a headless test with no ApplicationProperties gets
     *  (see synth::ui::loadFavouriteColours). */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    /** One marker's flag, in this component's coordinates. THE single enumeration paint() and
     *  markerAt() both walk — computing them separately is how a drawn flag drifts from the
     *  clickable one (the same rule GraphEditor::buildVisibleCables exists to enforce for wires). */
    struct MarkerFlag {
        synth::MarkerId id;
        double beat = 0.0;
        juce::Rectangle<float> bounds; // the painted AND clickable tab
        juce::Colour colour;
        juce::String text;
    };

    /** Every marker whose flag intersects the visible strip, in doc (beat, id) order. Empty with no
     *  doc, no markers, or a zero-width component. The marker being dragged reports its PREVIEW
     *  beat, so the flag under the cursor is the one the drop will commit. */
    std::vector<MarkerFlag> buildMarkerFlags() const;

    /** The marker whose flag contains `pos`, or an invalid id. Topmost wins: flags are walked back
     *  to front so an overlapping pair resolves to the one actually drawn on top. */
    synth::MarkerId markerAt(juce::Point<float> pos) const;

    /** What a marker's right-click menu can ask for. Rename and ChangeColour are deliberately INERT
     *  here (they open an editor / a picker rather than mutating, and neither has a headless
     *  meaning) — the enum exists so the menu's whole vocabulary is enumerable and a test can
     *  assert those two choices mutate nothing. The commit paths are renameMarker() and the
     *  picker's own onCommit; see TimelineClipLaneArea::ClipContextChoice for the same split. */
    enum class MarkerContextChoice { Rename, ChangeColour, Delete };

    /** Headless seam for the right-click menu (juce::PopupMenu::showMenuAsync does not run in a
     *  test process). Mirrors TimelineClipLaneArea::applyClipContextChoice. */
    void applyMarkerContextChoice(synth::MarkerId id, MarkerContextChoice choice);

    /** Commits a rename as ONE undo step. A `newText` over TimelineDoc::kMaxMarkerTextLength is
     *  refused by the doc, which leaves the old label in place. */
    void renameMarker(synth::MarkerId id, const juce::String& newText);

    /** Opens the inline rename editor over the marker's flag. A no-op when the id doesn't resolve
     *  or the flag isn't on screen. */
    void beginRenameMarker(synth::MarkerId id);

    /** The live rename editor, or nullptr. Test seam: a headless test types into it directly. */
    juce::TextEditor* getMarkerRenameEditorForTest() const noexcept { return renameEditor_.get(); }

    /** Builds the colour picker with the EXACT onPreview/onCommit callbacks the real right-click
     *  path uses, without launching a juce::CallOutBox — mirrors
     *  TimelineTrackHeaderComponent::createColourPickerForTest(). Null when `id` doesn't resolve. */
    std::unique_ptr<synth::ui::ColourPickerPopup> createMarkerColourPickerForTest(synth::MarkerId id);

    /** The marker a drag is in flight on (invalid when none), and the snapped beat it would commit
     *  to. Test seams — no OS mouse source exists headlessly. */
    synth::MarkerId getDraggingMarkerForTest() const noexcept { return draggingMarker_; }
    double getMarkerDragBeatForTest() const noexcept { return markerDragBeat_; }
    synth::MarkerId getHoveredMarkerForTest() const noexcept { return hoveredMarker_; }

    // While the piano roll is open it maps beats to x through its OWN zoom/scroll, so this strip
    // would otherwise label bars that have nothing to do with what is on screen below it.
    // TimelinePanelComponent hands the roll's view state here (plus the roll's keys-gutter width
    // as a pixel offset, since the roll's grid starts that far right of this strip's x == 0) on
    // openPianoRoll(), and clears it (nullptr) on close. Everything — labels, ticks, the loop
    // brace, drag-to-loop and drag-to-scrub — follows the override, so the ruler stays a truthful,
    // interactive ruler over the roll. The SNAP division still comes from the shared view state
    // (there is only one snap setting).
    void setMappingOverride(const TimelineViewState* view, int xOffsetPx) noexcept {
        overrideView_ = view;
        overrideOffsetPx_ = xOffsetPx;
        repaint();
    }
    bool hasMappingOverrideForTest() const noexcept { return overrideView_ != nullptr; }
    // The offset an installed override is currently using — what a test asserts against directly
    // rather than inferring it from painted tick positions (0 with no override installed, though
    // hasMappingOverrideForTest() is what actually gates whether that 0 means anything).
    int getMappingOverrideOffsetForTest() const noexcept { return overrideOffsetPx_; }

    void paint(juce::Graphics& g) override;

    // Loop zone (top half): press-drag-release sets the loop to the snapped
    // [min(anchor, current), max(anchor, current)] range; a click with no drag does NOTHING unless
    // it lands on a DIMMED brace, which re-arms that range (the inverse of the Cmd+click that
    // disarmed it) — a stray click anywhere else in this half still can't destroy an existing loop.
    // Playhead zone (bottom half): mouseDown seeks immediately and every mouseDrag keeps seeking, so
    // the cursor follows the mouse. Cmd+click toggles looping off from either zone. See
    // TimelineRulerComponent.cpp for the throttle that keeps both drag paths from flooding
    // TransportService's command FIFO.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Hover affordance only: sets the per-zone mouse cursor and paints a faint band over the
    // hovered half. Repaints only when the hovered zone actually changes.
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Pure: what paint() draws for this loop trio. The ONE place the rule lives — paint() and the
    // test seam below both call it, so a drawn brace and an asserted state can't diverge.
    static BraceState braceStateFor(bool looping, double loopStartBeat, double loopEndBeat) noexcept;
    // The colour each state paints in. Inactive is deliberately the muted TEXT colour rather than a
    // faded accent: the hover band is already accent at 10%, so a dimmed accent would still read as
    // lit over it.
    static juce::Colour braceColourFor(BraceState state, juce::Colour accent, juce::Colour textMuted) noexcept;
    // The brace state for the CURRENT transport (None with no transport at all).
    BraceState getBraceStateForTest() const noexcept;

    // Which zone the pointer is over; nullopt when the pointer is not over the strip.
    std::optional<Zone> getHoveredZoneForTest() const noexcept { return hoveredZone_; }
    // The zone the in-flight (or most recent) gesture is locked to.
    Zone getGestureZoneForTest() const noexcept { return gestureZone_; }
    // How many locateBeat() calls the scrub path has actually posted — proves the dedupe throttle.
    int getSeekPostCountForTest() const noexcept { return seekPostCount_; }

private:
    // ---- Markers ----
    // The ONE undo seam every marker mutation goes through: with a manager installed the mutation
    // is one recordTimelineChange step, without one it applies directly. Same shape as
    // TimelineTrackHeaderComponent::performEdit.
    void performMarkerEdit(const std::function<void()>& mutation);
    void showMarkerContextMenu(synth::MarkerId id);
    std::unique_ptr<synth::ui::ColourPickerPopup> buildMarkerColourPicker(synth::MarkerId id);
    void finishMarkerRename(bool commit);
    void cancelMarkerRename() { finishMarkerRename(false); }
    void setHoveredMarker(synth::MarkerId id);
    // The ONE place the mouse cursor is decided, called by both hover setters: a marker flag is the
    // smaller, more specific target sitting inside a zone, so it wins. Split out because each
    // setter only fires on ITS own change, and a zone change while the pointer sits inside a flag
    // would otherwise leave the zone's cursor showing over a draggable marker.
    void applyHoverCursor();
    // True when the press was consumed by a marker (a drag started, or a context menu opened), in
    // which case the loop/playhead gesture below it must not also run.
    bool handleMarkerMouseDown(const juce::MouseEvent& e);
    void paintMarkers(juce::Graphics& g) const;

    Zone zoneAtY(float y) const noexcept;
    // The beat<->x mapping every paint/gesture site goes through: the shared view state normally,
    // the override (offset by overrideOffsetPx_) while one is installed.
    double mapBeatToX(double beat) const noexcept;
    double mapXToBeat(double x) const noexcept;
    double mapPixelsPerBeat() const noexcept;
    double mapFirstVisibleBeat() const noexcept;
    double snappedBeatAtX(double x) const noexcept;
    double currentBeatsPerBar() const noexcept;
    void postLoopIfChanged(const juce::MouseEvent& e);
    void postSeekIfChanged(const juce::MouseEvent& e);
    // Re-arms an existing-but-disabled loop when a no-drag loop-zone click lands inside its span.
    // Inert in every other case (looping already on, no range, click outside the brace).
    void reArmLoopIfClickOnInactiveBrace(const juce::MouseEvent& e);
    void setHoveredZone(std::optional<Zone> zone);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;

    // Non-owning; set/cleared by the panel around the piano roll's open/close (see
    // setMappingOverride). The pointee outlives the override window — both live on the panel.
    const TimelineViewState* overrideView_ = nullptr;
    int overrideOffsetPx_ = 0;

    // Gesture state (message thread only; mouseDown/mouseDrag/mouseUp are always dispatched from
    // there). gestureZone_ is latched in mouseDown.
    Zone gestureZone_ = Zone::Playhead;
    double dragAnchorBeat_ = 0.0;
    // Sentinel (never a valid posted beat, since every posted beat is clamped to >= 0) meaning
    // "nothing posted yet this gesture".
    double lastPostedLoopStart_ = -1.0;
    double lastPostedLoopEnd_ = -1.0;
    double lastPostedSeekBeat_ = -1.0;
    int seekPostCount_ = 0;

    std::optional<Zone> hoveredZone_;

    // ---- Markers (message thread only, like every other gesture member here) ----
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    juce::PropertiesFile* propertiesFile_ = nullptr;

    // The marker a drag is in flight on, its snapped preview beat, and the beat offset between the
    // pointer and the marker at grab time (so the flag doesn't jump to sit under the cursor).
    synth::MarkerId draggingMarker_;
    double markerDragBeat_ = 0.0;
    double markerDragGrabOffsetBeats_ = 0.0;
    // A press that never moved commits nothing — the same rule the panel's resize handle follows,
    // so a stray click on a flag can't quietly re-snap the marker it landed on.
    bool markerDragMoved_ = false;

    synth::MarkerId hoveredMarker_;

    // The inline rename editor (see beginRenameMarker) — null when no rename is open.
    std::unique_ptr<juce::TextEditor> renameEditor_;
    synth::MarkerId renamingMarker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRulerComponent)
};

} // namespace synth::ui
