// TimelinePanelLayout.cpp
//
// Application-properties-backed preferences (snap/follow-playhead/scroll-invert),
// zoom/scroll helpers, resized(), paint()/paintOverChildren(), and the ResizeHandle
// child component (the panel's top-edge drag-to-resize affordance). TimelinePanelComponent
// is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this
// directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "../FocusRegion.h"
#include "../ScrollPolicy.h"
#include "../Theme/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr const char* kTimelineSnapPropertyKey = "timelineSnap";
constexpr const char* kTimelineSnapEnabledPropertyKey = "timelineSnapEnabled";
constexpr const char* kTimelineFollowPlayheadPropertyKey = "timelineFollowPlayhead";
// The roll's key-label density (PianoRollComponent::KeyLabelMode). "all" (default) labels every
// key row; "c" labels only the Cs. Owned by PreferencesSettingsTab's persistX pattern; read here
// by reloadPianoRollAppearancePrefs().
constexpr const char* kPianoRollKeyLabelsPropertyKey = "pianoRollKeyLabels";

// the "+ Track" strip at the top of the track-header column. Fixed height — the headers
// below it scroll, the button never does.
constexpr int kAddTrackButtonHeight = 22;

// Automation strip chrome geometry. Code-only (mirrors the rest of this file's literal
// fallbacks); the strip's own height comes from the themed Metrics::timelineAutomationStripHeight.
constexpr int kAutomationStripHeaderHeight = 24;
// 28 (was 24): timeline-panel button-size sweep — .reduced(2) at the setBounds() call site takes
// the effective width from 20 to 24 px.
constexpr int kAutomationToolButtonWidth = 28;
constexpr int kAutomationRecordModeComboWidth = 90;
constexpr int kAutomationCloseButtonWidth = 24;

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

void TimelinePanelComponent::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
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
    followPlayheadButton_.setToggleState(followPlayhead_, juce::dontSendNotification);
    pianoRoll_.setFollowPlayhead(followPlayhead_);

    // A pure forward — the transport bar owns and persists its own two keys. See this
    // method's header comment.
    transportBar_.setApplicationProperties(props);

    // Scale-panel visibility + user scales are the ROLL's own PropertiesFile-backed state (see
    // PianoRollComponent::setPropertiesFile); key-labels and note-colour overrides are read here.
    pianoRoll_.setPropertiesFile(appProperties_->getUserSettings());
    reloadPianoRollAppearancePrefs();
}

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

void TimelinePanelComponent::setScrollInverted(bool inverted) noexcept {
    scrollInverted_ = inverted;
    // Keep the roll in step — see this setter's header comment. It runs its OWN plain-scroll
    // branches (PianoRollComponent::mouseWheelMove), so a preference set on the panel chrome must
    // reach it directly rather than through anything shared like TimelineViewState.
    pianoRoll_.setScrollInverted(inverted);
}

void TimelinePanelComponent::setZoomScrollInverted(bool inverted) noexcept {
    zoomScrollInverted_ = inverted;
    pianoRoll_.setZoomScrollInverted(inverted); // same forwarding reason as setScrollInverted above
}

//==============================================================================
void TimelinePanelComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Reproject into the ruler's coordinate space regardless of whether the event originated on
    // this component or bubbled up from the ruler child — both share the same x == 0 origin as
    // TimelineViewState (the lanes/ruler content start), so this is exactly the anchor
    // beatToX/xToBeat expect.
    const double anchorX = (double)e.getEventRelativeTo(&ruler_).position.x;

    // Cubase-style bindings: Cmd = horizontal zoom, Cmd+Shift = vertical zoom (row height),
    // Shift or a trackpad's own deltaX = horizontal scroll, plain vertical wheel = vertical
    // track scroll (headers + lanes together).
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

    if (e.mods.isCommandDown() && e.mods.isShiftDown()) {
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
    const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (horizontal) {
        // Which axis the gesture arrived on, NOT "the dominant delta": a Shift+wheel that the OS
        // left on deltaY and a trackpad's own sideways deltaX are both horizontal scrolls, and
        // either one is the amount to move by.
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        const double deltaPx = (double)scrollAmount(delta, scrollInverted_) * kScrollPixelsPerWheelUnit;
        viewState_.scrollBeats(deltaPx / viewState_.pixelsPerBeat);
        ruler_.repaint();
        repaint();
        return;
    }

    scrollTrackRows((double)scrollAmount(wheel.deltaY, scrollInverted_) * kScrollPixelsPerWheelUnit);
}

void TimelinePanelComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch: deliberate enough that it needs no modifier. Plain pinch = horizontal zoom
    // around the pinch point; Shift+pinch = vertical (row height) zoom.
    if (e.mods.isShiftDown()) {
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
            return;
        zoomTrackRows((double)scaleFactor, (double)e.getEventRelativeTo(&clipLaneArea_).position.y);
        return;
    }
    zoomHorizontalAroundX((double)scaleFactor, (double)e.getEventRelativeTo(&ruler_).position.x);
}

//==============================================================================
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

void TimelinePanelComponent::zoomTimelineHorizontal(double factor) {
    zoomHorizontalAroundX(factor, visibleCentreXInRuler());
}

void TimelinePanelComponent::zoomTimelineVertical(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    zoomTrackRows(factor, visibleCentreYInLanes());
}

int TimelinePanelComponent::currentRowHeight() const {
    int base = TimelineTrackHeaderComponent::kRowHeight;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        base = lf->getTheme().metrics.timelineTrackRowHeight;
    return std::max(8, (int)std::llround((double)base * viewState_.rowHeightScale));
}

double TimelinePanelComponent::maxTrackScrollPx() const {
    const int rows = doc_ != nullptr ? (int)doc_->getTracks().size() : 0;
    return std::max(0.0, (double)(rows * currentRowHeight() - gridLanesBounds_.getHeight()));
}

void TimelinePanelComponent::scrollTrackRows(double deltaPx) {
    const double before = viewState_.trackScrollY;
    viewState_.scrollTracksPx(deltaPx, maxTrackScrollPx());
    if (viewState_.trackScrollY == before)
        return;
    syncTrackScroll();
}

void TimelinePanelComponent::zoomTrackRows(double factor, double anchorLaneY) {
    // Keep the row under the pointer put: contentY scales with the row height, the visible y must
    // not move. Recompute from the actual (rounded, clamped) row heights rather than `factor` so
    // the clamps can't make the anchor drift.
    const int oldRowHeight = currentRowHeight();
    const double contentY = anchorLaneY + viewState_.trackScrollY;
    viewState_.scaleRowHeight(factor);
    const int newRowHeight = currentRowHeight();
    if (newRowHeight == oldRowHeight)
        return;
    viewState_.trackScrollY = contentY * (double)newRowHeight / (double)oldRowHeight - anchorLaneY;
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
    repaint(gridLanesBounds_);
}

//==============================================================================
void TimelinePanelComponent::resized() {
    // Themed metrics with literal fallbacks for the headless test path (same pattern as
    // MainComponent::resized()).
    int transportBarHeight = 34;
    int trackHeaderWidth = 190;
    int rulerHeight = 30; // keep in step with Theme::Metrics::timelineRulerHeight
    int automationStripHeight = 72;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        transportBarHeight = m.timelineTransportBarHeight;
        trackHeaderWidth = m.timelineTrackHeaderWidth;
        rulerHeight = m.timelineRulerHeight;
        automationStripHeight = m.timelineAutomationStripHeight;
    }

    auto bounds = getLocalBounds();
    transportBarBounds_ = bounds.removeFromTop(transportBarHeight);
    trackHeaderBounds_ = bounds.removeFromLeft(trackHeaderWidth);
    lanesBounds_ = bounds; // remainder

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

    // The automation strip is carved from the BOTTOM of gridLanesBounds_ (which is what
    // shrinks the clip-lane area/piano roll below), leaving the ruler untouched above.
    if (automationStripVisible_ && gridLanesBounds_.getHeight() > automationStripHeight) {
        automationStripBounds_ = gridLanesBounds_.removeFromBottom(automationStripHeight);
    } else {
        automationStripBounds_ = {};
    }

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
    // needed anywhere in the overlay. Trimmed by the strip height too, so the line never
    // draws underneath the strip's own chrome.
    playhead_.setBounds(!automationStripBounds_.isEmpty() ? lanesBounds_.withTrimmedBottom(automationStripHeight)
                                                          : lanesBounds_);
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

    // Strip header row (tool buttons, lane/record-mode pickers, close) above the curve
    // canvas. Visibility follows automationStripVisible_ exactly — nothing else flips it.
    const bool stripOpen = !automationStripBounds_.isEmpty();
    automationEditor_.setVisible(stripOpen);
    automationToolPointerButton_.setVisible(stripOpen);
    automationToolPencilButton_.setVisible(stripOpen);
    automationToolLineButton_.setVisible(stripOpen);
    automationToolEraserButton_.setVisible(stripOpen);
    laneCombo_.setVisible(stripOpen);
    recordModeCombo_.setVisible(stripOpen);
    automationCloseButton_.setVisible(stripOpen);
    if (stripOpen) {
        auto strip = automationStripBounds_;
        auto header = strip.removeFromTop(kAutomationStripHeaderHeight);
        automationToolPointerButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolPencilButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolLineButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolEraserButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationCloseButton_.setBounds(header.removeFromRight(kAutomationCloseButtonWidth).reduced(2));
        recordModeCombo_.setBounds(header.removeFromRight(kAutomationRecordModeComboWidth).reduced(2));
        laneCombo_.setBounds(header.reduced(2));
        automationEditor_.setBounds(strip);
    }

    // The grab strip runs the panel's full width along its top edge, OVERLAPPING the transport-bar
    // strip — transportBarBounds_ stays the whole strip (the three regions still tile), but the
    // controls inside it are laid out BELOW the handle so a resize grab never lands on a button.
    resizeHandle_.setBounds(0, 0, getWidth(), kResizeHandleHeight);

    // Snap selector: right-hand side of the transport bar. The transport controls (play/stop/
    // record/loop + BPM/time-sig + readout) fill the rest, left-aligned.
    auto transportBar = transportBarBounds_.withTrimmedTop(kResizeHandleHeight);
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
    // lanes rect) — no timer, no per-frame work, so the §3 animation rules are untouched.
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

// T159: focus-region outline (Source/UI/FocusRegion.h) -- see the paintOverChildren declaration's
// comment in the header for why this can't just be tacked onto the end of paint() above.
void TimelinePanelComponent::paintOverChildren(juce::Graphics& g) { synth::ui::paintFocusRegionOutline(*this, g); }

//==============================================================================
// ---- Top-edge resize handle ----

TimelinePanelComponent::ResizeHandle::ResizeHandle(TimelinePanelComponent& owner)
    : owner_(owner) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void TimelinePanelComponent::ResizeHandle::paint(juce::Graphics& g) {
    juce::Colour line;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        line = isHighlighted() ? c.accent : c.border;
    } else {
        line = isHighlighted() ? juce::Colours::white : juce::Colours::grey;
    }

    // Idle: exactly the hairline the panel already draws at y == 0, so nothing new is visible until
    // the pointer arrives. Hovered/dragging: the hairline brightens and the strip picks up a faint
    // wash, which is the whole affordance.
    if (isHighlighted())
        g.fillAll(line.withAlpha(0.18f));
    g.setColour(line);
    g.fillRect(0, 0, getWidth(), 1);
}

void TimelinePanelComponent::ResizeHandle::mouseEnter(const juce::MouseEvent&) {
    if (hovered_)
        return; // repaint only on a CHANGE
    hovered_ = true;
    repaint();
}

void TimelinePanelComponent::ResizeHandle::mouseExit(const juce::MouseEvent&) {
    if (!hovered_)
        return;
    hovered_ = false;
    if (!dragging_)
        repaint();
}

int TimelinePanelComponent::ResizeHandle::desiredHeightFor(const juce::MouseEvent& e) const {
    // Absolute, not a delta: the owner moves the panel's top edge (and this handle with it) on
    // every callback, so only the panel's FIXED bottom edge is a stable reference.
    const int topY = e.getScreenPosition().y - grabOffsetY_;
    return owner_.getScreenBounds().getBottom() - topY;
}

void TimelinePanelComponent::ResizeHandle::mouseDown(const juce::MouseEvent& e) {
    const bool wasHighlighted = isHighlighted();
    dragging_ = true;
    moved_ = false;
    grabOffsetY_ = (int)e.getEventRelativeTo(&owner_).position.y;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (!wasHighlighted)
        repaint();
}

void TimelinePanelComponent::ResizeHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    moved_ = true;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (owner_.onResizeHeight)
        owner_.onResizeHeight(lastDesiredHeight_);
}

void TimelinePanelComponent::ResizeHandle::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (!hovered_)
        repaint(); // the highlight only changes when the pointer has already left
    // Persist point for the owner — and only for a real drag: a stray click must not write settings.
    if (moved_ && owner_.onResizeHeightCommitted)
        owner_.onResizeHeightCommitted(lastDesiredHeight_);
}

} // namespace synth::ui
