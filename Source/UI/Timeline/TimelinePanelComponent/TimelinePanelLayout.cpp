// TimelinePanelLayout.cpp
//
// Application-properties-backed preferences (snap/follow-playhead/scroll-invert),
// zoom/scroll helpers, resized() and paint()/paintOverChildren(). TimelinePanelComponent
// is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this
// directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/ScrollPolicy.h"

namespace synth::ui {

namespace {
constexpr const char* kTimelineSnapPropertyKey = "timelineSnap";
constexpr const char* kTimelineSnapEnabledPropertyKey = "timelineSnapEnabled";
constexpr const char* kTimelineFollowPlayheadPropertyKey = "timelineFollowPlayhead";
// The track-header column's width in px; absent = the themed default.
constexpr const char* kTimelineTrackHeaderWidthPropertyKey = "timelineTrackHeaderWidth";
constexpr int kTrackHeaderWidthHandleWidth = 6; // straddles the column seam
// The roll's key-label density (PianoRollComponent::KeyLabelMode). "all" (default) labels every
// key row; "c" labels only the Cs. Owned by PreferencesSettingsTab's persistX pattern; read here
// by reloadPianoRollAppearancePrefs().
constexpr const char* kPianoRollKeyLabelsPropertyKey = "pianoRollKeyLabels";

// the "+ Track" strip at the top of the track-header column. Fixed height — the headers
// below it scroll, the button never does.
constexpr int kAddTrackButtonHeight = 22;

// 28 (was 24): timeline-panel button-size sweep — .reduced(2) at the setBounds() call site takes
// the effective width from 20 to 24 px.
constexpr int kEditToolButtonWidth = 28;

// Same adaptive-density beat-tick threshold TimelineRulerComponent uses for its own beat ticks —
// duplicated (not shared) because it's a one-line, purely-cosmetic constant and the grid lives on
// the panel while the ticks live on the ruler.
constexpr double kMinBeatLinePixelsPerBeat = 8.0;

// Wheel tuning. Cmd+wheel zoom is exponential in deltaY so equal-and-opposite wheel gestures
// exactly cancel (factor(-d) == 1/factor(d)); plain wheel scroll moves a constant PIXEL distance
// per wheel unit, converted to beats at the CURRENT zoom ("natural": the same physical gesture
// covers less musical time when zoomed in).
constexpr double kZoomWheelSensitivity = 2.0;
constexpr double kScrollPixelsPerWheelUnit = 200.0;

constexpr int kSnapComboWidth = 90;
// 30 (was 26): part of the timeline-panel button-size sweep — both buttons are .reduced(2) at
// their setBounds() call site, so the effective on-screen size grows from 22 to 26 px.
// Wide enough for the word "Snap" (it used to read "Q" and be 30 px) — see the member's comment in
// TimelinePanelComponent.h for why the label is the verb and not the key.
constexpr int kSnapToggleButtonWidth = 46;

// A marker's stem where it crosses the CLIPS, well under the ruler flag's own alpha: it has to
// locate the marker against the arrangement without competing with the clips for attention.
constexpr float kMarkerLaneStemAlpha = 0.40f;
constexpr int kFollowPlayheadButtonWidth = 30;
} // namespace

// Restores/persists the snap-selector choice under the "timelineSnap" key, same pattern as
// AIChatComponent::setAccountService()'s non-owning setter. Also hands the roll its
// PropertiesFile (pianoRoll_.setPropertiesFile -- scale-panel visibility + user scales) and runs
// reloadPianoRollAppearancePrefs() once, below.
void TimelinePanelComponent::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
    // Closed until first opened (unlike the Mixer's pane): the routing view is opt-in.
    sidePane_.setPersistence(props != nullptr ? props->getUserSettings() : nullptr, "timeline", /*defaultOpen=*/false);
    // Forwarded even when there is no user-settings file: both of these degrade to "in-memory
    // only" / "read the default" rather than needing one, and the early return below would
    // otherwise leave them holding a stale pointer.
    clipLaneArea_.setApplicationProperties(props);
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr) {
        ruler_.setPropertiesFile(nullptr);
        return;
    }

    // Where the marker colour picker's favourites shelf persists to — the same shelf the track
    // header's swatch uses, so a colour saved from one is offered by the other.
    ruler_.setPropertiesFile(appProperties_->getUserSettings());

    int saved = appProperties_->getUserSettings()->getIntValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    saved = juce::jlimit((int)TimelineViewState::Snap::Off, (int)TimelineViewState::Snap::HundredTwentyEighth, saved);
    // setSnap (not a bare assignment) so a restored musical division is also where cycleSnapValue's
    // from-Off rule resumes — a restored Snap::Off leaves lastMusicalSnap at its default, which is
    // exactly the documented fallback.
    viewState_.setSnap((TimelineViewState::Snap)saved);
    snapCombo_.setSelectedId(saved + 1, juce::dontSendNotification);
    viewState_.snapEnabled =
        appProperties_->getUserSettings()->getBoolValue(kTimelineSnapEnabledPropertyKey, viewState_.snapEnabled);
    snapToggleButton_.setToggleState(viewState_.snapEnabled, juce::dontSendNotification);

    followPlayhead_ =
        appProperties_->getUserSettings()->getBoolValue(kTimelineFollowPlayheadPropertyKey, followPlayhead_);
    if (const int width = appProperties_->getUserSettings()->getIntValue(kTimelineTrackHeaderWidthPropertyKey, 0);
        width > 0)
        setTrackHeaderWidth(width, false);
    followPlayheadButton_.setToggleState(followPlayhead_, juce::dontSendNotification);
    pianoRoll_.setFollowPlayhead(followPlayhead_);

    // A pure forward -- the transport bar restores/persists ITS OWN two keys
    // ("timelineMetronomeEnabled", "timelineCountInBars") -- this panel has no other reason to know
    // either setting exists, so it is a pure forward, not a third copy of the restore/persist idiom.
    transportBar_.setApplicationProperties(props);

    // Scale-panel visibility + user scales are the ROLL's own PropertiesFile-backed state (see
    // PianoRollComponent::setPropertiesFile); key-labels and note-colour overrides are read here.
    pianoRoll_.setPropertiesFile(appProperties_->getUserSettings());
    reloadPianoRollAppearancePrefs();
}

// Called once from setApplicationProperties, and left public so a live settings change (the
// Preferences tab's key-labels toggle, a note-colour edit) can re-push without a restart --
// MainComponent wires that in a parallel task; this method itself does no listening of its own.
void TimelinePanelComponent::reloadPianoRollAppearancePrefs() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    auto& settings = *appProperties_->getUserSettings();

    const auto keyLabels = settings.getValue(kPianoRollKeyLabelsPropertyKey, "all");
    pianoRoll_.setKeyLabelMode(keyLabels.equalsIgnoreCase("c")
                                   ? synth::ui::PianoRollComponent::KeyLabelMode::OctavesOnly
                                   : synth::ui::PianoRollComponent::KeyLabelMode::AllNotes);
    pianoRoll_.setNoteColourOverrides(synth::ui::loadNoteColourOverrides(settings));
}

// Exactly what picking a division from the snap combo does now: the combo's onChange delegates
// here, so the combo, the shortcut layer and cycleSnapValue() share one path to the view state,
// one persist and one set of repaints. Like the combo, it re-arms the master snap switch (see
// setSnapEnabled): asking for a division means "snap to THIS", and choosing Snap::Off is how you
// ask for no grid from here. Also feeds TimelineViewState::lastMusicalSnap, which is what
// cycleSnapValue's from-Off rule reads.
bool TimelinePanelComponent::setSnapValue(TimelineViewState::Snap value) {
    const bool changed = viewState_.snap != value;
    viewState_.setSnap(value);
    // The combo REFLECTS the view state; it never owns it. dontSendNotification so a programmatic
    // set can't re-enter onChange (which would call straight back into here).
    snapCombo_.setSelectedId((int)value + 1, juce::dontSendNotification);
    // Picking a division is an explicit "snap to THIS" — flip the master switch back on so the
    // choice takes effect immediately (choosing Snap::Off already means "no grid"). This is also
    // the single persist + ruler/grid repaint path; see setSnapEnabled below.
    setSnapEnabled(true);
    // The roll's gridlines and its new-note length both come from the division — a snap change is
    // the only thing that moves them, so this is where they are redrawn.
    pianoRoll_.repaint();
    return changed;
}

// Two rules, both chosen for how they feel under a held-down key rather than for symmetry:
//
// - CLAMPED at both ends, never wrapping. Leaning on "finer" and parking at 1/128 is what the hand
//   expects; wrapping silently back to Bar mid-flow moves every subsequent edit onto a 64x coarser
//   grid, and the user finds out from the result, not from the keypress.
// - Snap::Off is never a stop on the cycle -- turning magnetism off stays the Q key's job. So
//   cycling FROM Off (in either direction, one simple rule) enters at
//   TimelineViewState::lastMusicalSnap, the last division the user actually chose, falling back
//   to Snap::Bar if there somehow isn't one. "Either direction" is deliberate: from Off there is
//   no current position for "one finer" to be relative to, so the only honest answer is "back
//   where you were".
bool TimelinePanelComponent::cycleSnapValue(int direction) {
    using Snap = TimelineViewState::Snap;
    if (direction == 0)
        return false;

    // From Off there is no position for "one step finer" to be relative to, so both directions
    // re-enter at the last division the user actually chose (Bar if there wasn't one). See the
    // header for why this beats picking an end.
    if (viewState_.snap == Snap::Off) {
        const Snap entry = viewState_.lastMusicalSnap != Snap::Off ? viewState_.lastMusicalSnap : Snap::Bar;
        return setSnapValue(entry);
    }

    // Snap is declared coarsest -> finest (see TimelineViewState), so the step is a clamped +-1 on
    // the enum's own int. CLAMPED, never wrapped: parking on 1/128 under a held key is far less
    // surprising than silently landing back on Bar.
    const int stepped =
        juce::jlimit((int)Snap::Bar, (int)Snap::HundredTwentyEighth, (int)viewState_.snap + (direction > 0 ? 1 : -1));
    return setSnapValue((Snap)stepped);
}

void TimelinePanelComponent::setSnapEnabled(bool enabled) {
    viewState_.snapEnabled = enabled;
    persistSnapChoice();
    snapToggleButton_.setToggleState(enabled, juce::dontSendNotification);
    ruler_.repaint();
    repaint();
}

void TimelinePanelComponent::persistSnapChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    appProperties_->getUserSettings()->setValue(kTimelineSnapEnabledPropertyKey, viewState_.snapEnabled);
    appProperties_->saveIfNeeded();
}

// A toggle next to snapToggleButton_ (same external-state pattern: setClickingTogglesState(false),
// the shared bool is the truth, the button only mirrors it). Default OFF -- an editor that
// silently starts scrolling under a user who never asked for it is worse than one that doesn't.
// Also forwards straight into pianoRoll_.setFollowPlayhead(enabled) -- one flag, one switch, for
// both the arrangement view and the roll -- including from the setApplicationProperties restore
// path.
void TimelinePanelComponent::setFollowPlayheadEnabled(bool enabled) {
    followPlayhead_ = enabled;
    followPlayheadButton_.setToggleState(enabled, juce::dontSendNotification);
    pianoRoll_.setFollowPlayhead(enabled);
    persistFollowPlayheadChoice();
}

void TimelinePanelComponent::persistFollowPlayheadChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineFollowPlayheadPropertyKey, followPlayhead_);
    appProperties_->saveIfNeeded();
}

// JUCE hands us pre-flipped wheel deltas (see ScrollPolicy.h), so this is a second, deliberate
// flip and not a re-application of the OS setting. Not persisted here: the owner (Preferences)
// decides whether a preference exists, the same way it owns the timeline's other opt-in
// behaviours.
void TimelinePanelComponent::setScrollInverted(bool inverted) noexcept {
    scrollInverted_ = inverted;
    // Keep the roll in step. It runs its OWN plain-scroll branches (PianoRollComponent::
    // mouseWheelMove), so a preference set on the panel chrome must reach it directly rather than
    // through anything shared like TimelineViewState.
    pianoRoll_.setScrollInverted(inverted);
}

// mouseWheelMove derives the physical gesture direction via synth::ui::wheelGestureIsUpward
// (isReversed-aware, unlike a raw delta sign -- see ScrollPolicy.h) and XORs it with this flag, so
// flipping the preference flips the sense of BOTH axes at once rather than requiring two separate
// settings. Not persisted here -- see setScrollInverted's comment above.
void TimelinePanelComponent::setZoomScrollInverted(bool inverted) noexcept {
    zoomScrollInverted_ = inverted;
    pianoRoll_.setZoomScrollInverted(inverted); // same forwarding reason as setScrollInverted above
}

//==============================================================================
// Implemented once here (rather than separately on the ruler) so the ruler and the lanes grid
// share identical behaviour -- JUCE bubbles an unhandled wheel event from the ruler child up to
// this override. No platform branch is needed for the zoom modifier: mods.isCommandDown() already
// resolves to Cmd on macOS and Ctrl everywhere else, so never add an #ifdef here.
void TimelinePanelComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Reproject into the ruler's coordinate space regardless of whether the event originated on
    // this component or bubbled up from the ruler child — both share the same x == 0 origin as
    // TimelineViewState (the lanes/ruler content start), so this is exactly the anchor
    // beatToX/xToBeat expect.
    const double anchorX = (double)e.getEventRelativeTo(&ruler_).position.x;

    // Cubase-style bindings: Cmd = horizontal zoom, Cmd+Shift = vertical zoom (row height),
    // Shift or a trackpad's own deltaX = horizontal scroll, plain vertical wheel = vertical
    // track scroll (headers + lanes together). Option + a vertical scroll is vertical zoom too,
    // the trackpad's one-hand way (macOS pinch has no axis, so it cannot say "vertical" itself).
    //
    // The two ZOOM branches are decided by their MODIFIERS, so they must not read a single axis:
    // macOS folds Shift+wheel into deltaX, which would leave Cmd+Shift+wheel reading deltaY == 0
    // and doing nothing at all. dominantWheelDelta() is that guard (see ScrollPolicy.h).
    //
    // Direction is read from the PHYSICAL gesture (wheelGestureIsUpward, isReversed-aware), not
    // from the delta's raw sign: "up zooms in" must mean the same finger motion whether or not the
    // OS has natural scrolling on, exactly the reasoning ScrollPolicy.h documents for that helper.
    // zoomScrollInverted_ XORs on top, the same second-deliberate-flip idiom scrollInverted_ already
    // applies to plain scrolling below. Magnitude is the dominant axis's unsigned size, so today's
    // exponential sensitivity curve is unchanged — only the sign moved from "the delta" to "the
    // gesture direction, then the preference".
    const bool zoomingIn = wheelGestureIsUpward(wheel) != zoomScrollInverted_;
    const double zoomMagnitude = std::abs((double)dominantWheelDelta(wheel)) * kZoomWheelSensitivity;
    const double zoomFactor = std::exp(zoomingIn ? zoomMagnitude : -zoomMagnitude);

    const bool optionVertical =
        e.mods.isAltDown() && !e.mods.isCommandDown() && std::abs(wheel.deltaY) >= std::abs(wheel.deltaX);
    if (e.mods.isCommandDown() || optionVertical)
        wheelTween_.stop();
    if ((e.mods.isCommandDown() && e.mods.isShiftDown()) || optionVertical) {
        zoomTrackRows(zoomFactor, (double)e.getEventRelativeTo(&clipLaneArea_).position.y);
        return;
    }

    if (e.mods.isCommandDown()) {
        zoomHorizontalAroundX(zoomFactor, anchorX);
        return;
    }

    // Plain scroll. scrollAmount() converts a wheel delta into the amount to ADD to a view origin
    // with juce::Viewport's sign convention (natural), plus this panel's inversion preference —
    // and both of these origins DO grow the Viewport way (firstVisibleBeat is the beat at x == 0,
    // trackScrollY the pixels scrolled off the top), so no extra axis mapping is needed here.
    const bool eased = !wheel.isSmooth && !wheel.isInertial; // a mouse-wheel notch, not a trackpad
    const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (horizontal) {
        // Which axis the gesture arrived on, NOT "the dominant delta": a Shift+wheel that the OS
        // left on deltaY and a trackpad's own sideways deltaX are both horizontal scrolls, and
        // either one is the amount to move by.
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        const double deltaPx = (double)scrollAmount(delta, scrollInverted_) * kScrollPixelsPerWheelUnit;
        if (deltaPx != 0.0)
            scrollByWheel(0, deltaPx / viewState_.pixelsPerBeat, eased);
        return;
    }

    const double deltaY = (double)scrollAmount(wheel.deltaY, scrollInverted_) * kScrollPixelsPerWheelUnit;
    if (deltaY != 0.0)
        scrollByWheel(1, deltaY, eased);
}

// Direct events, and a host that isn't showing (headless tests), apply at once after ending any
// tween; a notch eases in via ScrollTweenRunner instead.
void TimelinePanelComponent::scrollByWheel(int axis, double amount, bool eased) {
    const auto read = [this](int a) { return a == 0 ? viewState_.firstVisibleBeat : viewState_.trackScrollY; };
    const auto scroll = [this](int a, double d) { applyWheelScroll(a, d); };
    if (eased && wheelTween_.push(*this, axis, amount, read, scroll))
        return;
    wheelTween_.stop();
    applyWheelScroll(axis, amount);
}

// Horizontal scroll moves only the ruler and the lanes region (grid, clips/roll, automation lanes,
// marker stems, playhead) -- never the track headers, which scroll only vertically.
void TimelinePanelComponent::applyWheelScroll(int axis, double amount) {
    if (axis == 1) {
        scrollTrackRows(amount);
        return;
    }
    viewState_.scrollBeats(amount);
    ruler_.repaint();
    repaint(gridLanesBounds_);
}

void TimelinePanelComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch: deliberate enough that it needs no modifier. Plain pinch = horizontal zoom
    // around the pinch point; Option+pinch (or Shift+pinch) = vertical (row height) zoom. A pinch
    // reports one scale factor and no axis, so the modifier is the only way to say "vertical".
    if (e.mods.isAltDown() || e.mods.isShiftDown()) {
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
            return;
        zoomTrackRows((double)scaleFactor, (double)e.getEventRelativeTo(&clipLaneArea_).position.y);
        return;
    }
    zoomHorizontalAroundX((double)scaleFactor, (double)e.getEventRelativeTo(&ruler_).position.x);
}

//==============================================================================
// Cmd+wheel, trackpad pinch and zoomTimelineHorizontal() all land here, so the clamp behaviour and
// the repaint set are shared rather than copied three times.
void TimelinePanelComponent::zoomHorizontalAroundX(double factor, double anchorX) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    viewState_.zoomAroundX(factor, anchorX);
    ruler_.repaint();
    repaint();
}

double TimelinePanelComponent::visibleCentreXInRuler() const noexcept {
    // The ruler's local x == 0 IS TimelineViewState's origin (see resized()), so its half-width is
    // the beat-space centre of what is on screen — the same anchor a wheel zoom would pass if the
    // pointer happened to sit in the middle of the strip.
    return (double)ruler_.getWidth() * 0.5;
}

double TimelinePanelComponent::visibleCentreYInLanes() const noexcept {
    // clipLaneArea_ fills gridLanesBounds_ exactly, and zoomTrackRows' anchor is in that
    // component's coordinates — so the middle visible row is simply half its height.
    return (double)gridLanesBounds_.getHeight() * 0.5;
}

// Runs through the same TimelineViewState::zoomAroundX + repaint path as Cmd+wheel and the
// trackpad pinch -- one path, so a shortcut zoom and a wheel zoom can never drift apart in
// clamping or in what they repaint. Keeps the music in front of you put, since it zooms around the
// centre of what's visible rather than an arbitrary point.
void TimelinePanelComponent::zoomTimelineHorizontal(double factor) {
    zoomHorizontalAroundX(factor, visibleCentreXInRuler());
}

// Same zoomTrackRows path -- including the header-column relayout and the scroll re-clamp -- that
// Cmd+Shift+wheel and Shift+pinch use.
void TimelinePanelComponent::zoomTimelineVertical(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    zoomTrackRows(factor, visibleCentreYInLanes());
}

double TimelinePanelComponent::maxTrackScrollPx() const {
    return std::max(0.0, (double)(rowLayout().totalHeight() - gridLanesBounds_.getHeight()));
}

void TimelinePanelComponent::scrollTrackRows(double deltaPx) {
    const double before = viewState_.trackScrollY;
    viewState_.scrollTracksPx(deltaPx, maxTrackScrollPx());
    if (viewState_.trackScrollY == before)
        return;
    syncTrackScroll();
}

// Keeps the row under the pointer put: the content y under the anchor is mapped from the old row
// layout to the new one (clip rows and lane rows rescale, the section row does not), and the
// scroll moves by whatever keeps that y at the same visible position. Read from the actual
// (rounded, clamped) layouts rather than `factor`, so the clamps can't make the anchor drift.
void TimelinePanelComponent::zoomTrackRows(double factor, double anchorLaneY) {
    const auto before = rowLayout();
    const double contentY = anchorLaneY + viewState_.trackScrollY;
    viewState_.scaleRowHeight(factor);
    pushAutomationGeometry(); // lane rows follow the same zoom
    const auto after = rowLayout();
    if (after.trackRowHeight() == before.trackRowHeight() && after.totalHeight() == before.totalHeight())
        return;
    viewState_.trackScrollY = TimelineRowLayout::mapContentY(before, after, contentY) - anchorLaneY;
    viewState_.scrollTracksPx(0.0, maxTrackScrollPx()); // clamp into the new range
    layoutTrackHeaders();
    syncTrackScroll();
}

void TimelinePanelComponent::syncTrackScroll() {
    // One writer, two readers: the header viewport mirrors trackScrollY, the lanes/roll repaint.
    // setViewPosition fires visibleAreaChanged, whose handler sees an unchanged value and stops —
    // no feedback loop.
    trackHeaderViewport_.setViewPosition(0, (int)std::llround(viewState_.trackScrollY));
    clipLaneArea_.repaint();
    placeLaneBodies();
    repaint(gridLanesBounds_);
}

//==============================================================================
void TimelinePanelComponent::resized() {
    // Themed metrics with literal fallbacks for the headless test path (same pattern as
    // MainComponent::resized()).
    int transportBarHeight = 34;
    int rulerHeight = 30; // keep in step with Theme::Metrics::timelineRulerHeight
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        transportBarHeight = m.timelineTransportBarHeight;
        rulerHeight = m.timelineRulerHeight;
    }
    const int trackHeaderWidth = getTrackHeaderWidth();

    auto bounds = getLocalBounds();
    transportBarBounds_ = bounds.removeFromTop(transportBarHeight);
    layoutSidePane(bounds);
    trackHeaderBounds_ = bounds.removeFromLeft(trackHeaderWidth);
    lanesBounds_ = bounds; // remainder
    trackHeaderWidthHandle_.setBounds(trackHeaderBounds_.getRight() - kTrackHeaderWidthHandleWidth / 2,
                                      trackHeaderBounds_.getY(), kTrackHeaderWidthHandleWidth,
                                      trackHeaderBounds_.getHeight());

    // The "+ MIDI Track" strip is pinned at the top of the header column, the scrolling header list
    // below it. Both live INSIDE trackHeaderBounds_, so the panel's three regions still tile.
    auto headerColumn = trackHeaderBounds_;
    addTrackButton_.setBounds(headerColumn.removeFromTop(kAddTrackButtonHeight).reduced(2, 1));
    trackHeaderViewport_.setBounds(headerColumn);
    layoutTrackHeaders();

    auto lanes = lanesBounds_;
    // While the piano roll is OPEN its chip toolbar is the top row of the lanes region — ABOVE the
    // ruler, so the roll's chrome sits over the whole unit instead of being sandwiched between the
    // ruler and the note canvas. The roll's rect then spans toolbar + ruler + canvas, and the roll
    // leaves the ruler's band blank for this sibling to draw in (PianoRollComponent::
    // setRulerBandHeight). While the roll is closed nothing here changes: the ruler is the top row and
    // the roll gets gridLanesBounds_ like the clip lanes do.
    const bool rollOpen = pianoRoll_.isOpen();
    const int rollTop = lanes.getY();
    // Open, the ruler drops below the roll's toolbar row; closed, it is the top row as it always was.
    if (rollOpen)
        lanes.removeFromTop(synth::ui::PianoRollComponent::kToolbarHeight);
    ruler_.setBounds(lanes.removeFromTop(rulerHeight));
    gridLanesBounds_ = lanes;

    // The clip-lane area fills EXACTLY the rect the grid below is painted into (paint()'s
    // gridLanesBounds_ loop, unchanged) — so clips line up with the bar/beat grid pixel-for-pixel.
    clipLaneArea_.setBounds(gridLanesBounds_);
    // The piano roll covers the clip lanes' rect PLUS its own toolbar row and the ruler band between
    // them (see above) — so its canvas is pixel-aligned with the clip lanes and the playhead exactly
    // as before, because canvasTop() accounts for the two rows above it. Closed, the extra rows are
    // zero-height and this is literally gridLanesBounds_.
    // The band height is pushed UNCONDITIONALLY, even while the roll is closed and its rect is only
    // the clip-lane rect. That looks redundant but is load-bearing: openPianoRoll() calls openClip()
    // — which frames the clip against canvasTop() — BEFORE this re-layout runs, so a band pushed in
    // only on the open path would frame the very first clip against the wrong canvas height. A closed
    // roll is invisible, so a canvasTop() describing the open geometry costs nothing meanwhile.
    pianoRoll_.setRulerBandHeight(rulerHeight);
    // The rect, by contrast, IS two-mode: closed, the roll is exactly the clip-lane rect (the two are
    // interchangeable there, and leaving an invisible component sitting over the ruler is the kind of
    // thing that reads as a bug); open, it also covers its toolbar row and the ruler band.
    pianoRoll_.setBounds(rollOpen ? gridLanesBounds_.withTop(rollTop) : gridLanesBounds_);
    // The ruler is a SIBLING drawn inside the band the roll reserves for it, and the roll was added to
    // this panel after the ruler — so without this the roll would paint over it. Re-asserted here
    // rather than once at open time because a re-layout is the only moment the overlap can appear.
    if (rollOpen)
        ruler_.toFront(false);

    // The playhead spans the WHOLE lanes region, ruler included, so the line reads as one stroke
    // from the ruler down through the tracks. Its local x == 0 is lanesBounds_.getX(), which is
    // also the ruler's — i.e. exactly TimelineViewState's origin, so no offset arithmetic is
    // needed anywhere in the overlay. It runs straight through the automation lanes too.
    playhead_.setBounds(lanesBounds_);
    // The piano roll's rect PLUS the ruler strip above it, expressed in the OVERLAY's coordinates.
    // While the roll is open the overlay skips these rows — the roll draws its own line
    // (LocalPlayheadClient), and the ruler strip is skipped too because it then labels bars
    // through the ROLL's mapping (setMappingOverride), where the overlay's shared-mapping x would
    // be a lie. While the roll is closed the region is ignored and the overlay owns its whole rect.
    // The roll's OWN rect, not gridLanesBounds_, so the toolbar row it added above the ruler is in
    // the skipped region too — otherwise the overlay would draw its line straight across the chips.
    // Closed, pianoRoll_.getBounds() IS gridLanesBounds_, so this is unchanged there.
    playhead_.setLocalPlayheadRegion(ruler_.getBounds().getUnion(pianoRoll_.getBounds()) -
                                     playhead_.getBounds().getPosition());

    // The automation lane editors cover the clip lanes' rect (each sits at its lane row); the piano
    // roll takes that rect over while it is open, so they hide with the clip lanes.
    automationLanes_.getBodies().setBounds(gridLanesBounds_);
    automationLanes_.getBodies().setVisible(!rollOpen);
    placeLaneBodies();

    // Snap selector: right-hand side of the transport bar. The transport controls (play/stop/
    // record/loop + BPM/time-sig + readout) fill the rest, left-aligned.
    auto transportBar = transportBarBounds_;
    transportBar.removeFromLeft(SidePaneToggleButton::kWidth); // the side-pane button, laid out by layoutSidePane()
    snapCombo_.setBounds(transportBar.removeFromRight(kSnapComboWidth).reduced(2));
    snapToggleButton_.setBounds(transportBar.removeFromRight(kSnapToggleButtonWidth).reduced(2));
    // Follow-playhead sits immediately left of the snap toggle — see its member comment.
    followPlayheadButton_.setBounds(transportBar.removeFromRight(kFollowPlayheadButtonWidth).reduced(2));
    // The tool strip sits immediately left of the snap controls: both are "how the next edit
    // behaves" chrome, so they read as one group, and neither pushes the transport controls off
    // their left-aligned home. Laid out left-to-right in EditTool order (1, 3, 4, 5, 7, 8).
    auto toolStrip = transportBar.removeFromRight(kEditToolButtonWidth * (int)kAllEditTools.size());
    for (auto tool : kAllEditTools)
        if (auto* button = getToolButton(tool))
            button->setBounds(toolStrip.removeFromLeft(kEditToolButtonWidth).reduced(2));
    transportBar_.setBounds(transportBar);
}

//==============================================================================
// The header column's width: the themed metric until the person drags the seam, then their width,
// clamped so the M/S/R block and a few letters of the name always fit and the clips keep the room.
int TimelinePanelComponent::defaultTrackHeaderWidth() const {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getTheme().metrics.timelineTrackHeaderWidth;
    return synth::theme::Metrics{}.timelineTrackHeaderWidth;
}

int TimelinePanelComponent::getTrackHeaderWidth() const {
    return trackHeaderWidth_ > 0 ? trackHeaderWidth_ : defaultTrackHeaderWidth();
}

void TimelinePanelComponent::setTrackHeaderWidth(int width, bool persist) {
    const int clamped = juce::jlimit(kMinTrackHeaderWidth, kMaxTrackHeaderWidth, width);
    if (clamped != getTrackHeaderWidth()) {
        trackHeaderWidth_ = clamped;
        resized();
        repaint();
    }
    if (persist && appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr) {
        appProperties_->getUserSettings()->setValue(kTimelineTrackHeaderWidthPropertyKey, getTrackHeaderWidth());
        appProperties_->saveIfNeeded();
    }
}

// One Tab stop on the seam (Left/Right nudge, Return resets) rather than a rebindable action: the
// column has one edge, and its arrows only mean something while it holds focus.
void TimelinePanelComponent::initTrackHeaderWidthHandle() {
    auto& handle = trackHeaderWidthHandle_;
    handle.setComponentID("timelineTrackHeaderWidthHandle");
    handle.setTitle("Track column width");
    handle.setTooltip("Drag to resize the track column; double-click for the default width. "
                      "When focused, Left/Right resize it and Return resets it.");
    handle.setKeyboardFocusable(true);
    handle.onDragStarted = [this] { trackHeaderWidthAtPress_ = getTrackHeaderWidth(); };
    handle.onDragged = [this](int delta) { setTrackHeaderWidth(trackHeaderWidthAtPress_ + delta, false); };
    handle.onDragEnded = [this] { setTrackHeaderWidth(getTrackHeaderWidth(), true); };
    handle.onResetRequested = [this] { setTrackHeaderWidth(defaultTrackHeaderWidth(), true); };
    handle.onKeyboardStep = [this](int direction) {
        setTrackHeaderWidth(getTrackHeaderWidth() + direction * kTrackHeaderWidthKeyStep, true);
    };
    addAndMakeVisible(handle);
}

//==============================================================================
void TimelinePanelComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour bg, border;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.bg0;
        border = c.border;
    } else {
        bg = juce::Colours::darkgrey.darker(0.5f);
        border = juce::Colours::grey;
    }

    g.fillAll(bg);

    // Thin top border separating the panel from the graph editor above it.
    g.setColour(border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());

    // Follow-playhead toggle: an always-visible pill outline, drawn HERE in the PARENT's paint()
    // (which always runs before the child's — same ordering the grid/clip-lane-area comment above
    // relies on) rather than trusting either the button's own resting-state fill or its icon
    // having loaded. AppLookAndFeel::drawDrawableButton's off/non-hovered fill is deliberately
    // transparent for a plain DrawableButton, and applyToolStripTheme() may not have found a
    // themed LookAndFeel by the time it first ran (see parentHierarchyChanged() above) — or this
    // build may simply have no icon asset linked at all. Either way the button painted NOTHING at
    // rest: present in the tree and clickable, but genuinely invisible — the reported bug.
    // snapToggleButton_ never has this problem because a plain TextButton always gets
    // AppLookAndFeel::drawButtonBackground's pill+border fill; this gives the one DrawableButton
    // on the strip with no text label to fall back on that same always-on affordance.
    if (const auto followBounds = followPlayheadButton_.getBounds().toFloat(); !followBounds.isEmpty()) {
        g.setColour(border.withAlpha(0.35f));
        g.fillRoundedRectangle(followBounds, 3.0f);
        g.setColour(border.withAlpha(0.7f));
        g.drawRoundedRectangle(followBounds.reduced(0.5f), 3.0f, 1.0f);
    }

    // Bar/beat/subdivision grid across the lanes region (below the ruler), using the SAME shared
    // TimelineViewState the ruler paints from. Colours come from the shared three-level policy in
    // TimelineClipLaneArea.h (gridLineColourFor / gridLevelIsReadable) — the piano roll paints its
    // grid from the same policy, so the two surfaces can't drift apart in visibility. The policy
    // lifts `border` toward the background's contrasting colour before applying the per-level
    // alpha, because on dark themes `border` alone sits too close to bg0 to read at any alpha.
    if (gridLanesBounds_.getWidth() > 0 && gridLanesBounds_.getHeight() > 0) {
        synth::TransportService* transport = ruler_.getTransport();
        double beatsPerBar = 4.0;
        if (transport != nullptr) {
            const auto snap = transport->getPositionSnapshot();
            const double tsBeatsPerBar =
                (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
            if (tsBeatsPerBar > 0.0)
                beatsPerBar = tsBeatsPerBar;
        }

        const double widthPx = (double)gridLanesBounds_.getWidth();
        const double startBeat = viewState_.firstVisibleBeat;
        const double endBeat = viewState_.xToBeat(widthPx);
        const bool drawBeatLines = viewState_.pixelsPerBeat >= kMinBeatLinePixelsPerBeat;
        const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));

        const juce::int64 firstBar = (juce::int64)std::floor(startBeat / beatsPerBar) - 1;
        const juce::int64 lastBar = (juce::int64)std::ceil(endBeat / beatsPerBar) + 1;

        const int top = gridLanesBounds_.getY();
        const int bottom = gridLanesBounds_.getBottom();
        const int xOrigin = gridLanesBounds_.getX();

        // The current snap division paints as a third, lightest level BETWEEN the beat lines —
        // only when it is finer than a beat and wide enough on screen to read as a grid rather
        // than as noise (gridLevelIsReadable's ~3 px density guard, the same rule Cubase applies).
        // Requiring drawBeatLines keeps the hierarchy monotonic: a subdivision may never be
        // visible while its parent beat level is hidden (possible otherwise, because the beat
        // gate is ~8 px/beat while the readability guard is ~3 px/line).
        //
        // divisionBeatsRAW, never divisionBeats(): the snap SWITCH must not change which lines are
        // DRAWN. Snap off means "don't magnetise to the grid", not "hide the grid" — a ruler that
        // loses its subdivision lines the moment you turn magnetism off leaves you eyeballing
        // positions against nothing, and the chosen division is still what the roll and the lanes
        // are showing. Same split PianoRollComponent draws with (see its own note: magnetism reads
        // divisionBeats, drawing reads divisionBeatsRaw).
        const double division = viewState_.divisionBeatsRaw(beatsPerBar);
        const bool drawSubdivisionLines = drawBeatLines && division > 0.0 && division < 1.0 &&
                                          synth::ui::gridLevelIsReadable(division, viewState_.pixelsPerBeat);

        const auto barColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Bar, border, bg);
        const auto beatColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Beat, border, bg);
        const auto subColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Subdivision, border, bg);

        for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
            const double barBeat = (double)bar * beatsPerBar;
            const double x = viewState_.beatToX(barBeat);
            // Cull only the BAR LINE itself when it sits outside the view — never the whole bar.
            // A bar whose own line has scrolled off the left edge still owns beats and
            // subdivisions that ARE on screen; a whole-bar `continue` here is exactly the bug
            // where every grid line left of the first visible bar line vanished while scrolling.
            // The beat/subdivision loops below cull per line.
            if (x >= -1.0 && x <= widthPx + 1.0) {
                g.setColour(barColour);
                g.drawVerticalLine(xOrigin + (int)std::llround(x), (float)top, (float)bottom);
            }

            if (drawBeatLines) {
                for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                    const double beatX = viewState_.beatToX(barBeat + (double)beatInBar);
                    if (beatX < 0.0 || beatX > widthPx)
                        continue;
                    g.setColour(beatColour);
                    g.drawVerticalLine(xOrigin + (int)std::llround(beatX), (float)top, (float)bottom);
                }
            }

            if (drawSubdivisionLines) {
                g.setColour(subColour);
                for (double beatInBar = division; beatInBar < beatsPerBar - division * 0.5; beatInBar += division) {
                    // Skip positions that already got a bar/beat line — a subdivision line under
                    // a stronger one would just anti-alias the stronger line's edge.
                    if (std::abs(beatInBar - (double)std::llround(beatInBar)) < division * 0.25)
                        continue;
                    const double subX = viewState_.beatToX(barBeat + beatInBar);
                    if (subX < 0.0 || subX > widthPx)
                        continue;
                    g.drawVerticalLine(xOrigin + (int)std::llround(subX), (float)top, (float)bottom);
                }
            }
        }
    }

    // ---- Column divider: the track-header column | lanes seam ----
    //
    // Drawn HERE, by the component that owns the seam, rather than by whatever happens to sit on
    // either side of it. The header column butts straight up against the lanes region — and, when
    // the piano roll is open, against the roll's own right-hand utility sidebar (the scale panel) —
    // so without this the track list and that sidebar read as one undifferentiated block. Any
    // future right-side sidebar inherits the divider for free, because it is a property of the
    // panel's layout and not of the sidebar.
    //
    // Spans the panel below the transport strip only: the transport bar is one continuous row of
    // chrome across the full width, and cutting it in half would imply a column boundary that its
    // own controls do not respect.
    if (!trackHeaderBounds_.isEmpty()) {
        const int x = trackHeaderBounds_.getRight() - 1;
        g.setColour(border);
        g.drawVerticalLine(x, (float)trackHeaderBounds_.getY(), (float)getHeight());
    }

    // ---- Marker stems through the lanes ----
    //
    // The ruler's flag says WHAT a marker is; this says WHERE, against the clips. A static painted
    // line at low alpha, repainted only when the doc changes (timelineChanged -> repaint of the
    // lanes rect) — no timer, no per-frame work, so docs/layout/animation.md#the-time-bounded-animation-rule is
    // untouched.
    if (doc_ != nullptr && !gridLanesBounds_.isEmpty() && viewState_.pixelsPerBeat > 0.0) {
        const double widthPx = (double)gridLanesBounds_.getWidth();
        for (const auto& marker : doc_->getMarkers()) {
            const double x = viewState_.beatToX(marker.beat);
            if (x < 0.0 || x > widthPx)
                continue;
            g.setColour(juce::Colour(marker.colourArgb).withAlpha(kMarkerLaneStemAlpha));
            g.fillRect((float)(gridLanesBounds_.getX() + (int)std::llround(x)), (float)gridLanesBounds_.getY(),
                       synth::ui::kMarkerStemWidth, (float)gridLanesBounds_.getHeight());
        }
    }
}

// Focus-region outline (Source/UI/Layout/FocusRegion.h), drawn OVER children -- the ruler,
// track header viewport, transport bar and clip lane area all tile wall-to-wall against this
// panel's own edge, so an outline painted at the end of paint() above would sit UNDER them and
// never show.
void TimelinePanelComponent::paintOverChildren(juce::Graphics& g) { synth::ui::paintFocusRegionOutline(*this, g); }

} // namespace synth::ui
