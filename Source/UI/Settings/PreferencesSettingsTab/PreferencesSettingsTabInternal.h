#pragma once

// Shared between the split PreferencesSettingsTab*.cpp units: persistence keys the constructor
// (Lifecycle.cpp) writes into controls and the matching getter/setter/persist units read back, plus
// the combo-id <-> enum helpers the constructor and the getter/setter units both need.

#include "Mixer/TrackPresetManager.h"
#include "PreferencesSettingsTab.h"

namespace {

// Founder-review fix F5 (docs/macros/auto-ports.md). Duplicated from the constexpr
// GraphEditor::requestGroupSelectionIntoMacro() writes through propertiesFile_ directly for the
// "remember my choice" case (that modal can fire before this tab, or any Settings window, has
// ever been constructed) — the same "one-line string not worth a header dependency" reasoning
// kNaturalScrollingKey below documents, and the two writers MUST agree on the string values below.
// DEFAULT "ask" (Unset): a silent default of either auto-creating or dropping cables would change
// existing behaviour with no warning the first time this ships.
constexpr const char* kMacroAutoPortPreferenceKey = "macroAutoCreatePorts";

// Settings key for the scroll-direction preference. Duplicated rather than shared with
// MainComponent::kNaturalScrollingKey (which is what READS it) for the same reason
// "timelineLoopSelectionArms" is duplicated between here and TimelinePanelComponent: a one-line
// string constant is not worth a header dependency from a settings tab onto MainComponent. DEFAULT
// TRUE — natural is what every scrolling surface in the app already does, so an install that never
// opens this tab is unaffected.
constexpr const char* kNaturalScrollingKey = "naturalScrolling";

// The wheel-ZOOM direction preference, duplicated from MainComponent::kZoomScrollUpZoomsInKey for
// exactly the reason kNaturalScrollingKey above is. DEFAULT TRUE — "up zooms in" is what both
// wheel-zoom surfaces did before this preference existed, so an install that never opens this tab is
// unaffected.
constexpr const char* kZoomScrollUpZoomsInKey = "zoomScrollUpZoomsIn";

// The piano roll's key-label density (PianoRollComponent::KeyLabelMode), read by
// TimelinePanelComponent::reloadPianoRollAppearancePrefs(). "all" is the default and matches the
// roll's own KeyLabelMode default, so an install that never opens this tab is unaffected.
constexpr const char* kPianoRollKeyLabelsKey = "pianoRollKeyLabels";

// Read at use time by TimelineClipLaneArea::locatorSpanForDoubleClick, and duplicated here for the
// same reason kNaturalScrollingKey above is. DEFAULT TRUE: authoring a clip that fills the loop you
// just set is the whole point of the feature, so it ships on and the toggle exists to turn it OFF
// for anyone who wants the plain one-bar clip back.
constexpr const char* kTimelineDoubleClickSpansLocatorsKey = "timelineDoubleClickSpansLocators";

// Autosave (P8-4). Read at use time by MainComponent::maybeAutosave every timerCallback() tick,
// duplicated here for the same reason kNaturalScrollingKey above is. DEFAULT ON at 2 minutes:
// autosave is a safety net, not an opt-in, so an install that never opens this tab still gets it.
constexpr const char* kAutosaveEnabledKey = "autosaveEnabled";
constexpr const char* kAutosaveIntervalMinutesKey = "autosaveIntervalMinutes";
constexpr int kDefaultAutosaveIntervalMinutes = 2;
// Cubase-style rotating backup history — see ProjectBundle::saveAutosave. Duplicated from
// MainComponent's own copy of this key/default for the same "one-line string not worth a header
// dependency" reason as kAutosaveEnabledKey above.
constexpr const char* kAutosaveBackupCountKey = "autosaveBackupCount";
constexpr int kDefaultAutosaveBackupCount = 5;

// FRO13 (P9-7, docs/mixer/track-presets.md#saving-and-setting-a-default): the per-type default track preset. Value is the preset's
// sanitised file NAME (no extension), or absent/empty = "use the factory chain". Read at use time
// by MainComponent::addAudioTrack/addInstrumentTrack, duplicated here for the same "one-line
// string not worth a header dependency" reason as kAutosaveEnabledKey above.
constexpr const char* kMixerDefaultTrackPresetAudioKey = "mixerDefaultTrackPresetAudio";
constexpr const char* kMixerDefaultTrackPresetInstrumentKey = "mixerDefaultTrackPresetInstrument";
// juce::ComboBox reserves id 0 for "nothing selected", so the "Factory Default" sentinel row (which
// must itself be selectable) takes id 1; every listed preset starts at id 2.
constexpr int kMixerDefaultPresetFactoryComboId = 1;
constexpr int kMixerDefaultPresetComboIdBase = 2;

// FRO12 (P9-6, docs/mixer/panel.md): where the Mixer panel lives. Value is "tab"/"ownPanel"/
// "window", default "tab" (D4 = A, made configurable) -- read at use time by
// MixerPlacementController, duplicated here for the same "one-line string not worth a header
// dependency" reason as kAutosaveEnabledKey above.
constexpr const char* kMixerPlacementKey = "mixerPlacement";
constexpr int kMixerPlacementTabComboId = 1;
constexpr int kMixerPlacementOwnPanelComboId = 2;
constexpr int kMixerPlacementWindowComboId = 3;

} // namespace

// Combo-id <-> enum helpers, defined in PreferencesSettingsTabLifecycle.cpp (where the constructor
// first needs them) and called from the getter/setter/persist units too. Deliberately NOT in the
// anonymous namespace above: an unnamed namespace is uniqued per translation unit, so a declaration
// inside one here could never actually resolve to the single definition living in another .cpp.
extern int comboIdFromMode(GraphEditor::SmartConnectionMode mode);
extern GraphEditor::SmartConnectionMode modeFromComboId(int id);
extern int comboIdFromMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref);
extern GraphEditor::MacroAutoPortPreference macroAutoPortPreferenceFromComboId(int id);
extern GraphEditor::MacroAutoPortPreference macroAutoPortPreferenceFromString(const juce::String& s);
// FRO13 (P9-7), defined in PreferencesSettingsTabMixerDefaults.cpp, called from the constructor.
extern void populateMixerDefaultPresetCombo(juce::ComboBox& combo, synth::TrackPresetKind kind);
