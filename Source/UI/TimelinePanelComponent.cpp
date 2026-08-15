#include "TimelinePanelComponent.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
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
} // namespace

//==============================================================================
TimelinePanelComponent::TimelinePanelComponent() {
    addAndMakeVisible(ruler_);

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
}

//==============================================================================
void TimelinePanelComponent::setTransport(synth::TransportService* transport) { ruler_.setTransport(transport); }

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

    auto lanes = lanesBounds_;
    ruler_.setBounds(lanes.removeFromTop(rulerHeight));
    gridLanesBounds_ = lanes;

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
