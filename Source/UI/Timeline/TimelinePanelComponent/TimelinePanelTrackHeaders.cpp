// TimelinePanelTrackHeaders.cpp
//
// Track header column: the add-track menu (instrument/plugin submenu, marker creation),
// TimelineDoc::Listener::timelineChanged, header sync/layout and focus movement (the
// drag-to-reorder gesture lives in TimelinePanelTrackDrag.cpp). TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the
// rest of the class.

#include "TimelinePanelComponent.h"

#include "AppUndoManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// The Instrument -> Plugin submenu ALWAYS appends the format so a VST3 and an AU
// build of the same product (e.g. "Massive") don't show as two identical, unlabelled rows — same
// "disambiguate by format" job ModuleLibraryComponent's plugin sub-headers do for the library
// sidebar, just inline on the row instead of a separate group label (this submenu is flat, with no
// room for sub-headers). `format` is `juce::PluginDescription::pluginFormatName` ("VST3" /
// "AudioUnit"); "AudioUnit" is shortened to "AU" to match the sidebar's own short form, anything
// else (a format this app doesn't know about yet) is shown as-is rather than guessing an
// abbreviation.
juce::String shortPluginFormatLabel(const juce::String& format) {
    return format == "AudioUnit" ? juce::String("AU") : format;
}
} // namespace

// Handed to every track header (and driven by the "+ MIDI Track" button), so the header column's
// whole conversation with the app goes through one seam.
void TimelinePanelComponent::setTrackHeaderHost(TrackHeaderHost* host) {
    trackHeaderHost_ = host;
    routingPane_.setHost(host);
    automationLanes_.setHost(host); // lane headers name their parameter and module through it
    // Headers are constructed with the host, so any that already exist have to be rebuilt against
    // the new one rather than refreshed.
    trackHeaderList_.headers.clear();
    syncTrackHeaders();
    refreshRoutingPane();
    // The channel-chip meter tick. Started here rather than in the constructor -- there is
    // nothing to meter until an app host exists, and the constructor is already at its size cap.
    if (host != nullptr)
        startTimerHz(15);
    else
        stopTimer();
}

// ONE shared 15 Hz timer for every header row's channel chip -- never one per row (up to
// TimelineDoc::kMaxTracks of them). Unconditional iteration, deliberately GATED repaints:
// TimelineTrackHeaderComponent::tickChannelMeter() only repaints when the drawn level actually
// moved (ChannelChipComponent::kMeterRepaintThreshold), which is the same shape ModuleComponent's
// own 15 Hz meter poll uses -- so this is not an unconditional per-tick repaint (Source/UI/
// CLAUDE.md). A header whose track reaches no channel has no chip visible and costs a bool read.
void TimelinePanelComponent::timerCallback() {
    // The panel is hidden by setVisible(false) when the user closes it (MainComponentPanels), and
    // the timer runs from setTrackHeaderHost onwards regardless -- so the cheapest gate of all is
    // first: a hidden meter is a repaint nobody can see.
    if (!isVisible())
        return;
    for (auto* header : trackHeaderList_.headers)
        if (header != nullptr)
            header->tickChannelMeter();
    tickRoutingPane();
}

// The headless test seam for a menu that never runs in a test process -- the same split
// TimelineTrackHeaderComponent's binding and context menus use (applyBindingMenuChoice /
// applyContextMenuChoice).
void TimelinePanelComponent::applyAddTrackMenuChoice(int menuId) {
    // Marker first: it is the one entry that needs no TrackHeaderHost (a marker is document data
    // with no graph node behind it), so it must not be gated on the host check below.
    if (menuId == kAddMarkerMenuId) {
        addMarkerAtPlayhead();
        return;
    }

    if (trackHeaderHost_ == nullptr)
        return;

    if (menuId == kAddMidiTrackMenuId)
        trackHeaderHost_->addMidiTrack();
    else if (menuId == kAddAudioTrackMenuId)
        trackHeaderHost_->addAudioTrack();
    else if (menuId == kAddInstrumentOscillatorMenuId)
        trackHeaderHost_->addInstrumentTrack("Oscillator", false);
    else if (menuId == kAddInstrumentWavetableMenuId)
        trackHeaderHost_->addInstrumentTrack("Wavetable", false);
    else if (menuId == kAddInstrumentSamplerMenuId)
        trackHeaderHost_->addInstrumentTrack("Sampler", false);
    else if (menuId == kAddInstrumentOscillatorPolyMenuId)
        trackHeaderHost_->addInstrumentTrack("Oscillator", true);
    else if (menuId == kAddInstrumentWavetablePolyMenuId)
        trackHeaderHost_->addInstrumentTrack("Wavetable", true);
    else if (menuId == kCreateChannelsMenuId)
        trackHeaderHost_->createChannelsForExistingTracks();
    else if (menuId == kInsertTrackPresetFromFileMenuId)
        trackHeaderHost_->addTrackFromPresetFile();
    else if (menuId >= kAddTrackPresetBusMenuIdBase) {
        // Resolved against the snapshot buildAddTrackMenu() captured, same reason the
        // Audio/Instrument submenus are — see busTrackPresetMenuSnapshot_'s own comment. Checked
        // BEFORE kAddTrackPresetInstrumentMenuIdBase since its base (7000) sits above it (6000).
        const int index = menuId - kAddTrackPresetBusMenuIdBase;
        if (index >= 0 && index < (int)busTrackPresetMenuSnapshot_.size())
            trackHeaderHost_->addBusFromPreset(busTrackPresetMenuSnapshot_[(size_t)index].name);
    } else if (menuId >= kAddTrackPresetInstrumentMenuIdBase) {
        // Resolved against the snapshot buildAddTrackMenu() captured, same reason the
        // plugin list is (a preset can be saved/deleted between menu-open and click) — see
        // instrumentTrackPresetMenuSnapshot_'s own comment.
        const int index = menuId - kAddTrackPresetInstrumentMenuIdBase;
        if (index >= 0 && index < (int)instrumentTrackPresetMenuSnapshot_.size())
            trackHeaderHost_->addTrackFromPreset(instrumentTrackPresetMenuSnapshot_[(size_t)index].name,
                                                 synth::TrackPresetKind::Instrument);
    } else if (menuId >= kAddTrackPresetAudioMenuIdBase) {
        const int index = menuId - kAddTrackPresetAudioMenuIdBase;
        if (index >= 0 && index < (int)audioTrackPresetMenuSnapshot_.size())
            trackHeaderHost_->addTrackFromPreset(audioTrackPresetMenuSnapshot_[(size_t)index].name,
                                                 synth::TrackPresetKind::Audio);
    } else if (menuId >= kAddInstrumentPluginMenuIdBase) {
        // Resolved against the SNAPSHOT buildAddTrackMenu() captured when this menu was
        // built, never by re-running collectInstrumentPluginMenuOptions() here — the known-plugin
        // list can be mutated by a background scan between the menu opening and this click landing
        // (see instrumentPluginMenuSnapshot_'s own comment). kAddInstrumentPluginNoneMenuId itself
        // is a disabled row and JUCE never delivers a disabled item's id, so nothing here needs to
        // special-case it.
        const int index = menuId - kAddInstrumentPluginMenuIdBase;
        if (index >= 0 && index < (int)instrumentPluginMenuSnapshot_.size())
            trackHeaderHost_->addInstrumentPluginTrack(instrumentPluginMenuSnapshot_[(size_t)index]);
    }
}

synth::MarkerId TimelinePanelComponent::addMarkerAtPlayhead() {
    if (doc_ == nullptr)
        return {};

    // The transport's CURRENT position, unsnapped: a marker is a cue for something the user just
    // heard, so it belongs exactly where the playhead is rather than on the nearest grid line
    // (a drag afterwards DOES snap — see TimelineRulerComponent::mouseDrag).
    const double beat = transport_ != nullptr ? std::max(0.0, transport_->getPositionSnapshot().ppq) : 0.0;
    // "Marker N" counted off the existing markers, the same shape createMidiClipAt's "Clip N" uses.
    // Not unique by construction (deleting #2 of three makes the next one a second "Marker 3") —
    // a marker is identified by its id and its position, and a name collision is the user's to
    // resolve by renaming, not something to paper over with a hunt for a free number.
    const juce::String name = "Marker " + juce::String((int)doc_->getMarkers().size() + 1);

    synth::MarkerId newId;
    auto mutate = [this, beat, name, &newId] { newId = doc_->addMarker(beat, name, defaultMarkerColourArgb()); };
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    // The doc notification already repainted the ruler (timelineChanged) — nothing else to do.
    return newId;
}

juce::uint32 TimelinePanelComponent::defaultMarkerColourArgb() const {
    // A theme token, not a literal, so a marker lands in the palette the rest of the panel draws
    // in. `warning` is the amber family — deliberately NOT `accent`, which is what the loop brace
    // and the playhead already use in this same 24 px strip.
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getTheme().colors.warning.getARGB();
    return synth::Marker{}.colourArgb; // headless: the model's own amber default
}

// Used to POPULATE the menu (buildAddTrackMenu(), which also snapshots the result into
// instrumentPluginMenuSnapshot_) and by tests inspecting what the menu would currently show. NOT
// used to resolve a click -- applyAddTrackMenuChoice reads the snapshot instead.
std::vector<synth::PluginIdentity> TimelinePanelComponent::collectInstrumentPluginMenuOptions() const {
    if (trackHeaderHost_ == nullptr)
        return {};
    return trackHeaderHost_->getInstrumentPluginOptions();
}

// Same `juce::PopupMenu::MenuItemIterator` pattern MacroPortWidgetTests.cpp/
// MacroContainerTests.cpp use elsewhere -- unlike those, no context-menu hook is needed here
// because this menu was already a pure builder call away from showMenuAsync(), nothing to
// intercept.
juce::PopupMenu TimelinePanelComponent::buildAddTrackMenu() {
    if (trackHeaderHost_ != nullptr)
        trackHeaderHost_->ensureInstrumentPluginsScanned();

    juce::PopupMenu menu;
    menu.addItem(kAddMidiTrackMenuId, "MIDI Track");
    menu.addItem(kAddAudioTrackMenuId, "Audio Track");
    // A submenu rather than three flat entries — these are all "Instrument Track",
    // differing only by which audio-producing MIDI instrument drives it.
    juce::PopupMenu instrumentMenu;
    instrumentMenu.addItem(kAddInstrumentOscillatorMenuId, "Oscillator");
    instrumentMenu.addItem(kAddInstrumentWavetableMenuId, "Wavetable");
    instrumentMenu.addItem(kAddInstrumentSamplerMenuId, "Sampler");
    // Poly variants below a separator — Sampler has no "poly" parameter, so it has
    // no poly entry.
    instrumentMenu.addSeparator();
    instrumentMenu.addItem(kAddInstrumentOscillatorPolyMenuId, "Oscillator (Poly)");
    instrumentMenu.addItem(kAddInstrumentWavetablePolyMenuId, "Wavetable (Poly)");

    // A hosted plugin as the instrument, in its own sub-submenu rather than a flat
    // "Plugin..." entry — effects are filtered out host-side (getInstrumentPluginOptions), so
    // everything listed here really is choosable.
    instrumentMenu.addSeparator();
    juce::PopupMenu pluginMenu;
    const auto pluginOptions = collectInstrumentPluginMenuOptions();
    // Snapshot for applyAddTrackMenuChoice — see instrumentPluginMenuSnapshot_'s own comment. Taken
    // here, at the exact moment the menu below is built from this same list, so a click can never
    // resolve against anything other than what was actually shown.
    instrumentPluginMenuSnapshot_ = pluginOptions;
    if (pluginOptions.empty()) {
        const bool scanning = trackHeaderHost_ != nullptr && trackHeaderHost_->isPluginScanInProgress();
        pluginMenu.addItem(kAddInstrumentPluginNoneMenuId,
                           scanning ? "Scanning for plugins..." : "No instrument plugins found",
                           /*isEnabled=*/false);
    } else {
        for (int i = 0; i < (int)pluginOptions.size(); ++i)
            pluginMenu.addItem(kAddInstrumentPluginMenuIdBase + i,
                               pluginOptions[(size_t)i].name + " (" +
                                   shortPluginFormatLabel(pluginOptions[(size_t)i].format) + ")");
    }
    instrumentMenu.addSubMenu("Plugin", pluginMenu);

    menu.addSubMenu("Instrument Track", instrumentMenu);

    // docs/mixer/track-presets.md: every saved track preset, grouped by type — independent of
    // the per-type default (Preferences -> Mixer), which only steers the two plain entries above.
    // Snapshotted at build time, same reason instrumentPluginMenuSnapshot_ is: a preset can be
    // saved/deleted between the menu opening and the click landing.
    menu.addSeparator();
    auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    // listTrackPresets returns a juce::Array; converted to std::vector here (rather than changing
    // the member type) so the rest of this file can keep using std::vector's std::size_t indexing,
    // same as instrumentPluginMenuSnapshot_ below it.
    const auto audioPresets = synth::TrackPresetManager::listTrackPresets(dir, synth::TrackPresetKind::Audio);
    audioTrackPresetMenuSnapshot_.assign(audioPresets.begin(), audioPresets.end());
    const auto instrumentPresets = synth::TrackPresetManager::listTrackPresets(dir, synth::TrackPresetKind::Instrument);
    instrumentTrackPresetMenuSnapshot_.assign(instrumentPresets.begin(), instrumentPresets.end());
    if (!audioTrackPresetMenuSnapshot_.empty()) {
        juce::PopupMenu audioPresetMenu;
        for (int i = 0; i < (int)audioTrackPresetMenuSnapshot_.size(); ++i)
            audioPresetMenu.addItem(kAddTrackPresetAudioMenuIdBase + i, audioTrackPresetMenuSnapshot_[(size_t)i].name);
        menu.addSubMenu("Audio Track from Preset", audioPresetMenu);
    }
    if (!instrumentTrackPresetMenuSnapshot_.empty()) {
        juce::PopupMenu instrumentPresetMenu;
        for (int i = 0; i < (int)instrumentTrackPresetMenuSnapshot_.size(); ++i)
            instrumentPresetMenu.addItem(kAddTrackPresetInstrumentMenuIdBase + i,
                                         instrumentTrackPresetMenuSnapshot_[(size_t)i].name);
        menu.addSubMenu("Instrument Track from Preset", instrumentPresetMenu);
    }
    // docs/mixer/track-presets.md#a-third-kind-bus: a Bus preset creates no timeline track
    // at all, just the bus chain — still listed here, grouped like the other two kinds, since
    // "+ Track" is where every saved chain (track-bearing or not) is inserted from.
    const auto busPresets = synth::TrackPresetManager::listTrackPresets(dir, synth::TrackPresetKind::Bus);
    busTrackPresetMenuSnapshot_.assign(busPresets.begin(), busPresets.end());
    if (!busTrackPresetMenuSnapshot_.empty()) {
        juce::PopupMenu busPresetMenu;
        for (int i = 0; i < (int)busTrackPresetMenuSnapshot_.size(); ++i)
            busPresetMenu.addItem(kAddTrackPresetBusMenuIdBase + i, busTrackPresetMenuSnapshot_[(size_t)i].name);
        menu.addSubMenu("Bus from Preset", busPresetMenu);
    }
    menu.addItem(kInsertTrackPresetFromFileMenuId, "Insert Track Preset from File...");

    // Separated because it is not a track at all: a marker adds no row to the header column and
    // nothing to the graph, it drops a flag on the ruler.
    menu.addSeparator();
    menu.addItem(kAddMarkerMenuId, "Add Marker");
    return menu;
}

// Protected virtual for the same display-less-runner reason as
// `TimelineRulerComponent::openMarkerContextMenu`: a real menu window needs a display to be
// positioned on, and JUCE dereferences a null one on a headless CI runner. No test reaches this
// today (they all drive `applyAddTrackMenuChoice` directly, which is the documented headless
// seam), but a test that clicked the button would crash exactly the way the marker menu did -- so
// the override point exists before someone writes that test.
void TimelinePanelComponent::openAddTrackMenu() {
    juce::PopupMenu menu = buildAddTrackMenu();

    // docs/mixer/mixer.md#creating-channels-in-an-existing-project: disabled rather than hidden when
    // every track already has a channel (or there are no tracks at all) — a hidden entry would look like the feature
    // disappeared; a disabled one still tells the user it exists and why it's greyed out.
    menu.addSeparator();
    const bool canCreateChannels = trackHeaderHost_ != nullptr && trackHeaderHost_->hasTracksNeedingChannels();
    menu.addItem(kCreateChannelsMenuId, "Create Channels", canCreateChannels);

    juce::Component::SafePointer<TimelinePanelComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addTrackButton_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyAddTrackMenuChoice(result);
    });
}

void TimelinePanelComponent::timelineChanged(const synth::TimelineDoc&) {
    // The lane pools first: the header rebuild below lays rows out with their geometry.
    automationLanes_.sync();
    pushAutomationGeometry();
    syncTrackHeaders();
    refreshRoutingPane(); // a binding, name or colour change shows in the routing pane too
    clipLaneArea_.refreshFromDoc();
    // The ruler's marker flags come straight off the doc, so a mutation is the ONLY thing that can
    // move them — this is the repaint that replaces polling them (see TimelineRulerComponent). The
    // lanes rect goes with it, because this component paints each marker's stem down through the
    // clips (see paint()); bounded to that rect rather than the whole panel.
    ruler_.repaint();
    if (!gridLanesBounds_.isEmpty())
        repaint(gridLanesBounds_);
    // If the roll is open on a clip this mutation just removed, refreshFromDoc() closes it
    // itself and fires onCloseRequested -> closePianoRoll() (wired in the constructor), which is
    // what swaps clipLaneArea_ back into view.
    pianoRoll_.refreshFromDoc();

    // Fold arrows, row positions and editor positions all follow the lanes' new state.
    if (doc_ == nullptr || doc_->getLane(selectedAutomationLane_) == nullptr)
        selectedAutomationLane_ = {};
    layoutAutomationRows();
}

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

    // Preserve WHICH TRACK is focused across the rebuild (by id, never by index — the whole
    // point of resolving by id is that a track deleted ABOVE the focused one must not silently hand
    // focus to whatever track now sits at the old numeric index). Invalid (default-constructed) when
    // nothing was focused, and the loop below never matches an invalid id against a real track.
    const synth::TrackId previouslyFocusedTrackId =
        juce::isPositiveAndBelow(focusedTrackIndex_, trackHeaderList_.headers.size())
            ? trackHeaderList_.headers.getUnchecked(focusedTrackIndex_)->getTrackId()
            : synth::TrackId();

    trackHeaderList_.headers.clear();
    focusedTrackIndex_ = -1;
    // A drag's OWN commit (commitTrackDrag) sets committingTrackDrag_ and finishes the gesture
    // itself after this rebuild. Any OTHER rebuild (an AI patch apply, an undo) landing mid-drag
    // would orphan the lifted row and the animator against rows that no longer exist, and nothing
    // would fire onRowDragEnded for them, so the gesture is discarded.
    if (!committingTrackDrag_)
        discardTrackDrag();
    for (const auto& track : tracks) {
        auto* header =
            trackHeaderList_.headers.add(new TimelineTrackHeaderComponent(*doc_, track.id, trackHeaderHost_));
        header->setShortcutManager(shortcuts_);
        // The header only reports a fold-arrow press; this panel owns the fold state.
        const auto trackId = track.id;
        header->onAutomationToggleRequested = [this, trackId](synth::TrackId) { toggleAutomationForTrack(trackId); };
        // The header menu's "Add automation...": the picker opens anchored on the header row.
        header->onAddAutomationRequested = [this, header](synth::TrackId track) {
            openAddAutomationPicker(track, *header);
        };
        // Click-to-select and Up/Down between rows — see the two callbacks' own doc comments
        // in TimelineTrackHeaderComponent.h for why these are explicit callbacks rather than a real
        // focusGained() round trip.
        header->onSelectRequested = [this, trackId] { setFocusedTrack(trackId); };
        header->onFocusMoveRequested = [this](int direction) { moveFocusedTrack(direction); };
        header->onEnterClipsRequested = [this, trackId] { return enterTrackClips(trackId); };
        // Whole-row drag-to-reorder — see TimelineTrackHeaderComponent::onRowDragStarted's
        // own comment for the division of labour (the row detects the gesture, this panel resolves
        // screen Y against the ordered header list). The row hands us raw screen Y rather than
        // computing an insertion index itself because it doesn't know where its siblings are;
        // trackHeaderList_.headers is the ordered list and this panel is the one place that owns it.
        header->onRowPressed = [this, trackId](int screenY) { beginTrackDrag(trackId, screenY); };
        header->onRowDragStarted = [this](int screenY) { updateTrackDrag(screenY); };
        header->onRowDragged = [this](int screenY) { updateTrackDrag(screenY); };
        header->onRowDragEnded = [this](int) { endTrackDrag(); };
        if (trackId == previouslyFocusedTrackId)
            focusedTrackIndex_ = trackHeaderList_.headers.size() - 1;
        trackHeaderList_.addAndMakeVisible(header);
    }
    layoutTrackHeaders();
}

void TimelinePanelComponent::layoutTrackHeaders() {
    // Row geometry comes from rowLayout() — the SAME model synth::ui::TimelineClipLaneArea paints
    // and hit-tests with, so header rows and clip rows never drift apart.
    const auto layout = rowLayout();

    const int count = trackHeaderList_.headers.size();
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());

    // The header viewport is taller than the lanes region (the ruler is taller than the "+ Track"
    // strip), so the list carries that difference as slack below the last row: without it the
    // viewport clamps the shared scroll short of maxTrackScrollPx() and the last lane row can never
    // be scrolled fully into the lanes.
    const int viewHeight = trackHeaderViewport_.getMaximumVisibleHeight();
    const int slack = std::max(0, viewHeight - gridLanesBounds_.getHeight());
    trackHeaderList_.setSize(width, std::max(layout.trackTop(count) + slack, viewHeight));
    placeTrackHeaders();
}

// The row itself is the real focus target (TimelineTrackHeaderComponent::setWantsKeyboardFocus);
// focusedTrackIndex_ exists so Up/Down and ensureTrackVisible() below have somewhere to read
// "where am I" without walking the component tree asking each row whether it
// hasKeyboardFocus(true) (which is also unreliable headlessly with no native peer).
void TimelinePanelComponent::setFocusedTrack(synth::TrackId id) {
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        if (trackHeaderList_.headers.getUnchecked(i)->getTrackId() == id) {
            focusedTrackIndex_ = i;
            refreshRoutingPane();
            return;
        }
    }
    focusedTrackIndex_ = -1; // id no longer resolves (deleted between the click and this call)
    refreshRoutingPane();
}

// Nothing focused yet starts at row 0 either direction (there is no "current position" for a
// relative step to be relative TO); otherwise CLAMPS at the ends rather than wrapping, matching
// cycleSnapValue's own "a held key parks at the end" rule.
void TimelinePanelComponent::moveFocusedTrack(int direction) {
    const int count = trackHeaderList_.headers.size();
    if (count == 0)
        return;
    focusedTrackIndex_ = focusedTrackIndex_ < 0 ? 0 : juce::jlimit(0, count - 1, focusedTrackIndex_ + direction);
    // Best-effort: without a native peer (headless tests) this is a harmless no-op, same as every
    // other grabKeyboardFocus() call in this codebase (see TimelineClipLaneArea's own mouseDown).
    trackHeaderList_.headers.getUnchecked(focusedTrackIndex_)->grabKeyboardFocus();
    ensureTrackVisible(focusedTrackIndex_);
    refreshRoutingPane();
}

// Moves track-header focus one row `direction` (-1 up, +1 down) -- the same step the header's
// own Up/Down keys take, so M/S/R then act on that track. Clamps at the ends. Returns false when the
// timeline has no tracks.
bool TimelinePanelComponent::selectAdjacentTrack(int direction) {
    if (trackHeaderList_.headers.isEmpty())
        return false;
    moveFocusedTrack(direction);
    return true;
}

// Computed against viewState_.trackScrollY + trackHeaderViewport_.getMaximumVisibleHeight() rather
// than trackHeaderViewport_.getViewArea() -- the latter is a cached snapshot (lastVisibleArea) that
// is only correct after a layout round trip and reads zero-height before the panel has ever been
// sized, where trackScrollY is the one value every other scroll/zoom writer in this class already
// treats as ground truth (see syncTrackScroll()).
void TimelinePanelComponent::ensureTrackVisible(int index) {
    if (!juce::isPositiveAndBelow(index, trackHeaderList_.headers.size()))
        return;
    const auto span = rowLayout().trackSpan(index);
    const int rowTop = span.getStart();
    const int rowBottom = span.getEnd();
    const int viewTop = (int)std::llround(viewState_.trackScrollY);
    const int viewHeight = trackHeaderViewport_.getMaximumVisibleHeight();
    if (rowTop < viewTop)
        scrollTrackRows((double)(rowTop - viewTop));
    else if (rowBottom > viewTop + viewHeight)
        scrollTrackRows((double)(rowBottom - (viewTop + viewHeight)));
}

} // namespace synth::ui
