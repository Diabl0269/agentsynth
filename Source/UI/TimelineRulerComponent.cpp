#include "TimelineRulerComponent.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Adaptive density thresholds (paint()). Deterministic and cheap — no font-width measurement —
// so behaviour is identical across platforms/fonts, which is what the guard tests below rely on.
constexpr double kMinLabelSpacingPx = 40.0; // never draw two bar labels closer than this
// The beat-tick / beat-label band edges live in the header next to rulerTickPlanFor(), so the tests
// assert against the same constants paint() reads.
constexpr float kBarLabelFontHeight = 11.0f;
// Sub-labels are the same row, two points smaller and faded: the bar number has to stay the thing
// the eye lands on when scanning the strip.
constexpr float kBeatLabelFontHeight = 9.0f;
constexpr float kBeatLabelAlpha = 0.65f;
constexpr int kBarLabelWidth = 40;
constexpr int kBeatLabelWidth = 34;
constexpr float kLoopBraceHeight = 4.0f;
constexpr float kLoopBraceTickHeight = 8.0f;
// Hover affordance: just enough tint to read which half is armed, not enough to fight the ticks.
constexpr float kHoverBandAlpha = 0.10f;
// A disarmed brace stays fully drawn, just greyed — see braceColourFor().
constexpr float kInactiveBraceAlpha = 0.7f;
// Marker flag chrome. The hover lift is the only state a flag paints differently — a marker has no
// "selected", so there is nothing else to distinguish.
constexpr float kMarkerHoverBrighten = 0.25f;
constexpr float kMarkerStemAlpha = 0.75f;

// Same formula as TransportService::getPosition() (a beat is always a quarter note, regardless of
// the file's notated denominator) — kept in sync there rather than shared, since that one lives on
// the audio-thread-facing side and this is message-thread-only UI.
double beatsPerBarFrom(int numerator, int denominator) noexcept {
    return (double)numerator * 4.0 / (double)std::max(1, denominator);
}
} // namespace

//==============================================================================
TimelineRulerComponent::TimelineRulerComponent(TimelineViewState& viewState)
    : viewState_(viewState) {}

//==============================================================================
double TimelineRulerComponent::currentBeatsPerBar() const noexcept {
    if (transport_ == nullptr)
        return 4.0;
    const auto snap = transport_->getPositionSnapshot();
    const double beatsPerBar = beatsPerBarFrom(snap.timeSigNumerator, snap.timeSigDenominator);
    return beatsPerBar > 0.0 ? beatsPerBar : 4.0;
}

double TimelineRulerComponent::mapBeatToX(double beat) const noexcept {
    return overrideView_ != nullptr ? (double)overrideOffsetPx_ + overrideView_->beatToX(beat)
                                    : viewState_.beatToX(beat);
}

double TimelineRulerComponent::mapXToBeat(double x) const noexcept {
    return overrideView_ != nullptr ? overrideView_->xToBeat(x - (double)overrideOffsetPx_) : viewState_.xToBeat(x);
}

double TimelineRulerComponent::mapPixelsPerBeat() const noexcept {
    return overrideView_ != nullptr ? overrideView_->pixelsPerBeat : viewState_.pixelsPerBeat;
}

double TimelineRulerComponent::mapFirstVisibleBeat() const noexcept {
    // The beat at this component's x == 0 — through the override that sits LEFT of the roll's
    // keys gutter, which is fine for paint()'s "which bars are visible" sweep (it culls per bar).
    return mapXToBeat(0.0);
}

double TimelineRulerComponent::snappedBeatAtX(double x) const noexcept {
    // Snap division from the SHARED state (the one snap setting); mapping via the override.
    return viewState_.snapBeat(mapXToBeat(x), currentBeatsPerBar());
}

TimelineRulerComponent::Zone TimelineRulerComponent::zoneAtY(float y) const noexcept {
    return y < (float)getHeight() * 0.5f ? Zone::Loop : Zone::Playhead;
}

TimelineRulerComponent::BraceState TimelineRulerComponent::braceStateFor(bool looping, double loopStartBeat,
                                                                         double loopEndBeat) noexcept {
    if (!(loopEndBeat > loopStartBeat))
        return BraceState::None;
    return looping ? BraceState::Active : BraceState::Inactive;
}

juce::Colour TimelineRulerComponent::braceColourFor(BraceState state, juce::Colour accent,
                                                    juce::Colour textMuted) noexcept {
    return state == BraceState::Active ? accent : textMuted.withAlpha(kInactiveBraceAlpha);
}

TimelineRulerComponent::BraceState TimelineRulerComponent::getBraceStateForTest() const noexcept {
    if (transport_ == nullptr)
        return BraceState::None;
    const auto snap = transport_->getPositionSnapshot();
    return braceStateFor(snap.looping, snap.loopStartPpq, snap.loopEndPpq);
}

//==============================================================================
// ---- Markers ----

std::vector<TimelineRulerComponent::MarkerFlag> TimelineRulerComponent::buildMarkerFlags() const {
    std::vector<MarkerFlag> flags;
    if (doc_ == nullptr || getWidth() <= 0 || getHeight() <= 0)
        return flags;

    const double widthPx = (double)getWidth();
    // The band starts where the numbers row ends — ONE split, shared with paint() through
    // rulerLabelRowHeight(), so a bar number and a flag can never be handed overlapping rows.
    const float top = std::max(0.0f, rulerLabelRowHeight(getHeight()));

    for (const auto& marker : doc_->getMarkers()) {
        // A drag in flight reports its PREVIEW beat, so the flag the user is looking at is the one
        // the drop will commit — and hit-testing follows it for free, since both walk this list.
        const double beat = marker.id == draggingMarker_ ? markerDragBeat_ : marker.beat;
        const double x = mapBeatToX(beat);
        const float width = markerFlagWidthFor(marker.text.length());
        // Culled per flag, not per marker range: a flag whose anchor has scrolled off the left edge
        // may still have most of its tab on screen.
        if (x > widthPx || x + (double)width < 0.0)
            continue;

        MarkerFlag flag;
        flag.id = marker.id;
        flag.beat = beat;
        flag.bounds = {(float)x, top, width, std::min(kMarkerFlagHeight, (float)getHeight() - top)};
        flag.colour = juce::Colour(marker.colourArgb);
        flag.text = marker.text;
        flags.push_back(std::move(flag));
    }
    return flags;
}

synth::MarkerId TimelineRulerComponent::markerAt(juce::Point<float> pos) const {
    const auto flags = buildMarkerFlags();
    // Back to front: the later flag is the one paintMarkers drew on top, so it is the one a click
    // on an overlapping pair must resolve to.
    for (auto it = flags.rbegin(); it != flags.rend(); ++it)
        if (it->bounds.contains(pos))
            return it->id;
    return {};
}

void TimelineRulerComponent::performMarkerEdit(const std::function<void()>& mutation) {
    if (doc_ == nullptr || !mutation)
        return;
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutation);
    else
        mutation();
}

bool TimelineRulerComponent::handleMarkerMouseDown(const juce::MouseEvent& e) {
    if (doc_ == nullptr)
        return false;
    // Cmd+click is the zone-agnostic "switch looping off" gesture and must keep working over a
    // flag — a marker would otherwise punch small dead holes in it. (Cmd is not isPopupMenu(): a
    // Ctrl+click on macOS is a right-click and is handled just below.)
    if (e.mods.isCommandDown() && !e.mods.isPopupMenu())
        return false;

    const auto id = markerAt(e.position);
    if (!id.isValid())
        return false;

    if (e.mods.isPopupMenu()) {
        openMarkerContextMenu(id);
        return true;
    }

    const auto* marker = doc_->getMarker(id);
    if (marker == nullptr)
        return false; // the flag list is rebuilt per query, so this is belt and braces

    draggingMarker_ = id;
    markerDragBeat_ = marker->beat;
    // Grab offset in BEATS, so the flag keeps the same relationship to the pointer for the whole
    // drag instead of jumping its left edge under the cursor on the first move.
    markerDragGrabOffsetBeats_ = mapXToBeat((double)e.position.x) - marker->beat;
    markerDragMoved_ = false;
    return true;
}

void TimelineRulerComponent::applyHoverCursor() {
    if (hoveredMarker_.isValid()) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (!hoveredZone_.has_value())
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (*hoveredZone_ == Zone::Playhead)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void TimelineRulerComponent::setHoveredMarker(synth::MarkerId id) {
    if (id == hoveredMarker_)
        return; // repaint on changes only — never once per pixel of mouse movement
    hoveredMarker_ = id;
    applyHoverCursor();
    repaint();
}

void TimelineRulerComponent::renameMarker(synth::MarkerId id, const juce::String& newText) {
    if (doc_ == nullptr)
        return;
    // setMarkerText refuses an over-long label outright (kMaxMarkerTextLength), which leaves the
    // marker's current one in place — the same "a rejected edit is not an erase" reading
    // setClipName's blank-name refusal has.
    performMarkerEdit([this, id, newText] { doc_->setMarkerText(id, newText); });
    repaint();
}

void TimelineRulerComponent::beginRenameMarker(synth::MarkerId id) {
    if (doc_ == nullptr)
        return;

    // FIRST: any previous rename commits before a new one opens — and it may mutate the doc, so
    // nothing may hold a Marker pointer across it.
    finishMarkerRename(true);

    if (doc_->getMarker(id) == nullptr)
        return;

    const auto flags = buildMarkerFlags();
    const auto found = std::find_if(flags.begin(), flags.end(), [id](const MarkerFlag& f) { return f.id == id; });
    if (found == flags.end())
        return; // scrolled off screen: there is nowhere to put the editor

    const auto* marker = doc_->getMarker(id);
    const auto rect = found->bounds.toNearestInt();
    // The flag can be as narrow as kMarkerMinFlagWidth (an unlabelled marker), which is not a
    // field anyone can type into — widen it, then pull it back inside the strip.
    const int width = std::max(96, rect.getWidth());
    const int height = juce::jlimit(12, 18, std::max(12, getHeight() - 2));
    const int x = juce::jlimit(0, std::max(0, getWidth() - width), rect.getX());
    const int y = std::max(0, getHeight() - height - 1);

    renamingMarker_ = id;
    renameEditor_ = std::make_unique<juce::TextEditor>("markerRenameEditor");
    renameEditor_->setComponentID("timelineMarkerRenameEditor");
    renameEditor_->setMultiLine(false);
    renameEditor_->setReturnKeyStartsNewLine(false);
    renameEditor_->setText(marker->text, juce::dontSendNotification);
    renameEditor_->setBounds(x, y, width, height);
    renameEditor_->onReturnKey = [this] { finishMarkerRename(true); };
    renameEditor_->onEscapeKey = [this] { finishMarkerRename(false); };
    // Clicking away is a commit, not a cancel — the same reading every in-place rename in this app
    // (and every DAW) has. Escape is the cancel.
    renameEditor_->onFocusLost = [this] { finishMarkerRename(true); };
    addAndMakeVisible(*renameEditor_);
    renameEditor_->selectAll();
    renameEditor_->grabKeyboardFocus();
}

void TimelineRulerComponent::finishMarkerRename(bool commit) {
    if (renameEditor_ == nullptr)
        return;
    // Detach FIRST: deleting the editor takes focus away from it, which fires onFocusLost, which
    // re-enters here — and finds a null editor, so it stops.
    auto editor = std::move(renameEditor_);
    const auto id = renamingMarker_;
    renamingMarker_ = {};
    const juce::String text = editor->getText();
    editor.reset();

    if (commit && id.isValid())
        renameMarker(id, text);
}

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineRulerComponent::buildMarkerColourPicker(synth::MarkerId id) {
    if (doc_ == nullptr)
        return nullptr;
    const auto* marker = doc_->getMarker(id);
    if (marker == nullptr)
        return nullptr;

    // The colour a no-net-change close restores, and what a "keep the final pick" undo step
    // restores TO — the exact shape TimelineTrackHeaderComponent::buildColourPicker uses.
    const juce::uint32 originalColour = marker->colourArgb;
    juce::Component::SafePointer<TimelineRulerComponent> safeThis(this);

    return std::make_unique<synth::ui::ColourPickerPopup>(
        juce::Colour(originalColour), propertiesFile_,
        [safeThis, id](juce::Colour c) {
            // Live preview: writes the doc directly, no undo — every drag repaints the flag.
            if (auto* self = safeThis.getComponent())
                if (self->doc_ != nullptr)
                    self->doc_->setMarkerColour(id, c.getARGB());
        },
        [safeThis, id, originalColour](juce::Colour finalColour) {
            auto* self = safeThis.getComponent();
            if (self == nullptr || self->doc_ == nullptr)
                return; // the strip (or its window) is gone — nothing left to restore or undo
            if (finalColour.getARGB() == originalColour) {
                // No net change: put back exactly what was there (a preview may have nudged it)
                // and record no undo step.
                self->doc_->setMarkerColour(id, originalColour);
                return;
            }
            // ONE undo step whose undo restores the ORIGINAL colour: silently put the original
            // back first (outside the recorded mutation, so it does not itself become undoable),
            // then perform the real edit as the one recorded step.
            self->doc_->setMarkerColour(id, originalColour);
            self->performMarkerEdit(
                [self, id, finalColour] { self->doc_->setMarkerColour(id, finalColour.getARGB()); });
        });
}

std::unique_ptr<synth::ui::ColourPickerPopup>
TimelineRulerComponent::createMarkerColourPickerForTest(synth::MarkerId id) {
    return buildMarkerColourPicker(id);
}

void TimelineRulerComponent::openMarkerContextMenu(synth::MarkerId id) {
    if (doc_ == nullptr || doc_->getMarker(id) == nullptr)
        return;

    juce::Component::SafePointer<TimelineRulerComponent> safeThis(this);
    juce::PopupMenu menu;
    menu.addItem("Rename...", [safeThis, id] {
        if (auto* self = safeThis.getComponent())
            self->beginRenameMarker(id);
    });
    menu.addItem("Change colour...", [safeThis, id] {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        auto popup = self->buildMarkerColourPicker(id);
        if (popup == nullptr)
            return; // the marker is gone — nothing to pick a colour for
        // Anchored on the flag itself, re-derived NOW rather than captured: the strip may have
        // scrolled between the right-click and the menu choice.
        juce::Rectangle<int> anchor = self->getScreenBounds();
        const auto flags = self->buildMarkerFlags();
        for (const auto& flag : flags)
            if (flag.id == id)
                anchor = self->localAreaToGlobal(flag.bounds.toNearestInt());
        juce::CallOutBox::launchAsynchronously(std::move(popup), anchor, nullptr);
    });
    menu.addSeparator();
    menu.addItem("Delete", [safeThis, id] {
        if (auto* self = safeThis.getComponent())
            self->applyMarkerContextChoice(id, MarkerContextChoice::Delete);
    });

    // Plain Options(), exactly as TimelineClipLaneArea::showClipContextMenu does — the menu opens at
    // the mouse. Deliberately NOT withTargetComponent(this): this strip is full-width and 24 px
    // tall, so anchoring to the whole component puts the menu somewhere unrelated to the flag that
    // was clicked, and it ties the menu's lifetime to a component this gesture repaints.
    menu.showMenuAsync(juce::PopupMenu::Options());
}

void TimelineRulerComponent::applyMarkerContextChoice(synth::MarkerId id, MarkerContextChoice choice) {
    if (doc_ == nullptr)
        return;

    switch (choice) {
    case MarkerContextChoice::Delete:
        performMarkerEdit([this, id] { doc_->removeMarker(id); });
        // A dragged/hovered id must not outlive the marker it names.
        if (draggingMarker_ == id)
            draggingMarker_ = {};
        if (hoveredMarker_ == id)
            hoveredMarker_ = {};
        break;
    case MarkerContextChoice::Rename:
    case MarkerContextChoice::ChangeColour:
        // Deliberately inert: both open UI rather than mutating (see MarkerContextChoice).
        break;
    }

    repaint();
}

void TimelineRulerComponent::paintMarkers(juce::Graphics& g) const {
    const auto flags = buildMarkerFlags();
    if (flags.empty())
        return;

    const juce::Font markerFont{juce::FontOptions(kMarkerFlagFontHeight)};
    g.setFont(markerFont);

    for (const auto& flag : flags) {
        const bool lit = flag.id == hoveredMarker_ || flag.id == draggingMarker_;
        const auto fill = lit ? flag.colour.brighter(kMarkerHoverBrighten) : flag.colour;

        // Full-height stem at the marker's own beat: the tab runs to the right of it, so without
        // this the exact position would only be readable from the tab's left edge. Full opacity
        // across the marker BAND and faded above it, so the stem reads as belonging to the flag
        // without competing with the bar numbers it passes through.
        g.setColour(fill.withAlpha(kMarkerStemAlpha));
        g.fillRect(flag.bounds.getX(), 0.0f, kMarkerStemWidth, flag.bounds.getY());
        g.setColour(fill);
        g.fillRect(flag.bounds.getX(), flag.bounds.getY(), kMarkerStemWidth, flag.bounds.getHeight());

        g.setColour(fill);
        g.fillRect(flag.bounds);
        // A 1 px darker edge, so a light flag on a light theme still has a shape rather than
        // bleeding into the strip behind it.
        g.setColour(fill.darker(0.5f));
        g.drawRect(flag.bounds, 1.0f);

        if (flag.text.isNotEmpty()) {
            // Contrast against the USER'S colour, not a theme text token: a marker colour is
            // arbitrary, so a fixed token would go invisible over half the palette.
            g.setColour(markerLabelColourFor(fill));
            g.drawText(flag.text, flag.bounds.reduced(kMarkerFlagPadX, 0.0f).toNearestInt(),
                       juce::Justification::centredLeft, false);
        }
    }
}

//==============================================================================
void TimelineRulerComponent::mouseDown(const juce::MouseEvent& e) {
    // A press anywhere in the strip commits an open rename first (click-away commits) — and that
    // may mutate the doc, so nothing below may hold a Marker pointer across it.
    if (renameEditor_ != nullptr)
        finishMarkerRename(true);

    // Markers are hit-tested BEFORE the zone split and before the transport check: a marker lives
    // on the doc, so it stays draggable and editable in a build with no transport wired in.
    if (handleMarkerMouseDown(e))
        return;

    // RIGHT BUTTON STOPS HERE, always — the gate TimelineClipLaneArea::mouseDown has and this
    // component was missing. Without it the press fell through to the zone latch below and the
    // matching mouseUp then scrubbed the playhead (`postSeekIfChanged` -> locateBeat + repaint)
    // while a context menu was modal, which is what made the marker menu flash and dismiss itself.
    // A right-click opens a menu; it never seeks, never sets a loop and never latches a gesture.
    //
    // Gated on isPopupMenu() specifically rather than on `!isLeftButtonDown()` (the sibling's
    // stricter form): the popup is the only button whose press opens a modal window, and therefore
    // the only one that can be dismissed by what this component does next.
    if (e.mods.isPopupMenu())
        return;

    if (transport_ == nullptr)
        return;

    if (e.mods.isCommandDown()) {
        // Cmd+click: toggle looping off, keeping the existing bounds. Zone-agnostic. The way back
        // on is a plain click on the resulting dimmed brace (see mouseUp).
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, false);
        repaint();
        return;
    }

    // Latch the zone for the whole gesture: mid-drag the pointer routinely leaves the band it
    // started in, and the gesture must not change meaning under the user's hand.
    gestureZone_ = zoneAtY(e.position.y);

    dragAnchorBeat_ = snappedBeatAtX((double)e.position.x);
    lastPostedLoopStart_ = -1.0; // nothing posted yet this gesture
    lastPostedLoopEnd_ = -1.0;
    lastPostedSeekBeat_ = -1.0;

    // Playhead zone seeks on press, not on release — the cursor lands where you clicked and then
    // follows the drag.
    if (gestureZone_ == Zone::Playhead)
        postSeekIfChanged(e);
}

void TimelineRulerComponent::mouseDrag(const juce::MouseEvent& e) {
    // Right button: inert. See mouseDown — nothing may touch the transport or repaint while a
    // context menu opened by this gesture is modal.
    if (e.mods.isPopupMenu())
        return;

    // Marker drag: snapped through the SHARED view state, exactly like every other beat this strip
    // posts, and clamped at 0 because a marker's beat is >= 0 in the model.
    if (draggingMarker_.isValid()) {
        markerDragMoved_ = true;
        const double raw = mapXToBeat((double)e.position.x) - markerDragGrabOffsetBeats_;
        const double snapped = std::max(0.0, viewState_.snapBeat(raw, currentBeatsPerBar()));
        if (snapped == markerDragBeat_)
            return; // no new information — don't repaint once per pixel
        markerDragBeat_ = snapped;
        repaint();
        return;
    }

    if (transport_ == nullptr || e.mods.isCommandDown())
        return;

    if (gestureZone_ == Zone::Playhead)
        postSeekIfChanged(e);
    else
        postLoopIfChanged(e);
}

void TimelineRulerComponent::mouseUp(const juce::MouseEvent& e) {
    // Right button: inert, for the reason spelled out in mouseDown. THIS is the branch that used to
    // fire `postSeekIfChanged` under a stale latched `gestureZone_` and kill the menu it had just
    // opened.
    if (e.mods.isPopupMenu())
        return;

    if (draggingMarker_.isValid()) {
        const auto id = draggingMarker_;
        const double beat = markerDragBeat_;
        const bool moved = markerDragMoved_;
        draggingMarker_ = {};
        markerDragMoved_ = false;
        // A press that never dragged commits nothing: a stray click on a flag must not quietly
        // re-snap the marker it landed on. moveMarker is a no-op for an unchanged beat anyway, so
        // this only ever suppresses an undo entry that would have been empty.
        if (moved)
            performMarkerEdit([this, id, beat] { doc_->moveMarker(id, beat); });
        repaint();
        return;
    }

    if (transport_ == nullptr || e.mods.isCommandDown())
        return; // Cmd+click was already fully handled in mouseDown.

    // Both finalisers are the same "only post when the snapped value changed" throttle their drag
    // path used, so a release that adds no new information is a no-op.
    if (gestureZone_ == Zone::Playhead) {
        postSeekIfChanged(e);
        return;
    }

    // Loop zone: a click with no drag does nothing, except on a dimmed brace — the loop range is
    // the only thing this half owns, and a stray click must not clear or collapse it.
    if (e.mouseWasDraggedSinceMouseDown()) {
        postLoopIfChanged(e);
        return;
    }
    reArmLoopIfClickOnInactiveBrace(e);
}

void TimelineRulerComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    // Scoped to marker flags ONLY: this component had no double-click handler at all before, so
    // anywhere else in the strip the gesture keeps behaving as the two ordinary clicks it already
    // was (a seek in the playhead zone, the start of a loop drag in the loop zone).
    if (doc_ == nullptr || e.mods.isPopupMenu())
        return;
    const auto id = markerAt(e.position);
    if (!id.isValid())
        return;

    // The second press of the double-click has already latched a drag on this flag. LEAVE it
    // latched and only clear the "moved" flag: the mouseUp that follows then takes the marker
    // branch, commits nothing (a press that never dragged is a no-op) and tidies up. Clearing
    // draggingMarker_ here instead would send that mouseUp into the loop/playhead branch under a
    // STALE latched gestureZone_ and seek the cursor out from under the editor we are opening.
    markerDragMoved_ = false;
    beginRenameMarker(id);
}

void TimelineRulerComponent::mouseEnter(const juce::MouseEvent& e) {
    setHoveredZone(zoneAtY(e.position.y));
    setHoveredMarker(markerAt(e.position));
}

void TimelineRulerComponent::mouseMove(const juce::MouseEvent& e) {
    setHoveredZone(zoneAtY(e.position.y));
    setHoveredMarker(markerAt(e.position));
}

void TimelineRulerComponent::mouseExit(const juce::MouseEvent&) {
    setHoveredZone(std::nullopt);
    setHoveredMarker({});
}

void TimelineRulerComponent::setHoveredZone(std::optional<Zone> zone) {
    if (zone == hoveredZone_)
        return; // repaint on zone changes only — never once per pixel of mouse movement
    hoveredZone_ = zone;
    applyHoverCursor();
    repaint();
}

void TimelineRulerComponent::postSeekIfChanged(const juce::MouseEvent& e) {
    // Clamped the same way TransportService::locateBeat clamps, so the dedupe below can't be
    // fooled into re-posting identical seeks while the pointer drags left of beat 0.
    const double beat = std::max(0.0, snappedBeatAtX((double)e.position.x));
    if (beat == lastPostedSeekBeat_)
        return; // unchanged since the last post — the FIFO dedupes nothing, so don't spam it

    transport_->locateBeat(beat);
    lastPostedSeekBeat_ = beat;
    ++seekPostCount_;
    repaint();
}

void TimelineRulerComponent::postLoopIfChanged(const juce::MouseEvent& e) {
    const double current = snappedBeatAtX((double)e.position.x);
    const double start = std::min(dragAnchorBeat_, current);
    const double end = std::max(dragAnchorBeat_, current);
    if (end <= start)
        return; // degenerate/zero-length so far — wait for more drag distance
    if (start == lastPostedLoopStart_ && end == lastPostedLoopEnd_)
        return; // unchanged since the last post — the FIFO dedupes nothing, so don't spam it

    transport_->setLoop(start, end, true);
    lastPostedLoopStart_ = start;
    lastPostedLoopEnd_ = end;
    repaint();
}

void TimelineRulerComponent::reArmLoopIfClickOnInactiveBrace(const juce::MouseEvent& e) {
    const auto snap = transport_->getPositionSnapshot();
    if (braceStateFor(snap.looping, snap.loopStartPpq, snap.loopEndPpq) != BraceState::Inactive)
        return;

    // The target is the brace's whole x-span across the loop half, not just the 4 px bar it draws:
    // a 4 px strip is not a click target. Anything outside the span stays inert.
    const double x = (double)e.position.x;
    if (x < mapBeatToX(snap.loopStartPpq) || x > mapBeatToX(snap.loopEndPpq))
        return;

    transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, true);
    repaint();
}

//==============================================================================
void TimelineRulerComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour bg, border, textMuted, accent;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        border = c.border;
        textMuted = c.textMuted;
        accent = c.accent;
    } else {
        bg = juce::Colours::darkgrey.darker(0.3f);
        border = juce::Colours::grey;
        textMuted = juce::Colours::lightgrey;
        accent = juce::Colours::cyan;
    }

    const auto bounds = getLocalBounds();
    g.setColour(bg);
    g.fillRect(bounds);
    g.setColour(border);
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());

    const double widthPx = (double)getWidth();
    if (widthPx <= 0.0)
        return;

    double beatsPerBar = 4.0;
    bool looping = false;
    double loopStartBeat = 0.0;
    double loopEndBeat = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        beatsPerBar = beatsPerBarFrom(snap.timeSigNumerator, snap.timeSigDenominator);
        if (beatsPerBar <= 0.0)
            beatsPerBar = 4.0;
        looping = snap.looping;
        loopStartBeat = snap.loopStartPpq;
        loopEndBeat = snap.loopEndPpq;
    }

    const double startBeat = mapFirstVisibleBeat();
    const double endBeat = mapXToBeat(widthPx);

    // Adaptive bar-label density: widen the stride by powers of two until labelled bars are at
    // least kMinLabelSpacingPx apart, so labels never overlap regardless of zoom.
    const double barWidthPx = beatsPerBar * mapPixelsPerBeat();
    juce::int64 labelEveryNBars = 1;
    while (barWidthPx * (double)labelEveryNBars < kMinLabelSpacingPx)
        labelEveryNBars *= 2;

    // ONE decision per paint, from the pure helper the tests drive directly (see
    // rulerTickPlanFor) — never re-derived inside the loop.
    const auto tickPlan = rulerTickPlanFor(mapPixelsPerBeat(), beatsPerBar);
    const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));

    const juce::int64 firstBar = (juce::int64)std::floor(startBeat / beatsPerBar) - 1;
    const juce::int64 lastBar = (juce::int64)std::ceil(endBeat / beatsPerBar) + 1;

    // Both fonts built once per paint, outside the bar sweep — the loop itself allocates nothing.
    const juce::Font barFont{juce::FontOptions(kBarLabelFontHeight)};
    const juce::Font beatFont{juce::FontOptions(kBeatLabelFontHeight)};
    const juce::Colour beatLabelColour = textMuted.withAlpha(kBeatLabelAlpha);

    // The numbers row: everything ABOVE the marker band. Labels are centred in THIS height rather
    // than in the strip's full height, which is what keeps a bar number readable when a marker sits
    // on the same beat (see rulerLabelRowHeight — flags used to be drawn straight over the numbers).
    const int labelRowHeight = (int)std::llround(rulerLabelRowHeight(getHeight()));

    g.setFont(barFont);
    for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
        const double barBeat = (double)bar * beatsPerBar;
        const double x = mapBeatToX(barBeat);
        // Cull only the bar line and its number — never the whole bar. A bar whose line has
        // scrolled off the left edge still owns beat ticks/labels that ARE on screen; a whole-bar
        // `continue` here left the ruler blank to the left of the first visible bar line while
        // scrolling (the lanes-grid painter had the same bug, fixed the same way). The tick loop
        // below culls per tick.
        if (x >= -1.0 && x <= widthPx + 1.0) {
            g.setColour(border);
            g.drawVerticalLine((int)std::llround(x), 0.0f, (float)bounds.getHeight());

            if (bar >= 0 && (bar % labelEveryNBars) == 0) {
                g.setColour(textMuted);
                g.drawText(juce::String(bar + 1), (int)std::llround(x) + 3, 0, kBarLabelWidth, labelRowHeight,
                           juce::Justification::centredLeft, false);
            }
        }

        if (tickPlan.drawBeatTicks) {
            for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                const double beatX = mapBeatToX(barBeat + (double)beatInBar);
                if (beatX < 0.0 || beatX > widthPx)
                    continue;
                // Half-height and faded: a beat tick must read as subordinate to the full-height
                // bar line it sits between, at a glance and without reading the labels.
                g.setColour(border.withAlpha(0.4f));
                g.drawVerticalLine((int)std::llround(beatX), (float)bounds.getHeight() * 0.5f,
                                   (float)bounds.getHeight());

                // "bar.beat" (Cubase's "80.2"), 1-based on both halves so it matches the bar
                // numbers above and the transport bar's own readout. Only once there is real room
                // — see rulerTickPlanFor.
                if (tickPlan.drawBeatLabels && bar >= 0) {
                    g.setFont(beatFont);
                    g.setColour(beatLabelColour);
                    g.drawText(juce::String(bar + 1) + "." + juce::String(beatInBar + 1), (int)std::llround(beatX) + 3,
                               0, kBeatLabelWidth, labelRowHeight, juce::Justification::centredLeft, false);
                    g.setFont(barFont);
                }
            }
        }
    }

    // Hover affordance: tint the half the pointer is over, so which gesture is armed is visible
    // before pressing. Drawn under the loop brace so the brace stays legible.
    if (hoveredZone_.has_value()) {
        auto band = bounds.toFloat();
        if (*hoveredZone_ == Zone::Loop)
            band = band.removeFromTop(band.getHeight() * 0.5f);
        else
            band = band.removeFromBottom(band.getHeight() * 0.5f);
        g.setColour(accent.withAlpha(kHoverBandAlpha));
        g.fillRect(band);
    }

    // Loop brace: a bracket spanning [loopStartPpq, loopEndPpq]. Drawn whenever a RANGE exists —
    // looping switched off greys it out rather than hiding it, so the locators stay findable (and
    // a click on the grey brace re-arms them).
    const auto braceState = braceStateFor(looping, loopStartBeat, loopEndBeat);
    if (braceState != BraceState::None) {
        const double xStart = mapBeatToX(loopStartBeat);
        const double xEnd = mapBeatToX(loopEndBeat);
        if (xEnd >= 0.0 && xStart <= widthPx) {
            const float clampedStart = (float)juce::jlimit(0.0, widthPx, xStart);
            const float clampedEnd = (float)juce::jlimit(0.0, widthPx, xEnd);
            g.setColour(braceColourFor(braceState, accent, textMuted));
            g.fillRect(clampedStart, 0.0f, std::max(1.0f, clampedEnd - clampedStart), kLoopBraceHeight);
            g.fillRect(clampedStart, 0.0f, 1.5f, kLoopBraceTickHeight);
            g.fillRect(clampedEnd - 1.5f, 0.0f, 1.5f, kLoopBraceTickHeight);
        }
    }

    // Markers last: their flags sit in the lower band (clear of the brace above) and must read on
    // top of the bar lines and beat ticks they cross.
    paintMarkers(g);
}

} // namespace synth::ui
