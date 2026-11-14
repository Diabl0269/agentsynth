#include "Music/SongKey.h"
#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "UI/Timeline/FollowPlayhead.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripSettings.h"

// Concern: timeline/piano-roll editing preferences — loop-selection-arms, double-click-spans-
// locators, natural scrolling, wheel-zoom direction, piano-roll key-label density, and
// whether a hovered key in a key picker plays its scale.

bool PreferencesSettingsTab::isLoopSelectionArmsEnabled() const { return loopSelectionArmsToggle.getToggleState(); }

void PreferencesSettingsTab::setLoopSelectionArmsEnabled(bool enabled) {
    loopSelectionArmsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistLoopSelectionArms(enabled);
}

bool PreferencesSettingsTab::isDoubleClickSpansLocatorsEnabled() const {
    return doubleClickSpansLocatorsToggle.getToggleState();
}

void PreferencesSettingsTab::setDoubleClickSpansLocatorsEnabled(bool enabled) {
    doubleClickSpansLocatorsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistDoubleClickSpansLocators(enabled);
}

void PreferencesSettingsTab::persistDoubleClickSpansLocators(bool enabled) {
    // Nothing live to push: TimelineClipLaneArea reads this key at use time (on the next
    // double-click), the same way the row above it is read by the timeline's P handler.
    appProperties.getUserSettings()->setValue(kTimelineDoubleClickSpansLocatorsKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

bool PreferencesSettingsTab::isShowSectionsStripEnabled() const { return showSectionsStripToggle.getToggleState(); }

void PreferencesSettingsTab::setShowSectionsStripEnabled(bool enabled) {
    showSectionsStripToggle.setToggleState(enabled, juce::dontSendNotification);
    persistSectionsSetting(synth::ui::kTimelineShowSectionsStripKey, enabled);
}

bool PreferencesSettingsTab::isSectionsCoverOnDropEnabled() const { return sectionsDropCombo.getSelectedId() == 2; }

void PreferencesSettingsTab::setSectionsCoverOnDropEnabled(bool enabled) {
    sectionsDropCombo.setSelectedId(enabled ? 2 : 1, juce::dontSendNotification);
    persistSectionsSetting(synth::ui::kTimelineSectionsCoverOnDropKey, enabled);
}

void PreferencesSettingsTab::persistSectionsSetting(const char* key, bool enabled) {
    // The strip's visibility is pushed live by MainComponent's settings listener (applyTimelineTransportPreferences);
    // the cover-or-make-room choice is read at use time, on the next place or move.
    appProperties.getUserSettings()->setValue(key, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

bool PreferencesSettingsTab::isAskBeforeRemovingLfoEnabled() const {
    return askBeforeRemovingLfoToggle.getToggleState();
}

void PreferencesSettingsTab::setAskBeforeRemovingLfoEnabled(bool enabled) {
    askBeforeRemovingLfoToggle.setToggleState(enabled, juce::dontSendNotification);
    persistAskBeforeRemovingLfo(enabled);
}

bool PreferencesSettingsTab::isAskBeforeDeletingTrackEnabled() const {
    return askBeforeDeletingTrackToggle.getToggleState();
}

void PreferencesSettingsTab::setAskBeforeDeletingTrackEnabled(bool enabled) {
    askBeforeDeletingTrackToggle.setToggleState(enabled, juce::dontSendNotification);
    persistAskBeforeDeletingTrack(enabled);
}

// The toggles after the loop-selection one: double-click-spans-locators and the LFO-removal question, each
// DEFAULT TRUE like the rows around them and read at use time (TimelineClipLaneArea, MainComponent::removeModulator).
PreferencesSettingsTab::~PreferencesSettingsTab() {
    if (auto* settings = appProperties.getUserSettings())
        settings->removeChangeListener(this);
}

// The settings file broadcasts every write: the key menus' play-scale row writes the key this checkbox shows, so the
// checkbox re-reads it here instead of waiting for the tab to be rebuilt.
void PreferencesSettingsTab::changeListenerCallback(juce::ChangeBroadcaster*) {
    playScaleOnBrowseToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(synth::kPlayScaleOnBrowseSettingsKey, true),
        juce::dontSendNotification);
}

void PreferencesSettingsTab::initTimelineEditingToggles() {
    contentHost.addAndMakeVisible(doubleClickSpansLocatorsToggle);
    // DEFAULT TRUE, same idiom as the rows above.
    doubleClickSpansLocatorsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kTimelineDoubleClickSpansLocatorsKey, true),
        juce::dontSendNotification);
    doubleClickSpansLocatorsToggle.setTooltip(
        "When on (the default), double-clicking empty lane space inside the loop locators creates a clip spanning "
        "them. Outside the locators - or with no locators set - you still get a one-bar clip. Turn it off to always "
        "get one bar.");
    doubleClickSpansLocatorsToggle.onClick = [this] {
        persistDoubleClickSpansLocators(doubleClickSpansLocatorsToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(showSectionsStripToggle);
    showSectionsStripToggle.setToggleState(synth::ui::sectionsStripShown(appProperties.getUserSettings()),
                                           juce::dontSendNotification);
    showSectionsStripToggle.setTooltip(
        "When on (the default), the Sections strip sits above the timeline ruler: drag across bars to make a section, "
        "then place it again anywhere. Turn it off to hide the strip; your sections stay in the song and play as "
        "before.");
    showSectionsStripToggle.onClick = [this] {
        persistSectionsSetting(synth::ui::kTimelineShowSectionsStripKey, showSectionsStripToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(sectionsDropLabel);
    sectionsDropLabel.setText("Timeline: dropping a section on other music", juce::dontSendNotification);
    sectionsDropLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(sectionsDropCombo);
    sectionsDropCombo.setTitle(sectionsDropLabel.getText());
    sectionsDropCombo.addItem("Pushes the music later", 1);
    sectionsDropCombo.addItem("Covers it", 2);
    sectionsDropCombo.setTooltip(
        "Pushes the music later (the default): the section goes in between and everything after it moves later. Covers "
        "it: the section replaces the music where it lands. Hold Shift while you drop to do the other one.");
    sectionsDropCombo.setSelectedId(synth::ui::sectionsCoverOnDrop(appProperties.getUserSettings()) ? 2 : 1,
                                    juce::dontSendNotification);
    sectionsDropCombo.onChange = [this] {
        persistSectionsSetting(synth::ui::kTimelineSectionsCoverOnDropKey, sectionsDropCombo.getSelectedId() == 2);
    };

    contentHost.addAndMakeVisible(sectionsDropHint);
    sectionsDropHint.setText("Hold Shift while you drop to do the other one, that time only.",
                             juce::dontSendNotification);
    styleMutedHintLabel(sectionsDropHint);

    contentHost.addAndMakeVisible(askBeforeRemovingLfoToggle);
    askBeforeRemovingLfoToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kTimelineAskBeforeRemovingLfoKey, true),
        juce::dontSendNotification);
    askBeforeRemovingLfoToggle.setTooltip(
        "When on (the default), removing the last modulator an LFO has asks first: remove the LFO too, or keep the "
        "module. Ticking Remember my choice in that window turns this off and keeps the answer; turn it back on here "
        "to forget the answer and be asked again. Cmd+Z / Ctrl+Z always brings a removal back. An LFO that moves "
        "other things never asks.");
    askBeforeRemovingLfoToggle.onClick = [this] {
        persistAskBeforeRemovingLfo(askBeforeRemovingLfoToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(askBeforeDeletingTrackToggle);
    askBeforeDeletingTrackToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kTimelineAskBeforeDeletingTrackKey, true),
        juce::dontSendNotification);
    askBeforeDeletingTrackToggle.setTooltip(
        "When on (the default), pressing Cmd+Backspace / Ctrl+Backspace on a focused track, or deleting a macro a "
        "track plays (Delete Macro, or Delete / Backspace on the selected macro), asks first. Turn it off to delete "
        "straight away; Cmd+Z / Ctrl+Z still brings everything back. For a track's macro, turning the question off "
        "from its window keeps the answer you gave (Remove track or Keep track); turning it back on here forgets "
        "it. The track menu's Delete Track never asks.");
    askBeforeDeletingTrackToggle.onClick = [this] {
        persistAskBeforeDeletingTrack(askBeforeDeletingTrackToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(recordStartsPlaybackToggle);
    recordStartsPlaybackToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kRecordStartsPlaybackKey, true), juce::dontSendNotification);
    recordStartsPlaybackToggle.setTooltip(
        "When on (the default), Record also starts playback, as in most DAWs. Turn it off to have Record only "
        "engage recording; the take then starts when you press Play.");
    recordStartsPlaybackToggle.onClick = [this] {
        persistRecordStartsPlayback(recordStartsPlaybackToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(playScaleOnBrowseToggle);
    playScaleOnBrowseToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(synth::kPlayScaleOnBrowseSettingsKey, true),
        juce::dontSendNotification);
    playScaleOnBrowseToggle.setTooltip(
        "When on (the default), hovering a key in a key picker (the transport bar's Song key, the piano roll's Key, "
        "a sequencer's Key) plays its scale. Turn it off to browse in silence; picking a key still works.");
    playScaleOnBrowseToggle.onClick = [this] { persistPlayScaleOnBrowse(playScaleOnBrowseToggle.getToggleState()); };
    appProperties.getUserSettings()->addChangeListener(this);

    contentHost.addAndMakeVisible(followPointLabel);
    followPointLabel.setText("Follow playhead point:", juce::dontSendNotification);
    followPointLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    contentHost.addAndMakeVisible(followPointEditor);
    followPointEditor.setTitle("Follow playhead point, percent of the visible width");
    followPointEditor.setMultiLine(false);
    followPointEditor.setReturnKeyStartsNewLine(false);
    followPointEditor.setSelectAllWhenFocused(true);
    followPointEditor.setJustification(juce::Justification::centred);
    followPointEditor.setInputRestrictions(2, "0123456789");
    followPointEditor.setTooltip("In Smooth follow mode the playhead stays at this point of the visible timeline "
                                 "while the view scrolls. Any value from 10 to 90 percent; applies at once.");
    followPointEditor.setText(
        juce::String(synth::ui::clampFollowPointPercent(appProperties.getUserSettings()->getIntValue(
            synth::ui::kFollowPointPercentKey, synth::ui::kDefaultFollowPointPercent))),
        juce::dontSendNotification);
    // Commit on Return and on focus-lost; an empty or out-of-range entry snaps visibly to a valid value.
    auto commitFollowPoint = [this] { setFollowPointPercent(followPointEditor.getText().getIntValue()); };
    followPointEditor.onReturnKey = commitFollowPoint;
    followPointEditor.onFocusLost = commitFollowPoint;
    contentHost.addAndMakeVisible(followPointUnitLabel);
    followPointUnitLabel.setText("% of the view", juce::dontSendNotification);
    followPointUnitLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    setupDuplicatePointsControls(); // Chained here, the constructor is baselined
}

bool PreferencesSettingsTab::isRecordStartsPlaybackEnabled() const {
    return recordStartsPlaybackToggle.getToggleState();
}

void PreferencesSettingsTab::setRecordStartsPlaybackEnabled(bool enabled) {
    recordStartsPlaybackToggle.setToggleState(enabled, juce::dontSendNotification);
    persistRecordStartsPlayback(enabled);
}

void PreferencesSettingsTab::persistRecordStartsPlayback(bool enabled) {
    // The key is read at use time by the next Record press; MainComponent's settings listener refreshes the tooltip.
    appProperties.getUserSettings()->setValue(kRecordStartsPlaybackKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

int PreferencesSettingsTab::getFollowPointPercent() const { return followPointEditor.getText().getIntValue(); }

void PreferencesSettingsTab::setFollowPointPercent(int percent) {
    const int clamped = synth::ui::clampFollowPointPercent(percent);
    followPointEditor.setText(juce::String(clamped), juce::dontSendNotification);
    persistFollowPointPercent(clamped);
}

void PreferencesSettingsTab::persistFollowPointPercent(int percent) {
    // MainComponent's settings listener pushes the value into the timeline and the piano roll, so it applies live.
    appProperties.getUserSettings()->setValue(synth::ui::kFollowPointPercentKey, percent);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistAskBeforeDeletingTrack(bool enabled) {
    // Nothing live to push: MainComponent::deleteTrackAfterConfirm and deleteMacrosOfTracks read this key at use time.
    // Asking again forgets the answers "Remember my choice" stored for a track's macro and for a track's delete.
    appProperties.getUserSettings()->setValue(kTimelineAskBeforeDeletingTrackKey, enabled ? "1" : "0");
    if (enabled) {
        appProperties.getUserSettings()->removeValue(kTimelineDeleteMacroChoiceKey);
        appProperties.getUserSettings()->removeValue(kTimelineDeleteTrackMacroChoiceKey);
    }
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistAskBeforeRemovingLfo(bool enabled) {
    // Nothing live to push: MainComponent::removeModulator reads this key at use time (on the next removal).
    // Asking again forgets the remembered answer, so turning the question off by hand starts from "remove".
    appProperties.getUserSettings()->setValue(kTimelineAskBeforeRemovingLfoKey, enabled ? "1" : "0");
    if (enabled)
        appProperties.getUserSettings()->removeValue(kTimelineRemoveLastSourceChoiceKey);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistLoopSelectionArms(bool enabled) {
    // Read at use time by TimelinePanelComponent's P handler and MainComponent's
    // onLoopRangeRequested — nothing live to push here, unlike the GraphEditor settings above.
    appProperties.getUserSettings()->setValue("timelineLoopSelectionArms", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

bool PreferencesSettingsTab::isNaturalScrollingEnabled() const { return naturalScrollingToggle.getToggleState(); }

void PreferencesSettingsTab::setNaturalScrollingEnabled(bool enabled) {
    naturalScrollingToggle.setToggleState(enabled, juce::dontSendNotification);
    persistNaturalScrolling(enabled);
}

void PreferencesSettingsTab::persistNaturalScrolling(bool enabled) {
    appProperties.getUserSettings()->setValue(kNaturalScrollingKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here, unlike the GraphEditor settings below: juce::PropertiesFile is a
    // ChangeBroadcaster, and MainComponent listens to it precisely so a scroll-direction change
    // reaches the timeline and the piano roll without this tab having to know they exist (see
    // MainComponent::applyNaturalScrollingPreference). That is also why there is no
    // onNaturalScrollingToggled callback for SettingsWindow to wire — one constructor argument per
    // preference does not scale.
}

bool PreferencesSettingsTab::isZoomScrollUpZoomsInEnabled() const { return zoomScrollUpZoomsInToggle.getToggleState(); }

void PreferencesSettingsTab::setZoomScrollUpZoomsInEnabled(bool enabled) {
    zoomScrollUpZoomsInToggle.setToggleState(enabled, juce::dontSendNotification);
    persistZoomScrollUpZoomsIn(enabled);
}

void PreferencesSettingsTab::persistZoomScrollUpZoomsIn(bool enabled) {
    appProperties.getUserSettings()->setValue(kZoomScrollUpZoomsInKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here either: the SAME juce::PropertiesFile ChangeBroadcaster path
    // persistNaturalScrolling documents above carries it, landing in
    // MainComponent::applyZoomScrollPreference.
}

bool PreferencesSettingsTab::isPianoRollKeyLabelModeAll() const { return pianoRollKeyLabelsToggle.getToggleState(); }

void PreferencesSettingsTab::setPianoRollKeyLabelModeAll(bool labelEveryKey) {
    pianoRollKeyLabelsToggle.setToggleState(labelEveryKey, juce::dontSendNotification);
    persistPianoRollKeyLabelMode(labelEveryKey);
}

void PreferencesSettingsTab::persistPianoRollKeyLabelMode(bool labelEveryKey) {
    appProperties.getUserSettings()->setValue(kPianoRollKeyLabelsKey, labelEveryKey ? "all" : "c");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here: TimelinePanelComponent::reloadPianoRollAppearancePrefs() reads this
    // key directly, the same "no onXToggled callback" reasoning persistNaturalScrolling documents
    // — MainComponent wires the live re-push in a parallel task.
}

bool PreferencesSettingsTab::isPlayScaleOnBrowseEnabled() const { return playScaleOnBrowseToggle.getToggleState(); }

void PreferencesSettingsTab::setPlayScaleOnBrowseEnabled(bool enabled) {
    playScaleOnBrowseToggle.setToggleState(enabled, juce::dontSendNotification);
    persistPlayScaleOnBrowse(enabled);
}

void PreferencesSettingsTab::persistPlayScaleOnBrowse(bool enabled) {
    // Nothing live to push: KeyAuditioner reads this key when each scale starts.
    appProperties.getUserSettings()->setValue(synth::kPlayScaleOnBrowseSettingsKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::styleMutedHintLabel(juce::Label& hint) {
    hint.setFont(juce::Font(juce::FontOptions(11.5f)));
    hint.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
    // Wrap instead of squeeze: the default minimum-horizontal-scale (~0.7) lets drawFittedText
    // cram an over-wide single line into the box by shrinking it horizontally, which is exactly
    // the "cramped/narrow" look this was fixed for. With kHintHeight giving room for two lines,
    // there is never a reason to squeeze instead of wrapping.
    hint.setMinimumHorizontalScale(1.0f);
    hint.setJustificationType(juce::Justification::topLeft);
}

// Lays out the Timeline category's rows (loop locators, wheel direction, piano roll key labels).
void PreferencesSettingsTab::layoutTimelineGroups(int& y, int contentWidth, bool& pendingDivider,
                                                  const GroupMatchFn& groupMatches, const SetVisibleFn& setGroupVisible,
                                                  const BeginGroupFn& beginGroup) {
    enterCategory(Category::Timeline, y);
    // Group 5: the two loop-locator toggles and the LFO-removal and track-delete questions (no divider between them).
    {
        const std::initializer_list<juce::Component*> rows{&loopSelectionArmsToggle,    &doubleClickSpansLocatorsToggle,
                                                           &showSectionsStripToggle,    &sectionsDropLabel,
                                                           &sectionsDropCombo,          &sectionsDropHint,
                                                           &askBeforeRemovingLfoToggle, &askBeforeDeletingTrackToggle};
        const bool visible = groupMatches(rows);
        setGroupVisible(rows, visible);
        beginGroup(visible);
        if (visible) {
            loopSelectionArmsToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            doubleClickSpansLocatorsToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            showSectionsStripToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            juce::Rectangle<int> dropRow(0, y, contentWidth, 24);
            sectionsDropLabel.setBounds(dropRow.removeFromLeft(270));
            sectionsDropCombo.setBounds(dropRow.removeFromLeft(200));
            y += 24;
            sectionsDropHint.setBounds({0, y, contentWidth, kHintHeight});
            y += kHintHeight + 2;
            askBeforeRemovingLfoToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            askBeforeDeletingTrackToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 5b: how record and follow-playhead behave while the transport runs.
    {
        const std::initializer_list<juce::Component*> rows{&recordStartsPlaybackToggle, &followPointLabel,
                                                           &followPointEditor, &followPointUnitLabel};
        const bool visible = groupMatches(rows);
        setGroupVisible(rows, visible);
        beginGroup(visible);
        if (visible) {
            recordStartsPlaybackToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            juce::Rectangle<int> row(0, y, contentWidth, 24);
            followPointLabel.setBounds(row.removeFromLeft(150));
            row.removeFromLeft(4);
            followPointEditor.setBounds(row.removeFromLeft(36));
            row.removeFromLeft(4);
            followPointUnitLabel.setBounds(row.removeFromLeft(100));
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 6: the two wheel-direction toggles + their hints.
    {
        const bool visible = groupMatches(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint});
        setGroupVisible(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint},
            visible);
        beginGroup(visible);
        if (visible) {
            naturalScrollingToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
            // Indented under the toggle it explains, so the hint reads as a caption.
            naturalScrollingHint.setBounds({24, y, contentWidth - 24, kHintHeight});
            y += kHintHeight + 10;
            zoomScrollUpZoomsInToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
            zoomScrollUpZoomsInHint.setBounds({24, y, contentWidth - 24, kHintHeight});
            y += kHintHeight;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 7: piano roll key labels, and the scale the key pickers play
    {
        const std::initializer_list<juce::Component*> rows{&pianoRollKeyLabelsToggle, &playScaleOnBrowseToggle};
        const bool visible = groupMatches(rows);
        setGroupVisible(rows, visible);
        beginGroup(visible);
        if (visible) {
            pianoRollKeyLabelsToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            playScaleOnBrowseToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 8: where Cmd+D puts the copies of selected points.
    {
        const std::initializer_list<juce::Component*> rows{&duplicatePointsLabel, &duplicatePointsCombo};
        const bool visible = groupMatches(rows);
        setGroupVisible(rows, visible);
        beginGroup(visible);
        if (visible) {
            juce::Rectangle<int> row(0, y, contentWidth, 24);
            duplicatePointsLabel.setBounds(row.removeFromLeft(150));
            row.removeFromLeft(4);
            duplicatePointsCombo.setBounds(row.removeFromLeft(280));
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }
}
