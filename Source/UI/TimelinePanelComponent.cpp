#include "TimelinePanelComponent.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
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
constexpr const char* kTimelineSnapPropertyKey = "timelineSnap";

// TL5-3: the "+ MIDI Track" strip at the top of the track-header column. Fixed height — the
// headers below it scroll, the button never does.
constexpr int kAddTrackButtonHeight = 22;
} // namespace

//==============================================================================
TimelinePanelComponent::TimelinePanelComponent() {
    addAndMakeVisible(ruler_);

    addAndMakeVisible(addTrackButton_);
    addTrackButton_.setComponentID("timelineAddTrackButton");
    addTrackButton_.onClick = [this] {
        if (trackHeaderHost_ != nullptr)
            trackHeaderHost_->addMidiTrack();
    };

    addAndMakeVisible(trackHeaderViewport_);
    trackHeaderViewport_.setComponentID("timelineTrackHeaderViewport");
    trackHeaderViewport_.setScrollBarsShown(true, false);
    trackHeaderViewport_.setViewedComponent(&trackHeaderList_, false);

    addAndMakeVisible(snapCombo_);
    snapCombo_.setComponentID("timelineSnapCombo");
    snapCombo_.addItem("Off", 1);
    snapCombo_.addItem("Bar", 2);
    snapCombo_.addItem("1", 3);
    snapCombo_.addItem("1/2", 4);
    snapCombo_.addItem("1/4", 5);
    snapCombo_.addItem("1/8", 6);
    snapCombo_.addItem("1/16", 7);
    snapCombo_.setSelectedId((int)viewState_.snap + 1, juce::dontSendNotification);
    snapCombo_.onChange = [this] {
        viewState_.snap = (TimelineViewState::Snap)(snapCombo_.getSelectedId() - 1);
        persistSnapChoice();
        ruler_.repaint();
    };

    // TL5-4: added LAST so it is topmost — it draws over the ruler AND the lanes grid.
    addAndMakeVisible(playhead_);
    playhead_.setComponentID("timelinePlayhead");
}

TimelinePanelComponent::~TimelinePanelComponent() {
    if (doc_ != nullptr)
        doc_->removeListener(this);
}

//==============================================================================
void TimelinePanelComponent::setTransport(synth::TransportService* transport) {
    ruler_.setTransport(transport);
    playhead_.setTransport(transport);
}

void TimelinePanelComponent::updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot,
                                                 double outputLatencySeconds) {
    ++transportUpdateCount_;
    playhead_.updateFromTransport(snapshot, outputLatencySeconds);

    // Nothing else repaints the ruler when the time signature or the loop range changes from
    // OUTSIDE its own mouse gestures (a preset/bundle load, a host tempo map, TL5-5's transport
    // controls), so this poll is where that is noticed. Diffed, not unconditional: an idle poll
    // repaints nothing.
    const RulerTransportState state{snapshot.timeSigNumerator, snapshot.timeSigDenominator, snapshot.looping,
                                    snapshot.loopStartPpq, snapshot.loopEndPpq};
    if (!hasRulerState_) {
        hasRulerState_ = true;
        rulerState_ = state;
        return;
    }
    if (state == rulerState_)
        return;

    const bool timeSigChanged = state.timeSigNumerator != rulerState_.timeSigNumerator ||
                                state.timeSigDenominator != rulerState_.timeSigDenominator;
    rulerState_ = state;
    ruler_.repaint();
    // The lanes grid's bar spacing comes from the time signature too — but only that, so a mere
    // loop change costs the ruler strip alone.
    if (timeSigChanged)
        repaint();
}

void TimelinePanelComponent::setTimelineDoc(synth::TimelineDoc* doc) {
    if (doc_ == doc)
        return;
    if (doc_ != nullptr)
        doc_->removeListener(this);
    doc_ = doc;
    if (doc_ != nullptr)
        doc_->addListener(this);
    syncTrackHeaders();
}

void TimelinePanelComponent::setTrackHeaderHost(TrackHeaderHost* host) {
    trackHeaderHost_ = host;
    // Headers are constructed with the host, so any that already exist have to be rebuilt against
    // the new one rather than refreshed.
    trackHeaderList_.headers.clear();
    syncTrackHeaders();
}

void TimelinePanelComponent::timelineChanged(const synth::TimelineDoc&) { syncTrackHeaders(); }

void TimelinePanelComponent::syncTrackHeaders() {
    if (doc_ == nullptr) {
        if (!trackHeaderList_.headers.isEmpty()) {
            trackHeaderList_.headers.clear();
            layoutTrackHeaders();
        }
        return;
    }

    const auto& tracks = doc_->getTracks();

    // Rebuild only when the SET of tracks changed. A mute toggle, a rename or a re-bind must not
    // destroy and re-create every row (it would drop an in-progress name edit and churn the UI).
    bool sameTracks = (int)tracks.size() == trackHeaderList_.headers.size();
    if (sameTracks) {
        for (int i = 0; i < (int)tracks.size(); ++i) {
            if (!(trackHeaderList_.headers.getUnchecked(i)->getTrackId() == tracks[(size_t)i].id)) {
                sameTracks = false;
                break;
            }
        }
    }

    if (sameTracks) {
        for (auto* header : trackHeaderList_.headers)
            header->refreshFromDoc();
        return;
    }

    trackHeaderList_.headers.clear();
    for (const auto& track : tracks) {
        auto* header =
            trackHeaderList_.headers.add(new TimelineTrackHeaderComponent(*doc_, track.id, trackHeaderHost_));
        trackHeaderList_.addAndMakeVisible(header);
    }
    layoutTrackHeaders();
}

void TimelinePanelComponent::layoutTrackHeaders() {
    const int rowHeight = TimelineTrackHeaderComponent::kRowHeight;
    const int count = trackHeaderList_.headers.size();
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());

    trackHeaderList_.setSize(width, std::max(count * rowHeight, trackHeaderViewport_.getMaximumVisibleHeight()));
    for (int i = 0; i < count; ++i)
        trackHeaderList_.headers.getUnchecked(i)->setBounds(0, i * rowHeight, width, rowHeight);
}

void TimelinePanelComponent::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;

    int saved = appProperties_->getUserSettings()->getIntValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    saved = juce::jlimit((int)TimelineViewState::Snap::Off, (int)TimelineViewState::Snap::Sixteenth, saved);
    viewState_.snap = (TimelineViewState::Snap)saved;
    snapCombo_.setSelectedId(saved + 1, juce::dontSendNotification);
}

void TimelinePanelComponent::persistSnapChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    appProperties_->saveIfNeeded();
}

//==============================================================================
void TimelinePanelComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Reproject into the ruler's coordinate space regardless of whether the event originated on
    // this component or bubbled up from the ruler child — both share the same x == 0 origin as
    // TimelineViewState (the lanes/ruler content start), so this is exactly the anchor
    // beatToX/xToBeat expect.
    const double anchorX = (double)e.getEventRelativeTo(&ruler_).position.x;

    if (e.mods.isCommandDown()) {
        const double factor = std::exp((double)wheel.deltaY * kZoomWheelSensitivity);
        viewState_.zoomAroundX(factor, anchorX);
    } else {
        const double deltaBeats = -(double)wheel.deltaY * kScrollPixelsPerWheelUnit / viewState_.pixelsPerBeat;
        viewState_.scrollBeats(deltaBeats);
    }

    ruler_.repaint();
    repaint();
}

//==============================================================================
void TimelinePanelComponent::resized() {
    // Themed metrics with literal fallbacks for the headless test path (same pattern as
    // MainComponent::resized()/computePanelBounds()).
    int transportBarHeight = 28;
    int trackHeaderWidth = 160;
    int rulerHeight = 24;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        transportBarHeight = m.timelineTransportBarHeight;
        trackHeaderWidth = m.timelineTrackHeaderWidth;
        rulerHeight = m.timelineRulerHeight;
    }

    auto bounds = getLocalBounds();
    transportBarBounds_ = bounds.removeFromTop(transportBarHeight);
    trackHeaderBounds_ = bounds.removeFromLeft(trackHeaderWidth);
    lanesBounds_ = bounds; // remainder

    // TL5-3: "+ MIDI Track" pinned at the top of the header column, the scrolling header list
    // below it. Both live INSIDE trackHeaderBounds_, so the panel's three regions still tile.
    auto headerColumn = trackHeaderBounds_;
    addTrackButton_.setBounds(headerColumn.removeFromTop(kAddTrackButtonHeight).reduced(2, 1));
    trackHeaderViewport_.setBounds(headerColumn);
    layoutTrackHeaders();

    auto lanes = lanesBounds_;
    ruler_.setBounds(lanes.removeFromTop(rulerHeight));
    gridLanesBounds_ = lanes;

    // The playhead spans the WHOLE lanes region, ruler included, so the line reads as one stroke
    // from the ruler down through the tracks. Its local x == 0 is lanesBounds_.getX(), which is
    // also the ruler's — i.e. exactly TimelineViewState's origin, so no offset arithmetic is
    // needed anywhere in the overlay.
    playhead_.setBounds(lanesBounds_);

    // Snap selector: right-hand side of the transport bar; the rest of that strip stays empty
    // until TL5-5's transport controls arrive.
    auto transportBar = transportBarBounds_;
    snapCombo_.setBounds(transportBar.removeFromRight(kSnapComboWidth).reduced(2));
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

    // Bar/beat grid across the lanes region (below the ruler), using the SAME shared
    // TimelineViewState the ruler paints from — bar lines stronger, beat lines fainter. No
    // dedicated grid colour tokens exist yet (checked Theme::Colors — nothing grid-specific), so
    // this reuses `border` at two alpha levels rather than adding new tokens for one caller.
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

        for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
            const double barBeat = (double)bar * beatsPerBar;
            const double x = viewState_.beatToX(barBeat);
            if (x < -1.0 || x > widthPx + 1.0)
                continue;

            g.setColour(border);
            g.drawVerticalLine(xOrigin + (int)std::llround(x), (float)top, (float)bottom);

            if (drawBeatLines) {
                for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                    const double beatX = viewState_.beatToX(barBeat + (double)beatInBar);
                    if (beatX < 0.0 || beatX > widthPx)
                        continue;
                    g.setColour(border.withAlpha(0.35f));
                    g.drawVerticalLine(xOrigin + (int)std::llround(beatX), (float)top, (float)bottom);
                }
            }
        }
    }
}

} // namespace synth::ui
