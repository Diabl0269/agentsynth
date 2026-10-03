// Concern: TimelineRoutingPane's state -- deriving what to show from the document and the host, the children's
// content and visibility, and the section layout. Painting is TimelineRoutingPanePaint.cpp; the clicks are
// TimelineRoutingPaneActions.cpp.
#include "TimelineRoutingPane.h"

#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

namespace {
constexpr int kPad = 8;
constexpr int kHeaderHeight = 34;
constexpr int kHeadingHeight = 16;
constexpr int kButtonHeight = 24;
constexpr int kLinkHeight = 18;
constexpr int kChipHeight = 22;
constexpr int kNoteHeight = 42;
constexpr int kRowGap = 4;
constexpr int kEmptyLineHeight = 48;

juce::String chevron() { return juce::String::fromUTF8("\xE2\x80\xBA"); }
} // namespace

TimelineRoutingPane::TimelineRoutingPane()
    : showOnCanvas_("Show on canvas " + chevron())
    , showInMixer_("Show in mixer " + chevron()) {
    setComponentID("timelineRoutingPane");
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    canvasNode_.setTitle("Canvas node");
    canvasNode_.setDescription("The node that plays this track. Opens a menu to choose a different one.");
    canvasNode_.onClick = [this] { openBindingMenu(); };
    midiDestinations_.setTitle("MIDI destinations");
    midiDestinations_.setDescription("The instruments this track's notes are sent to. Opens a picker to change them.");
    midiDestinations_.onClick = [this] { openMidiDestinationsPicker(); };
    showOnCanvas_.setDescription("Selects the node that plays this track in the graph");
    showOnCanvas_.onClick = [this] { showOnCanvas(); };
    showInMixer_.setDescription("Finds this track's channel in the mixer");
    showInMixer_.onClick = [this] { showInMixer(); };
    // The channel row is read-only: the chip is only a name and a meter here, the link beside it does the finding.
    channelChip_.setInterceptsMouseClicks(false, false);

    // A click on a link must never move keyboard focus off the panel's focus root.
    for (auto* link : {&showOnCanvas_, &showInMixer_}) {
        link->setWantsKeyboardFocus(false);
        link->setMouseClickGrabsKeyboardFocus(false);
    }

    for (auto* child : std::initializer_list<juce::Component*>{&canvasNode_, &showOnCanvas_, &midiDestinations_,
                                                               &channelChip_, &showInMixer_})
        addChildComponent(*child);
    refresh();
}

TimelineRoutingPane::~TimelineRoutingPane() = default;

void TimelineRoutingPane::setDoc(synth::TimelineDoc* doc) { doc_ = doc; }

void TimelineRoutingPane::setHost(TrackHeaderHost* host) { host_ = host; }

void TimelineRoutingPane::setTrack(synth::TrackId track) {
    track_ = track;
    refresh();
}

void TimelineRoutingPane::refresh() {
    computeView();
    applyViewToChildren();
    layoutSections();
    repaint();
}

bool TimelineRoutingPane::tickMeter() {
    if (!channelChip_.isVisible() || host_ == nullptr)
        return false;
    auto* link = host_->getChannelLinkSurface();
    return link != nullptr && channelChip_.setMeterLevel(link->getChannelMeterPeak(track_));
}

// ---- What to show -----------------------------------------------------------------------------

void TimelineRoutingPane::computeView() {
    view_ = {};
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (track == nullptr) {
        view_.emptyLine = "Select a MIDI or audio track to see its routing.";
        return;
    }
    if (track->kind == synth::TrackKind::Automation) {
        view_.emptyLine = "Nothing to route on an automation track.";
        return;
    }

    view_.hasTrack = true;
    view_.name = track->name;
    view_.isMidi = track->kind == synth::TrackKind::Midi;
    view_.kindBadge = view_.isMidi ? "MIDI" : "Audio";
    int index = 0;
    for (const auto& other : doc_->getTracks()) {
        if (other.id == track->id)
            break;
        ++index;
    }
    view_.colour = resolveTrackColour(track->colourArgb, index, false);
    computeCanvasNode(*track);
    computeNotesAndChannel();
}

// Three states, like the header's binding chip: bound to a live node, never bound, or orphaned. The gone node's own
// name is not knowable (the document keeps only its uuid), so the orphaned case names the kind of node instead.
void TimelineRoutingPane::computeCanvasNode(const synth::Track& track) {
    const juce::String kindNode = view_.isMidi ? "Track In" : "Track Audio";
    if (track.bindingUuid.isEmpty()) {
        view_.canvasNodeText = "Not connected";
        view_.canvasNodeWarning = true;
    } else if (track.orphaned) {
        view_.canvasNodeText = kindNode + " (missing)";
        view_.canvasNodeWarning = true;
        view_.missingNote = juce::String(view_.isMidi ? "Its notes" : "Its clips") + " are kept. Pick a new " +
                            kindNode + " to hear them again.";
    } else {
        const auto name = host_ != nullptr ? host_->getNodeDisplayName(track.bindingUuid) : juce::String();
        view_.canvasNodeText = name.isNotEmpty() ? name : kindNode;
        view_.bound = true;
    }
}

void TimelineRoutingPane::computeNotesAndChannel() {
    if (view_.isMidi) {
        juce::StringArray connected;
        if (host_ != nullptr)
            for (const auto& option : host_->getMidiDestinationOptions(track_))
                if (option.connected)
                    connected.add(option.displayName);
        view_.midiDestinationsText = connected.isEmpty() ? juce::String("None") : connected.joinIntoString(", ");
    }

    auto* link = host_ != nullptr ? host_->getChannelLinkSurface() : nullptr;
    const auto info = link != nullptr ? link->getChannelInfo(track_) : TrackChannelLinkSurface::ChannelInfo{};
    view_.hasChannel = info.hasChannel;
    view_.channelText =
        info.hasChannel ? juce::String::fromUTF8("Channel \xC2\xB7 ") + info.channelName : "No mixer channel";
}

void TimelineRoutingPane::applyViewToChildren() {
    const bool routable = view_.hasTrack;
    canvasNode_.setVisible(routable);
    midiDestinations_.setVisible(routable && view_.isMidi);
    showOnCanvas_.setVisible(routable && view_.bound);
    channelChip_.setVisible(routable && view_.hasChannel);
    showInMixer_.setVisible(routable && view_.hasChannel);
    if (!routable)
        return;

    canvasNode_.setButtonText(view_.canvasNodeText);
    canvasNode_.setWarning(view_.canvasNodeWarning);
    canvasNode_.setTooltip(view_.canvasNodeWarning ? "This track plays nowhere yet. Click to choose a node."
                                                   : "This track plays through '" + view_.canvasNodeText +
                                                         "'. Click to choose a different node.");
    midiDestinations_.setButtonText(view_.midiDestinationsText);
    midiDestinations_.setTooltip("MIDI destinations: " + view_.midiDestinationsText + ". Click to change.");
    channelChip_.setChannelName(view_.channelText);
    setTitle("Routing for " + view_.name);
}

// ---- Layout -----------------------------------------------------------------------------------

// Sections stack top to bottom, each set off from the next by a 1 px border. The rectangles paint() needs are kept in
// layout_; the children are placed directly.
void TimelineRoutingPane::layoutSections() {
    layout_ = {};
    const int contentWidth = juce::jmax(0, getWidth() - 2 * kPad);
    if (!view_.hasTrack) {
        layout_.emptyLine = {kPad, kPad, contentWidth, kEmptyLineHeight};
        setContentHeight(2 * kPad + kEmptyLineHeight);
        return;
    }

    layout_.header = {0, 0, getWidth(), kHeaderHeight};
    int y = kHeaderHeight + kPad;
    auto row = [&](int height) {
        const juce::Rectangle<int> bounds(kPad, y, contentWidth, height);
        y += height;
        return bounds;
    };
    auto endSection = [&] {
        y += kPad;
        layout_.dividers.push_back(y);
        y += 1 + kPad;
    };

    layout_.canvasNodeHeading = row(kHeadingHeight);
    canvasNode_.setBounds(row(kButtonHeight));
    y += kRowGap;
    if (view_.missingNote.isNotEmpty()) {
        layout_.missingNote = row(kNoteHeight);
        y += kRowGap;
    }
    if (showOnCanvas_.isVisible())
        showOnCanvas_.setBounds(row(kLinkHeight));
    endSection();

    if (view_.isMidi) {
        layout_.midiDestinationsHeading = row(kHeadingHeight);
        midiDestinations_.setBounds(row(kButtonHeight));
        endSection();
    }

    layout_.mixerChannelHeading = row(kHeadingHeight);
    if (view_.hasChannel) {
        channelChip_.setBounds(row(kChipHeight));
        y += kRowGap;
        showInMixer_.setBounds(row(kLinkHeight));
    } else {
        layout_.noChannel = row(kLinkHeight);
    }
    setContentHeight(y + kPad);
}

// A section appearing or going (a missing-node note, an audio track's missing Notes row) changes the height the
// scroller has to offer; its fit() lays out again at the same width, so this settles in one pass.
void TimelineRoutingPane::setContentHeight(int height) {
    const bool changed = height != contentHeight_;
    contentHeight_ = height;
    if (changed && !fitting_)
        scroller_.fit();
}

void TimelineRoutingPane::resized() { layoutSections(); }

TimelineRoutingPane::Scroller::Scroller(TimelineRoutingPane& pane)
    : pane_(pane) {
    viewport_.setViewedComponent(&pane_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
}

// Width first (the layout, and so the content height, depends on it), then the height: at least the visible
// height so the pane's background fills it, more when the sections need it and the scrollbar takes its strip.
void TimelineRoutingPane::Scroller::fit() {
    if (pane_.fitting_)
        return;
    pane_.fitting_ = true;
    viewport_.setBounds(getLocalBounds());
    const int visibleHeight = viewport_.getHeight();
    pane_.setSize(viewport_.getWidth(), juce::jmax(visibleHeight, pane_.getHeight()));
    if (pane_.getContentHeight() > visibleHeight)
        pane_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(), pane_.getHeight());
    pane_.setSize(pane_.getWidth(), juce::jmax(visibleHeight, pane_.getContentHeight()));
    pane_.fitting_ = false;
}

} // namespace synth::ui
