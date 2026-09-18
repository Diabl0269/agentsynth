#pragma once

#include "PianoRollTypes.h"
#include "Timeline/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/PianoRoll/NoteColour.h"
#include "UI/PianoRoll/NoteSelectionModel.h"
#include "UI/PianoRoll/ScaleAssistPanel.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineViewState.h"
#include <array>
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class ShortcutManager; // Forward declaration (Source/ShortcutManager/ShortcutManager.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// PianoRollComponent — the per-clip note editor shown INSIDE the timeline panel's lanes region (no
// separate window). TimelinePanelComponent swaps this in for synth::ui::TimelineClipLaneArea (same
// rect, same z-order slot, below the playhead overlay) when a clip is double-clicked, and swaps
// back on the back button, Escape-with-nothing-selected, or the edited clip disappearing.
//
// Non-owning refs/pointers (TimelineViewState&, TimelineDoc* / AppUndoManager* / TransportService*)
// may be null and degrade to "read but don't mutate". Every edit previews locally and commits to
// the doc exactly once on mouse-up via AppUndoManager::recordTimelineChange, so a multi-note
// move/resize/velocity-scrub/delete is ONE undo step however many notes it touches.
//
// The roll owns its OWN horizontal (beatToX/xToBeat) and vertical (yForPitch/pitchForY) mapping,
// independent of the panel-wide TimelineViewState (consulted only for the shared snap division) and
// of the panel-wide TimelinePlayheadOverlay (this class implements
// TimelinePlayheadOverlay::LocalPlayheadClient to draw its own playhead instead) — see
// PianoRollComponent.cpp for the full coordinate-system contract every unit in this directory
// shares. Notes are clip-relative in the doc (MidiNote::startBeat); every doc read/write here
// converts to absolute beats via clip->startBeat and back.
//
// See docs/timeline_panel_piano_roll.md §2 (TL5-8) for the gesture table.
namespace synth::ui {

class PianoRollComponent
    : public juce::Component
    , public juce::TooltipClient
    , public TimelinePlayheadOverlay::LocalPlayheadClient
    , private juce::Timer {
public:
    // Piano-roll-only constants; not shared with Theme::Metrics.
    static constexpr int kKeysColumnWidth = 44;
    // The scale-assist panel's fixed width when open, carved from the LEFT of the keys column —
    // see leftGutterWidth() and PianoRollComponent.cpp's coordinate-system contract.
    static constexpr int kScalePanelWidth = 170;
    // The CHIP TOOLBAR row's height — the roll's own chrome strip, at the very top of its rect and
    // ABOVE the ruler band (see setRulerBandHeight and the layout note in resized()). Named as its own
    // constant, and the single place the row's height is decided, because more context toolbars are
    // expected here: adding one is a second `removeFromTop` in the one carve-up, not a hunt through
    // every y-coordinate in the file.
    static constexpr int kToolbarHeight = 20;
    // Default vertical zoom, and its clamps (Cmd+Shift+wheel scales it — see mouseWheelMove).
    static constexpr double kPixelsPerSemitone = 10.0;
    static constexpr double kMinPixelsPerSemitone = 4.0;
    static constexpr double kMaxPixelsPerSemitone = 40.0;
    // Floor under a new/resized note's length when Snap is Off: 1/16 of a beat, matching
    // TransportService::kMinLoopLengthBeats (finer than the snap selector's finest division —
    // Snap Off is exactly the mode that wants sub-grid freedom).
    // With Snap ON a NEW note is exactly one snap division long (1 bar quantise -> a 1-bar note).
    static constexpr double kMinNoteLengthBeats = 0.0625;
    // Resize handle zone at a note's right edge, in px (no left-edge resize in v1).
    static constexpr int kResizeZonePx = 5;
    // Local playhead line: same width/strip margin the panel overlay uses, so the line reads as one
    // stroke across the ruler and the roll.
    // canvasTop()/setRulerBandHeight()/getRulerBandHeight() — see PianoRollEditTools.cpp.
    int canvasTop() const noexcept;
    void setRulerBandHeight(int heightPx);
    int getRulerBandHeight() const noexcept;

    static constexpr float kPlayheadLineWidth = TimelinePlayheadOverlay::kLineWidth;
    static constexpr int kPlayheadStripHalfWidth = TimelinePlayheadOverlay::kStripHalfWidth;
    // Momentary "Q was pressed" highlight, in ms. Driven by a ONE-SHOT juce::Timer (it stops itself
    // in the first callback) and repainting only the button's own rect — bounded, never a loop.
    static constexpr int kQuantiseFlashMs = 120;
    // Velocity a keys-column press auditions at. Fixed, and deliberately not 127: a virtual keyboard
    // has no velocity sensor, and previewing everything at full blast misrepresents how the patch
    // actually sounds under the notes the user is writing. ~0.8 of full scale.
    static constexpr int kKeysColumnVelocity = 102;

    explicit PianoRollComponent(TimelineViewState& viewState);
    ~PianoRollComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    // Cmd+wheel        -> horizontal zoom around the beat under the cursor
    // Cmd+Shift+wheel  -> vertical zoom (pixels per semitone) around the pitch under the cursor
    // Shift+wheel / trackpad deltaX -> horizontal scroll
    // plain wheel      -> vertical (pitch) scroll
    // Nothing bubbles to the panel: the roll's zoom/scroll are its own, so the shared
    // TimelineViewState must not move when the wheel lands here. See PianoRollMouse.cpp for the
    // full per-branch contract.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    // Trackpad pinch: plain = horizontal zoom around the pinch point, Shift = vertical zoom —
    // the same pair TimelinePanelComponent::mouseMagnify binds for the lanes.
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    // Hover-only work: the Split tool's cut-position preview and the Select tool's resize-zone
    // cursor — see PianoRollMouse.cpp for the state-change gating that keeps this free while idle.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    // Theme switch: drops the tool-cursor cache and re-applies the active tool's cursor — see
    // PianoRollEditTools.cpp.
    void lookAndFeelChanged() override;
    // Ends any note audition in flight when hidden — see onAuditionNote and PianoRollAudition.cpp.
    void visibilityChanged() override;

    // Panel-scoped Delete/Escape. Returns false (key falls through) when there is nothing to act
    // on, the same TimelineClipLaneArea contract.
    bool keyPressed(const juce::KeyPress& key) override;

    // juce::TooltipClient — the "Q" button's tooltip (the header's buttons are drawn shapes, not
    // child juce::Buttons, so the tooltip is resolved by position).
    juce::String getTooltip() override;
    juce::String getTooltipFor(juce::Point<int> pos) const;

    // Non-owning; may be null. Same degrade-gracefully contract as every other timeline
    // sub-component's setter.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept;
    synth::TimelineDoc* getTimelineDoc() const noexcept;
    void setUndoManager(AppUndoManager* undoManager) noexcept;
    AppUndoManager* getUndoManager() const noexcept;
    // Only consulted for Snap::Bar's beatsPerBar, same reasoning as
    // TimelineClipLaneArea::setTransport.
    void setTransport(synth::TransportService* transport) noexcept;

    // The user's keyboard bindings for this surface's OWN keys — see PianoRollZoom.cpp for the full
    // resolution contract.
    void setShortcutManager(const ShortcutManager* manager) noexcept;
    const ShortcutManager* getShortcutManager() const noexcept;

    // The app-level scroll/zoom-wheel-invert preferences — see PianoRollZoom.cpp.
    void setScrollInverted(bool inverted) noexcept;
    bool isScrollInverted() const noexcept;
    void setZoomScrollInverted(bool inverted) noexcept;
    bool isZoomScrollInverted() const noexcept;

    // Anchored horizontal/vertical zoom (menu/keyboard path, not the wheel) — see PianoRollZoom.cpp.
    void zoomHorizontal(double factor);
    void zoomVertical(double factor);

    // ---- Keys column labels ----

    // AllNotes labels every key row (subject to the row-height floor below); OctavesOnly labels
    // only the C rows — see keyLabelFor (PianoRollScaleAssist.cpp) for the per-row decision.
    enum class KeyLabelMode { AllNotes, OctavesOnly };

    void setKeyLabelMode(KeyLabelMode mode) noexcept;
    KeyLabelMode getKeyLabelMode() const noexcept;

    static juce::String keyLabelFor(int pitch, KeyLabelMode mode, int rowHeightPx);
    // The colour paintKeysColumn draws a row's label in — see PianoRollPainting.cpp.
    static juce::Colour labelColourFor(juce::Colour keyFill) noexcept;

    // ---- Note colouring & scale context ----

    // The per-pitch-class colour overrides (NoteColour.h) a note's fill is resolved through. See
    // synth::ui::resolveNoteColour for the precedence (out-of-scale beats an override beats the
    // theme default).
    void setNoteColourOverrides(const synth::ui::NoteColourOverrides& overrides);
    const synth::ui::NoteColourOverrides& getNoteColourOverrides() const noexcept;

    // The scale the grid checks notes/rows against — `isInScale` empty means no scale at all; see
    // PianoRollComponent.cpp for the pitchVisibilityOn/out-of-scale contract in full.
    void setScaleContext(std::function<bool(int)> isInScale, bool pitchVisibilityOn);

    // ---- Scale assist panel ----

    // Non-owning; may stay null (tests pass null) — see PianoRollScaleAssist.cpp.
    void setPropertiesFile(juce::PropertiesFile* props);

    synth::ui::ScaleAssistPanel& getScaleAssistPanel() noexcept;
    const synth::ui::ScaleAssistPanel& getScaleAssistPanel() const noexcept;
    void toggleScalePanel();
    juce::Rectangle<int> getScaleButtonBounds() const noexcept;

    // quantisePitchesToScale/quantisePitchesToActiveScale/isPitchQuantiseEnabled — see
    // PianoRollScaleAssist.cpp for the full contract (undo-step shape, which scale each resolves).
    void quantisePitchesToScale(const synth::MusicalScale& scale);
    bool quantisePitchesToActiveScale();
    bool isPitchQuantiseEnabled() const;

    // extendClipTo/applyExtendPromptAnswer — see PianoRollAudition.cpp for the full contract
    // (why the clip id is captured explicitly, what each overrun-prompt arm does).
    bool extendClipTo(synth::ClipId clipId, double lengthBeats);
    void applyExtendPromptAnswer(synth::ClipId clipId, double requiredLengthBeats, bool extend);

    // Fills the open clip with a fresh random pattern — see PianoRollScaleAssist.cpp for the full
    // contract (addToExisting replace-vs-overlay semantics, grid-step/scale resolution).
    void generateRandomNotesIntoClip(const synth::MusicalScale* scale, int minPitch, int maxPitch, juce::Random& rng,
                                     bool addToExisting = false);

    // ---- Edit tools (Cubase-style; see EditTool.h) ----

    // The active tool — see PianoRollEditTools.cpp for the Select-vs-other-five gesture contract.
    void setActiveTool(EditTool tool);
    EditTool getActiveTool() const noexcept;

    // ---- Note clipboard ----

    // ClipboardNote is defined in PianoRollTypes.h; re-exposed here as a nested-type alias so
    // PianoRollComponent::ClipboardNote keeps resolving exactly as it did when it was nested.
    using ClipboardNote = pianoroll::ClipboardNote;

    // copySelectedNotes/canPasteNotes/pasteNotesAtPlayhead/duplicateSelectedNotes/cutSelectedNotes/
    // selectAllNotes/repeatSelectedNotes — see PianoRollClipboardAndKeys.cpp for the full contract
    // of each (anchor/paste rules, undo-step shape, clipboard-vs-duplicate distinction).
    bool copySelectedNotes();
    bool canPasteNotes() const noexcept;
    bool pasteNotesAtPlayhead();
    bool duplicateSelectedNotes();
    bool cutSelectedNotes();
    bool selectAllNotes();
    bool repeatSelectedNotes(int count);
    bool hasNoteSelection() const noexcept;

    // ---- Entry/exit (panel API surface: openPianoRoll/closePianoRoll/isPianoRollOpen forward
    // straight to these three) ----

    // openClip/closeRoll — see PianoRollComponent.cpp for the full open/close contract.
    void openClip(synth::ClipId id);
    void closeRoll();
    bool isOpen() const noexcept;
    synth::ClipId getClipId() const noexcept;

    // Fired when the roll asks to be closed (back button, Escape, or the edited clip disappearing)
    // — not fired by a direct closeRoll() call. See PianoRollComponent.cpp (refreshFromDoc).
    std::function<void()> onCloseRequested;

    // Fired after toggleSnap() flipped the shared snap flag — see PianoRollClipboardAndKeys.cpp.
    std::function<void()> onSnapToggled;

    // Fired whenever the roll's OWN horizontal mapping changed — see PianoRollComponent.cpp.
    std::function<void()> onHorizontalViewChanged;

    /** NOTE AUDITION — "clicking a note plays it". Fired with `on == true` on a mouse-down that hits
     *  a note (Select tool only), `on == false` on the release, and as a noteOff/noteOn PAIR
     *  whenever a Move drag carries the grabbed note onto a different pitch.
     *
     *  THE CONTRACT, because a missed `false` is a note stuck on until the app quits: exactly one
     *  `false` follows every `true`, emitted from mouse-up, a gesture cancelled by a tool switch,
     *  openClip/closeRoll, visibilityChanged, and the destructor. See PianoRollAudition.cpp. */
    std::function<void(int pitch, float velocity01, bool on)> onAuditionNote;

    // The roll's own zoom/scroll, exposed for the panel's ruler mapping override.
    const TimelineViewState& getRollViewState() const noexcept;

    // kKeysColumnWidth plus the scale-assist panel's CURRENT animated width — see
    // PianoRollComponent.cpp for the full seam contract.
    int leftGutterWidth() const noexcept;

    // Flips the shared snap switch, flashes the Snap chip and fires onSnapToggled — see
    // PianoRollClipboardAndKeys.cpp.
    void toggleSnap();

    // "Show only scale notes" — the row filter; see PianoRollComponent.cpp for the full contract.
    void toggleScaleFilter();
    bool isScaleFilterOn() const noexcept;
    bool isRowFilterActive() const noexcept;

    // ---- Follow playhead ----

    // When on, setPlayheadBeat page-flips the roll's own horizontal view rather than letting it
    // scroll off the edge — see PianoRollPainting.cpp (setPlayheadBeat) and PianoRollMouse.cpp
    // (autoScrollTick, which this gates against) for the full contract.
    void setFollowPlayhead(bool follow) noexcept;
    bool isFollowPlayhead() const noexcept;

    // THE refresh seam, called on every doc mutation — see PianoRollComponent.cpp.
    void refreshFromDoc();

    // ---- The roll's OWN horizontal mapping ----

    // Absolute beat <-> this component's x — see PianoRollComponent.cpp.
    double beatToX(double absBeat) const noexcept;
    double xToBeat(double x) const noexcept;

    double getPixelsPerBeat() const noexcept;
    double getFirstVisibleBeat() const noexcept;
    double getPixelsPerSemitone() const noexcept;

    // Pins the horizontal mapping directly — see PianoRollComponent.cpp.
    void setHorizontalView(double pixelsPerBeat, double firstVisibleBeat);
    void setPixelsPerSemitone(double pixelsPerSemitone);

    // ---- TimelinePlayheadOverlay::LocalPlayheadClient ----
    // While open, the panel overlay hands the drawn beat here instead of drawing inside this rect
    // — see PianoRollPainting.cpp (setPlayheadBeat) for the full contract.
    bool isLocalPlayheadActive() const override { return isOpen(); }
    void setPlayheadBeat(double absoluteBeat) override;

    double getPlayheadBeat() const noexcept;
    int getPlayheadLineX() const noexcept;
    bool hasPlayheadPosition() const noexcept;

    // ---- Test hooks (mirrors TimelineClipLaneArea's getClipRect / isMarqueeActiveForTest); each
    // one's contract is documented next to its out-of-line definition in the matching
    // PianoRoll<Concern>.cpp unit ----
    juce::Rectangle<int> getBackButtonBounds() const noexcept;
    juce::Rectangle<int> getQuantiseButtonBounds() const noexcept;
    juce::Rectangle<int> getQuantiseLengthButtonBounds() const noexcept;
    juce::Rectangle<int> getQuantisePitchButtonBounds() const noexcept;
    juce::Rectangle<int> getScaleFilterButtonBounds() const noexcept;
    juce::Rectangle<int> getKeysColumnBounds() const noexcept;
    juce::Rectangle<int> getNoteGridBounds() const noexcept;
    int getFirstVisiblePitchForTest() const noexcept;
    double getTopRowPositionForTest() const noexcept;
    void setTopRowPositionForTest(double position) noexcept;
    double getMinTopRowPositionForTest() const noexcept;
    double getMaxTopRowPositionForTest() const noexcept;
    bool isMarqueeActiveForTest() const noexcept;
    NoteSelectionModel& getSelectionForTest() noexcept;

    void tickAutoScrollForTest();
    bool isAutoScrollTimerRunningForTest() const noexcept;

    const std::vector<int>& getVisiblePitchesForTest() const noexcept;
    int blackKeyInsetForTest() const noexcept;

    synth::ui::NotePaint notePaintFor(const synth::MidiNote& note) const;

    double getGridDivisionForTest() const noexcept;
    double getDrawnGridDivisionForTest() const noexcept;
    int getGridLineCountForTest(double spacingBeats) const noexcept;

    bool isQuantiseFlashingForTest() const noexcept;
    bool isQuantiseEnabled() const;

    juce::Rectangle<int> getNoteRect(synth::NoteId id) const;

    int yForPitch(int pitch) const noexcept;
    int pitchForY(int y) const noexcept;

    bool hasSplitPreviewForTest() const noexcept;
    double getSplitPreviewBeatForTest() const noexcept;
    synth::NoteId getSplitPreviewNoteForTest() const noexcept;
    int getClipboardSizeForTest() const noexcept;
    double getDrawPreviewLengthForTest() const noexcept;

    void setScalePanelOpenProgressForTest(float progress);
    float getScalePanelOpenProgressForTest() const noexcept;
    float getScalePanelAnimFromForTest() const noexcept;
    bool isScalePanelAnimatingForTest() const noexcept;
    bool isScalePanelTargetVisibleForTest() const noexcept;

    int getAuditionPitchForTest() const noexcept;
    bool isAuditionActiveForTest() const noexcept;
    int getPressedKeyForTest() const noexcept;

    double getResizeDeltaForTest() const noexcept;
    int getResizeNoteCountForTest() const noexcept;
    bool isResizeUnquantizedForTest() const noexcept;
    double getLastExtendPromptLengthForTest() const noexcept;
    synth::ClipId getLastExtendPromptClipForTest() const noexcept;

    // Six header chips, left to right: Back ("Clips"), Quantise, QuantiseLength, QuantisePitches,
    // Scale, ScaleFilter. ScaleFilter is a TOGGLE (it paints lit); the other four are actions.
    enum class HeaderButtonId { None, Back, Quantise, QuantiseLength, QuantisePitches, Scale, ScaleFilter };
    HeaderButtonId getHoveredHeaderButtonForTest() const noexcept;
    bool isHeaderButtonHoveredForTest(HeaderButtonId which) const noexcept;

protected:
    // Paint-count seams for tests (the local playhead line, the Split-tool hover preview, and the
    // header buttons' hover wash respectively) — see PianoRollPainting.cpp for the full contract.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);
    virtual void requestRepaintPreviewStrip(juce::Rectangle<int> strip);
    virtual void requestRepaintHeaderButtonStrip(juce::Rectangle<int> strip);

    /** Raised on mouse-up when a resize leaves a note past the clip's end — asks whether to grow the
     *  clip to fit or leave it overrunning. Protected virtual so a headless test can override it
     *  instead of the real async juce::AlertWindow; see PianoRollAudition.cpp for the full contract. */
    virtual void promptExtendClipToFitNotes(synth::ClipId clipId, double requiredLengthBeats);

    // THE edge-auto-scroll timer's seam — see PianoRollMouse.cpp for the full contract.
    virtual void autoScrollTick();

private:
    enum class DragMode { None, Move, Resize, Marquee, VelocityScrub, DrawNew };

    // NoteHit/NoteOrigin are defined in PianoRollTypes.h; re-exposed as nested-type aliases so
    // PianoRollComponent::NoteHit / ::NoteOrigin keep resolving exactly as when they were nested.
    using NoteHit = pianoroll::NoteHit;
    using NoteOrigin = pianoroll::NoteOrigin;

    std::optional<NoteHit> hitTestNote(juce::Point<int> pos) const;
    std::vector<std::pair<synth::NoteId, juce::Rectangle<int>>> collectNoteRects() const;
    juce::Rectangle<int> computeNoteRect(double absStartBeat, double absLengthBeats, int pitch) const;
    double currentBeatsPerBar() const;
    // currentGridBeats (magnetism only) vs drawnGridBeats (drawing only, survives snap-off) — see
    // PianoRollScaleAssist.cpp for why the two must never be merged back into one function.
    double currentGridBeats() const;
    double drawnGridBeats() const;
    double snappedBeatAt(double rawBeat) const;
    void clampToClipWindow(double& start, double& length) const;
    juce::Rectangle<int> gridRegion() const noexcept;

    // ---- Row mapping (visiblePitches_) ---- see PianoRollComponent.cpp for the full contract of
    // rebuildVisiblePitches/nearestVisibleRowIndex/rowShiftedPitch (called from setScaleContext,
    // openClip, refreshFromDoc; the seam every pitch<->row path routes through).
    void rebuildVisiblePitches();
    size_t nearestVisibleRowIndex(int pitch) const noexcept;
    int rowShiftedPitch(int originPitch, long long rowDelta) const noexcept;

    // ---- topRowPosition_ (the continuous vertical scroll anchor) ----
    // setTopRowPosition/minTopRowPosition/maxTopRowPosition — see PianoRollComponent.cpp for the
    // full contract, including why the anchor is kept fractional rather than a whole row.
    bool setTopRowPosition(double raw) noexcept;
    double minTopRowPosition() const noexcept;
    double maxTopRowPosition() const noexcept;

    synth::ui::NotePaint resolveNoteColourFor(int pitch, int velocity, bool selected, bool muted) const;

    // Pixel width of the narrower black-key overlay — see PianoRollPainting.cpp.
    static constexpr float kBlackKeyWidthFraction = 0.62f;
    static int blackKeyWidthPx(int columnWidth) noexcept;

    // NoteGeometry/LineRange are defined in PianoRollTypes.h; re-exposed as nested-type aliases so
    // PianoRollComponent::NoteGeometry / ::LineRange keep resolving exactly as when nested.
    using NoteGeometry = pianoroll::NoteGeometry;
    using LineRange = pianoroll::LineRange;
    NoteGeometry effectiveGeometryFor(const synth::MidiNote& note) const;

    // createNoteAt/computeNewNoteAnchor/commitNewNote/beginMoveOrResize/beginVelocityScrub/
    // beginMarquee/updateMarquee/endMarquee — gesture-start helpers; see PianoRollEditTools.cpp for
    // the full contract of each (snap-vs-floor anchor rules, the one-undo-step shape).
    void createNoteAt(juce::Point<int> pos);
    bool computeNewNoteAnchor(juce::Point<int> pos, bool floorToGrid, double& startOut, double& lengthOut,
                              int& pitchOut) const;
    void commitNewNote(double startBeat, double lengthBeats, int pitch);
    void beginMoveOrResize(const NoteHit& hit, juce::Point<int> pos);
    void beginVelocityScrub(juce::Point<int> pos);
    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    // ---- Edge auto-scroll (see EdgeAutoScroll.h) ---- updateDragPreviewFromLastPointer/
    // updateAutoScrollArming — see PianoRollMouse.cpp for the full contract.
    void updateDragPreviewFromLastPointer();
    void updateAutoScrollArming();

    // ---- Tool gestures (everything below acts on a single click; see setActiveTool) ---- see
    // PianoRollEditTools.cpp for the full contract of each.
    void handleToolMouseDown(juce::Point<int> pos);
    std::optional<double> splitBeatFor(const synth::MidiNote& note, int x) const;
    void performSplit(synth::NoteId id, juce::Point<int> pos);
    std::optional<synth::NoteId> glueCandidateFor(const synth::MidiNote& note) const;
    void performGlue(synth::NoteId id);
    void performErase(synth::NoteId id);
    void performMuteToggle(synth::NoteId id);

    // ---- Split-tool hover preview ----
    void updateSplitPreview(juce::Point<int> pos);
    void clearSplitPreview();
    juce::Rectangle<int> splitPreviewStrip() const;

    // ---- Tool cursors (built once per theme, never per mouse move) ----
    void rebuildToolCursors();
    juce::MouseCursor cursorForActiveTool();
    void applyToolCursor();
    void updateHoverCursor(juce::Point<int> pos);

    // ---- Header button hover (Back/Quantise/Scale chips — Task D affordance) ---- see
    // PianoRollPainting.cpp for the full contract.
    juce::Rectangle<int> headerButtonBoundsFor(HeaderButtonId which) const noexcept;
    void updateHeaderButtonHover(juce::Point<int> pos);

    // ---- Clipboard plumbing ---- see PianoRollClipboardAndKeys.cpp for the full contract of each
    // (anchor-block rules, clip-window clamp, one-undo-step shape).
    std::vector<ClipboardNote> captureSelectionEntries(double& earliestStartOut, double& spanBeatsOut) const;
    bool buildPastedNotes(const std::vector<ClipboardNote>& entries, double anchorBeat,
                          std::vector<synth::MidiNote>& out) const;
    bool commitPastedNotes(const std::vector<synth::MidiNote>& notes);

    // ---- Arrow-key editing ---- see PianoRollClipboardAndKeys.cpp for the full contract
    // (shared-delta clamping, row-vs-semitone stepping).
    bool nudgeSelectedNotes(int direction);
    bool transposeSelectedNotes(int semitones);
    bool transposeSelectedNotesByRow(int rowDelta);

    // ---- Alt+Left/Right note navigation (selection only — never a mutation, never an undo step)
    // ---- see PianoRollClipboardAndKeys.cpp for the full contract (canonical order, anchor edge).
    bool selectAdjacentNote(bool forward);
    void scrollNoteIntoView(const synth::MidiNote& note);

    // ---- Rebindable surface keys ---- see PianoRollZoom.cpp for the full resolution contract
    // (strict-once-installed, the Shift-chord normalisation matchesAction routes through).
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;

    // ---- Dynamic shortcut-hint tooltips (see synth::shortcutHintFor) ---- rebuilt fresh on every
    // call, no cache needed — see PianoRollAudition.cpp.
    juce::String quantiseTooltipText() const;
    juce::String quantiseLengthTooltipText() const;
    juce::String quantisePitchTooltipText() const;
    juce::String scaleTooltipText() const;
    juce::String scaleFilterTooltipText() const;

    // ---- Header chip glyphs (drawn vector paths — see paintHeader, PianoRollPainting.cpp) ----
    static void drawQuantiseGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour);
    static void drawQuantiseLengthGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour);
    static void drawQuantisePitchGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour);
    static void drawScaleFilterGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour);

    // The in-flight resize's length for one snapshotted note — see PianoRollScaleAssist.cpp.
    double resizePreviewLengthFor(const NoteOrigin& origin) const noexcept;

    // ---- Anchored zoom, shared by the wheel, the pinch and the public zoom API ---- see
    // PianoRollZoom.cpp.
    void zoomHorizontalAroundX(double factor, double anchorGridX);
    void zoomVerticalAroundY(double factor, double anchorY);
    double wheelZoomFactor(const juce::MouseWheelDetails& wheel) const noexcept;

    void performQuantise();
    void performQuantiseLength();
    void flashQuantiseButton();
    void flashQuantiseLengthButton();
    void timerCallback() override; // one-shot: ends the quantise flash and stops itself
    void requestClose();

    double maxNoteEndAmong(const std::vector<synth::NoteId>& ids) const;

    // ---- Note audition (see onAuditionNote) ---- see PianoRollAudition.cpp for the full contract.
    void startAudition(int pitch, int velocity);
    void retriggerAudition(int pitch);
    void stopAudition();

    // ---- Scale assist panel plumbing (toggleScalePanel is public — see the accessors above) ----
    // see PianoRollScaleAssist.cpp for the full contract of each.
    void setScalePanelVisible(bool visible, bool animate = true);
    void applyScalePanelOpenProgress(float progress);
    void finishScalePanelAnimation();
    std::optional<synth::MusicalScale> activeScaleForOpenClip() const;
    void pushScaleContextFromMemory();
    void restoreScaleMemoryForOpenClip();

    // ---- Keys-column audition ---- see PianoRollAudition.cpp for the full contract.
    bool isKeysColumnPoint(juce::Point<int> pos) const noexcept;
    void beginKeysColumnPress(juce::Point<int> pos);
    void updateKeysColumnPress(juce::Point<int> pos);
    void endKeysColumnPress();
    juce::Rectangle<int> keyRowRect(int pitch) const noexcept;

    void paintKeysColumn(juce::Graphics& g);
    void paintHeader(juce::Graphics& g);
    void paintGrid(juce::Graphics& g);
    // `lineColour`/`background` feed the shared three-level colour policy — see PianoRollPainting.cpp.
    void paintGridLines(juce::Graphics& g, juce::Colour lineColour, juce::Colour background);
    void paintNote(juce::Graphics& g, const synth::MidiNote& note);
    void paintPlayhead(juce::Graphics& g);
    void paintMarquee(juce::Graphics& g);
    void paintDrawPreview(juce::Graphics& g);
    void paintSplitPreview(juce::Graphics& g);

    // LineRange is defined in PianoRollTypes.h (aliased above, near NoteGeometry).
    LineRange visibleLineRange(double spacingBeats) const noexcept;

    juce::Rectangle<int> playheadStripFor(int x) const noexcept;

    TimelineViewState& viewState_; // shared: SNAP ONLY (see the class comment)
    // The roll's own zoom/scroll. Reuses TimelineViewState purely for its clamped
    // zoomAroundX/scrollBeats math; its x origin is the GRID's left edge, which beatToX/xToBeat
    // offset by leftGutterWidth().
    TimelineViewState rollView_;
    double pixelsPerSemitone_ = kPixelsPerSemitone;

    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    // Non-owning, may stay null (see setShortcutManager). const because the roll only ever READS
    // bindings — rebinding is the Settings window's job.
    const ShortcutManager* shortcuts_ = nullptr;
    // App-level scroll-direction preference; false == natural == the juce::Viewport convention.
    bool scrollInverted_ = false;
    // App-level ZOOM-direction preference; false == wheel up zooms in. A separate flag from
    // scrollInverted_ — see setZoomScrollInverted.
    bool zoomScrollInverted_ = false;
    synth::ClipId clipId_;
    NoteSelectionModel selection_;

    // The continuous vertical scroll anchor: the FRACTIONAL index into visiblePitches_ of the row
    // whose top edge sits at y == canvasTop() — see PianoRollComponent.cpp's coordinate-system
    // contract and setTopRowPosition (the one seam every writer goes through). Replaces the old
    // "truncate to a whole row, carry the remainder" scheme (a separate pitchScrollRemainder_
    // accumulator) with the position itself simply staying fractional — the accumulator is
    // redundant once there is nothing left to round away. Default 60.0 matches
    // firstVisiblePitch_'s old default (row index == pitch value in the unfiltered/default case).
    double topRowPosition_ = 60.0;

    // firstVisiblePitch_ is the HIGHEST pitch drawn at the grid's top row (y == canvasTop()),
    // clamped to [0, 127] — see yForPitch/pitchForY. Named to match the design doc's "scroll
    // position", even though (unlike TimelineViewState::firstVisibleBeat, the beat at x==0) this
    // one is the TOP of the range rather than conceptually its start, because pitch increases
    // upward while beats increase rightward. DERIVED from topRowPosition_ (never written
    // directly outside setTopRowPosition) — visiblePitches_[floor(clamp(topRowPosition_))] — so it
    // is always a member of visiblePitches_, exactly like before; see PianoRollComponent.cpp.
    int firstVisiblePitch_ = 60;

    // ---- Row mapping / scale context ----
    // Every pitch that currently gets a row, ascending. All 128 when no filter is active — see
    // rebuildVisiblePitches. Populated in the constructor so yForPitch/pitchForY are well-defined
    // even before openClip or setScaleContext is ever called.
    std::vector<int> visiblePitches_;
    // Empty (the default) means "no scale at all" — see setScaleContext.
    std::function<bool(int)> isInScale_;
    bool pitchVisibilityOn_ = false;

    // ---- Note colouring ----
    synth::ui::NoteColourOverrides overrides_;

    // ---- Keys column label density ----
    KeyLabelMode keyLabelMode_ = KeyLabelMode::AllNotes;

    DragMode dragMode_ = DragMode::None;
    synth::NoteId activeNote_;
    juce::Point<int> mouseDownPos_;
    // The BEAT under the pointer at mouseDown (xToBeat(mouseDownPos_.x)), captured alongside the
    // pixel position — every Move/DrawNew drag computes its delta as xToBeat(currentX) -
    // mouseDownBeat_ rather than xToBeat(currentX) - xToBeat(mouseDownPos_.x). The pixel form only
    // agrees with the beat form while the view is static; a mid-drag edge-scroll (or, in
    // principle, any other scroll/zoom) moves rollView_'s firstVisibleBeat, and re-deriving
    // xToBeat(mouseDownPos_.x) at that point would silently reinterpret the anchor at the NEW
    // scroll position instead of the one the gesture actually started at. Same reasoning as
    // TimelineClipLaneArea::mouseDownBeat_.
    double mouseDownBeat_ = 0.0;
    // The pitch under the pointer at mouseDown (pitchForY(mouseDownPos_.y)) — the Move drag's
    // vertical anchor, expressed as a PITCH (not a row index) so it survives rebuildVisiblePitches
    // rebuilding the row set mid-gesture; every read of it goes back through
    // nearestVisibleRowIndex to recover its row. See previewDeltaPitch_ for why the anchor has to
    // be row-space at all.
    int mouseDownPitch_ = 60;
    // The last pointer position mouseDrag() saw, in this component's local coordinates. An
    // auto-scroll tick has no MouseEvent of its own — the pointer isn't moving, the view is — so
    // updateDragPreviewFromLastPointer() re-derives the in-flight preview from this instead of
    // from a synthesized event.
    juce::Point<int> lastDragPointer_;

    // ---- Move preview (one or many notes, one shared snapped beat delta + ROW delta) ----
    std::vector<NoteOrigin> dragNotes_;
    double previewDeltaBeats_ = 0.0;
    // A delta in visiblePitches_ ROW INDICES, not semitones: with pitch-visibility collapsing
    // out-of-scale rows, adding a raw semitone count straight to a note's pitch could land it on a
    // pitch that is not itself a row right now. Every consumer (effectiveGeometryFor's Move
    // branch, the mouseUp commit) resolves it through rowShiftedPitch, never by direct addition.
    // In the common unfiltered case (every pitch visible) this is numerically identical to a
    // semitone delta, because row index == pitch there.
    int previewDeltaPitch_ = 0;

    // ---- Resize preview (one or many notes, one shared LENGTH delta) ----
    //
    // previewLength_ is the GRABBED note's own new length — what the pointer literally says, and the
    // only value the snap/floor maths is applied to. previewLengthDelta_ is that minus
    // resizeOriginalLength_, and it is what every OTHER note in resizeNotes_ gets, so a chord
    // resizes by one shared amount rather than every note snapping to the same absolute end (which
    // would collapse a staggered group onto one edge). Same "one shared delta, origins snapshotted
    // at mouse-down" shape as the Move and VelocityScrub previews above.
    double resizeOriginalLength_ = 0.0;
    double previewLength_ = 0.0;
    double previewLengthDelta_ = 0.0;
    // Every note the gesture is resizing: the selection when the grabbed note is part of it (which,
    // after mouseDown's "select what you grabbed" step, it always is), otherwise just the grabbed
    // note. Separate from dragNotes_ so a Resize and a Move can never read each other's snapshot.
    std::vector<NoteOrigin> resizeNotes_;
    // Cmd+drag on a right edge: the grid is bypassed entirely for this gesture (11.2), so the note's
    // end follows the raw beat under the pointer and the length floor drops to kMinNoteLengthBeats.
    // Latched at mouse-down and never re-read from the live modifiers — a gesture must not change
    // meaning half way through because the user let go of Cmd.
    bool resizeUnquantized_ = false;
    // Cmd+drag on a note BODY: the move ignores the grid, the same way Cmd+drag on the right EDGE
    // ignores it for a resize (one modifier, one meaning — "do this smoothly"). Latched at mouse-down
    // for the same reason resizeUnquantized_ is.
    bool moveUnquantized_ = false;
    // Cmd+CLICK on a note is an additive-select TOGGLE, but Cmd+DRAG is an unsnapped move — and at
    // mouse-down the two are indistinguishable. So the note is ADDED immediately (the drag needs it in
    // the selection) and, if the gesture turns out not to have moved anything, mouse-up undoes that:
    // a note that was ALREADY selected before the press is removed, completing the toggle. Same
    // deferred-classification trick pendingEmptyClick_ uses for the empty-grid press.
    synth::NoteId cmdToggleNote_;
    bool cmdToggleWasSelected_ = false;
    // The length the last overrun prompt asked for (0.0 = never prompted) — a test hook, and the one
    // piece of the prompt that outlives the async alert. See promptExtendClipToFitNotes.
    double lastExtendPromptLength_ = 0.0;
    // See setRulerBandHeight. 0 = no ruler above us, which is the standalone/test geometry.
    int rulerBandHeight_ = 0;
    // The clip that prompt was raised FOR. Recorded alongside the length purely so a test can pin
    // the capture; the real answer path carries the id in the alert's own callback, not through here
    // (a member would be overwritten by a second prompt before the first was answered).
    synth::ClipId lastExtendPromptClip_;

    // ---- Keys-column audition (a virtual keyboard down the roll's left gutter) ----
    // The pitch whose key is currently held down there, or -1. Distinct from auditionPitch_: that is
    // "what is sounding" (a note click sets it too), this is "the KEY the pointer is on", and it is
    // what paintKeysColumn draws pressed.
    int keysColumnPitch_ = -1;
    bool keysColumnPressing_ = false;

    // ---- Note audition (see onAuditionNote) ----
    // auditionActive_ is the single source of truth for "we have emitted a noteOn nobody has matched
    // with a noteOff yet". Every cancel path calls stopAudition(), which is a no-op when this is
    // false — so calling it unconditionally is always safe and never double-fires.
    bool auditionActive_ = false;
    int auditionPitch_ = -1;
    // The velocity the gesture started on, carried across a drag retrigger: a Move drag changes
    // pitch, never velocity, so re-reading the (possibly mid-preview) note would be both wrong and
    // dependent on the doc still holding the note.
    int auditionVelocity_ = 100;

    // ---- Velocity-scrub preview (one or many notes, one shared delta) ----
    int previewDeltaVelocity_ = 0;

    // ---- Draw-tool preview (a note that does not exist in the doc yet) ----
    double drawStartBeat_ = 0.0; // clip-relative
    double drawLengthBeats_ = 0.0;
    int drawPitch_ = 60;

    // ---- Marquee (any drag that STARTED on empty grid — see mouseDown's comment) ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::NoteId> marqueeBaseSelection_;

    // Deferred-deselect, the same trick TimelineClipLaneArea::pendingEmptyClick_ (and, before it,
    // GraphEditor's pendingEmptyCanvasClick) uses: a PLAIN press on empty grid is ambiguous at
    // mouse-down time — it becomes a replace-marquee if it ever moves, and a deselect if it does
    // not — so neither is committed until mouseDrag or mouseUp says which one happened. Only the
    // Select tool ever sets it, and setActiveTool clears it with the rest of the in-flight gesture.
    bool pendingEmptyClick_ = false;

    // ---- Local playhead (fed by TimelinePlayheadOverlay; no timer of our own) ----
    double playheadBeat_ = 0.0;
    int playheadLineX_ = 0;
    bool hasPlayheadX_ = false;
    // See setFollowPlayhead. Off by default.
    bool followPlayhead_ = false;

    bool quantiseFlash_ = false;
    bool quantiseLengthFlash_ = false;

    // ---- Scale assist panel ----
    //
    // A child component, always present (addChildComponent — so it starts invisible), toggled by
    // the header's "Scale" button and restored from PropertiesFile in setPropertiesFile. Owned by
    // value: it outlives openClip/closeRoll exactly like the note clipboard, so switching clips
    // never rebuilds it.
    synth::ui::ScaleAssistPanel scalePanel_;
    juce::Rectangle<int> scaleButtonBounds_;
    // Non-owning; may stay null (see setPropertiesFile). Only ever used to persist THIS roll's own
    // "was the panel open" flag — the panel persists its OWN user scales through the same pointer,
    // handed to it directly in setPropertiesFile.
    juce::PropertiesFile* propertiesFile_ = nullptr;

    // ---- Scale-panel slide animation (in/out — see setScalePanelVisible) ----
    //
    // The LOGICAL target (what the header button paints lit and what persists), separate from
    // scalePanelOpenProgress_'s mid-slide VISUAL value below — the same split
    // ModuleLibraryComponent's collapsedSections/sectionProgress pair uses for its own fold.
    bool scalePanelVisible_ = false;
    // 0 = fully closed, 1 = fully open — leftGutterWidth() is the ONE reader (see its comment).
    // Animated by scalePanelAnim_'s per-frame callback via applyScalePanelOpenProgress(), or
    // pinned straight to its target by finishScalePanelAnimation() when there is no VBlank to
    // animate with (a headless test, a PropertiesFile restore, or a clip switch mid-slide).
    float scalePanelOpenProgress_ = 0.0f;
    // The tween's own start/end, captured at the moment a toggle/restore is requested — kept as
    // members (not lambda captures alone) so a test can assert the START point without a running
    // VBlank (see getScalePanelAnimFromForTest).
    float scalePanelAnimFrom_ = 0.0f;
    float scalePanelAnimTo_ = 0.0f;
    synth::ui::AnimationDriver scalePanelAnim_;
    // Lazily created on the FIRST real (on-screen) toggle — mirrors ModuleLibraryComponent's own
    // vblankUpdater, which is `this` (a juce::Component) and must therefore not exist before the
    // component does.
    std::optional<juce::VBlankAnimatorUpdater> scalePanelVblankUpdater_;
    // Within the house 160-220 ms spec (docs/layout_visuals_animation.md §3), matching MainComponent's own
    // kPanelSlideMs (~190 ms) feel for the app's other show/hide sidebars.
    static constexpr double kScalePanelAnimMs = 200.0;

    // ClipScaleMemory is defined in PianoRollTypes.h; re-exposed as a nested-type alias so
    // PianoRollComponent::ClipScaleMemory keeps resolving exactly as when it was nested.
    using ClipScaleMemory = pianoroll::ClipScaleMemory;
    std::map<synth::ClipId, ClipScaleMemory> clipScaleMemory_;

    // ---- Edge auto-scroll timer ----
    //
    // A NESTED juce::Timer rather than a second responsibility multiplexed onto the class's own
    // private juce::Timer base (used above for the one-shot quantise flash, timerCallback()) —
    // one juce::Timer answering to two unrelated reasons would need a mode flag in every callback,
    // exactly the kind of "which timer is this tick for" bug a dedicated Timer avoids by
    // construction. autoScrollTick() (the protected virtual seam a test overrides) does the real
    // work; this struct only forwards juce::Timer's callback to it.
    struct AutoScrollTimer final : public juce::Timer {
        explicit AutoScrollTimer(PianoRollComponent& ownerRef)
            : owner(ownerRef) {}
        void timerCallback() override { owner.autoScrollTick(); }
        PianoRollComponent& owner;
    };
    AutoScrollTimer autoScrollTimer_{*this};

    // ---- Edit tool + its cursor cache ----
    EditTool activeTool_ = EditTool::Select;
    // Indexed by (size_t)EditTool. Built once (lazily, and again after a theme switch) because
    // rasterising six icons into six juce::Images on every mouse move would be exactly the kind of
    // per-frame work the repaint invariant exists to prevent.
    std::array<juce::MouseCursor, kAllEditTools.size()> toolCursors_;
    bool toolCursorsBuilt_ = false;
    // Select-tool only: whether the resize-zone cursor is the one currently installed, so a hover
    // that stays inside (or outside) the zone does no work at all.
    bool showingResizeCursor_ = false;

    // ---- Split-tool hover preview (the cut line drawn on the hovered note) ----
    synth::NoteId splitPreviewNote_;
    double splitPreviewBeat_ = 0.0; // clip-relative
    bool hasSplitPreview_ = false;

    // ---- Note clipboard: a MEMBER, so a copy survives switching clips (see copySelectedNotes) ----
    std::vector<ClipboardNote> noteClipboard_;

    juce::Rectangle<int> backButtonBounds_;
    // Quantise note STARTS to the grid (a drawn "blocks aligned on gridlines" glyph). An action.
    juce::Rectangle<int> quantiseButtonBounds_;
    // Quantise note LENGTHS to the grid (a drawn "block's trailing edge snapping to a gridline"
    // glyph). An action; Alt+Q's visible twin (see PianoRollZoom.cpp's keyPressed).
    juce::Rectangle<int> quantiseLengthButtonBounds_;
    // Quantise note PITCHES into the scale (a drawn "note head snapping onto a row" glyph).
    juce::Rectangle<int> quantisePitchButtonBounds_;
    // "Show only scale notes" — the row filter, surfaced here as its PRIMARY control (a drawn funnel
    // glyph). A toggle, so it paints lit; shares one state with the scale panel — see
    // toggleScaleFilter().
    juce::Rectangle<int> scaleFilterButtonBounds_;
    juce::Rectangle<int> keysColumnBounds_;
    juce::Rectangle<int> noteGridBounds_;

    // Which header chip (if any) the pointer is currently over — see updateHeaderButtonHover.
    HeaderButtonId hoveredHeaderButton_ = HeaderButtonId::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace synth::ui
