// TimelinePanelTrackHeaders.cpp
//
// Track header column: the add-track menu (instrument/plugin submenu, marker creation),
// TimelineDoc::Listener::timelineChanged, header sync/layout, focus movement and the
// header drag-to-reorder gesture. TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the
// rest of the class.

#include "TimelinePanelComponent.h"

#include "AppUndoManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// FRO42 review fix: the Instrument -> Plugin submenu ALWAYS appends the format so a VST3 and an AU
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
    // Headers are constructed with the host, so any that already exist have to be rebuilt against
    // the new one rather than refreshed.
    trackHeaderList_.headers.clear();
    syncTrackHeaders();
    // FRO14: the channel-chip meter tick. Started here rather than in the constructor -- there is
    // nothing to meter until an app host exists, and the constructor is already at its size cap.
    if (host != nullptr)
        startTimerHz(15);
    else
        stopTimer();
}

// FRO14: ONE shared 15 Hz timer for every header row's channel chip -- never one per row (up to
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
    else if (menuId >= kAddTrackPresetInstrumentMenuIdBase) {
        // FRO13: resolved against the snapshot buildAddTrackMenu() captured, same reason the
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
        // FRO42: resolved against the SNAPSHOT buildAddTrackMenu() captured when this menu was
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
    // T183 (P9-3b): a submenu rather than three flat entries — these are all "Instrument Track",
    // differing only by which audio-producing MIDI instrument drives it.
    juce::PopupMenu instrumentMenu;
    instrumentMenu.addItem(kAddInstrumentOscillatorMenuId, "Oscillator");
    instrumentMenu.addItem(kAddInstrumentWavetableMenuId, "Wavetable");
    instrumentMenu.addItem(kAddInstrumentSamplerMenuId, "Sampler");
    // FRO48 (P9-3k): poly variants below a separator — Sampler has no "poly" parameter, so it has
    // no poly entry.
    instrumentMenu.addSeparator();
    instrumentMenu.addItem(kAddInstrumentOscillatorPolyMenuId, "Oscillator (Poly)");
    instrumentMenu.addItem(kAddInstrumentWavetablePolyMenuId, "Wavetable (Poly)");

    // FRO42 (P9-3h): a hosted plugin as the instrument, in its own sub-submenu rather than a flat
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

    // FRO13 (P9-7, docs/mixer/track-presets.md): every saved track preset, grouped by type — independent of
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

    // FRO26 (P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project): disabled rather than hidden when
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
    syncTrackHeaders();
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

    // If the strip is open, re-derive it from the doc — the SAME "refresh, don't poll"
    // discipline every other timeline sub-component follows. A mutation that removed the selected
    // lane closes the strip outright (there is nothing left to show); anything else just repaints
    // the curve and re-syncs the two pickers (a lane could have been added/removed elsewhere, or
    // its recordMode could have changed from under us — AutomationRecorder's own Write-drops-to-
    // Touch-on-stop).
    if (automationStripVisible_) {
        if (doc_ == nullptr || doc_->getLane(selectedAutomationLane_) == nullptr) {
            closeAutomationStrip();
        } else {
            syncAutomationLaneCombo();
            syncAutomationRecordModeCombo();
            automationEditor_.repaint();
        }
    }
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

    // T161: preserve WHICH TRACK is focused across the rebuild (by id, never by index — the whole
    // point of resolving by id is that a track deleted ABOVE the focused one must not silently hand
    // focus to whatever track now sits at the old numeric index). Invalid (default-constructed) when
    // nothing was focused, and the loop below never matches an invalid id against a real track.
    const synth::TrackId previouslyFocusedTrackId =
        juce::isPositiveAndBelow(focusedTrackIndex_, trackHeaderList_.headers.size())
            ? trackHeaderList_.headers.getUnchecked(focusedTrackIndex_)->getTrackId()
            : synth::TrackId();

    trackHeaderList_.headers.clear();
    focusedTrackIndex_ = -1;
    // T166: a drag's OWN completion (endTrackDrag) already clears these before this rebuild ever
    // runs — this is for the other case, some UNRELATED mutation (e.g. an AI patch apply) landing
    // mid-drag: without this, a rebuild would orphan the drop indicator against a header column
    // it no longer describes, and it would paint forever (nothing left to fire onRowDragEnded).
    draggingTrackId_ = {};
    dragInsertionIndex_ = -1;
    for (const auto& track : tracks) {
        auto* header =
            trackHeaderList_.headers.add(new TimelineTrackHeaderComponent(*doc_, track.id, trackHeaderHost_));
        header->setShortcutManager(shortcuts_);
        // The header only ever reports "the A button was clicked" — this panel is the one that
        // knows whether the strip is already open on this track's lane, so it's the one that
        // decides open vs. close.
        const auto trackId = track.id;
        header->onAutomationToggleRequested = [this, trackId](synth::TrackId) { toggleAutomationForTrack(trackId); };
        // T161: click-to-select and Up/Down between rows — see the two callbacks' own doc comments
        // in TimelineTrackHeaderComponent.h for why these are explicit callbacks rather than a real
        // focusGained() round trip.
        header->onSelectRequested = [this, trackId] { setFocusedTrack(trackId); };
        header->onFocusMoveRequested = [this](int direction) { moveFocusedTrack(direction); };
        // T166: whole-row drag-to-reorder — see TimelineTrackHeaderComponent::onRowDragStarted's
        // own comment for the division of labour (the row detects the gesture, this panel resolves
        // screen Y against the ordered header list). The row hands us raw screen Y rather than
        // computing an insertion index itself because it doesn't know where its siblings are;
        // trackHeaderList_.headers is the ordered list and this panel is the one place that owns it.
        header->onRowDragStarted = [this, trackId](int screenY) { beginTrackDrag(trackId, screenY); };
        header->onRowDragged = [this](int screenY) { updateTrackDrag(screenY); };
        header->onRowDragEnded = [this](int screenY) { endTrackDrag(screenY); };
        if (trackId == previouslyFocusedTrackId)
            focusedTrackIndex_ = trackHeaderList_.headers.size() - 1;
        trackHeaderList_.addAndMakeVisible(header);
    }
    layoutTrackHeaders();
}

void TimelinePanelComponent::toggleAutomationForTrack(synth::TrackId trackId) {
    if (doc_ == nullptr)
        return;
    const auto* t = doc_->getTrack(trackId);
    if (t == nullptr || t->lanes.empty())
        return; // no-op: the button is hidden in this case anyway (see refreshFromDoc())

    // Already open on one of THIS track's lanes -> close. Anything else (closed, or open on a
    // different track) -> open this track's first lane, switching the strip if needed.
    const bool openOnThisTrack = automationStripVisible_ && doc_->getTrackForLane(selectedAutomationLane_) == t;
    if (openOnThisTrack)
        closeAutomationStrip();
    else
        showAutomationLane(t->lanes.front().id);
}

void TimelinePanelComponent::layoutTrackHeaders() {
    // Themed with a literal fallback, same pattern as resized() above — and the SAME value
    // (token x vertical-zoom scale) synth::ui::TimelineClipLaneArea reads for its own row height,
    // so header rows and clip rows never drift apart.
    const int rowHeight = currentRowHeight();

    const int count = trackHeaderList_.headers.size();
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());

    trackHeaderList_.setSize(width, std::max(count * rowHeight, trackHeaderViewport_.getMaximumVisibleHeight()));
    for (int i = 0; i < count; ++i)
        trackHeaderList_.headers.getUnchecked(i)->setBounds(0, i * rowHeight, width, rowHeight);
}

// The row itself is the real focus target (TimelineTrackHeaderComponent::setWantsKeyboardFocus);
// focusedTrackIndex_ exists so Up/Down and ensureTrackVisible() below have somewhere to read
// "where am I" without walking the component tree asking each row whether it
// hasKeyboardFocus(true) (which is also unreliable headlessly with no native peer).
void TimelinePanelComponent::setFocusedTrack(synth::TrackId id) {
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        if (trackHeaderList_.headers.getUnchecked(i)->getTrackId() == id) {
            focusedTrackIndex_ = i;
            return;
        }
    }
    focusedTrackIndex_ = -1; // id no longer resolves (deleted between the click and this call)
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
}

// Computed against viewState_.trackScrollY + trackHeaderViewport_.getMaximumVisibleHeight() rather
// than trackHeaderViewport_.getViewArea() -- the latter is a cached snapshot (lastVisibleArea) that
// is only correct after a layout round trip and reads zero-height before the panel has ever been
// sized, where trackScrollY is the one value every other scroll/zoom writer in this class already
// treats as ground truth (see syncTrackScroll()).
void TimelinePanelComponent::ensureTrackVisible(int index) {
    if (!juce::isPositiveAndBelow(index, trackHeaderList_.headers.size()))
        return;
    const int rowHeight = currentRowHeight();
    const int rowTop = index * rowHeight;
    const int rowBottom = rowTop + rowHeight;
    const int viewTop = (int)std::llround(viewState_.trackScrollY);
    const int viewHeight = trackHeaderViewport_.getMaximumVisibleHeight();
    if (rowTop < viewTop)
        scrollTrackRows((double)(rowTop - viewTop));
    else if (rowBottom > viewTop + viewHeight)
        scrollTrackRows((double)(rowBottom - (viewTop + viewHeight)));
}

int TimelinePanelComponent::trackDropBoundaryForScreenY(int screenY) const {
    const int count = trackHeaderList_.headers.size();
    if (count == 0)
        return 0;
    // getLocalPoint with a null source component treats the point as already being in SCREEN
    // coordinates (see its own JUCE doc comment) — the same "compare against something that isn't
    // this row" idiom ResizeHandle::desiredHeightFor uses, just resolved against the list instead
    // of the panel.
    const int localY = trackHeaderList_.getLocalPoint(nullptr, juce::Point<int>(0, screenY)).y;
    const int rowHeight = currentRowHeight();
    if (rowHeight <= 0)
        return 0;
    // Rounds to the NEAREST row boundary (not the row the pointer is over) so the drop indicator
    // reads as "insert here between these two rows" rather than "replace this row".
    return juce::jlimit(0, count, (localY + rowHeight / 2) / rowHeight);
}

void TimelinePanelComponent::beginTrackDrag(synth::TrackId trackId, int screenY) {
    draggingTrackId_ = trackId;
    dragInsertionIndex_ = trackDropBoundaryForScreenY(screenY);
    trackHeaderList_.repaint();
}

void TimelinePanelComponent::updateTrackDrag(int screenY) {
    if (!draggingTrackId_.isValid())
        return;
    const int newBoundary = trackDropBoundaryForScreenY(screenY);
    if (newBoundary == dragInsertionIndex_)
        return;
    dragInsertionIndex_ = newBoundary;
    trackHeaderList_.repaint();
}

void TimelinePanelComponent::endTrackDrag(int screenY) {
    if (!draggingTrackId_.isValid())
        return;

    // Read everything drag-related out of member state and reset it FIRST: performTrackEdit below
    // fires TimelineDoc::Listener::timelineChanged synchronously, which drives syncTrackHeaders(),
    // which — for an actual reorder — rebuilds trackHeaderList_.headers from scratch (see its own
    // "rebuild only when the SET of tracks changed" comment; a reorder changes the id at each
    // index, so it counts). That destroys every TimelineTrackHeaderComponent, INCLUDING the one
    // whose mouseUp is still on the call stack below this function (see
    // TimelineTrackHeaderComponent::onRowDragEnded's own ordering-hazard comment) — so this panel's
    // own state must already be consistent before that happens, not after.
    const synth::TrackId trackId = draggingTrackId_;
    const int dropBoundary = trackDropBoundaryForScreenY(screenY);
    draggingTrackId_ = {};
    dragInsertionIndex_ = -1;
    trackHeaderList_.repaint();

    if (doc_ == nullptr)
        return;
    const auto& tracks = doc_->getTracks();
    int fromIndex = -1;
    for (int i = 0; i < (int)tracks.size(); ++i) {
        if (tracks[(size_t)i].id == trackId) {
            fromIndex = i;
            break;
        }
    }
    if (fromIndex < 0)
        return; // the dragged track is gone (deleted mid-drag) — nothing to move

    // dropBoundary is "insert before row N" counted in the array WITH the dragged track still in
    // it; TimelineDoc::moveTrack wants the track's final resting index. Below its own old slot the
    // boundary already IS that index; above it, removing the track shifts everything after it down
    // by one, so the boundary overshoots by exactly one.
    const int targetIndex = dropBoundary > fromIndex ? dropBoundary - 1 : dropBoundary;

    // Same no-host fallback TimelineTrackHeaderComponent::performEdit uses — a panel driven
    // directly against a doc (no MainComponent/undo wiring) still works, e.g. every ungated
    // panel-level test in TimelinePanelTests.cpp.
    auto mutate = [this, trackId, targetIndex] { doc_->moveTrack(trackId, targetIndex); };
    if (trackHeaderHost_ != nullptr)
        trackHeaderHost_->performTrackEdit(mutate);
    else
        mutate();
}

// T166: also draws the track-reorder drop indicator, in paintOverChildren() rather than paint() --
// the header rows are children painted AFTER this component, and each fills its own bounds
// (TimelineTrackHeaderComponent::paint()'s g.fillAll(colours.surface)), so a line drawn in paint()
// would be painted over at every interior row boundary. Same trap this file already documents
// twice (TimelineTrackHeaderComponent::paintOverChildren, TimelinePanelComponent::
// paintOverChildren). Needs the owner's drag state (dragInsertionIndex_) and row height, so this
// holds a reference to the owning panel.
void TimelinePanelComponent::TrackHeaderList::paintOverChildren(juce::Graphics& g) {
    if (owner_.dragInsertionIndex_ < 0)
        return;

    const int rowHeight = owner_.currentRowHeight();
    // Clamped so the full 2px line stays visible at both the top boundary (0) and the bottom one
    // (getHeight()), rather than being clipped in half by this component's own edge.
    const int centreY = juce::jlimit(1, std::max(1, getHeight() - 1), owner_.dragInsertionIndex_ * rowHeight);

    juce::Colour line = juce::Colours::white;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        line = lf->getTheme().colors.accent;

    g.setColour(line);
    g.fillRect(0, centreY - 1, getWidth(), 2);
}

} // namespace synth::ui
