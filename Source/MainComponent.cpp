#include "MainComponent.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AIStateMapper.h"
#include "Branding.h"
#include "Modules/TimelineAudioSourceModule.h"
#include "Modules/TimelineMidiSourceModule.h" // auditionTrackNote pushes into the bound Track In node
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "Timeline/AutomationBinding.h"
#include "Timeline/TakePlacement.h"
#include "Timeline/TimelineReconciler.h"
#include "UI/PreferencesSettingsTab.h"
#include "UI/SettingsWindow.h"
#include "UI/TrackColour.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

// The file filter both patch dialogs use. A `.agsproj` project bundle only exists in a build that
// has the timeline compiled in — offering it otherwise would let a user save a "project" whose
// timeline half can never be non-empty.
constexpr const char* kPatchFileFilter = "*.json;*.agsproj";

// Where an UNSAVED project's takes go, under <app data>/<settings folder>. Also the reserved
// prefix such a take's clip assetRef carries — see chooseTakeFiles and ProjectBundle's asset policy.
constexpr const char* kRecordingsFolderName = "Recordings";

// Subdirectories a saved bundle gets its exports/patch-only snapshots written into by default (P8-5
// follow-up). Deliberately NOT reserved names on ProjectBundle: unlike Audio/Peaks they carry no
// asset-integrity contract and AssetManager::cleanUnusedAssets never looks past Audio/, so nesting
// them inside the bundle is safe - they are just a destination choice, not part of the bundle's
// asset policy.
constexpr const char* kExportsFolderName = "Exports";
constexpr const char* kPatchesFolderName = "Patches";

// The folder a "Export Audio..."/"Export Patch Only..." dialog starts in: <bundle>/<subFolderName>
// when a real bundle is open (created on demand), otherwise the same Music/AgentSynth root every
// other save/open dialog defaults to.
juce::File resolveExportSubdirectory(const juce::File& currentBundleDir, const char* subFolderName) {
    if (currentBundleDir != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir)) {
        auto dir = currentBundleDir.getChildFile(subFolderName);
        dir.createDirectory();
        return dir;
    }
    return synth::ProjectBundle::getDefaultProjectsDirectory();
}

// Upper bound on the take-number search. A folder with 10000 takes in it is a bug report, not a
// session, and an unbounded loop on a stat() call is not something a UI click should be able to do.
constexpr int kMaxTakeNumber = 10000;

// Floor on a committed audio clip's length, so a take stopped the instant it started still produces
// a clip the model accepts (addClip requires a strictly positive length). Same value and same
// reasoning as synth::MidiRecorder::kMinNoteLengthBeats.
constexpr double kMinAudioClipLengthBeats = 1.0 / 32.0;

// Autosave preferences, read directly (no cached member — the 10 Hz timerCallback() cost of a
// juce::PropertiesFile lookup is negligible, and a direct read means Preferences can never go stale
// between a settings write and the next tick). Duplicated from PreferencesSettingsTab's own key
// constants, the same "one-line string not worth a header dependency" reasoning as
// kNaturalScrollingKey there. DEFAULT ON at 2 minutes: autosave is a safety net, not an opt-in.
constexpr const char* kAutosaveEnabledKey = "autosaveEnabled";
constexpr const char* kAutosaveIntervalMinutesKey = "autosaveIntervalMinutes";
constexpr int kDefaultAutosaveIntervalMinutes = 2;
// Cubase-style rotating backup history (see ProjectBundle::saveAutosave) - how many PREVIOUS
// sidecars are kept as numbered autosave-<n>.json files alongside the live autosave.json. 0
// disables rotation (plain overwrite); clamped to [0, 50] the same way the combo/slider limits it.
constexpr const char* kAutosaveBackupCountKey = "autosaveBackupCount";
constexpr int kDefaultAutosaveBackupCount = 5;

// A clip's assetRef always names the .wav asset — chooseTakeFiles() is what establishes the
// pairing with its .agpk peaks sidecar: same stem, and either a sibling "Peaks/" directory (a saved
// bundle: "Audio/take-n.wav" <-> "Peaks/take-n.agpk") or the SAME "Recordings/" directory (an
// unsaved project, where chooseTakeFiles points audioDir and peaksDir at the same root). Returns
// the peaks SIDECAR'S ref, in the same bundle/root-relative form the streamer's own
// resolveAssetRef() understands — so that one function stays the single place a ref becomes a
// juce::File, for both the audio and the peaks half. Empty in, empty out.
juce::String peaksRefForAssetRef(const juce::String& assetRef) {
    if (assetRef.isEmpty())
        return {};

    juce::String ref = assetRef;
    const juce::String audioPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    if (ref.startsWith(audioPrefix))
        ref = juce::String(synth::ProjectBundle::kPeaksSubdirName) + "/" + ref.substring(audioPrefix.length());

    return ref.upToLastOccurrenceOf(".", false, false) + ".agpk";
}

// The add-track flow's auto-wire target set: MIDI-DRIVEN INSTRUMENTS only.
//
// juce::AudioProcessor::acceptsMidi() cannot be the rule — ModuleBase overrides it to `true` for
// EVERY module in this app, so it would match a Reverb. The module type is the rule instead, and
// the set mirrors AIStateMapper's own midiAcceptingTypes (Oscillator, Sampler, Sequencer, Poly
// Sequencer, Poly MIDI) plus Wavetable, which consumes note-ons exactly the way Oscillator does.
//
// MIDI *sources* are deliberately excluded: Track In itself, External MIDI and MIDI Keyboard
// generate notes, so wiring a new Track In into one of them is never what the user meant.
bool isMidiInstrumentNode(juce::AudioProcessor* processor) {
    auto* module = dynamic_cast<ModuleBase*>(processor);
    if (module == nullptr)
        return false;

    return isMidiInstrumentType(module->getModuleType());
}
// True when `candidate` IS `ancestor` or sits anywhere inside its component subtree. The
// "click grabs focus" idiom (GraphEditor::mouseDown and every timeline sub-component that copies
// it) means the currently-focused component is always either a surface's root component itself or
// one of its rare children, never a cousin — checking both keeps resolveEditSurface() correct even
// if a sub-widget ever grows its own focusable child.
bool isOrIsChildOf(const juce::Component* candidate, const juce::Component& ancestor) noexcept {
    return candidate != nullptr && (candidate == &ancestor || ancestor.isParentOf(candidate));
}

// Human-readable identity for a graph node — the binding chip's base label and the re-bind menu's
// starting point. Plain processor name only: appending "#id" unconditionally was itself the source
// of founder confusion (a chip reading "Track In #16" reads like a module name, not a binding), and
// the id means nothing outside a menu actually showing two same-named candidates at once.
// getAvailableTrackInNodes (below) is where that disambiguation happens, over the option list it is
// building — never here, and never on the chip.
juce::String describeNodeForBinding(juce::AudioProcessorGraph::Node* node) {
    if (node == nullptr || node->getProcessor() == nullptr)
        return {};
    return node->getProcessor()->getName();
}

// ---- Command <-> ShortcutManager action id, for the two blocks handled as case-fallthrough runs ----
//
// The ten grid commands and the four zoom commands share one getCommandInfo case each (their
// enablement rule is per BLOCK, not per command), so the block needs to recover which action id it
// is reporting for — that is what supplies the row's label and its default keypress. Written as a
// lookup rather than fourteen near-identical cases so a renamed id is one edit, and so a command
// that grows an id without a description shows up as the id itself in Settings rather than compiling
// to a blank row.
juce::String snapActionIdForCommand(juce::CommandID commandID) {
    switch (commandID) {
    case AppCommands::snapSetWhole:
        return "snapSetWhole";
    case AppCommands::snapSetHalf:
        return "snapSetHalf";
    case AppCommands::snapSetQuarter:
        return "snapSetQuarter";
    case AppCommands::snapSetEighth:
        return "snapSetEighth";
    case AppCommands::snapSetSixteenth:
        return "snapSetSixteenth";
    case AppCommands::snapSetThirtySecond:
        return "snapSetThirtySecond";
    case AppCommands::snapSetSixtyFourth:
        return "snapSetSixtyFourth";
    case AppCommands::snapSetHundredTwentyEighth:
        return "snapSetHundredTwentyEighth";
    case AppCommands::snapCyclePrev:
        return "snapCyclePrev";
    case AppCommands::snapCycleNext:
        return "snapCycleNext";
    default:
        return {};
    }
}

// The note-value name for a grid division — the SAME strings TimelinePanelComponent's snap combo
// shows ("Off", "Bar", "1", "1/2", …), so the status-bar report after a grid shortcut and the
// selector the user can see never disagree about what the grid is called.
juce::String snapDivisionLabel(synth::ui::TimelineViewState::Snap snap) {
    using Snap = synth::ui::TimelineViewState::Snap;
    switch (snap) {
    case Snap::Off:
        return "Off";
    case Snap::Bar:
        return "Bar";
    case Snap::Whole:
        return "1";
    case Snap::Half:
        return "1/2";
    case Snap::Quarter:
        return "1/4";
    case Snap::Eighth:
        return "1/8";
    case Snap::Sixteenth:
        return "1/16";
    case Snap::ThirtySecond:
        return "1/32";
    case Snap::SixtyFourth:
        return "1/64";
    case Snap::HundredTwentyEighth:
        return "1/128";
    }
    return "Off";
}

// GraphEditor's public zoom entry point (zoomAroundCentre) takes a WHEEL DELTA, because that is
// what its one zoom implementation was written against — it applies zoomLevel *= (1 + step * delta)
// with step == 0.1. The timeline surfaces take a multiplicative factor instead, so the zoom commands
// speak in factors and convert here, in ONE place, rather than each call site carrying a magic
// delta. Keep `kGraphZoomWheelStep` in step with GraphEditor::applyZoomAt if that formula changes:
// the consequence of drift is only that a canvas zoom step stops matching a timeline zoom step, but
// it is invisible until someone measures it.
constexpr double kGraphZoomWheelStep = 0.1;
float graphZoomWheelDeltaFor(double factor) { return (float)((factor - 1.0) / kGraphZoomWheelStep); }

juce::String zoomActionIdForCommand(juce::CommandID commandID) {
    switch (commandID) {
    case AppCommands::zoomInHorizontal:
        return "zoomInHorizontal";
    case AppCommands::zoomOutHorizontal:
        return "zoomOutHorizontal";
    case AppCommands::zoomInVertical:
        return "zoomInVertical";
    case AppCommands::zoomOutVertical:
        return "zoomOutVertical";
    default:
        return {};
    }
}

} // namespace

// ---- Primary constructor (injected ThemeManager + LookAndFeel from Main.cpp) ----
MainComponent::MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                             std::unique_ptr<synth::AIProvider> provider)
    : ownedAudioEngine(std::make_unique<AudioEngine>(AudioEngine::HostMode::Standalone))
    , audioEngine(*ownedAudioEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    // Setup ApplicationProperties — the shared location, never a local copy of the fields (the
    // plugin processor opens the same file for the scan list; see synth::userSettingsOptions()).
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Restore persisted theme and apply it. Must come AFTER appProperties is configured.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());

    // Subscribe to theme changes so we can re-skin on every switch.
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), synth::AIProviderRegistry::createDefault());
}

// ---- Plugin constructor (engine owned by AgentSynthAudioProcessor) ----
// Identical to the primary ctor apart from where the engine comes from; the shared body lives in
// initialiseCommon(), which skips engine initialise/shutdown when we don't own the engine.
MainComponent::MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                             AudioEngine& externalEngine, std::unique_ptr<synth::AIProvider> provider)
    : audioEngine(externalEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), synth::AIProviderRegistry::createDefault());
}

// ---- Delegating constructor for tests / legacy call sites ----
MainComponent::MainComponent(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry)
    : ownedAudioEngine(std::make_unique<AudioEngine>(AudioEngine::HostMode::Standalone))
    , audioEngine(*ownedAudioEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties) {
    // Own a default ThemeManager + LookAndFeel so the code behaves identically
    // to the primary-ctor path (no special-casing in the rest of the class).
    ownedThemeManager = std::make_unique<synth::theme::ThemeManager>();
    ownedLookAndFeel = std::make_unique<synth::theme::AppLookAndFeel>();
    themeManager = ownedThemeManager.get();
    lookAndFeel = ownedLookAndFeel.get();

    // Setup ApplicationProperties (same as primary ctor)
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Initialise theme with appProperties so the persisted theme is restored.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), std::move(registry));
}

// ---- Shared post-construction body ----
void MainComponent::initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry) {
    // Route AI patch applies through the app undo manager so Apply/Merge on a patch card is Cmd+Z-able.
    // Safe in both ctors: undoManager is declared before aiService, so it is already constructed here.
    aiService.setUndoManager(&undoManager);

    // juce::UndoManager is a ChangeBroadcaster that fires on every perform/undo/redo — the one
    // signal that means "something changed since the last save/load" without this class having to
    // hook every individual mutation site. changeListenerCallback dispatches on the source, so this
    // never fires the theme re-skin or settings-file branches.
    undoManager.getUndoManager().addChangeListener(this);

    // ORDERING CONTRACT: read the persisted panel-visibility flags FIRST, before any
    // setVisible()/addAndMakeVisible() call that depends on them. These override the member
    // initialisers (isLibraryVisible{true}, isAiPanelVisible=false).
    isLibraryVisible = appProperties.getUserSettings()->getBoolValue("librarySidebarVisible", true);
    isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
    isTimelineVisible = appProperties.getUserSettings()->getBoolValue("timelinePanelVisible", false);
    // ...and snap the fractions resized() lays the panels out from onto them. A restore must never
    // itself look like a panel sliding open, and the first resized() (setSize() at the end of this
    // function) runs before any window exists — see beginPanelSlide().
    librarySlide_.snapTo(isLibraryVisible ? 1.0f : 0.0f);
    aiPanelSlide_.snapTo(isAiPanelVisible ? 1.0f : 0.0f);
    timelineSlide_.snapTo(isTimelineVisible ? 1.0f : 0.0f);
    // The theme metric is the DEFAULT height, not the law: a height the user dragged wins. Clamped
    // here and on every resized() — see clampTimelinePanelHeight().
    timelinePanelHeight_ = clampTimelinePanelHeight(
        appProperties.getUserSettings()->getIntValue(kTimelinePanelHeightKey, defaultTimelinePanelHeight()));
    graphEditor.setAlignmentGuidesEnabled(
        appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true));
    graphEditor.setSmartConnectionMode(GraphEditor::smartConnectionModeFromString(
        appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired")));
    graphEditor.setDoubleClickPortDisconnectEnabled(
        appProperties.getUserSettings()->getBoolValue("doubleClickPortDisconnect", true));
    // Stored here, but APPLIED to the patch further down — the default preset does not exist yet.
    // AudioEngine::initialise() builds it, and that runs at the end of this constructor. See
    // applyStoredDualIOPreferenceToPatch().
    graphEditor.setDefaultDualIOForNewModules(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false));
    // Per-module overrides of the default above (Preferences → "Per-module I/O defaults..."),
    // same new-modules-only scope as the toggle just above — no patch to retro-apply here either.
    graphEditor.setDualIOPerModuleOverrides(PreferencesSettingsTab::loadDualIOPerModuleOverrides(appProperties));

    // Minimap overlay visibility (issue #159), defaults to visible.
    const bool minimapVisible = appProperties.getUserSettings()->getBoolValue("minimapVisible", true);
    graphEditor.setMinimapVisible(minimapVisible);

    // Scroll direction, and the LIVE path for it. juce::PropertiesFile is a ChangeBroadcaster that
    // fires on every value written, so subscribing here is what lets a Preferences toggle reach the
    // timeline and the piano roll without a restart — and without SettingsWindow having to grow yet
    // another constructor callback to hand down (the "Show timeline" kill switch already needs one,
    // and one wire per preference does not scale). changeListenerCallback dispatches on the source,
    // so a settings write never triggers the theme re-skin and vice versa.
    if (auto* settings = appProperties.getUserSettings())
        settings->addChangeListener(this);
    applyNaturalScrollingPreference();
    applyZoomScrollPreference();

    // Cable colour config (issue #157). Restored HERE rather than only in AppearanceSettingsTab:
    // that tab is built lazily when the Settings window opens, so leaving it to the tab would
    // mean the canvas ignored the user's saved colours until they went looking for them.
    graphEditor.setCableColourMode(synth::ui::loadCableColourMode(*appProperties.getUserSettings()));
    graphEditor.setCableColourOverrides(synth::ui::loadCableColourOverrides(*appProperties.getUserSettings()));

    // Macro recolour picker's favourites shelf (P8-14): the same PropertiesFile the timeline
    // ruler's marker colour picker persists to (TimelinePanelComponent::setApplicationProperties
    // -> ruler_.setPropertiesFile), so a favourite saved from one is offered by the other.
    graphEditor.setPropertiesFile(appProperties.getUserSettings());

    // Wavetable browser folder (issue #180): GraphEditor holds the value so every Wavetable
    // card can seed its browser from it, MainComponent owns the ApplicationProperties round
    // trip — the same split as the cable-colour config above.
    {
        const juce::String saved = appProperties.getUserSettings()->getValue("wavetableFolder", juce::String());
        if (saved.isNotEmpty())
            graphEditor.rememberWavetableFolder(juce::File(saved));
    }
    graphEditor.onWavetableFolderChanged = [this](const juce::File& folder) {
        appProperties.getUserSettings()->setValue("wavetableFolder", folder.getFullPathName());
        appProperties.saveIfNeeded();
    };

    if (provider) {
        aiService.setProvider(std::move(provider));
    } else {
        // Load AI provider preference. The persisted id (see AIProviderRegistry) is not a
        // display name — registry.create() falls back to the first registered provider ("ollama")
        // if the saved id is unknown (e.g. stale pre-registry value, or empty).
        //
        // P4-6 migration: "aiProvider" is only ever WRITTEN by AISettingsTab::updateSettings(), so
        // most existing installs have never persisted it, even after months of use — its absence
        // alone can't distinguish "brand new install" from "existing user who never opened AI
        // settings". existsAsFile() can: it reflects whether the settings file was already on disk
        // before this launch touched anything (nothing above this point in initialiseCommon(), nor
        // shortcutManager.loadFromProperties()/themeManager->initialise() in the constructor, writes
        // to appProperties — all read-only). See resolveDefaultProviderId() for the pure decision.
        const bool hasExistingSettingsFile = appProperties.getUserSettings()->getFile().existsAsFile();
        const juce::String defaultProviderId = resolveDefaultProviderId(hasExistingSettingsFile);
        juce::String savedProviderId = appProperties.getUserSettings()->getValue("aiProvider", defaultProviderId);

        // Pin the resolved id so every other reader of "aiProvider" (AISettingsTab) agrees with
        // what actually got constructed here, instead of independently re-deriving a default.
        // saveIfNeeded() is required, not optional: without it, a fresh install that resolves to
        // "remote" here only holds that in memory — if the process exits before some OTHER write
        // flushes the file, launch 2 finds a settings file on disk (from this launch's theme/
        // shortcut/panel-visibility writes) with no "aiProvider" key in it, resolves
        // hasExistingSettingsFile=true, and silently reverts a brand new install to "ollama".
        if (!appProperties.getUserSettings()->containsKey("aiProvider")) {
            appProperties.getUserSettings()->setValue("aiProvider", savedProviderId);
            appProperties.saveIfNeeded();
        }

        // Each provider persists its own host under its own key — "ollamaHost" and "remoteHost"
        // must never collide, or switching providers in Settings silently points one of them at
        // the other's address (see AISettingsTab::hostSettingsKeyFor()). An empty remoteHost
        // default lets AIProviderRegistry::createDefault() fall back to
        // synth::branding::kApiBaseUrl.
        const juce::String hostKey = savedProviderId == "remote" ? "remoteHost" : "ollamaHost";
        const juce::String hostDefault =
            savedProviderId == "remote" ? juce::String() : juce::String("http://localhost:11434");
        juce::String savedHost = appProperties.getUserSettings()->getValue(hostKey, hostDefault);

        aiService.setProvider(registry.create(savedProviderId, {savedHost, {}}));
    }

    // ORDERING CONTRACT: aiChatComponent is a member, so its constructor (which calls
    // refreshModels()) already ran BEFORE this body — at that point aiService had no
    // provider, so discovery short-circuited and no model was ever selected. We must
    // refresh again HERE, after setProvider(), or currentModel stays empty and every
    // /api/chat request is rejected by Ollama with HTTP 400 "model is required".
    // Regression: see #96 / f7cba4a.
    aiChatComponent.refreshModels();

    // Same ORDERING CONTRACT as refreshModels() above: aiChatComponent's constructor read
    // "aiRequestTimeoutMs" before appProperties.setStorageParameters() (called earlier in this
    // body) had pointed it at the real settings file, so that read saw an empty store and fell
    // back to the default. Re-load and re-push now that the file is actually open.
    aiChatComponent.setRequestTimeoutMs(appProperties.getUserSettings()->getIntValue(
        "aiRequestTimeoutMs", synth::AIChatComponent::kDefaultRequestTimeoutMs));

    // Gives AIIntegrationService's outgoing-request context builder a way to read the
    // app's one live TimelineDoc/TransportService — see AIIntegrationService::setTimelineContext().
    // Both outlive aiService (declaration order: timelineDoc, then audioEngine's referent, then
    // aiService), so this pointer never dangles for aiService's lifetime.
    aiService.setTimelineContext(&timelineDoc, &audioEngine.getTransport());
    // The timeline is GA: the AI's timeline/automation authoring surface is on unconditionally
    // from first launch (no Preferences toggle left to react to).
    aiService.setTimelineToolsEnabled(true);
    // The chat's Patch/Arrange selector reads this switch but gets no notification of it — the
    // refreshModels() call above ran BEFORE the switch (and before the timeline context existed),
    // so its gate check saw "off". Re-sync now that both are installed; same ownership shape as
    // the refreshModels() ordering contract itself.
    aiChatComponent.refreshModeControls();

    // The WRITE half. The service only ever holds the doc as const (it is a context reader),
    // and it owns no undo manager for the timeline, so the host supplies the apply path — the same
    // objects every other timeline edit in this class goes through, which is what puts an AI-applied
    // batch on the one shared undo stack alongside the user's own edits. `this` is safe to capture:
    // aiService is a member destroyed with us, and it clears the callback with it.
    aiService.setTimelineOpsApplyCallback([this](const juce::var& envelope) {
        return synth::TimelineOps::apply(envelope, timelineDoc, audioEngine.getGraph(), undoManager);
    });

    // Wire the account row/dialog up BEFORE attemptSilentSignIn() so the wiring is live for any
    // state changes that arrive from it (P3-2: sign-in surface for the AI panel).
    aiChatComponent.setAccountService(&accountService);
    accountService.attemptSilentSignIn();

    aiService.addListener(this);
    undoManager.setGraphEditor(&graphEditor);
    setWantsKeyboardFocus(true);

    // ---- Snippets + library collapse state (issue #156) ----
    // GraphEditor owns no file dialogs and the sidebar owns no filesystem access, so
    // MainComponent brokers between them.
    graphEditor.onSaveSnippetRequested = [this] { promptSaveSnippet(); };
    // Macros (P8-12): GraphEditor owns no status bar — see onStatusMessage's own comment.
    graphEditor.onStatusMessage = [this](const juce::String& msg) { statusBar.showMessage(msg); };
    // Right-click-any-knob -> the automation lane editor. Mirrors onSaveSnippetRequested's
    // shape exactly — GraphEditor owns no TimelineDoc, so it hands the (nodeId, paramId) pair back
    // to the one component that owns both the doc and the graph.
    graphEditor.onAutomateParameterRequested = [this](juce::AudioProcessorGraph::NodeID nodeId,
                                                      const juce::String& paramId) {
        automateParameter(nodeId, paramId);
    };
    // A hosted-plugin card's "Open Editor" button. Mirrors onAutomateParameterRequested's
    // shape — GraphEditor owns neither the module lookup nor the window manager.
    graphEditor.onOpenPluginEditorRequested = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
            if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()))
                pluginWindowManager.openEditorFor(hostedPlugin, nodeId);
    };
    graphEditor.snippetProvider = [this](const juce::String& name) -> juce::var {
        return synth::SnippetManager::loadSnippet(
            synth::SnippetManager::fileForName(synth::SnippetManager::getDefaultSnippetsDirectory(), name));
    };
    moduleLibrary.onSnippetDeleteRequested = [this](const juce::String& name) {
        if (synth::SnippetManager::deleteSnippet(synth::SnippetManager::getDefaultSnippetsDirectory(), name)) {
            refreshSnippetLibrary();
            statusBar.showMessage("Deleted snippet \"" + name + "\"");
        }
    };
    moduleLibrary.onCollapseStateChanged = [this] {
        appProperties.getUserSettings()->setValue("libraryCollapsedSections",
                                                  moduleLibrary.getCollapsedSections().joinIntoString("\n"));
        appProperties.saveIfNeeded();
    };
    moduleLibrary.setCollapsedSections(juce::StringArray::fromLines(
        appProperties.getUserSettings()->getValue("libraryCollapsedSections", juce::String())));
    refreshSnippetLibrary();

    // ---- Hosted plugins -----------------------------------------------------------
    // Restore the saved scan list, install it as the process-wide identity resolver, and wire the
    // two sidebar callbacks. Nothing here starts a scan: scanning launches child processes and is
    // only ever done because the user asked (and never at all in a hosted build — see
    // startPluginScan). The list IS needed in a hosted build, because a DAW session that hosts a
    // plugin still has to resolve its identity to something.
    auto* pluginBackend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault());
    if (ownedAudioEngine == nullptr && pluginBackend != nullptr && pluginBackend->getScanService() != nullptr) {
        // The plugin path with a resolver already installed: it is AgentSynthAudioProcessor's, and
        // it outlives this editor (a host closes and reopens the window freely). Adopt it instead of
        // installing ours over it — replacing it would leave the session with a resolver that dies
        // with the window, which is the bug this branch exists to prevent.
        activeScanService = pluginBackend->getScanService();
    } else {
        if (auto savedScanList = juce::parseXML(appProperties.getUserSettings()->getValue(kPluginScanListKey)))
            pluginScanService.loadFromXml(*savedScanList);
        if (pluginBackend != nullptr)
            pluginBackend->setScanService(&pluginScanService);
    }
    if (auto savedRecentProjects = juce::parseXML(appProperties.getUserSettings()->getValue(kRecentProjectsKey)))
        recentProjects.loadFromXml(*savedRecentProjects);

    moduleLibrary.onScanPluginsRequested = [this] { startPluginScan(); };
    moduleLibrary.onPluginActivated = [this](const synth::PluginIdentity& identity) {
        graphEditor.addHostedPluginAtCanvasPosition(identity, graphEditor.getViewportCentreInCanvasSpace());
    };
    refreshPluginLibrary();
    // Register commands for the macOS native menu bar (Edit→Undo shows Cmd+Z).
    // Do NOT add commandManager.getKeyMappings() as a KeyListener — it intercepts
    // keys like Cmd+Shift+Z and silently fails to invoke the command, preventing
    // our keyPressed() fallback from running. All key dispatch goes through keyPressed().
    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    shortcutManager.onBindingsChanged = [this] { updateCommandShortcuts(); };
    startTimerHz(10);
    addAndMakeVisible(graphEditor);
    addAndMakeVisible(moduleLibrary);

    // Grey out the singleton I/O rows once the patch already has one, and repaint the library
    // whenever the graph's module set changes so that state stays accurate.
    moduleLibrary.isModuleAvailable = [this](const juce::String& name) {
        return !GraphEditor::isSingletonIOModule(name) ||
               !GraphEditor::graphHasModuleNamed(audioEngine.getGraph(), name);
    };
    graphEditor.onGraphStructureChanged = [this] {
        moduleLibrary.repaint();
        // Close-on-node-delete. A pure NodeID -> graph lookup — see
        // HostedPluginWindowManager::pruneClosedNodes for why it must never dereference the module a
        // removed node used to carry.
        pluginWindowManager.pruneClosedNodes(audioEngine.getGraph());
        // The same catch-all, for the other hosted-plugin observer pair. A node that has just
        // appeared (a library drop, a preset load, an undo restore) needs MainComponent's latency /
        // publish callbacks installed on it, and this is the one hook every path that adds a node
        // already runs through. Idempotent — re-assigning the same two slots costs nothing.
        installHostedPluginObservers();
        // Safety net for graph changes with no explicit post-apply site of their own — the
        // canonical one being "the user deleted the Track In node from the canvas", which goes
        // through recordStructuralChange (a RECORD, not a restore, so the undo hooks don't fire).
        //
        // Deliberately reconcile-ONLY, never an unconditional republish: this runs at the end of
        // every updateComponents(), and building a snapshot each time would be wasteful. A
        // reconcile that flips a flag is itself a doc mutation, so timelineChanged republishes for
        // exactly the cases that need it — a binding can only start or stop resolving when a node
        // appears or disappears, which is also the only way an orphan flag moves.
        reconcileTimelineBindingsOnly();
    };
    addAndMakeVisible(aiChatComponent);
    aiChatComponent.setVisible(isAiPanelVisible);
    moduleLibrary.setVisible(isLibraryVisible);
    // Added unconditionally — isTimelineVisible stays false forever in a flag-OFF build
    // (the only code that ever flips it, the toggle button's onClick, is gated below), so this is
    // an inert invisible child there, same as any other never-shown component.
    addAndMakeVisible(timelinePanel);
    timelinePanel.setVisible(isTimelineVisible);
    graphEditor.getModMatrix().setVisible(graphEditor.isModMatrixVisible());

    // Z-ORDER CONSTRAINT: add the toolbar strip + status bar BEFORE the toolbar buttons.
    // JUCE paints children in addAndMakeVisible order, so the toolbar background must be
    // registered first (the buttons are direct children of MainComponent and paint on top).
    addAndMakeVisible(toolbar);
    addAndMakeVisible(statusBar);

    // Buttons
    addAndMakeVisible(newButton);
    newButton.setComponentID("newButton");
    newButton.onClick = [this] { commandManager.invokeDirectly(AppCommands::newPatch, true); };

    addAndMakeVisible(saveButton);
    saveButton.setComponentID("saveButton");
    saveButton.onClick = [this] { performSaveProject(false); };

    addAndMakeVisible(loadButton);
    loadButton.setComponentID("loadButton");
    loadButton.onClick = [this] {
        juce::PopupMenu menu;
        auto presets = synth::PresetManager::getPresetList();
        auto categories = synth::PresetManager::getCategories();
        for (const auto& cat : categories) {
            juce::PopupMenu subMenu;
            for (int i = 0; i < presets.size(); ++i) {
                if (presets[i].category == cat)
                    subMenu.addItem(i + 1, presets[i].name);
            }
            menu.addSubMenu(cat, subMenu);
        }
        menu.addSeparator();
        menu.addItem(1000, "Load from file...");

        // Recent Projects — pruned of anything that vanished from disk since the last time this
        // menu was built (a moved/deleted bundle), which is also the only time the pruned list
        // needs re-persisting.
        if (recentProjects.pruneMissing() > 0)
            saveRecentProjects();
        auto recents = recentProjects.getEntries();
        if (!recents.empty()) {
            menu.addSeparator();
            juce::PopupMenu recentMenu;
            for (int i = 0; i < (int)recents.size(); ++i)
                recentMenu.addItem(2000 + i, recents[(size_t)i].getFileNameWithoutExtension());
            menu.addSubMenu("Recent Projects", recentMenu);
        }

        // Capture `presets`/`recents` by value — the outer locals are gone by the time the async
        // callback runs.
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton), [this, presets,
                                                                                         recents](int result) {
            if (result == 1000) {
                openPresetFromFile();
            } else if (result >= 2000) {
                const auto index = (size_t)(result - 2000);
                if (index >= recents.size())
                    return;
                // Same guard as "Load from file...": the recent project itself is opened through
                // openFromFile, which re-adds it (moving it back to the front) on success.
                guardUnsavedChanges("Opening a recent project", [this, file = recents[index]] { openFromFile(file); });
            } else if (result > 0) {
                // Capture `presets` by value again (the outer local is gone by the time this runs)
                // and `index` so the guard's continuation still knows which preset was picked.
                guardUnsavedChanges("Loading a preset", [this, presets, index = result - 1] {
                    statusBar.showMessage("Loading preset...");
                    // loadFactoryPresetAtIndex detaches existing components (stopping scope timers) before the
                    // graph is cleared — avoids a use-after-free where a ScopeComponent reads a freed
                    // VisualBuffer — and reconciles the timeline's bindings against the new graph.
                    loadFactoryPresetAtIndex(index);
                    setCurrentPatchName(presets[index].name);
                    statusBar.showMessage("Loaded: " + presets[index].name);
                });
            }
        });
    };

    addAndMakeVisible(undoButton);
    undoButton.setComponentID("undoButton");
    undoButton.setEnabled(false);
    undoButton.onClick = [this] {
        if (undoManager.canUndo())
            undoManager.undo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(redoButton);
    redoButton.setComponentID("redoButton");
    redoButton.setEnabled(false);
    redoButton.onClick = [this] {
        if (undoManager.canRedo())
            undoManager.redo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(toggleAiPanelButton);
    toggleAiPanelButton.setComponentID("toggleAiPanel");
    toggleAiPanelButton.onClick = [this] {
        isAiPanelVisible = !isAiPanelVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("aiPanelVisible", isAiPanelVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        // Everything geometric — showing/hiding the panel, the slide, the synchronous landing when
        // there is no VBlank to slide on — belongs to the one shared seam.
        beginPanelSlide();
    };

    // Bottom-docked timeline panel toggle. Mirrors the AI-panel handler above exactly (flip +
    // persist BEFORE the slide, applyToolbarIcons, then beginPanelSlide) — the axis it slides on
    // is resized()'s business, not the toggle's.
    // Wire the panel to the real transport + persisted settings.
    timelinePanel.setTransport(&audioEngine.getTransport());
    timelinePanel.setMetronome(&audioEngine.getMetronome());
    timelinePanel.setApplicationProperties(&appProperties);

    // The user's bindings for the three surfaces that resolve their OWN keys (see
    // PianoRollComponent::setShortcutManager for the strict-resolution contract). All three are
    // installed together and MUST stay together: with a manager installed, resolution is strict —
    // an action id missing from ShortcutManager::resetToDefaults() has NO key at all rather than
    // falling back to its hardcoded default, so installing the manager before registering an id
    // makes that key silently inert. ShortcutManagerTest's surface-id tripwire pins the id list
    // these three consult against the defaults table for exactly that reason.
    timelinePanel.setShortcutManager(&shortcutManager);
    timelinePanel.getPianoRoll().setShortcutManager(&shortcutManager);
    timelinePanel.getClipLaneArea().setShortcutManager(&shortcutManager);

    // The panel's top-edge drag reports a desired height; THIS component owns it — clamp, lay out
    // live, and persist once the drag ends (not per pixel).
    timelinePanel.onResizeHeight = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight, /*persist=*/false);
    };
    timelinePanel.onResizeHeightCommitted = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight, /*persist=*/true);
    };

    // This component owns the app's one live TimelineDoc, so it owns the four hooks that
    // keep the rest of the system in step with it. The full inventory is in docs/architecture.md
    // ("App wiring") — keep the two in sync.
    //
    //  1. PUBLISH-ON-CHANGE: every effective doc mutation notifies us, and we republish the
    //     snapshot to the audio thread and rebuild the recorder's lane bindings.
    //  2. RECORDER: attached to (doc, undo, transport) and registered with the engine so the
    //     applier can see its gesture claims; driven by update() on the existing 10 Hz timer.
    //  3. RESTORE HOOKS: undo/redo suspends capture for the span of the restore and reconciles
    //     bindings afterwards (both domains — see AppUndoManager::setRestoreHooks).
    //  4. PANEL: the track-header column reads the doc and calls back into us (TrackHeaderHost)
    //     to create, re-bind and delete the Track In nodes its chips name.
    timelineDoc.addListener(this);
    automationRecorder.attachTo(timelineDoc, undoManager, audioEngine.getTransport());
    audioEngine.setAutomationRecorder(&automationRecorder);
    undoManager.setRestoreHooks(
        [this] { programmaticApplyScopes.push_back(std::make_unique<ProgrammaticApplyScope>(*this)); },
        [this] {
            if (!programmaticApplyScopes.empty())
                programmaticApplyScopes.pop_back();
            reconcileTimelineAfterGraphChange();
        });
    timelinePanel.setTrackHeaderHost(this);
    timelinePanel.setTimelineDoc(&timelineDoc);
    // Same undo stack every graph/timeline mutation already shares — a clip drag/trim/
    // split/duplicate/delete is one more AppUndoManager::recordTimelineChange call, same as every
    // other timeline-only edit.
    timelinePanel.setUndoManager(&undoManager);
    // The SAME resolution the audio streamer uses (AudioClipStreamer::resolveAssetRef),
    // re-targeted at the peaks sidecar via peaksRefForAssetRef() — see that function's comment.
    // One shared resolver: the clip-lane area never re-derives bundle-vs-Recordings root logic.
    timelinePanel.getClipLaneArea().setPeaksResolver([this](const juce::String& assetRef) -> juce::File {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(peaksRefForAssetRef(assetRef));
    });
    // Same resolution playback uses, answering existence rather than handing back a File —
    // what paints the missing-asset placeholder instead of an (impossible) waveform.
    timelinePanel.getClipLaneArea().setAssetExistsResolver([this](const juce::String& assetRef) -> bool {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(assetRef) != juce::File();
    });
    // "Relink audio…" bubbles up here rather than being handled inside the lane area itself
    // — it needs a host FileChooser and synth::AssetManager import, neither of which that class has.
    timelinePanel.getClipLaneArea().onRelinkAudioRequested = [this](synth::ClipId id) { promptRelinkClipAsset(id); };
    // Same division of labour for the authoring gestures: the lane area decides WHICH audio track
    // and WHICH beat (double-click on an empty audio row, or an OS file drop on one), and this owns
    // the import + clip creation, because only it knows the bundle root.
    timelinePanel.getClipLaneArea().onAudioFileDropped = [this](synth::TrackId track, double startBeat,
                                                                juce::File file) {
        importAudioFileToClip(track, startBeat, file);
    };
    // P on the clip lanes = loop the selection. The lane area knows the span; only this owns the
    // transport (same division as every other callback above). Whether P also ARMS looping is the
    // "timelineLoopSelectionArms" preference (default yes; off = place the locators, keep the
    // current loop state) — the same key TimelinePanelComponent's own P fallback reads.
    timelinePanel.getClipLaneArea().onLoopRangeRequested = [this](double startBeat, double endBeat) {
        auto& transport = audioEngine.getTransport();
        bool arm = true;
        if (auto* settings = appProperties.getUserSettings())
            arm = settings->getBoolValue("timelineLoopSelectionArms", true);
        transport.setLoop(startBeat, endBeat, arm || transport.getPositionSnapshot().looping);
    };
    // BEFORE the first publish below — publishTimeline() syncs the clip streamer, and it can
    // only resolve an asset ref once it knows the roots.
    refreshAssetRoots();
    // One publish before anything else happens, so the audio thread starts from this document
    // rather than from the exchange's never-published empty fallback.
    publishTimelineAndRebindRecorder();

    // MidiRecorder is now app-wired (docs/architecture.md's hook inventory gains a
    // fifth entry) — this component owns the one live MidiRecorder, since it is the only thing
    // that can see both the armed tracks (timelineDoc) and the transport bar's record button.
    audioEngine.setMidiCaptureSink(&midiRecorder);
    timelinePanel.getTransportBar().onRecordToggled = [this](bool wantRecording) {
        if (!wantRecording) {
            // Both are no-ops unless their own kind of take is in flight, so Record-off can call
            // them unconditionally and neither path has to know the other exists.
            commitAudioRecording();
            commitMidiRecording();
            return;
        }

        // Record does NOT require an armed track (see docs/timeline_panel_core.md's transport
        // section) — pressing Record always rolls the transport with the record indicator lit,
        // capturing on whichever track (if any) happens to be armed. With nothing armed this is
        // identical to Play plus a lit record indicator, plus a transient status-bar notice so the
        // silence isn't mistaken for a bug.
        //
        // The lookup considers Audio tracks too, and FIRST-ARMED WINS. With one armed track
        // of each kind the one earlier in the document decides which kind of take this is; there is
        // deliberately no "record both at once" (two takes, two commits, two undo steps for one
        // gesture). Automation-kind tracks are not recordable and are skipped.
        synth::TrackId armedTrack;
        synth::TrackKind armedKind = synth::TrackKind::Midi;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.armed && (track.kind == synth::TrackKind::Midi || track.kind == synth::TrackKind::Audio)) {
                armedTrack = track.id;
                armedKind = track.kind;
                break;
            }
        }
        const bool anyArmed = armedTrack.isValid();

        // An audio take's tap and its destination files are resolved BEFORE the transport
        // moves, for the same reason as always — a request that cannot be honoured must not leave
        // the transport rolling. Only reachable with an armed Audio track; with nothing armed (or a
        // MIDI track armed) there is no take to resolve here.
        const bool isAudioTake = anyArmed && (armedKind == synth::TrackKind::Audio);
        AudioTake take;
        RecordTapModule* tapModule = nullptr;
        if (isAudioTake) {
            auto* tapNode = ensureMasterRecordTap();
            tapModule = tapNode != nullptr ? dynamic_cast<RecordTapModule*>(tapNode->getProcessor()) : nullptr;
            if (tapModule == nullptr) {
                timelinePanel.getTransportBar().setRecordingState(false);
                statusBar.showMessage("Can't record audio: no Audio Output in the patch");
                return;
            }
            take.track = armedTrack;
            take.tapNode = tapNode->nodeID;
            if (!chooseTakeFiles(take)) {
                timelinePanel.getTransportBar().setRecordingState(false);
                statusBar.showMessage("Can't record audio: could not create the take file");
                return;
            }
        }

        // Record implies roll (DAW convention): starting a take also starts the transport if it
        // isn't already running.
        auto& transport = audioEngine.getTransport();
        const auto snap = transport.getPositionSnapshot();
        const int countInBars = timelinePanel.getTransportBar().getCountInBars();

        // Count-in pre-roll, only from a full stop — a record engaged while already playing
        // gets no pre-roll (the user is already mid-performance) and no forced click, matching
        // today's plain "record implies roll" behaviour exactly.
        double punchInBeat = snap.ppq;
        if (!snap.playing && countInBars > 0) {
            const double beatsPerBar =
                (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
            const double preRollStart = std::max(0.0, punchInBeat - (double)countInBars * beatsPerBar);
            transport.locateBeat(preRollStart);
            // Forced audible through the pre-roll regardless of the user's own metronome toggle;
            // cleared by the 10 Hz poll once the transport reaches punchInBeat (see timerCallback)
            // and unconditionally by commitMidiRecording() on stop.
            audioEngine.getMetronome().setForcedOn(true);
            transport.play();
        } else {
            if (!snap.playing)
                transport.play();
            punchInBeat = transport.getPositionSnapshot().ppq;
        }

        if (isAudioTake) {
            // The capture starts HERE, at record-on — not on the 10 Hz poll, which is what
            // used to cost a take up to ~100 ms of head. The count-in's pre-roll is therefore
            // RECORDED, and excluded from the committed clip by a trim (see commitAudioRecording);
            // the tap itself reports the exact transport sample its frame 0 landed on, so the clip's
            // placement is sample arithmetic rather than a poll observation.
            //
            // Deliberately AFTER the locate/play posted above, not before: those are transport
            // commands that take effect at the top of a block, and a frame captured before a locate
            // belongs to the OLD position — which would break the one thing the anchor promises,
            // that take frame `f` sits at `captureStart + f`. Starting after them can cost at most
            // the one block that may already be in flight (~10 ms at 512/48k), and that block is
            // pre-roll, honestly accounted for either way.
            const double rate = snap.sampleRate > 0.0 ? snap.sampleRate : 44100.0;
            if (!tapModule->startCapture(take.wavFile, take.peaksFile, rate, RecordTapModule::kNumChannels)) {
                // The tap vanished, or the file could not be opened. Nothing to salvage.
                timelinePanel.getTransportBar().setRecordingState(false);
                audioEngine.getMetronome().setForcedOn(false);
                statusBar.showMessage("Can't record audio: the take file could not be opened");
                return;
            }
            take.punchInBeat = punchInBeat;
            take.capturing = true;
            // Frozen NOW, not re-read at commit time — see the AudioTake field comments.
            take.captureSampleRate = rate;
            take.captureBpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
            take.captureRecordingLatencySamples = audioEngine.getRecordingLatencySamples();
            audioTake_ = take;
        } else if (anyArmed) {
            // punchInBeat is BOTH the recorder's own bookkeeping and the audio-thread filter
            // threshold — captureBlock() drops everything before it, so the pre-roll bars the
            // performer plays along with the click are heard but never committed.
            midiRecorder.startRecording(armedTrack, punchInBeat);
        } else {
            // Nothing armed: the transport is rolling and the indicator is about to light, but no
            // take of either kind starts. Arming a track MID-ROLL does not retroactively start one
            // either — TimelineDoc::setTrackArmed() has no listener watching for this; the user has
            // to stop and press Record again once something is armed.
            statusBar.showMessage("Recording started - no track is armed");
        }

        // Lit regardless of arming — a bare "record" is still record-on.
        timelinePanel.getTransportBar().setRecordingState(true);
    };

    addAndMakeVisible(toggleTimelineButton);
    toggleTimelineButton.setComponentID("toggleTimeline");
    toggleTimelineButton.onClick = [this] {
        isTimelineVisible = !isTimelineVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("timelinePanelVisible", isTimelineVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        beginPanelSlide();
    };

    addAndMakeVisible(toggleMinimapButton);
    toggleMinimapButton.setComponentID("toggleMinimap");
    toggleMinimapButton.onClick = [this] {
        graphEditor.toggleMinimapVisibility();
        appProperties.getUserSettings()->setValue("minimapVisible", graphEditor.isMinimapVisible() ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
    };

    addAndMakeVisible(toggleModMatrixButton);
    toggleModMatrixButton.setComponentID("toggleModMatrix");
    toggleModMatrixButton.onClick = [this] {
        graphEditor.toggleModMatrixVisibility();
        applyToolbarIcons();
        resized();
    };

    addAndMakeVisible(toggleLibraryButton);
    toggleLibraryButton.setComponentID("toggleLibrary");
    toggleLibraryButton.onClick = [this] { setLibraryVisible(!isLibraryVisible); };

    addAndMakeVisible(themeToggleButton);
    themeToggleButton.setComponentID("themeToggle");
    themeToggleButton.onClick = [this] { themeManager->toggleLightDarkMode(); };

    addAndMakeVisible(autoArrangeButton);
    autoArrangeButton.setComponentID("autoArrangeButton");
    autoArrangeButton.onClick = [this] { graphEditor.autoArrange(); };

    addAndMakeVisible(settingsButton);
    settingsButton.setComponentID("settingsButton");
    settingsButton.onClick = [this]() {
        auto* settingsComp =
            new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService, aiChatComponent,
                               shortcutManager, *themeManager, &graphEditor, &accountService,
                               /*showAudioTab=*/!audioEngine.isHosted());
        settingsComp->setSize(500, 450);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(settingsComp);
        options.dialogTitle = "Settings";
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    };

    addAndMakeVisible(feedbackButton);
    feedbackButton.setComponentID("feedbackButton");
    feedbackButton.onClick = [this]() {
        auto* settingsComp =
            new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService, aiChatComponent,
                               shortcutManager, *themeManager, &graphEditor, &accountService,
                               /*showAudioTab=*/!audioEngine.isHosted(), "Feedback");
        settingsComp->setSize(500, 450);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(settingsComp);
        options.dialogTitle = "Settings";
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    };

    // Hand the (now-constructed) buttons to the toolbar for FlexBox layout. Order MUST match
    // ToolbarComponent::Slot.
    // ORDERING CONTRACT: setButtons() MUST be called BEFORE setSize() so that the first
    // resized() -> layoutButtons() pass finds the registered buttons and positions them.
    // Calling setSize() before setButtons() leaves all buttons with zero bounds on first launch.
    toolbar.setButtons({&toggleLibraryButton, &newButton, &saveButton, &loadButton, &settingsButton, &feedbackButton,
                        &undoButton, &redoButton, &autoArrangeButton, &toggleMinimapButton, &toggleModMatrixButton,
                        &toggleAiPanelButton, &toggleTimelineButton, &themeToggleButton});

    // Now that buttons are registered, trigger the first layout pass. resized() calls
    // toolbar.layoutButtons() which positions the buttons using their registered pointers.
    setSize(1600, 900);

    // Master-mute: toggles AudioEngine's master mute (audio keeps running; output is zero-filled).
    statusBar.getMasterMuteButton().setComponentID("masterMute");
    statusBar.getMasterMuteButton().onClick = [this] {
        audioEngine.setMasterMute(!audioEngine.isMasterMuted());
        statusBar.repaint();
    };

    // Status bar play/stop: the SAME TransportService actions TimelineTransportBar's own play/stop
    // button uses (see its onClick), so the transport responds identically whether the click came
    // from here or from inside the (possibly-hidden) timeline panel. "The transport is the truth" —
    // this button's visual is never flipped directly; timerCallback()'s statusBar.updateTransport()
    // call is what sets it, from the same PositionSnapshot poll TimelineTransportBar uses.
    statusBar.getTransportButton().onClick = [this] {
        auto& transport = audioEngine.getTransport();
        if (transport.getPositionSnapshot().playing)
            transport.stop();
        else
            transport.play();
    };

    // One unconditional icon/text pass at startup (subsequent calls only fire on narrow-mode flips).
    applyToolbarIcons();
    setCurrentPatchName("Default");

    // Audio device state. Guarded the same way the engine-lifecycle block below is: on the plugin
    // path the host owns the device (there is not even an Audio tab), so this app's settings file
    // has no say over it.
    //
    // ORDERING CONTRACT: both halves must be in place BEFORE audioEngine.initialise() below — the
    // saved state because initialise() is what restores it, the callback because opening a device
    // can itself broadcast a change. MainComponent owns the ApplicationProperties round trip and
    // the engine owns the device: Core never reads or writes settings.
    if (ownedAudioEngine != nullptr) {
        const juce::String savedDeviceXml =
            appProperties.getUserSettings()->getValue("audioDeviceState", juce::String());
        if (savedDeviceXml.isNotEmpty()) {
            if (auto parsed = juce::parseXML(savedDeviceXml))
                audioEngine.setSavedDeviceState(std::move(parsed));
        }

        audioEngine.onDeviceStateChanged = [this](std::unique_ptr<juce::XmlElement> state) {
            // BEFORE the null check below: a device change that JUCE does not consider
            // an explicit setup still changes how many input channels exist, and the Audio Input
            // card's jacks (plus any cable on a jack that just vanished) have to follow it.
            graphEditor.refreshIoModulesAfterDeviceChange();

            // Same reasoning for the Audio Output card's destination line (docs/layout.md —
            // module chrome): a device/rate/channel change is exactly what it needs to reflect,
            // and it must not wait for a repaint that has no other reason to happen.
            graphEditor.refreshOutputDeviceInfo();

            // Null until the user has explicitly chosen a device setup — see the declaration of
            // onDeviceStateChanged. Persisting nothing then is the point: the absent key is what
            // keeps the next launch on the inputs-off defaults.
            if (state == nullptr)
                return;
            appProperties.getUserSettings()->setValue("audioDeviceState",
                                                      state->toString(juce::XmlElement::TextFormat().singleLine()));
            appProperties.saveIfNeeded();
        };
    }

    // Output-card identity treatment: installed unconditionally (both Standalone and Hosted use
    // it — computeOutputDeviceInfoText() branches on AudioEngine::isHosted() itself), then primed
    // once below so the card is populated at startup rather than waiting for the first device
    // change, which on a fresh install may never come (see onDeviceStateChanged's own comment
    // about staying null until the user explicitly touches the Audio tab).
    graphEditor.setOutputDeviceInfoProvider([this] { return computeOutputDeviceInfoText(); });

    // Engine lifecycle is the owner's job. On the plugin path the processor already called
    // initialise() (and will call shutdown()), and its graph may already hold host-restored
    // state — re-initialising here would overwrite the user's session with the default patch.
    if (ownedAudioEngine == nullptr) {
        // Plugin path: the graph already holds the host-restored session, whose modules each carry
        // their own saved dualIO value. Forcing the preference over that would rewrite the user's
        // session, which is exactly what restoring state is supposed to avoid.
        graphEditor.updateComponents();
        graphEditor.refreshOutputDeviceInfo();
        return;
    }

    if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio) &&
        !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio)) {
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [&](bool granted) {
            if (granted) {
                audioEngine.initialise();
                applyStoredDualIOPreferenceToPatch();
                graphEditor.updateComponents();
                graphEditor.refreshOutputDeviceInfo();
            }
        });
    } else {
        audioEngine.initialise();
        applyStoredDualIOPreferenceToPatch();
        graphEditor.updateComponents();
        graphEditor.refreshOutputDeviceInfo();
    }
}

void MainComponent::applyStoredDualIOPreferenceToPatch() {
    // Runs once, right after AudioEngine::initialise() has built the opening patch. Storing the
    // preference on the GraphEditor is not enough on its own: the default preset's modules are
    // constructed by the preset loader, which knows nothing about preferences, so they come up
    // holding their constructor defaults. The voice modules default to dual — so a user who had
    // chosen single jacks got a split Oscillator and Filter on every launch.
    //
    // A patch the user saved carries an explicit dualIO value per module and is applied later by
    // applyJSONToGraph, so this governs the factory patch rather than a reload of their own work.
    graphEditor.applyDualIOToExistingModules(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false));
}

juce::String MainComponent::computeOutputDeviceInfoText() const {
    // Hosted mode (plugin): AudioEngine never opens a device or touches deviceManager — the host
    // owns the clock and the hardware (see HostMode::Hosted in docs/architecture.md) — so there is
    // no device to describe, only which world this editor is running in.
    if (audioEngine.isHosted())
        return "Host audio";

    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return {}; // No device open yet (headless/CI, or between devices) — the card hides the line.

    const double sampleRateHz = device->getCurrentSampleRate();
    const double khz = sampleRateHz / 1000.0;
    // "48 kHz" for a whole number, "44.1 kHz" when it isn't — matches how the format is normally
    // spoken, rather than always showing a decimal point.
    const juce::String khzText =
        (std::abs(khz - std::round(khz)) < 0.01) ? juce::String((int)std::round(khz)) : juce::String(khz, 1);

    const int numOutputChannels = device->getActiveOutputChannels().countNumberOfSetBits();

    // Plain ASCII separator. A "\xc2\xb7" (UTF-8 U+00B7 MIDDLE DOT) used to sit here, on the theory
    // that an escape survives a non-UTF-8 editor better than a literal byte — but the escape is the
    // same three bytes, and juce::String's `const char*` constructor decodes bytes as LATIN-1, so
    // both spellings reached the status bar as mojibake. ASCII or juce::CharPointer_UTF8; see the
    // string-literal invariant in CLAUDE.md, guarded by scripts/tests/check-nonascii-literals.test.sh.
    return device->getName() + " - " + khzText + " kHz - " + juce::String(numOutputChannels) + "ch";
}

MainComponent::~MainComponent() {
    // Every plugin editor window must die before the graph/engine below do — see
    // HostedPluginWindowManager's class comment (this explicit call is one of two independent
    // safeguards; declaration order is the other).
    pluginWindowManager.closeAll();

    // Uninstall what installHostedPluginObservers() installed. Both callbacks capture
    // `this`, and on the plugin path the engine — and every hosted module in its graph — OUTLIVES
    // this editor-owned component (hosts close and reopen editors freely), so a latency change or
    // publish after this destructor would otherwise call through freed memory inside the host.
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor())) {
            hosted->onLatencyChanged = nullptr;
            hosted->onInstancePublished = nullptr;
        }
    }

    // The process-wide backend holds a bare pointer to our scan service, so unhook it before
    // anything else that could resolve an identity — a hosted-plugin restore after this point would
    // otherwise go through freed memory. Guarded on "still ours" because a second MainComponent
    // (tests construct several) will have replaced it — and because on the plugin path the installed
    // service is the PROCESSOR's, adopted rather than owned, and must survive this editor closing.
    if (auto* backend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault()))
        if (backend->getScanService() == &pluginScanService)
            backend->setScanService(nullptr);
    pluginScanService.cancelScan();

    // Unsubscribe before the manager (or our owned copy) is torn down.
    if (themeManager != nullptr)
        themeManager->removeChangeListener(this);
    // Same reason, one level down: timelinePanel is declared BEFORE shortcutManager, so member
    // teardown destroys the manager first — detach the panel's ChangeListener subscription (its
    // dynamic tooltip refresh) while the manager is still alive, or ~TimelinePanelComponent()
    // dereferences a dangling pointer on quit.
    timelinePanel.setShortcutManager(nullptr);
    // Same reason: the settings file outlives this component on the plugin path (it is a shared
    // location — see synth::userSettingsOptions()), so a write from any other holder after this
    // point would call back into freed memory.
    if (auto* settings = appProperties.getUserSettings())
        settings->removeChangeListener(this);
    // Same reason: undoManager outlives this call (its own destructor runs after this body), so a
    // stray perform/undo/redo between now and then must not reach a callback that touches
    // half-torn-down members.
    undoManager.getUndoManager().removeChangeListener(this);
    stopTimer();
    aiService.removeListener(this);
    // Order matters: stop listening to the doc first (nothing may republish while we tear down),
    // drop the panel's view of it, unhook the engine from the recorder's audio-visible state, and
    // only then detach the recorder — which commits anything still in flight into the doc and the
    // undo manager, both of which are still alive here (and outlive this body; see the declaration
    // order note in MainComponent.h). Finally drop the undo hooks: they reach back into members
    // that are about to go.
    timelineDoc.removeListener(this);
    timelinePanel.setTimelineDoc(nullptr);
    audioEngine.setAutomationRecorder(nullptr);
    audioEngine.setMidiCaptureSink(nullptr);
    automationRecorder.detach();
    undoManager.setRestoreHooks({}, {});
    graphEditor.detachAllModuleComponents();
    // Only tear down an engine we own. On the plugin path the processor's engine must survive
    // the editor being closed and reopened.
    if (ownedAudioEngine != nullptr) {
        // Drop the device-state callback first — it captures `this`, and shutdown() is the
        // call that unsubscribes the engine from its device manager.
        audioEngine.onDeviceStateChanged = nullptr;
        audioEngine.shutdown();
    }
}

// ---- Change callbacks: theme re-skin, and the live settings-file path ----
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // The undo manager broadcasts on every perform/undo/redo/new-transaction. This RECOMPUTES the
    // answer from the edit serial rather than blindly setting dirty, because that broadcast is
    // ASYNC (juce::ChangeBroadcaster::sendChangeMessage) and therefore always potentially stale:
    // New Patch clears the timeline and the graph — two real undoable steps — and only then resets
    // the document, so a notification for those steps is still queued when the reset runs. Setting
    // the flag on arrival would re-dirty a brand-new "Untitled" document one message-loop pass
    // after it was created; comparing serials makes the late notification a no-op instead, since
    // markDocumentClean() captured the baseline AFTER those steps.
    // Checked before the settings/theme dispatch below since it is neither of those broadcasters.
    if (source != nullptr && source == &undoManager.getUndoManager()) {
        const bool nowDirty = undoManager.getEditSerial() != savedEditSerial_;
        if (nowDirty != isDirty_) {
            isDirty_ = nowDirty;
            notifyDocumentTitleChanged();
        }
        return;
    }

    // Dispatch on the source, not "assume theme": two broadcasters reach here now. A settings write
    // must NOT trigger a full re-skin (persisting a panel height or a snap division would re-theme
    // the whole window), and a theme switch must not re-read preferences.
    if (source != nullptr && source == appProperties.getUserSettings()) {
        applyNaturalScrollingPreference();
        applyZoomScrollPreference();
        // Piano-roll key-label mode and note colour overrides live in the same properties file —
        // re-read them on every settings write so an Appearance-tab edit shows up immediately,
        // the same "re-read on notify" treatment as the two calls above. No startup call needed:
        // TimelinePanelComponent::setApplicationProperties already does the initial load.
        timelinePanel.reloadPianoRollAppearancePrefs();
        return;
    }

    // Push new theme values into the LookAndFeel (colours / treatment / metrics), then
    // propagate lookAndFeelChanged() + a single repaint so every widget re-skins.
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    if (auto* top = getTopLevelComponent())
        top->sendLookAndFeelChange();
    // Re-tint the toolbar / status-bar icons from the already-retinted IconLibrary cache.
    applyToolbarIcons();
    repaint();
}

void MainComponent::timerCallback() {
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());

    // Message-thread driver, on the timer we already run: drains the gesture ring and polls
    // the transport (Write spans open on play; every open span commits on stop). Allocation-free
    // and — like everything else in this callback — completely silent.
    automationRecorder.update();

    // Mirrors AutomationRecorder's own playing->stopped edge detection (see its update()) —
    // a MIDI take still open when the transport stops (the user hit Space/Stop rather than the
    // record button itself) commits exactly the same way an explicit Record-off click does. Read
    // unconditionally (cheap, lock-free) rather than only when the panel is visible: recording must
    // not depend on the timeline panel staying open.
    const auto position = audioEngine.getTransport().getPositionSnapshot();
    if (wasTransportPlaying_ && !position.playing && midiRecorder.isRecording())
        commitMidiRecording();

    // The audio half of the same rule — COMMIT ON STOP, mirroring the MIDI one above: a take
    // still open when the transport stops (the user hit Space rather than the record button) commits
    // down the same path. Capture itself starts at the Record-on click, not here; the pre-roll is
    // trimmed at commit time from the tap's own sample-accurate anchor.
    if (wasTransportPlaying_ && !position.playing && audioTake_.capturing)
        commitAudioRecording();

    // A device/sample-rate change strands whatever take was rolling — unlike the two commit-
    // on-stop checks above, the transport typically keeps PLAYING right through a format change (see
    // TransportService::prepare), so neither of those edge-triggered checks would ever fire. Consumed
    // once here (exchange-back-to-false, same contract as consumeFeedbackGuardTripped) and routed
    // through the SAME commit choke points a manual Record-off or a transport stop already use — see
    // AudioEngine::handleStreamFormatChange, which is what actually sets the flag, at the moment of
    // the change. At most one of the two checks below ever fires: recording never runs both an audio
    // and a MIDI take at once (first-armed-wins — see onRecordToggled).
    if (audioEngine.consumeFormatChangedDuringCapture()) {
        if (audioTake_.capturing)
            commitAudioRecording();
        if (midiRecorder.isRecording())
            commitMidiRecording();
        statusBar.showMessage("Recording stopped: audio device changed");
    }

    // Autosave's gate, on the same driver — see maybeAutosave()'s own comment. Placed AFTER the
    // three commit-on-stop checks above (not before): a take that just committed on this very tick
    // must be allowed to autosave immediately, not wait for the take flag to clear on some later
    // tick, and a take still genuinely in flight must still block it.
    maybeAutosave();

    // Polls BounceRunner's progress onto the dialog's progress bar - on the SAME 10 Hz driver as
    // everything else here, rather than a second timer just for this. exportDialog_ is a
    // SafePointer: the dialog can only go away by the user closing the (modal) window, but nothing
    // stops that from racing a tick.
    if (isBounceInProgress_ && bounceRunner_ != nullptr && exportDialog_ != nullptr)
        exportDialog_->reportProgress(bounceRunner_->getProgress());

    wasTransportPlaying_ = position.playing;

    // Clears the count-in pre-roll's forced-on click once the transport reaches the punch-in
    // point. Gated on isRecording() so this never fires outside an actual take; idempotent
    // otherwise (setForcedOn(false) on an already-off metronome is a no-op), so polling it every
    // tick while recording costs nothing once the pre-roll has already ended. An audio take's
    // pre-roll rides the same rule, ending at its own punch-in beat.
    if ((midiRecorder.isRecording() && position.ppq >= midiRecorder.getPunchInBeat()) ||
        (audioTake_.capturing && position.ppq >= audioTake_.punchInBeat))
        audioEngine.getMetronome().setForcedOn(false);

    // The input-monitoring gate's poll-side half. Any Audio-kind track armed -> monitoring
    // should be on; none armed -> off. A guard trip (audioEngine.consumeFeedbackGuardTripped(),
    // consumed exactly once here) latches monitoring off for as long as the SAME arm state
    // persists — disarming every Audio track and re-arming one is the explicit reset gesture, and
    // that is exactly the false->true edge of "is any Audio track armed" below. See
    // docs/architecture.md's "Input monitoring & feedback guard".
    bool anyAudioTrackArmed = false;
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.kind == synth::TrackKind::Audio && track.armed) {
            anyAudioTrackArmed = true;
            break;
        }
    }
    if (anyAudioTrackArmed && !wasAnyAudioTrackArmed_)
        feedbackGuardLatched_ = false; // the reset gesture: disarmed, then armed again
    wasAnyAudioTrackArmed_ = anyAudioTrackArmed;

    if (audioEngine.consumeFeedbackGuardTripped()) {
        feedbackGuardLatched_ = true;
        statusBar.showMessage("Input muted - sustained clipping (feedback protection)");
    }

    audioEngine.setInputMonitoringEnabled(anyAudioTrackArmed && !feedbackGuardLatched_);

    // The timeline panel's low-rate transport poll, on the same existing timer — no new
    // timer, and nothing at all when the panel is hidden (a collapsed timeline must cost exactly
    // what it did before). This is what starts/stops the playhead's playing-only 30 Hz strip
    // repaint; see docs/layout.md §11.
    if (timelinePanel.isVisible()) {
        // Device-buffer latency only. The graph's own reported latency is deliberately left out:
        // it is report-only, patch-dependent and mostly zero, whereas the output buffer is the term
        // that actually separates "rendered" from "heard".
        const double sampleRate = position.sampleRate > 0.0 ? position.sampleRate : 44100.0;
        const double outputLatencySeconds = (double)audioEngine.getOutputLatencySamples() / sampleRate;
        timelinePanel.updateFromTransport(position, outputLatencySeconds);

        // The clip lane's growing recording strip, on the same poll. Cheap and a no-op when
        // nothing is capturing — synth::ui::TimelineClipLaneArea::updateLiveRecording() internally
        // repaints only when new peak buckets actually arrived (see its own comment), never merely
        // because the transport tick moved.
        synth::ui::TimelineClipLaneArea::LiveRecordingInfo liveInfo;
        if (audioTake_.capturing) {
            if (auto* tap = findMasterRecordTap()) {
                liveInfo.active = true;
                liveInfo.track = audioTake_.track;
                liveInfo.punchBeat = audioTake_.punchInBeat;
                liveInfo.currentBeat = position.ppq;
                liveInfo.tap = tap;
            }
        }
        timelinePanel.getClipLaneArea().updateLiveRecording(liveInfo);
    }

    // Status bar polls at 5 Hz (every 2nd tick of the 10 Hz timer). update() is gated — it
    // only repaints the status bar when a displayed value actually changes. ZERO logging.
    if (++statusBarTickCount_ >= 2) {
        statusBarTickCount_ = 0;
        // Hosted (plugin) mode has no device manager of its own — the host owns the device, so
        // getCpuUsage() would report a constant 0. Show 0 rather than a misleading reading.
        const float cpu = audioEngine.isHosted() ? 0.0f : (float)(audioEngine.getDeviceManager().getCpuUsage() * 100.0);
        statusBar.update(cpu, audioEngine.getDisplayVoiceCount(), currentPatchName_);

        // The round-trip readout, on the same 5 Hz tick.
        updateRoundTripLatencyReadout();

        // The always-visible transport cluster (play/stop + position + BPM) — fed from `position`,
        // which is read UNCONDITIONALLY above (before the timelinePanel.isVisible() guard), so this
        // is identical whether the timeline panel is open or closed; see docs/layout.md §5. Reuses
        // TimelineTransportBar's own static formatBarBeat() for the "bar.beat.ticks" text rather
        // than reimplementing it — StatusBarComponent can't call it itself (Core cannot depend on
        // AppUI), so this is the one call site that does the formatting.
        statusBar.updateTransport(position.playing,
                                  synth::ui::TimelineTransportBar::formatBarBeat(
                                      position.ppq, position.timeSigNumerator, position.timeSigDenominator),
                                  position.bpm);
    }
}

void MainComponent::updateRoundTripLatencyReadout() {
    // Gated by its own string diff inside StatusBarComponent — so calling it more
    // often than the 5 Hz poll (a hosted plugin's latency change does) costs nothing when
    // the number has not moved. Hosted mode has no device of ours to report a round trip for (the
    // host owns both ends), so it shows the placeholder rather than a made-up 0.0 ms.
    const double statusRate = audioEngine.getTransport().getPositionSnapshot().sampleRate;
    const double roundTripMs =
        statusRate > 0.0 ? 1000.0 * (double)audioEngine.getRecordingLatencySamples() / statusRate : 0.0;
    statusBar.updateRoundTripLatency(roundTripMs, !audioEngine.isHosted());
}

// ---- Hosted-plugin latency compensation ----

void MainComponent::installHostedPluginObservers() {
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor());
        if (hosted == nullptr)
            continue;

        // Two SEPARATE slots, neither of them onInstanceChanged — that one belongs to
        // HostedPluginEditorWindow and reassigning it here would close the user's plugin
        // window on the next graph change.
        hosted->onLatencyChanged = [this] { rebuildGraphForLatencyChange(); };
        hosted->onInstancePublished = [this] {
            // A lane bound to a hosted-plugin parameter cannot resolve until the instance exists,
            // so re-run the reconcile when an async load completes — otherwise the lane sits
            // orphaned until some unrelated graph edit happens to trigger the next pass.
            reconcileTimelineBindingsOnly();
            // ...and a publish takes the node's latency 0 -> N, so the graph's compensation delays
            // are stale for exactly the same reason a runtime change leaves them stale.
            rebuildGraphForLatencyChange();
        };
    }
}

void MainComponent::rebuildGraphForLatencyChange() {
    // juce::AudioProcessorGraph bakes {bus layout, latencySamples} per node into
    // its render sequence and only re-derives the parallel-path compensation delays when that
    // sequence is rebuilt — so without this, a plugin reporting 512 samples of lookahead is simply
    // uncompensated and its branch of the patch drifts against every parallel one. rebuild() is
    // public, message-thread-safe (it dispatches to the message thread if called from anywhere
    // else) and a no-op when nothing about the sequence actually changed.
    //
    // Host-agnostic: in a plugin build this is our own INNER graph, which we own in both modes.
    audioEngine.getGraph().rebuild();

    // The graph term of AudioEngine::getRecordingLatencySamples() just moved, so the status bar's
    // number is wrong until its next poll. Same feed as that poll, not a second one.
    updateRoundTripLatencyReadout();
}

void MainComponent::aiPatchAboutToApply() {
    // Runs synchronously before the AI patch clears/rebuilds the graph. Detach module components now so
    // their ScopeComponent timers stop and no component references a soon-to-be-freed VisualBuffer.
    graphEditor.detachAllModuleComponents();
    // Everything the apply writes into parameters is programmatic. Assignment closes any scope an
    // earlier, failed apply abandoned (see the member's comment).
    aiApplyScope = std::make_unique<ProgrammaticApplyScope>(*this);
}

void MainComponent::aiPatchApplied() {
    aiApplyScope.reset();
    // The graph is fully applied by the time this fires (on the initial apply AND on undo/redo,
    // which reuse this pair as their restore hooks), so bindings can be reconciled straight away —
    // no need to wait for the async updateComponents() below.
    reconcileTimelineAfterGraphChange();
    setCurrentPatchName("AI Patch");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->graphEditor.updateComponents();
    });
}

void MainComponent::simulateLoadFactoryPresetForTest(int index) {
    auto presets = synth::PresetManager::getPresetList();
    if (index < 0 || index >= presets.size())
        return;
    loadFactoryPresetAtIndex(index);
    setCurrentPatchName(presets[index].name);
}

void MainComponent::loadFactoryPresetAtIndex(int index) {
    ProgrammaticApplyScope guard(*this);
    graphEditor.loadFactoryPreset(index);
    reconcileTimelineAfterGraphChange();
    // A factory preset is not the bundle that was open, so the next Cmd+S must prompt for a new
    // location rather than silently overwrite it — same reasoning as the newPatch case's "A new
    // document is not the old bundle" comment.
    currentBundleDir_ = juce::File();
    refreshAssetRoots();
    // NOT markDocumentClean(), unlike newPatch/open/save. A factory preset replaces the GRAPH and
    // leaves the live timeline exactly where it was, so "this document now matches something on
    // disk" would be a lie the moment the timeline holds anything: an unsaved arrangement would
    // survive the load with the dirty flag cleared, and quitting after that would discard it
    // without ever asking. The load records no undo transaction of its own (it goes through
    // PresetManager, not AppUndoManager), so leaving the flag alone is also accurate in the other
    // direction - a clean document stays clean, a dirty one stays dirty.
}

// Guards BEFORE the dialog opens — the chooser itself is the post-guard half, below.
void MainComponent::openPresetFromFile() {
    guardUnsavedChanges("Opening another project", [this] { launchOpenPresetChooser(); });
}

void MainComponent::launchOpenPresetChooser() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Preset", synth::ProjectBundle::getDefaultProjectsDirectory(), kPatchFileFilter);
    // A `.agsproj` bundle is a DIRECTORY, not a file, so the browser has to allow picking one; a
    // plain `.json` preset is still an ordinary file pick. openFromFile() branches on what comes
    // back, so the two cases never depend on which flag the platform's dialog honoured.
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    flags |= juce::FileBrowserComponent::canSelectDirectories;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            openFromFile(file);
    });
}

// Cmd+S's decision (see the header comment): resave silently to the remembered bundle when one is
// open and the caller isn't forcing the chooser, otherwise prompt. The suggested name defaults to
// `.agsproj` — not because the filter forbids `.json` (it still lists both, and saveToFile still
// branches on whatever extension comes back), but because a first-time saver who just hits Enter
// should land on the bundle format, which is what actually keeps the timeline.
void MainComponent::performSaveProject(bool forceChooser, std::function<void(bool saved)> onFinished) {
    if (!forceChooser && currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        const bool ok = saveToFile(currentBundleDir_);
        if (onFinished)
            onFinished(ok);
        return;
    }

    const auto suggested = synth::ProjectBundle::getDefaultProjectsDirectory().getChildFile(
        currentPatchName_ + synth::ProjectBundle::kBundleExtension);
    fileChooser = std::make_unique<juce::FileChooser>("Save Project", suggested, kPatchFileFilter);
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this, onFinished](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File{}) {
            if (onFinished)
                onFinished(false);
            return;
        }
        const bool ok = saveToFile(file);
        if (onFinished)
            onFinished(ok);
    });
}

// The legacy patch-only export — see the header comment for why this calls graphEditor.savePreset
// directly rather than saveToFile: exporting a snapshot from an open bundle must never look like
// the project itself was (re)saved.
void MainComponent::exportPatchOnly(const juce::File& file) {
    graphEditor.savePreset(file);
    statusBar.showMessage("Exported patch: " + file.getFileNameWithoutExtension());
}

void MainComponent::promptExportPatchOnly() {
    const auto suggested =
        resolveExportSubdirectory(currentBundleDir_, kPatchesFolderName).getChildFile(currentPatchName_ + ".json");
    fileChooser = std::make_unique<juce::FileChooser>("Export Patch Only", suggested, "*.json");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            exportPatchOnly(file);
    });
}

// The offline bounce/export flow (P8-5): show the options dialog, then drive a BounceRunner from
// what it reports. See Source/Transport/BounceRunner.h and Source/UI/ExportAudioDialog.h for why
// the render is chunked rather than blocking, and docs/architecture.md for the full design.
void MainComponent::promptExportAudio() {
    if (isBounceInProgress_)
        return; // the command is reported inactive while one is running - see getCommandInfo.

    const double arrangementEndBeat = timelineDoc.getArrangementEndBeat();
    const auto position = audioEngine.getTransport().getPositionSnapshot();
    // "Current loop range" is offered as a bounce range whenever the loop LOCATORS describe a
    // non-degenerate region, independent of whether looping is currently ARMED (P8-17). The region
    // is the source, not the live loop: a disengaged loop still names a real span. TransportService
    // always carries a valid [start, end) (its own default is [0, 4)), so only a collapsed region
    // (end <= start) disables the option; there is no separate "locators unset" state to detect. A
    // bounce renders that span linearly regardless (BounceExporter unloops for the duration and
    // restores it), so arming state never changes what lands in the file.
    const bool hasLoopRange = position.loopEndPpq > position.loopStartPpq;
    const bool projectIsSaved = currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_);

    auto* dialog = new synth::ui::ExportAudioDialog(
        arrangementEndBeat, hasLoopRange, position.loopStartPpq, position.loopEndPpq, position.bpm, projectIsSaved,
        resolveExportSubdirectory(currentBundleDir_, kExportsFolderName), currentPatchName_);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Export Audio";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    auto* window = options.launchAsync();
    exportDialog_ = dialog;

    dialog->onRequestClose = [window] {
        if (window != nullptr)
            window->exitModalState(0);
    };
    dialog->onCancelRender = [this] {
        if (bounceRunner_ != nullptr)
            bounceRunner_->cancel();
    };
    dialog->onExport = [this, dialog](synth::BounceOptions bounceOptions, juce::File destination) {
        // Publish unconditionally right before rendering rather than gate on "was it ever
        // published": publishTimeline is cheap and always correct to re-call (see its own header
        // comment), and this makes a stale-binding bug impossible instead of merely detected.
        publishTimelineAndRebindRecorder();

        isBounceInProgress_ = true;
        dialog->showProgressPage();

        bounceRunner_ = std::make_unique<synth::BounceRunner>(audioEngine, destination, bounceOptions,
                                                              [this](synth::BounceResult result) {
                                                                  isBounceInProgress_ = false;
                                                                  bounceRunner_.reset();
                                                                  // exportDialog_ is a SafePointer: if the window was
                                                                  // somehow closed while the render was still going (a
                                                                  // bounce keeps running to completion regardless - it
                                                                  // is owned by MainComponent, not by the dialog), this
                                                                  // is simply null rather than dangling, and the two
                                                                  // lines above are still what matters: the flag clears
                                                                  // and the next Export Audio is not permanently locked
                                                                  // out.
                                                                  if (exportDialog_ != nullptr)
                                                                      exportDialog_->reportComplete(result);
                                                                  statusBar.showMessage(result.message);
                                                              });
    };

    // Genuinely modal, not just visible - New Patch/Open/Load preset/Quit refuse to run while
    // isBounceInProgress_ is true (see guardUnsavedChanges), but nothing stops the user from
    // reaching them if the window itself is merely floating. enterModalState's `deleteWhenDismissed`
    // means the window (and dialog) are freed once exitModalState() runs above.
    window->enterModalState(true, nullptr, true);
}

// ---- Save / open: one `.json` preset path, one `.agsproj` bundle path ----

bool MainComponent::saveToFile(const juce::File& file) {
    statusBar.showMessage("Saving...");

    if (file.getFileExtension() == synth::ProjectBundle::kBundleExtension) {
        // Adopt any Recordings/-convention takes (recorded before this project had ever
        // been saved) into THIS bundle's own Audio/ BEFORE serialising below, so project.json is
        // written with the post-adoption refs — the reserved Recordings/ prefix must never end up
        // inside a saved bundle. A plain, direct doc mutation: saving must never create undo
        // history (see synth::AssetManager::adoptRecordingsAssets's own comment). Safe to call
        // every save, including a resave with nothing left to adopt (a no-op — see that method).
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(kRecordingsFolderName);
        synth::AssetManager::adoptRecordingsAssets(timelineDoc, recordingsRoot, file);

        // The bundle carries the graph, timeline AND macros; PatchDocument comes from the graph
        // editor so the unknown-top-level-key stash a plain preset load filled is re-merged here too.
        const auto result = synth::ProjectBundle::save(file, audioEngine.getGraph(), timelineDoc,
                                                       graphEditor.getPatchDocument(), graphEditor.getMacros());
        if (!result.ok) {
            statusBar.showMessage("Save failed: " + result.message);
            return false;
        }
        // From here on this document IS a bundle, so the next take is written into it
        // (Audio/ + Peaks/) rather than into app data.
        currentBundleDir_ = file;
        refreshAssetRoots(); // Clip playback resolves against the bundle we just became
        // A fresh explicit save supersedes any pending autosave sidecar — project.json now carries
        // everything the sidecar would have offered to recover.
        synth::ProjectBundle::discardAutosave(file);
        recentProjects.addProject(file);
        saveRecentProjects();
        // Clear BEFORE setCurrentPatchName, which is what fires the title notify — the notify must
        // see the just-saved, clean state.
        markDocumentClean();
        setCurrentPatchName(file.getFileNameWithoutExtension());
        statusBar.showMessage("Saved: " + file.getFileNameWithoutExtension());
        return true;
    }

    // Plain preset save — byte-identical to what it has always written.
    graphEditor.savePreset(file);
    markDocumentClean();
    setCurrentPatchName(file.getFileNameWithoutExtension());
    statusBar.showMessage("Saved: " + file.getFileNameWithoutExtension());
    return true;
}

bool MainComponent::openFromFile(const juce::File& file) {
    statusBar.showMessage("Loading preset...");

    if (file.isDirectory() || file.getFileExtension() == synth::ProjectBundle::kBundleExtension) {
        if (!synth::ProjectBundle::isBundle(file)) {
            statusBar.showMessage("Not a project bundle: " + file.getFileName());
            return false;
        }

        // A prior session left a sidecar this bundle's project.json has never seen (autosave, or a
        // crash before the next explicit save) — ask BEFORE either file loads, rather than loading
        // project.json and silently discarding a possibly-newer autosave. Asynchronous, so this
        // reports "handled" rather than the eventual load's own success/failure; the only reader of
        // openFromFile's return today (openProjectForTest) is exercised exclusively by tests that
        // don't pre-seed a sidecar, so this branch changes nothing about any existing synchronous
        // assertion — new autosave-recovery tests drive autosaveRecoveryPrompt directly instead, the
        // same idiom promptUnsavedChanges's own tests already use.
        if (synth::ProjectBundle::hasAutosave(file)) {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            promptAutosaveRecovery([safeThis, file](AutosaveRecoveryChoice choice) {
                if (auto* self = safeThis.getComponent())
                    self->applyAutosaveRecoveryAnswer(choice, file);
            });
            return true;
        }

        return loadBundleFromFile(file);
    }

    ProgrammaticApplyScope guard(*this);
    graphEditor.loadPreset(file);
    reconcileTimelineAfterGraphChange();
    // A legacy patch is not a bundle, so the document that is now open has no bundle to resave to;
    // leaving the previous bundle's path installed would make the next Cmd+S overwrite a project
    // this patch was never part of, timeline included. (The .agsproj branch above already sets
    // currentBundleDir_ = file, so only this plain-preset tail needs the reset.)
    currentBundleDir_ = juce::File();
    refreshAssetRoots();
    markDocumentClean();
    setCurrentPatchName(file.getFileNameWithoutExtension());
    statusBar.showMessage("Loaded: " + file.getFileNameWithoutExtension());
    return true;
}

bool MainComponent::loadBundleFromFile(const juce::File& bundleDir) {
    ProgrammaticApplyScope guard(*this);
    // Detach BEFORE the load frees the current graph's processors — the same ordering
    // GraphEditor::loadPreset uses, and for the same reason (a live ScopeComponent timer would
    // otherwise read a freed VisualBuffer).
    graphEditor.detachAllModuleComponents();

    // The roots move to the new bundle BEFORE the load, not after it. ProjectBundle::load moves
    // the timeline into the live doc, and that fires timelineChanged synchronously — publishing
    // to the engine and the clip streamer while the load is still running. With the old roots
    // still installed, that publish resolves this bundle's clip refs against the PREVIOUS
    // bundle's Audio/ folder: the wrong file, or silence. Takes recorded from here on belong to
    // this bundle for the same reason.
    const juce::File previousBundleDir = currentBundleDir_;
    currentBundleDir_ = bundleDir;
    refreshAssetRoots();

    const auto result = synth::ProjectBundle::load(bundleDir, audioEngine.getGraph(), timelineDoc,
                                                   graphEditor.getPatchDocument(), graphEditor.getMacros());
    // Reconcile the view whatever happened: on failure the load left the graph exactly as it
    // was, and the components still have to come back after the detach above.
    graphEditor.updateComponents();
    if (!result.ok) {
        // load() is all-or-nothing, so a failure has to leave the previous project intact —
        // roots included, or the still-open document would start resolving its clips against a
        // bundle it was never part of.
        currentBundleDir_ = previousBundleDir;
        refreshAssetRoots();
        statusBar.showMessage("Load failed: " + result.message);
        return false;
    }

    // ProjectBundle::load already reconciled once; this republishes the freshly loaded document
    // (and rebinds the recorder) against the graph as it now stands.
    reconcileTimelineAfterGraphChange();
    markDocumentClean();
    recentProjects.addProject(bundleDir);
    saveRecentProjects();
    setCurrentPatchName(bundleDir.getFileNameWithoutExtension());
    statusBar.showMessage("Loaded: " + bundleDir.getFileNameWithoutExtension());
    return true;
}

bool MainComponent::loadAutosaveFromFile(const juce::File& bundleDir) {
    ProgrammaticApplyScope guard(*this);
    graphEditor.detachAllModuleComponents();

    const juce::File previousBundleDir = currentBundleDir_;
    currentBundleDir_ = bundleDir;
    refreshAssetRoots();

    const auto result = synth::ProjectBundle::loadAutosave(bundleDir, audioEngine.getGraph(), timelineDoc,
                                                           graphEditor.getPatchDocument(), graphEditor.getMacros());
    graphEditor.updateComponents();
    if (!result.ok) {
        currentBundleDir_ = previousBundleDir;
        refreshAssetRoots();
        statusBar.showMessage("Recovery failed: " + result.message);
        return false;
    }

    reconcileTimelineAfterGraphChange();
    // Deliberately NOT markDocumentClean(): the recovered state is not what's on disk (project.json
    // still holds the older, last-explicitly-saved content), so the document must read as dirty —
    // see the header comment on loadAutosaveFromFile for why isDirty_ is written directly here
    // rather than through the usual recompute-from-serial path.
    isDirty_ = true;
    // Rebase both autosave baselines to this instant: the in-memory state now exactly matches what
    // the (about to be discarded) sidecar held, so nothing "new" exists to autosave yet — the next
    // autosave should only fire once the user edits further, same as right after an explicit save.
    lastAutosavedEditSerial_ = undoManager.getEditSerial();
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
    recentProjects.addProject(bundleDir);
    saveRecentProjects();
    // setCurrentPatchName() calls notifyDocumentTitleChanged() at its end and nowhere else in this
    // file (see that function's comment) — isDirty_ is set BEFORE this call so the notify's " *"
    // marker reflects the just-restored dirty state, not a stale one.
    setCurrentPatchName(bundleDir.getFileNameWithoutExtension());
    statusBar.showMessage("Recovered unsaved changes: " + bundleDir.getFileNameWithoutExtension());
    return true;
}

void MainComponent::applyAutosaveRecoveryAnswer(AutosaveRecoveryChoice choice, const juce::File& bundleDir) {
    if (choice == AutosaveRecoveryChoice::Restore) {
        // A corrupt/invalid sidecar must not strand the user on whatever was open before, nor
        // silently destroy the only copy of the data it held: on failure, fall back to the normal
        // load and keep the sidecar so the user isn't left with neither the restore nor the file.
        if (loadAutosaveFromFile(bundleDir))
            synth::ProjectBundle::discardAutosave(bundleDir);
        else
            loadBundleFromFile(bundleDir);
        return;
    }
    // Discard: the sidecar is stale/unwanted either way, so it goes before the normal load runs —
    // a load failure here must not leave a discarded-but-still-on-disk sidecar behind.
    synth::ProjectBundle::discardAutosave(bundleDir);
    loadBundleFromFile(bundleDir);
}

void MainComponent::promptAutosaveRecovery(std::function<void(AutosaveRecoveryChoice)> onChoice) {
    if (autosaveRecoveryPrompt) {
        autosaveRecoveryPrompt(std::move(onChoice));
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Recover Unsaved Changes")
                       .withMessage("An autosave from a previous session was found for this project. "
                                    "Restore it, or discard it and open the last saved version?")
                       .withButton("Restore")
                       .withButton("Discard");
    // ASYNC, never a modal loop — same reasoning as promptUnsavedChanges.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, onChoice](int result) {
        if (safeThis.getComponent() == nullptr)
            return;
        // juce::AlertWindow::showAsync's documented TWO-button result convention (see its header
        // comment — different from the three-button one promptUnsavedChanges uses): button[0]
        // ("Restore") returns 1, button[1] ("Discard") returns 0 — and a dismissed/closed window
        // ALSO returns 0, which lands on Discard here. That is deliberately the non-destructive arm:
        // project.json, the last known-good state, is what a dismissed prompt falls back to, never a
        // silent Restore the user never asked for.
        onChoice(result == 1 ? AutosaveRecoveryChoice::Restore : AutosaveRecoveryChoice::Discard);
    });
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g) {
    // (Our component is opaque, so we must completely fill the background with a
    // solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    commands.addArray({AppCommands::openSettings, AppCommands::savePreset, AppCommands::saveProjectAs,
                       AppCommands::exportPatchOnly, AppCommands::exportAudio, AppCommands::openPreset,
                       AppCommands::newPatch, AppCommands::undo, AppCommands::redo, AppCommands::toggleModMatrix,
                       AppCommands::toggleMinimap, AppCommands::toggleAiPanel, AppCommands::autoArrange,
                       AppCommands::groupSelection, AppCommands::ungroupSelection, AppCommands::collapseMacro,
                       AppCommands::toggleLibrary, AppCommands::selectAllModules, AppCommands::saveSnippet,
                       AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection,
                       AppCommands::cutSelection,
                       // Registered unconditionally alongside togglePlayback below even though only
                       // the timeline surfaces implement it — reported inactive rather than dropping
                       // the row from Settings.
                       AppCommands::repeatSelection,
                       // Registered unconditionally (like every command above), reported inactive
                       // rather than dropping it from the Settings shortcut list entirely.
                       AppCommands::togglePlayback,
                       // The grid block and both zoom pairs follow the same rule: registered in
                       // every build configuration, reported inactive where there is nothing to act
                       // on (see getCommandInfo). The ten grid commands act on the timeline's
                       // shared snap value, so they are inactive whenever the panel is not on
                       // screen; the four zoom commands route per focused surface.
                       AppCommands::snapSetWhole, AppCommands::snapSetHalf, AppCommands::snapSetQuarter,
                       AppCommands::snapSetEighth, AppCommands::snapSetSixteenth, AppCommands::snapSetThirtySecond,
                       AppCommands::snapSetSixtyFourth, AppCommands::snapSetHundredTwentyEighth,
                       AppCommands::snapCyclePrev, AppCommands::snapCycleNext, AppCommands::zoomInHorizontal,
                       AppCommands::zoomOutHorizontal, AppCommands::zoomInVertical, AppCommands::zoomOutVertical});
    commands.add(AppCommands::toggleTimelinePanel);
#if JUCE_MAC || JUCE_WINDOWS
    commands.add(AppCommands::checkForUpdates);
#endif
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) {
    switch (commandID) {
    case AppCommands::openSettings: {
        result.setInfo("Open Settings", "Open the settings window", "General", 0);
        auto kp = shortcutManager.getBinding("openSettings");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::savePreset: {
        result.setInfo("Save Preset", "Save the current preset", "General", 0);
        auto kp = shortcutManager.getBinding("savePreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::saveProjectAs: {
        result.setInfo("Save Project As...", "Save the project to a new location", "General", 0);
        auto kp = shortcutManager.getBinding("saveProjectAs");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::exportPatchOnly: {
        result.setInfo("Export Patch Only (.json)...",
                       "Save just the patch, without the timeline, as a plain JSON preset", "General", 0);
        auto kp = shortcutManager.getBinding("exportPatchOnly");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::exportAudio: {
        result.setInfo("Export Audio...", "Bounce the arrangement or the current loop range to a WAV or AIFF file",
                       "General", 0);
        auto kp = shortcutManager.getBinding("exportAudio");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        // Greyed out rather than re-entrant: only one bounce (and one modal progress window) at a
        // time - see isBounceInProgress_.
        result.setActive(!isBounceInProgress_);
        break;
    }
    case AppCommands::openPreset: {
        result.setInfo("Open Preset", "Open a preset file", "General", 0);
        auto kp = shortcutManager.getBinding("openPreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::newPatch: {
        result.setInfo("New Patch", "Clear the canvas and start a new patch", "General", 0);
        auto kp = shortcutManager.getBinding("newPatch");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::undo: {
        result.setInfo("Undo", "Undo the last action", "Edit", 0);
        auto kp = shortcutManager.getBinding("undo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::redo: {
        result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
        auto kp = shortcutManager.getBinding("redo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleModMatrix: {
        result.setInfo("Toggle Mod Matrix", "Toggle the modulation matrix panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleModMatrix");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleMinimap: {
        result.setInfo("Toggle Minimap", "Toggle the graph editor minimap overlay", "View", 0);
        auto kp = shortcutManager.getBinding("toggleMinimap");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleAiPanel: {
        result.setInfo("Toggle AI Panel", "Toggle the AI chat panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleAiPanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::autoArrange: {
        result.setInfo("Auto Arrange", "Auto-arrange modules by signal flow", "View", 0);
        auto kp = shortcutManager.getBinding("autoArrange");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleLibrary: {
        result.setInfo("Toggle Module Library", "Toggle the module library sidebar", "View", 0);
        auto kp = shortcutManager.getBinding("toggleLibrary");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::selectAllModules: {
        // Still AppCommands::selectAllModules / actionId "selectAllModules" — the NAME is frozen so
        // a persisted user binding keeps resolving, but the verb now means "select everything in
        // the focused editor" and routes like Cmd+C/V/D below. Deliberately left always-active on
        // every surface: unlike the clipboard verbs it needs no pre-existing selection, and each
        // surface's own selectAll* returns false harmlessly when there is nothing to select.
        result.setInfo("Select All", "Select everything in the focused editor", "Edit", 0);
        auto kp = shortcutManager.getBinding("selectAllModules");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::saveSnippet: {
        result.setInfo("Save Selection as Snippet", "Save the selected modules as a reusable snippet", "Edit", 0);
        result.setActive(graphEditor.getSelectionCount() > 0);
        auto kp = shortcutManager.getBinding("saveSnippet");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::groupSelection: {
        // Static label — Cmd+G now dispatches to whichever verb applies (P8-14,
        // GraphEditor::groupOrToggleSelectionMacros), so the label can't claim to be only one of
        // them. Mirrors collapseMacro's "static label covers both directions" reasoning above.
        result.setInfo("Group / Toggle Macro",
                       "Group the selection into a new Macro, or toggle collapse/expand if it already touches one",
                       "Edit", 0);
        // Active whenever EITHER branch of the dispatch could do something: enough modules to
        // group, or the selection touches at least one macro to toggle (mirrors collapseMacro's
        // gate below).
        bool touchesAnyMacro = false;
        for (auto nodeId : graphEditor.getSelectedNodes()) {
            if (graphEditor.macroForNode(nodeId) != nullptr) {
                touchesAnyMacro = true;
                break;
            }
        }
        result.setActive(graphEditor.getSelectionCount() > 1 || touchesAnyMacro);
        auto kp = shortcutManager.getBinding("groupSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::ungroupSelection: {
        result.setInfo("Ungroup Macro", "Dissolve the macro the selection belongs to, keeping its modules", "Edit", 0);
        result.setActive(graphEditor.getSelectionCount() > 0);
        auto kp = shortcutManager.getBinding("ungroupSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::collapseMacro: {
        result.setInfo("Collapse / Expand Macro", "Toggle the collapsed state of the macro the selection belongs to",
                       "Edit", 0);
        // toggleSelectionMacrosCollapsed() itself refuses (with a status message) when the
        // selection touches no macro at all — this gate is the more precise "touches ANY macro"
        // check (not just "there's a selection"), since a plain non-macro selection can never
        // succeed here either.
        bool touchesAnyMacro = false;
        for (auto nodeId : graphEditor.getSelectedNodes()) {
            if (graphEditor.macroForNode(nodeId) != nullptr) {
                touchesAnyMacro = true;
                break;
            }
        }
        result.setActive(touchesAnyMacro);
        auto kp = shortcutManager.getBinding("collapseMacro");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::copySelection: {
        result.setInfo("Copy", "Copy the selection in the focused editor", "Edit", 0);
        // Routed by resolveEditSurface() — Graph's own behaviour (below) is unchanged; each
        // timeline surface gates on its own selection.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.getClipSelection().size() > 0);
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("copySelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::pasteSelection: {
        result.setInfo("Paste", "Paste into the focused editor", "Edit", 0);
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.canPasteClips());
            break;
        case EditSurface::PianoRoll:
            // canPasteNotes() is BOTH halves: a non-empty note clipboard AND an open clip. A roll
            // with nothing open has nowhere to put the block, so the row greys out rather than
            // silently discarding a paste.
            result.setActive(timelinePanel.getPianoRoll().canPasteNotes());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.canPaste());
            break;
        }
        auto kp = shortcutManager.getBinding("pasteSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::duplicateSelection: {
        result.setInfo("Duplicate", "Duplicate the selection in the focused editor", "Edit", 0);
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.getClipSelection().size() > 0);
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("duplicateSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::cutSelection: {
        result.setInfo("Cut", "Cut the selection in the focused editor", "Edit", 0);
        // Same enablement predicate Copy uses on every surface — a cut is a copy that also
        // deletes, so anything copyable is cuttable and the two rows can never disagree.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.canCutClips());
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("cutSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::repeatSelection: {
        result.setInfo("Repeat", "Repeat the selection a number of times", "Edit", 0);
        // The ONLY edit verb that is inactive on the Graph surface: "repeat N times, each copy one
        // block further along" is a time-axis idea, and a spatial canvas has no such axis — the
        // graph's answer to "another one of these" is Duplicate. See performRepeatSelection.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.hasClipSelection());
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(false);
            break;
        }
        auto kp = shortcutManager.getBinding("repeatSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::togglePlayback: {
        result.setInfo("Toggle Playback", "Play or stop the timeline transport", "Transport", 0);
        // Space is GLOBAL — no resolveEditSurface() branch, unlike C/V/D above.
        auto kp = shortcutManager.getBinding("togglePlayback");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    // ---- Grid division: eight absolute setters plus the two-step cycle ----
    // One block, one enablement rule: the grid is a property of the timeline's view state, so these
    // are active exactly when the panel is on screen. Not routed by resolveEditSurface() — the snap
    // value is SHARED by the clip lanes and the piano roll (one grid, whichever is in the lane
    // rect), so "which timeline surface has focus" is not a question these need to ask.
    case AppCommands::snapSetWhole:
    case AppCommands::snapSetHalf:
    case AppCommands::snapSetQuarter:
    case AppCommands::snapSetEighth:
    case AppCommands::snapSetSixteenth:
    case AppCommands::snapSetThirtySecond:
    case AppCommands::snapSetSixtyFourth:
    case AppCommands::snapSetHundredTwentyEighth:
    case AppCommands::snapCyclePrev:
    case AppCommands::snapCycleNext: {
        const auto actionId = snapActionIdForCommand(commandID);
        result.setInfo(ShortcutManager::getActionDescription(actionId), "Set the timeline's snap grid", "Timeline", 0);
        result.setActive(isTimelineVisible);
        auto kp = shortcutManager.getBinding(actionId);
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    // ---- Zoom: routed per focused surface, like the clipboard verbs ----
    case AppCommands::zoomInHorizontal:
    case AppCommands::zoomOutHorizontal:
    case AppCommands::zoomInVertical:
    case AppCommands::zoomOutVertical: {
        const auto actionId = zoomActionIdForCommand(commandID);
        const bool vertical = commandID == AppCommands::zoomInVertical || commandID == AppCommands::zoomOutVertical;
        result.setInfo(ShortcutManager::getActionDescription(actionId), "Zoom the focused editor", "View", 0);
        // The graph canvas zooms UNIFORMLY (GraphEditor::zoomAroundCentre — one zoomLevel, no
        // separate axes), so the horizontal pair drives it and the vertical pair is inactive there
        // rather than silently doing the same thing twice under a different key.
        switch (resolveEditSurface()) {
        case EditSurface::Graph:
            result.setActive(!vertical);
            break;
        case EditSurface::TimelineClips:
        case EditSurface::PianoRoll:
            result.setActive(isTimelineVisible);
            break;
        }
        auto kp = shortcutManager.getBinding(actionId);
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleTimelinePanel: {
        result.setInfo("Toggle Timeline Panel", "Toggle the bottom-docked timeline panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleTimelinePanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
#if JUCE_MAC || JUCE_WINDOWS
    case AppCommands::checkForUpdates: {
        result.setInfo("Check for Updates...", "Check for a newer version of the app", "Help", 0);
        result.setActive(updateManager.isAvailable());
        break;
    }
#endif
    default:
        break;
    }
}

bool MainComponent::perform(const InvocationInfo& info) {
    switch (info.commandID) {
    case AppCommands::openSettings:
        if (settingsButton.onClick)
            settingsButton.onClick();
        return true;
    case AppCommands::savePreset:
        performSaveProject(false);
        return true;
    case AppCommands::saveProjectAs:
        performSaveProject(true);
        return true;
    case AppCommands::exportPatchOnly:
        promptExportPatchOnly();
        return true;
    case AppCommands::exportAudio:
        promptExportAudio();
        return true;
    case AppCommands::openPreset:
        openPresetFromFile();
        return true;
    case AppCommands::newPatch:
        guardUnsavedChanges("New Patch", [this] { newPatch(); });
        return true;
    case AppCommands::undo:
        if (undoManager.canUndo())
            undoManager.undo();
        return true;
    case AppCommands::redo:
        if (undoManager.canRedo())
            undoManager.redo();
        return true;
    case AppCommands::toggleModMatrix:
        toggleModMatrixButton.triggerClick();
        return true;
    case AppCommands::toggleMinimap:
        toggleMinimapButton.triggerClick();
        return true;
    case AppCommands::toggleAiPanel:
        toggleAiPanelButton.triggerClick();
        return true;
    case AppCommands::autoArrange:
        graphEditor.autoArrange();
        return true;
    case AppCommands::groupSelection:
        graphEditor.groupOrToggleSelectionMacros();
        return true;
    case AppCommands::ungroupSelection:
        graphEditor.ungroupSelection();
        return true;
    case AppCommands::collapseMacro:
        graphEditor.toggleSelectionMacrosCollapsed();
        return true;
    case AppCommands::toggleLibrary:
        setLibraryVisible(!isLibraryVisible);
        return true;
    case AppCommands::selectAllModules: {
        // Routed by the same resolveEditSurface() the clipboard verbs use. The command id and
        // actionId keep their historical "…Modules" names (persisted bindings resolve by string),
        // but the behaviour is per-surface: Cmd+Shift+A means "everything in whatever I'm editing".
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.selectAllClips())
                statusBar.showMessage("Selected " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to select - the arrangement has no clips");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().selectAllNotes())
                statusBar.showMessage("Selected every note in the clip");
            else
                statusBar.showMessage("Nothing to select - the clip has no notes");
            return true;
        case EditSurface::Graph:
            break;
        }
        graphEditor.selectAllModules();
        statusBar.showMessage("Selected " + juce::String(graphEditor.getSelectionCount()) + " modules");
        return true;
    }
    case AppCommands::saveSnippet:
        promptSaveSnippet();
        return true;
    // Each of these reports what it did in the status bar rather than failing silently. The "did
    // nothing" branches are the residual cases only — getCommandInfo marks all three inactive when
    // there is nothing to act on, and ApplicationCommandTarget::tryToInvoke refuses an inactive
    // command outright, so the menu row greys out and the key never gets this far.
    case AppCommands::copySelection: {
        // Routed by the SAME resolveEditSurface() getCommandInfo just consulted — the
        // command manager already refused an inactive PianoRoll invocation (see
        // ApplicationCommandTarget::tryToInvoke), so the PianoRoll case below is belt-and-suspenders
        // for a caller that invokes perform() directly.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.copySelectedClips())
                statusBar.showMessage("Copied " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to copy - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().copySelectedNotes())
                statusBar.showMessage("Copied the selected notes");
            else
                statusBar.showMessage("Nothing to copy - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        if (graphEditor.copySelection())
            statusBar.showMessage("Copied " + juce::String(graphEditor.getClipboardModuleCount()) + " modules");
        else
            statusBar.showMessage("Nothing to copy - select one or more modules first");
        return true;
    }
    case AppCommands::pasteSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.pasteClipsAtPlayhead())
                statusBar.showMessage("Pasted " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to paste - copy some clips first");
            return true;
        case EditSurface::PianoRoll:
            // PRIMING, not a side effect: the roll anchors a paste on the last beat something
            // PUSHED into it via setPlayheadBeat, and a stopped transport never pushes one (the
            // playhead only animates while playing). Read the transport's live position here — the
            // same source pasteClipsAtPlayhead reads for the clip surface — so a paste with the
            // transport parked lands under the playhead the user can actually see, rather than at
            // whatever beat the last playback happened to stop pushing at.
            timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().getPositionSnapshot().ppq);
            if (timelinePanel.getPianoRoll().pasteNotesAtPlayhead())
                statusBar.showMessage("Pasted notes at the playhead");
            else
                statusBar.showMessage("Nothing to paste - copy some notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        // Counted AFTER the fact: both leave the new copies selected, so the selection is the
        // authoritative count of what actually landed (ineligible nodes never make it in).
        if (graphEditor.pasteClipboard())
            statusBar.showMessage("Pasted " + juce::String(graphEditor.getSelectionCount()) + " modules");
        else
            statusBar.showMessage("Nothing to paste - copy a selection first");
        return true;
    }
    case AppCommands::duplicateSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.duplicateSelectedClips())
                statusBar.showMessage("Duplicated " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to duplicate - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().duplicateSelectedNotes())
                statusBar.showMessage("Duplicated the selected notes");
            else
                statusBar.showMessage("Nothing to duplicate - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        if (graphEditor.duplicateSelection())
            statusBar.showMessage("Duplicated " + juce::String(graphEditor.getSelectionCount()) + " modules");
        else
            statusBar.showMessage("Nothing to duplicate - select one or more modules first");
        return true;
    }
    case AppCommands::cutSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            // The panel's own verb: copy + delete inside ONE recordTimelineChange. Never wrap it —
            // a second transaction around it would make Cmd+Z a two-step undo for one gesture.
            if (timelinePanel.cutSelectedClips())
                statusBar.showMessage("Cut clips");
            else
                statusBar.showMessage("Nothing to cut - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().cutSelectedNotes())
                statusBar.showMessage("Cut notes");
            else
                statusBar.showMessage("Nothing to cut - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        // The graph has no cut verb of its own, so it is composed here from the two that exist —
        // copySelection() fills the clipboard WITHOUT touching the graph or the undo stack, and
        // deleteSelection() then does the whole removal inside its own single
        // recordStructuralChange (the same transaction Delete and the canvas menu already use). So
        // a cut is exactly one graph-undo step, and the copy half survives the undo.
        const int cutCount = graphEditor.getSelectionCount();
        if (graphEditor.copySelection()) {
            graphEditor.deleteSelection();
            statusBar.showMessage("Cut " + juce::String(cutCount) + " modules");
        } else {
            statusBar.showMessage("Nothing to cut - select one or more modules first");
        }
        return true;
    }
    case AppCommands::repeatSelection:
        promptRepeatSelection();
        return true;
    case AppCommands::togglePlayback:
        // Reuses the transport bar's own play/stop choke point (reads the transport's CURRENT
        // playing state at click time) rather than re-deciding play-vs-stop here, so the bar's
        // button visual and a Space-bar toggle can never disagree — same triggerClick() idiom as
        // toggleModMatrix/toggleMinimap/toggleAiPanel above.
        timelinePanel.getTransportBar().getPlayStopButton().triggerClick();
        return true;
    // ---- Grid division ----
    // Every one of these goes through TimelinePanelComponent::setSnapValue / cycleSnapValue, which
    // is the panel's ONE writer for the shared snap value: the combo, these commands and the grid
    // cycle therefore share one persist and one set of repaints (see that method's comment). View
    // state only — nothing on the undo stack.
    case AppCommands::snapSetWhole:
    case AppCommands::snapSetHalf:
    case AppCommands::snapSetQuarter:
    case AppCommands::snapSetEighth:
    case AppCommands::snapSetSixteenth:
    case AppCommands::snapSetThirtySecond:
    case AppCommands::snapSetSixtyFourth:
    case AppCommands::snapSetHundredTwentyEighth: {
        using Snap = synth::ui::TimelineViewState::Snap;
        const Snap value = info.commandID == AppCommands::snapSetWhole          ? Snap::Whole
                           : info.commandID == AppCommands::snapSetHalf         ? Snap::Half
                           : info.commandID == AppCommands::snapSetQuarter      ? Snap::Quarter
                           : info.commandID == AppCommands::snapSetEighth       ? Snap::Eighth
                           : info.commandID == AppCommands::snapSetSixteenth    ? Snap::Sixteenth
                           : info.commandID == AppCommands::snapSetThirtySecond ? Snap::ThirtySecond
                           : info.commandID == AppCommands::snapSetSixtyFourth  ? Snap::SixtyFourth
                                                                                : Snap::HundredTwentyEighth;
        timelinePanel.setSnapValue(value);
        statusBar.showMessage("Grid: " + snapDivisionLabel(value));
        return true;
    }
    case AppCommands::snapCyclePrev:
    case AppCommands::snapCycleNext: {
        const int direction = info.commandID == AppCommands::snapCycleNext ? 1 : -1;
        timelinePanel.cycleSnapValue(direction);
        // Reports where the grid ENDED UP, not which way it was asked to move: the cycle clamps at
        // both ends, so a held key parked on 1/16 says so instead of implying it moved again.
        statusBar.showMessage("Grid: " + snapDivisionLabel(timelinePanel.getViewState().snap));
        return true;
    }
    // ---- Zoom ----
    case AppCommands::zoomInHorizontal:
    case AppCommands::zoomOutHorizontal:
    case AppCommands::zoomInVertical:
    case AppCommands::zoomOutVertical: {
        const bool zoomIn =
            info.commandID == AppCommands::zoomInHorizontal || info.commandID == AppCommands::zoomInVertical;
        const bool vertical =
            info.commandID == AppCommands::zoomInVertical || info.commandID == AppCommands::zoomOutVertical;
        const double factor = zoomIn ? kZoomInFactor : kZoomOutFactor;

        switch (resolveEditSurface()) {
        case EditSurface::PianoRoll:
            if (vertical)
                timelinePanel.getPianoRoll().zoomVertical(factor);
            else
                timelinePanel.getPianoRoll().zoomHorizontal(factor);
            statusBar.showMessage(vertical ? "Piano roll: vertical zoom" : "Piano roll: zoom");
            return true;
        case EditSurface::TimelineClips:
            if (vertical)
                timelinePanel.zoomTimelineVertical(factor);
            else
                timelinePanel.zoomTimelineHorizontal(factor);
            statusBar.showMessage(vertical ? "Timeline: track height" : "Timeline: zoom");
            return true;
        case EditSurface::Graph:
            break;
        }

        // The canvas has ONE zoom level (see getCommandInfo), so only the horizontal pair reaches
        // it. GraphEditor's public zoom takes a wheel DELTA rather than a factor — see
        // graphZoomWheelDeltaFor() for the conversion and why it lives in one place.
        if (vertical)
            return true; // reported inactive on Graph; belt-and-suspenders for a direct perform()
        graphEditor.zoomAroundCentre(graphZoomWheelDeltaFor(factor));
        statusBar.showMessage("Canvas: zoom");
        return true;
    }
    case AppCommands::toggleTimelinePanel:
        toggleTimelineButton.triggerClick();
        return true;
#if JUCE_MAC || JUCE_WINDOWS
    case AppCommands::checkForUpdates:
        updateManager.checkForUpdates();
        return true;
#endif
    default:
        return false;
    }
}

void MainComponent::updateCommandShortcuts() {
    commandManager.commandStatusChanged();
    // Tooltips embed the resolved key ("Save preset  (Cmd+S)"), so a rebind has to re-run them or
    // every toolbar hint — and the minimap's — keeps advertising the old binding until some
    // unrelated toggle happens to refresh it.
    applyToolbarIcons();
}

// ---- Hosted plugins ----

void MainComponent::refreshPluginLibrary() {
    moduleLibrary.setPlugins(getPluginScanService().getKnownPluginIdentities());
}

void MainComponent::savePluginScanList() {
    if (auto xml = getPluginScanService().toXml()) {
        appProperties.getUserSettings()->setValue(kPluginScanListKey, xml->toString());
        appProperties.saveIfNeeded();
    }
}

// ---- Recent projects ----

void MainComponent::saveRecentProjects() {
    if (auto xml = recentProjects.toXml()) {
        appProperties.getUserSettings()->setValue(kRecentProjectsKey, xml->toString());
        appProperties.saveIfNeeded();
    }
}

void MainComponent::startPluginScan() {
    // Never inside a host. The scan re-launches `currentExecutableFile`, which in a VST3/AU build is
    // the HOST's binary — one extra copy of the DAW per candidate plugin. The host owns plugin
    // discovery in that world anyway. The scan LIST is still loaded and installed in hosted mode
    // (see the constructor): a session that hosts a plugin has to be able to resolve its identity,
    // it just cannot rebuild the list from here.
    if (audioEngine.isHosted()) {
        statusBar.showMessage("Plugin scanning is only available in the standalone app");
        return;
    }

    if (getPluginScanService().isScanning()) {
        statusBar.showMessage("Already scanning for plugins...");
        return;
    }

    statusBar.showMessage("Scanning for plugins...");

    // Both callbacks arrive on the message thread (PluginScanService posts them), so touching the
    // status bar and the sidebar from here is safe. Progress is one message per plugin, not per
    // frame — a scan is seconds-per-plugin, so this is nowhere near the high-frequency logging /
    // repaint traps.
    getPluginScanService().scanAsync(
        synth::hostedPluginFormatNames(),
        [this](const juce::String& fileOrIdentifier, int scanned, int total) {
            // Trailing segment, not File::getFileName(): an AudioUnit's identifier is not a path.
            statusBar.showMessage("Scanning plugins " + juce::String(scanned) + "/" + juce::String(total) + ": " +
                                  fileOrIdentifier.fromLastOccurrenceOf("/", false, false));
        },
        [this](const synth::PluginScanService::Result& result) {
            savePluginScanList();
            refreshPluginLibrary();

            if (result.cancelled) {
                statusBar.showMessage("Plugin scan cancelled");
                return;
            }

            const int found = getPluginScanService().getNumKnownPlugins();
            juce::String message = "Found " + juce::String(found) + " plugin" + (found == 1 ? "" : "s");
            if (result.added > 0)
                message += " (" + juce::String(result.added) + " new)";
            if (result.failed > 0)
                message += "; " + juce::String(result.failed) + " could not be loaded and were skipped";
            statusBar.showMessage(message);
        });
}

// ---- Snippets (issue #156) ----

void MainComponent::refreshSnippetLibrary() {
    moduleLibrary.setSnippets(
        synth::SnippetManager::listSnippets(synth::SnippetManager::getDefaultSnippetsDirectory()));
}

void MainComponent::promptSaveSnippet() {
    const int selectionCount = graphEditor.getSelectionCount();
    if (selectionCount == 0) {
        statusBar.showMessage("Select one or more modules first (Shift+drag on the canvas)");
        return;
    }

    auto* window = new juce::AlertWindow("Save Snippet",
                                         "Name this group of " + juce::String(selectionCount) +
                                             (selectionCount == 1 ? " module:" : " modules:"),
                                         juce::AlertWindow::NoIcon);
    window->addTextEditor("name", {}, "Snippet name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // SafePointer, not a raw `this`: the dialog outlives this call and the window can be closed
    // while it is still open, which would otherwise leave the callback touching a destroyed
    // MainComponent. The AlertWindow itself is owned by the unique_ptr below (it added itself to
    // the desktop in its constructor, so it is visible without any extra call).
    juce::Component::SafePointer<MainComponent> safeThis(this);

    window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, window](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;

                                auto* self = safeThis.getComponent();
                                if (self == nullptr)
                                    return;

                                auto name = synth::SnippetManager::sanitiseName(owned->getTextEditorContents("name"));
                                if (name.isEmpty()) {
                                    self->statusBar.showMessage("Snippet not saved - the name was empty or unusable");
                                    return;
                                }

                                auto snippet = self->graphEditor.extractSelectionSnippet(name);
                                auto dir = synth::SnippetManager::getDefaultSnippetsDirectory();
                                if (synth::SnippetManager::saveSnippet(dir, name, snippet)) {
                                    self->refreshSnippetLibrary();
                                    self->statusBar.showMessage("Saved snippet \"" + name + "\"");
                                } else {
                                    self->statusBar.showMessage("Could not save snippet \"" + name + "\"");
                                }
                            }),
                            false);
}

// ---- Repeat ----

bool MainComponent::performRepeatSelection(int count) {
    // Clamped here as well as in the dialog: this is the public door (tests, and any future
    // scripting caller), and neither of those goes through the AlertWindow's own validation.
    const int repeats = juce::jlimit(kMinRepeatCount, kMaxRepeatCount, count);

    switch (resolveEditSurface()) {
    case EditSurface::TimelineClips:
        // Both surface verbs own their single recordTimelineChange for the WHOLE repeat — one
        // Cmd+Z undoes all N copies, so nothing here may open a transaction of its own.
        return timelinePanel.repeatSelectedClips(repeats);
    case EditSurface::PianoRoll:
        return timelinePanel.getPianoRoll().repeatSelectedNotes(repeats);
    case EditSurface::Graph:
        break;
    }
    // Graph: deliberately unsupported. Repeat tiles copies along a time axis the canvas doesn't
    // have; Duplicate is the graph's equivalent gesture, and getCommandInfo already reports the
    // command inactive here so the key never reaches this line in normal use.
    return false;
}

void MainComponent::promptRepeatSelection() {
    juce::String subject;
    switch (resolveEditSurface()) {
    case EditSurface::TimelineClips:
        if (timelinePanel.hasClipSelection())
            subject = "selected clips";
        break;
    case EditSurface::PianoRoll:
        if (timelinePanel.getPianoRoll().hasNoteSelection())
            subject = "selected notes";
        break;
    case EditSurface::Graph:
        break;
    }

    if (subject.isEmpty()) {
        statusBar.showMessage("Repeat works on the timeline - select some clips or notes first");
        return;
    }

    auto* window =
        new juce::AlertWindow("Repeat", "How many copies of the " + subject + "?", juce::AlertWindow::NoIcon);
    window->addTextEditor("count", juce::String(kMinRepeatCount), "Repeats:");
    window->addButton("Repeat", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // SafePointer + a unique_ptr taken inside the callback — promptSaveSnippet's modal idiom
    // exactly; see its comment for why the dialog can outlive this component.
    juce::Component::SafePointer<MainComponent> safeThis(this);

    window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, window](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;

                                auto* self = safeThis.getComponent();
                                if (self == nullptr)
                                    return;

                                // getIntValue() returns 0 for anything unparseable, which the
                                // clamp turns into the minimum — an empty or garbage field
                                // repeats once rather than failing with a modal error.
                                const int requested = owned->getTextEditorContents("count").getIntValue();
                                const int repeats = juce::jlimit(kMinRepeatCount, kMaxRepeatCount, requested);
                                if (self->performRepeatSelection(repeats))
                                    self->statusBar.showMessage("Repeated " + juce::String(repeats) +
                                                                (repeats == 1 ? " time" : " times"));
                                else
                                    self->statusBar.showMessage("Nothing was repeated");
                            }),
                            false);
}

// ---- Keyboard/focus arbitration ----
MainComponent::EditSurface MainComponent::resolveEditSurface() const {
    if (editSurfaceOverrideForTest_.has_value())
        return *editSurfaceOverrideForTest_;

    // A hidden panel never owns the verbs, whatever a stale focus pointer inside it points at —
    // check visibility BEFORE even asking what's focused.
    if (isTimelineVisible) {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent()) {
            if (isOrIsChildOf(focused, timelinePanel.getPianoRoll()))
                return EditSurface::PianoRoll;
            if (isOrIsChildOf(focused, timelinePanel.getClipLaneArea()))
                return EditSurface::TimelineClips;
        }
    }
    return EditSurface::Graph;
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    // The LAST stop for a key: JUCE bubbles an unhandled keyPressed up the parent chain, so
    // everything the focused surface wanted has already had its turn. Only COMMAND actions are
    // dispatched from here.
    //
    // The shortcut table now also holds SURFACE actions — bare arrows, Q/L/P, the tool digits —
    // which the components resolve themselves. Those must fall through this handler untouched: a
    // bare Left arrow that reached us means no surface claimed it, and turning it into a command
    // invocation (there is none) or swallowing it (returning true) would both be wrong. So the
    // resolution is "the first action bound to this key that HAS a command", not "the first action
    // bound to this key" — otherwise a surface id sharing a key with a command in another category
    // would shadow it, silently, in whichever order the table happened to list them.
    for (const auto& action : shortcutManager.getActionsForKeyPress(key)) {
        const auto cmdId = AppCommands::getCommandForAction(action);
        if (cmdId == AppCommands::kNoCommand)
            continue;
        return commandManager.invokeDirectly(cmdId, true);
    }

    // LAST-CHANCE FORWARD for a short, explicit list of timeline-panel surface actions.
    //
    // The bug this fixes: a surface action only runs if the focused component is inside the owning
    // panel's subtree, because that is how JUCE bubbles an unhandled key. Under the timeline panel
    // the ONLY things that take keyboard focus are the clip lane area and the piano roll — the
    // ruler, the track headers and the transport bar do not. So setting the loop locators by
    // dragging the ruler (the obvious way to do it) leaves focus on the canvas, and
    // TimelinePanelComponent::keyPressed never saw the locator-jump keystroke at all: it died here,
    // silently, because a surface action has no command to dispatch.
    //
    // Deliberately a WHITELIST of two ids rather than a blanket forward. Forwarding everything the
    // panel resolves would make its bare letters and tool digits (J/L/P/F, 1/3/4/5/7/8) fire while
    // the graph canvas has focus, which is a different feature with its own design question. These
    // two act on the transport, are chorded, and mean nothing on any other surface.
    if (isTimelineVisible) {
        static const juce::StringArray forwardsToTimelinePanel{"timelineJumpToLocator1", "timelineJumpToLocator2"};
        for (const auto& action : shortcutManager.getActionsForKeyPress(key))
            if (forwardsToTimelinePanel.contains(action))
                return timelinePanel.keyPressed(key);
    }
    return false;
}

void MainComponent::resized() {
    // CANONICAL LAYOUT (§2.4). Carve top→bottom: toolbar strip, status bar, timeline panel
    // (bottom), AI panel (right), library sidebar (left), canvas (remainder). Dimensions come
    // from the themed Metrics tokens, with literal fallbacks for the headless test path.
    //
    // Each panel's SIZE is its open fraction times its full size, NOT a binary read of its
    // visible/hidden flag (docs/layout.md §11). That is what makes this pass correct whenever it
    // runs — window resize, theme change, a timeline height drag mid-slide — and what lets a
    // toggle animate by moving the fraction and calling straight back in here (see
    // beginPanelSlide()). A fraction resting at 0 or 1 lays out pixel-identically to the old
    // binary carve; sidebarCollapsedWidth is the library's own 0.
    int tbH = 44, sbH = 24; // 44 matches Theme::Metrics::toolbarHeight's default (see Theme.h)
    int libClosedW = 0, libOpenW = 200, aiOpenW = 300;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        tbH = m.toolbarHeight;
        sbH = m.statusBarHeight;
        libClosedW = m.sidebarCollapsedWidth;
        libOpenW = m.librarySidebarWidth;
        aiOpenW = m.aiPanelWidth;
    }
    const int libW = librarySlide_.sizeBetween(libClosedW, libOpenW);
    const int aiW = aiPanelSlide_.sizeBetween(0, aiOpenW);

    auto bounds = getLocalBounds();
    auto toolbarBounds = bounds.removeFromTop(tbH);
    toolbar.setBounds(toolbarBounds);

    // Gate the Drawable clone work to narrow-mode transitions only: layoutButtons() updates
    // toolbar.isNarrowMode(); we only re-run applyToolbarIcons() when the mode actually flips.
    const bool prevNarrow = toolbarNarrowMode_;
    toolbar.layoutButtons(toolbarBounds);
    toolbarNarrowMode_ = toolbar.isNarrowMode();
    if (toolbarNarrowMode_ != prevNarrow)
        applyToolbarIcons();

    statusBar.setBounds(bounds.removeFromBottom(sbH));

    // Full-width panel carved AFTER the status bar and BEFORE the AI/library removals, so
    // it sits directly above the status bar spanning the whole window width. Its height is the
    // USER's height (not the theme metric) scaled by the fraction, so the slide is up from — and
    // back down to — a zero-height rect against a pinned bottom edge.
    //
    // The `|| isVisible()` on each panel below is the frame-0 case: a panel that has just been
    // made visible for an opening slide still measures 0 px, and must be pinned to a zero-size
    // rect at its docked edge rather than left showing the bounds it had when it was last open.
    // A panel that is both closed AND hidden is skipped entirely — its bounds are dead state, and
    // removeFrom*(0) would carve nothing from the canvas anyway.
    if (timelineSlide_.getProgress() > 0.0f || timelinePanel.isVisible()) {
        // Re-clamped every pass: the window may have shrunk since the height was set (or persisted
        // on a larger one), and the canvas must stay usable.
        timelinePanelHeight_ = clampTimelinePanelHeight(timelinePanelHeight_);
        timelinePanel.setBounds(bounds.removeFromBottom(timelineSlide_.sizeBetween(0, timelinePanelHeight_)));
    }

    if (aiW > 0 || aiChatComponent.isVisible())
        aiChatComponent.setBounds(bounds.removeFromRight(aiW));
    if (libW > 0 || moduleLibrary.isVisible())
        moduleLibrary.setBounds(bounds.removeFromLeft(libW));

    graphEditor.setBounds(bounds);
}

// ---- Toolbar icon + text application ----
void MainComponent::applyToolbarIcons() {
    using synth::theme::Icon;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // In narrow mode the buttons are 32 px wide — icon only, no text. In wide mode the
    // toggle buttons carry stateful text; the rest carry a static label.
    const bool iconOnly = toolbarNarrowMode_;

    // Uniform edge indent so every toolbar icon renders at the SAME optical size (~18 px)
    // regardless of the button's width — DrawableButton::getImageBounds() is height-bound here
    // (button height is fixed at Theme::Metrics::toolbarHeight for every slot), so one indent
    // value is all that's needed for icon-size consistency across the whole strip.
    static constexpr int kToolbarIconEdgeIndent = 8;

    // setImages no-ops (leaves the button blank) when the icon is absent (headless LnF null).
    // Builds three tinted clones from the SAME cached (muted) base — see retintIcons() — so the
    // icon glyph steps through the identical rest -> hover -> toggled-on ladder as the label text
    // AppLookAndFeel::drawDrawableButton draws (muted -> textPrimary -> accent). DrawableButton's
    // own over/down/on state resolution (getCurrentImage() et al.) picks the right clone per state.
    auto setIcon = [&](juce::DrawableButton& b, Icon id) {
        if (lf == nullptr)
            return;
        auto base = lf->getIcon(id); // fresh clone at its cached rest (muted) tint
        if (base == nullptr)
            return;

        const auto& colors = lf->getTheme().colors;
        auto hoverIcon = base->createCopy();
        hoverIcon->replaceColour(colors.textMuted, colors.textPrimary);
        auto onIcon = base->createCopy();
        onIcon->replaceColour(colors.textMuted, colors.accent);

        b.setEdgeIndent(kToolbarIconEdgeIndent);
        b.setImages(base.get(), hoverIcon.get(), hoverIcon.get(), nullptr, onIcon.get(), onIcon.get(), onIcon.get());
    };

    setIcon(toggleLibraryButton, Icon::ToggleLibrary);
    setIcon(newButton, Icon::ActionNew);
    setIcon(saveButton, Icon::ActionSave);
    setIcon(loadButton, Icon::ActionLoad);
    setIcon(settingsButton, Icon::ActionSettings);
    setIcon(feedbackButton, Icon::ActionFeedback);
    setIcon(undoButton, Icon::ActionUndo);
    setIcon(redoButton, Icon::ActionRedo);
    setIcon(autoArrangeButton, Icon::ActionAutoArrange);
    setIcon(toggleModMatrixButton, Icon::ToggleMatrix);
    setIcon(toggleMinimapButton, Icon::ToggleMinimap);
    setIcon(toggleAiPanelButton, Icon::ToggleAI);
    // No dedicated timeline glyph exists yet — reuse TransportPlay, otherwise unused this
    // phase ("scaffolding only — no DrawableButton wired"; see IconLibrary.h).
    setIcon(toggleTimelineButton, Icon::TransportPlay);
    setIcon(themeToggleButton, Icon::ThemeToggle);

    // Master-mute uses the transport-stop glyph (no real play/stop transport this phase).
    if (lf != nullptr)
        if (auto d = lf->getIcon(Icon::TransportStop))
            statusBar.getMasterMuteButton().setImages(d.get());

    // Toggle-pill state for AppLookAndFeel::drawDrawableButton (hover/press/toggled-on fill).
    // dontSendNotification: onClick already flips the visibility flag by hand; sendNotification
    // would re-fire the button's own listener and double-toggle.
    toggleLibraryButton.setToggleState(isLibraryVisible, juce::dontSendNotification);
    toggleMinimapButton.setToggleState(graphEditor.isMinimapVisible(), juce::dontSendNotification);
    toggleModMatrixButton.setToggleState(graphEditor.isModMatrixVisible(), juce::dontSendNotification);
    toggleAiPanelButton.setToggleState(isAiPanelVisible, juce::dontSendNotification);
    toggleTimelineButton.setToggleState(isTimelineVisible, juce::dontSendNotification);

    // Text: cleared in narrow mode; stateful for the toggles in wide mode.
    newButton.setButtonText(iconOnly ? "" : "New");
    saveButton.setButtonText(iconOnly ? "" : "Save");
    loadButton.setButtonText(iconOnly ? "" : "Load Presets");
    settingsButton.setButtonText(iconOnly ? "" : "Settings");
    undoButton.setButtonText(iconOnly ? "" : "Undo");
    redoButton.setButtonText(iconOnly ? "" : "Redo");
    autoArrangeButton.setButtonText(iconOnly ? "" : "Auto Arrange");
    toggleModMatrixButton.setButtonText(iconOnly ? ""
                                                 : (graphEditor.isModMatrixVisible() ? "Hide Matrix" : "Show Matrix"));
    toggleMinimapButton.setButtonText(iconOnly ? ""
                                               : (graphEditor.isMinimapVisible() ? "Hide Minimap" : "Show Minimap"));
    toggleAiPanelButton.setButtonText(iconOnly ? "" : (isAiPanelVisible ? "Hide AI" : "Show AI"));
    toggleTimelineButton.setButtonText(iconOnly ? "" : (isTimelineVisible ? "Hide Timeline" : "Show Timeline"));
    toggleLibraryButton.setButtonText(iconOnly ? "" : (isLibraryVisible ? "Hide Library" : "Show Library"));
    themeToggleButton.setButtonText(
        iconOnly ? ""
                 : (themeManager != nullptr && themeManager->getActiveTheme().isDark ? "Light Mode" : "Dark Mode"));

    // Tooltips remain available even in icon-only mode; include shortcut hints where applicable.
    auto hint = [&](const juce::String& base, const juce::String& action) {
        return synth::ui::formatShortcutHint(
            base, ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(action)));
    };

    newButton.setTooltip(hint("New patch", "newPatch"));
    saveButton.setTooltip(hint("Save preset", "savePreset"));
    loadButton.setTooltip(hint("Load preset", "openPreset"));
    settingsButton.setTooltip(hint("Open settings", "openSettings"));
    feedbackButton.setTooltip("Send feedback");
    undoButton.setTooltip(hint("Undo", "undo"));
    redoButton.setTooltip(hint("Redo", "redo"));
    autoArrangeButton.setTooltip(hint("Auto-arrange modules", "autoArrange"));
    themeToggleButton.setTooltip("Toggle light/dark mode");

    const juce::String matrixBase = graphEditor.isModMatrixVisible() ? "Hide Mod Matrix" : "Show Mod Matrix";
    toggleModMatrixButton.setTooltip(hint(matrixBase, "toggleModMatrix"));

    const juce::String minimapBase = graphEditor.isMinimapVisible() ? "Hide Minimap" : "Show Minimap";
    toggleMinimapButton.setTooltip(hint(minimapBase, "toggleMinimap"));
    // The map itself advertises the same binding on hover. MinimapComponent has no ShortcutManager
    // dependency, so the display string is resolved here and pushed down.
    graphEditor.getMinimap().setShortcutHint(
        ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding("toggleMinimap")));

    const juce::String aiBase = isAiPanelVisible ? "Hide AI Panel" : "Show AI Panel";
    toggleAiPanelButton.setTooltip(hint(aiBase, "toggleAiPanel"));

    const juce::String timelineBase = isTimelineVisible ? "Hide Timeline" : "Show Timeline";
    toggleTimelineButton.setTooltip(hint(timelineBase, "toggleTimelinePanel"));

    const juce::String libBase = isLibraryVisible ? "Hide Library" : "Show Library";
    toggleLibraryButton.setTooltip(hint(libBase, "toggleLibrary"));
}

// ---- Timeline panel height (user-resizable, persisted) ----

int MainComponent::defaultTimelinePanelHeight() const {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getTheme().metrics.timelinePanelHeight;
    return 220; // headless literal fallback, same pattern as resized()
}

int MainComponent::clampTimelinePanelHeight(int desiredHeight) const {
    const int minHeight = defaultTimelinePanelHeight();
    // Window not laid out yet: only the floor applies, so a persisted height survives construction
    // and is capped by the first real resized() instead.
    const int maxHeight =
        getHeight() > 0 ? std::max(minHeight, (getHeight() * 3) / 4) : std::max(minHeight, desiredHeight);
    return juce::jlimit(minHeight, maxHeight, desiredHeight);
}

void MainComponent::setTimelinePanelHeight(int desiredHeight, bool persist) {
    const int clamped = clampTimelinePanelHeight(desiredHeight);
    if (clamped != timelinePanelHeight_) {
        timelinePanelHeight_ = clamped;
        // One layout pass per drag callback — user-driven, so it is not a free-running repaint.
        resized();
    }
    if (persist) {
        appProperties.getUserSettings()->setValue(kTimelinePanelHeightKey, timelinePanelHeight_);
        appProperties.saveIfNeeded();
    }
}

// ---- Panel slides (one driver, three fractions) ----

const synth::ui::PanelSlide& MainComponent::panelSlide(SlidingPanel p) const noexcept {
    switch (p) {
    case SlidingPanel::Library:
        return librarySlide_;
    case SlidingPanel::AiChat:
        return aiPanelSlide_;
    case SlidingPanel::Timeline:
        break;
    }
    return timelineSlide_;
}

synth::ui::PanelSlide& MainComponent::panelSlide(SlidingPanel p) noexcept {
    return const_cast<synth::ui::PanelSlide&>(static_cast<const MainComponent*>(this)->panelSlide(p));
}

void MainComponent::beginPanelSlide() {
    // A panel that is OPENING must be visible before its first frame; a panel that is CLOSING
    // stays visible for the whole slide and disappears only in finishPanelSlide(). Hiding it here
    // instead is what used to make "close" not an animation at all — it just vanished.
    if (isLibraryVisible)
        moduleLibrary.setVisible(true);
    if (isAiPanelVisible)
        aiChatComponent.setVisible(true);
    if (isTimelineVisible)
        timelinePanel.setVisible(true);

    // No VBlank reaches an off-screen component, so an off-screen toggle has to land NOW rather
    // than wait for frames that will never arrive (headless tests; a restore before the window
    // exists). Every slide is retargeted, not just the one whose flag moved: they share a driver,
    // so restarting it must carry any slide already in flight to its own target instead of
    // stranding it half-open. Each retarget starts from the fraction's CURRENT value — a
    // mid-flight reversal, not a jump to an extreme.
    const bool canAnimate = isShowing();
    const bool libTweening = librarySlide_.retarget(isLibraryVisible ? 1.0f : 0.0f, canAnimate);
    const bool aiTweening = aiPanelSlide_.retarget(isAiPanelVisible ? 1.0f : 0.0f, canAnimate);
    const bool timelineTweening = timelineSlide_.retarget(isTimelineVisible ? 1.0f : 0.0f, canAnimate);

    if (!(libTweening || aiTweening || timelineTweening)) {
        finishPanelSlide();
        return;
    }

    // Lay out once at the fractions' current values BEFORE the first frame: a panel that just
    // became visible would otherwise be painted at the bounds it had when it was last open, in
    // the gap before the next VBlank — the flash this whole path exists to remove.
    resized();
    panelSlideAnim_.start(
        vblankUpdater, kPanelSlideMs, synth::ui::easeInOutCubic, [this](float t) { applyPanelSlideFrame(t); },
        [this] { finishPanelSlide(); });
}

void MainComponent::applyPanelSlideFrame(float t) {
    librarySlide_.applyTweenAt(t);
    aiPanelSlide_.applyTweenAt(t);
    timelineSlide_.applyTweenAt(t);
    // The single geometry authority: every panel's size comes back out of the fractions, and the
    // canvas gets whatever is left. No repaint() call — moving a child's bounds already
    // invalidates both the region it left and the one it arrived at, and a full-window repaint per
    // frame is exactly what the no-free-running-repaint rule forbids.
    resized();
}

void MainComponent::finishPanelSlide() {
    // Time-bounded by construction: the driver auto-stops at t == 1 and this drops it, so nothing
    // is left registered with the VBlank updater between slides.
    panelSlideAnim_.stop(vblankUpdater);
    librarySlide_.finish(); // pin the EXACT end fractions — the last frame need not be t == 1
    aiPanelSlide_.finish();
    timelineSlide_.finish();

    // Closed at rest: hidden only once the slide is actually done, and BEFORE the final layout so
    // a fully-closed panel keeps its bounds out of the canvas carve entirely.
    if (!isLibraryVisible)
        moduleLibrary.setVisible(false);
    if (!isAiPanelVisible)
        aiChatComponent.setVisible(false);
    if (!isTimelineVisible)
        timelinePanel.setVisible(false);

    resized();
}

// ---- Collapsible library sidebar (slides, persisted) ----
void MainComponent::setLibraryVisible(bool v) {
    isLibraryVisible = v;
    appProperties.getUserSettings()->setValue("librarySidebarVisible", v ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // Refresh the toggle button's wide-mode label, toggled-pill state and tooltip.
    if (!toolbarNarrowMode_)
        toggleLibraryButton.setButtonText(v ? "Hide Library" : "Show Library");
    toggleLibraryButton.setToggleState(v, juce::dontSendNotification);
    toggleLibraryButton.setTooltip(synth::ui::formatShortcutHint(
        v ? "Hide Library" : "Show Library",
        ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding("toggleLibrary"))));

    beginPanelSlide();
}

// ---- Alignment guides toggle (UI Phase 7 - Item 4) ----
void MainComponent::setAlignmentGuidesEnabled(bool enabled) {
    isAlignmentGuidesEnabled = enabled;
    graphEditor.setAlignmentGuidesEnabled(enabled);
}

// ---- Patch name (status bar) ----
void MainComponent::setCurrentPatchName(const juce::String& name) {
    currentPatchName_ = name;
    statusBar.repaint();
    notifyDocumentTitleChanged();
}

void MainComponent::notifyDocumentTitleChanged() {
    if (onDocumentTitleChanged)
        onDocumentTitleChanged(currentPatchName_ + (isDirty_ ? juce::String(" *") : juce::String()));
}

// The ONE way the document becomes clean — every save/load/new-document path calls this instead of
// writing isDirty_ directly. Capturing the undo manager's serial here is what makes the flag
// immune to the async change broadcast (see changeListenerCallback): the baseline is taken AFTER
// whatever edits the caller just made, so a notification still queued for those edits recomputes
// to "clean" rather than undoing this reset. Callers clear BEFORE setCurrentPatchName(), which is
// what fires the title notify — the notify has to see the settled state.
void MainComponent::markDocumentClean() {
    savedEditSerial_ = undoManager.getEditSerial();
    isDirty_ = false;
    // See the header comment: autosave's own baseline rebases here too, both fields, so the very
    // next qualifying tick doesn't fire off zero real edits or a stale elapsed-time gap.
    lastAutosavedEditSerial_ = savedEditSerial_;
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
}

bool MainComponent::isRecordingActive() const { return audioTake_.capturing || midiRecorder.isRecording(); }

void MainComponent::maybeAutosave() {
    if (!synth::ProjectBundle::isBundle(currentBundleDir_))
        return; // an unsaved project has no bundle to put a sidecar in — inert until first save.
    if (isRecordingActive())
        return;
    if (isBounceInProgress_)
        return; // see isBounceInProgress_'s comment - a bounce is chunked over timer ticks now.

    auto* settings = appProperties.getUserSettings();
    const bool enabled = settings == nullptr || settings->getBoolValue(kAutosaveEnabledKey, true);
    if (!enabled)
        return;

    if (undoManager.getEditSerial() == lastAutosavedEditSerial_)
        return; // nothing new since the last autosave (or the last explicit save/load).

    const int intervalMinutes =
        settings == nullptr ? kDefaultAutosaveIntervalMinutes
                            : settings->getIntValue(kAutosaveIntervalMinutesKey, kDefaultAutosaveIntervalMinutes);
    const juce::uint32 intervalMs = (juce::uint32)juce::jmax(1, intervalMinutes) * 60000u;
    if (juce::Time::getMillisecondCounter() - lastAutosaveMs_ < intervalMs)
        return;

    performAutosave();
    // Bumped regardless of performAutosave()'s outcome: a persistently failing write (disk full,
    // permissions) must not retry every single tick, only every interval.
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
}

void MainComponent::performAutosave() {
    auto* settings = appProperties.getUserSettings();
    const int backupCount =
        settings == nullptr
            ? kDefaultAutosaveBackupCount
            : juce::jlimit(0, 50, settings->getIntValue(kAutosaveBackupCountKey, kDefaultAutosaveBackupCount));
    const auto result =
        synth::ProjectBundle::saveAutosave(currentBundleDir_, audioEngine.getGraph(), timelineDoc,
                                           graphEditor.getPatchDocument(), graphEditor.getMacros(), backupCount);
    if (result.ok)
        lastAutosavedEditSerial_ = undoManager.getEditSerial();
    else
        statusBar.showMessage("Autosave failed: " + result.message);
}

// ---- Unsaved-changes guard ----

void MainComponent::guardUnsavedChanges(const juce::String& actionLabel, std::function<void()> proceed) {
    if (!proceed)
        return;
    if (isBounceInProgress_) {
        // New Patch/Open/Load preset/Quit all fund through here - none of them may mutate or
        // replace the graph while BounceRunner's offline driver owns it. Refuse rather than queue:
        // the export's own progress window is modal, so the user cannot even reach this path
        // without first cancelling or waiting for it to finish.
        statusBar.showMessage(actionLabel + " must wait for the export to finish, or cancel it first.");
        return;
    }
    if (!isDirty_) {
        proceed();
        return;
    }
    promptUnsavedChanges(actionLabel,
                         [this, proceed](UnsavedChangesChoice choice) { applyUnsavedChangesAnswer(choice, proceed); });
}

void MainComponent::promptUnsavedChanges(const juce::String& actionLabel,
                                         std::function<void(UnsavedChangesChoice)> onChoice) {
    if (unsavedChangesPrompt) {
        unsavedChangesPrompt(actionLabel, std::move(onChoice));
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Unsaved Changes")
                       .withMessage(actionLabel + " will discard unsaved changes to \"" + currentPatchName_ + "\".")
                       .withButton("Save")
                       .withButton("Discard")
                       .withButton("Cancel");
    // ASYNC, never a modal loop — same reasoning as PianoRollComponent::promptExtendClipToFitNotes.
    // SafePointer because the answer can arrive after this component is gone (Quit's own
    // continuation destroys it).
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, onChoice](int result) {
        // onChoice (via guardUnsavedChanges) closes over `this`, so the guard below is what keeps
        // a dismissed component from having a member function invoked on it after destruction —
        // the same SafePointer role PianoRollComponent::promptExtendClipToFitNotes's callback plays.
        if (safeThis.getComponent() == nullptr)
            return;
        // Verified against JUCE's own showAsync plumbing (build/_deps/juce-src/modules/
        // juce_gui_basics/lookandfeel/juce_LookAndFeel_V2.cpp, createAlertWindow's 3-button branch):
        // button 1 (the FIRST .withButton, "Save") returns 1, button 2 ("Discard") returns 2, button
        // 3 ("Cancel") returns 0 — and 0 is also what a dismissed/closed window returns, so anything
        // but 1 or 2 has to fall back to Cancel rather than a destructive arm.
        UnsavedChangesChoice choice = UnsavedChangesChoice::Cancel;
        if (result == 1)
            choice = UnsavedChangesChoice::Save;
        else if (result == 2)
            choice = UnsavedChangesChoice::Discard;
        onChoice(choice);
    });
}

void MainComponent::applyUnsavedChangesAnswer(UnsavedChangesChoice choice, std::function<void()> proceed) {
    switch (choice) {
    case UnsavedChangesChoice::Cancel:
        return;
    case UnsavedChangesChoice::Discard:
        // The user just explicitly said to throw these changes away — a pending autosave sidecar
        // holds exactly that same discarded content, so leaving it behind would prompt to "recover"
        // it again next time this bundle is opened. discardAutosave() is a safe no-op when
        // currentBundleDir_ is empty (never-yet-saved project) or has no sidecar.
        synth::ProjectBundle::discardAutosave(currentBundleDir_);
        proceed();
        return;
    case UnsavedChangesChoice::Save:
        // A failed save (or a cancelled chooser, which reports saved=false) must not continue —
        // the whole point of the guard is that Save only counts once it actually happened.
        performSaveProject(false, [proceed](bool saved) {
            if (saved)
                proceed();
        });
        return;
    }
}

// =============================================================================
// Timeline app wiring
// =============================================================================

void MainComponent::applyNaturalScrollingPreference() {
    // The preference is phrased POSITIVELY ("Natural scrolling", default on) because that is how
    // the OS phrases it, while the components carry the inversion flag — so this is the one place
    // the polarity is flipped. Both surfaces get the same value: one preference, not two, because a
    // user who wants their wheel flipped wants it flipped everywhere.
    const bool natural = appProperties.getUserSettings() == nullptr ||
                         appProperties.getUserSettings()->getBoolValue(kNaturalScrollingKey, true);
    timelinePanel.setScrollInverted(!natural);
    timelinePanel.getPianoRoll().setScrollInverted(!natural);
}

void MainComponent::applyZoomScrollPreference() {
    // Same shape as applyNaturalScrollingPreference above.
    //
    // Phrased POSITIVELY in Preferences ("Scroll up to zoom in", default on) while the components
    // carry the INVERSION flag, so this is the one place the polarity flips — exactly the natural-scrolling
    // idiom next door. ONE call, to the panel: setZoomScrollInverted forwards to the piano roll
    // itself (see its header comment), so reaching into getPianoRoll() here would be a second writer
    // for the same flag and the two could drift.
    const bool upZoomsIn = appProperties.getUserSettings() == nullptr ||
                           appProperties.getUserSettings()->getBoolValue(kZoomScrollUpZoomsInKey, true);
    timelinePanel.setZoomScrollInverted(!upZoomsIn);
}

void MainComponent::timelineChanged(const synth::TimelineDoc&) { publishTimelineAndRebindRecorder(); }

void MainComponent::publishTimelineAndRebindRecorder() {
    audioEngine.publishTimeline(timelineDoc);

    // The recorder's binding table is rebuilt with the SAME resolution AudioEngine::publishTimeline
    // runs for the applier's table — uuid -> node (built once, not per lane), then paramId ->
    // parameter — because a lane the applier can play back is exactly a lane the recorder must be
    // able to capture into. Anything unresolvable is simply left unbound (an orphaned lane is
    // retained in the doc, it just automates nothing).
    automationRecorder.unbindAll();

    std::map<juce::String, juce::AudioProcessorGraph::Node*> nodesByUuid;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            nodesByUuid.emplace(uuid, node);
    }

    for (const auto& track : timelineDoc.getTracks()) {
        for (const auto& lane : track.lanes) {
            if (lane.nodeUuid.isEmpty() || lane.paramId.isEmpty())
                continue;
            const auto found = nodesByUuid.find(lane.nodeUuid);
            if (found == nodesByUuid.end())
                continue;
            // The shared resolver, exactly like the applier's own binding build — a lane the
            // audio thread can play back is exactly a lane the recorder must be able to capture into.
            const auto resolved =
                synth::resolveLaneParameter(found->second->getProcessor(), lane.paramId, lane.paramIndexHint);
            if (auto* param = resolved.liveParameter())
                automationRecorder.bindLane(lane.id, param, found->second);
        }
    }
}

void MainComponent::reconcileTimelineAfterGraphChange() {
    // reconcileBindings routes through the doc's single mutation choke point when (and only when) a
    // flag actually flips, which fires timelineChanged and therefore publishes. Publishing again
    // here would be a wasted snapshot build, so this only publishes for the "nothing flipped" case
    // — where the graph still changed under us and the recorder's bindings must be re-resolved.
    if (!synth::TimelineReconciler::reconcile(timelineDoc, audioEngine.getGraph()))
        publishTimelineAndRebindRecorder();
}

void MainComponent::reconcileTimelineBindingsOnly() {
    synth::TimelineReconciler::reconcile(timelineDoc, audioEngine.getGraph());
}

void MainComponent::commitMidiRecording() {
    // stopAndCommit()'s own return just says whether a clip was created (an empty take commits
    // nothing) — not something either caller (the transport bar's Record-off click, and the 10 Hz
    // poll's auto-commit-on-stop) needs to react to differently, so it is deliberately ignored here.
    midiRecorder.stopAndCommit(timelineDoc, undoManager);
    if (midiRecorder.hadOverrun())
        statusBar.showMessage("Dropped MIDI events during recording");
    timelinePanel.getTransportBar().setRecordingState(false);
    // Every stop (explicit or auto-committed) ends any in-flight count-in pre-roll —
    // unconditional and idempotent, so a take that was never in a pre-roll to begin with just
    // clears an already-false flag.
    audioEngine.getMetronome().setForcedOn(false);
}

// ---- Audio recording ----

juce::AudioProcessorGraph::Node* MainComponent::ensureMasterRecordTap() {
    auto& graph = audioEngine.getGraph();

    // Already spliced in? THE master tap is a singleton by construction — this function is the only
    // thing that ever creates one — so the first Rec Tap found is it.
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<RecordTapModule*>(node->getProcessor()) != nullptr)
            return node;

    // The node the tap goes in FRONT of. Still a bare juce::AudioGraphIOProcessor (unlike Audio
    // Input), so it is identified by name exactly like every other lookup in the app.
    juce::AudioProcessorGraph::Node* outputNode = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == "Audio Output")
            outputNode = node;
    if (outputNode == nullptr)
        return nullptr; // nothing to record: there is no master bus

    juce::AudioProcessorGraph::Node* created = nullptr;
    // ONE compound undo step for the node, its position, and the whole re-splice. recordCombinedChange
    // pushes only the domain(s) that actually changed, so this is a single graph SnapshotAction —
    // the timeline is untouched here (the clip is a separate step, committed when the take ends).
    undoManager.recordCombinedChange(graph, timelineDoc, [&] {
        // Through the factory, not constructed ad hoc: that is what makes the node round-trip
        // through graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all
        // reproduce it. Same reasoning as createTrackInNode().
        auto processor = synth::AIStateMapper::createModule("Rec Tap");
        if (processor == nullptr)
            return;
        auto node = graph.addNode(std::move(processor));
        if (node == nullptr)
            return;
        created = node.get();

        // Ensure-uuid, mirrored into the processor in the same breath — the pairing every uuid
        // writer site keeps (see ModuleBase::setNodeUuid).
        const juce::String uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);

        const auto size = GraphEditor::estimateModuleSize("Rec Tap");
        const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
        node->properties.set("x", position.x);
        node->properties.set("y", position.y);

        // THE SPLICE. Everything that fed the output's audio channels now feeds the tap's matching
        // input, and the tap's outputs feed the output. Collected first and mutated afterwards
        // because removeConnection invalidates the list we would otherwise be iterating.
        //
        // MIDI connections into the output are left alone (a tap carries audio, not MIDI), and so
        // is anything on a channel the tap does not have — re-routing an 8-channel master through a
        // stereo tap would silently drop six channels.
        std::vector<juce::AudioProcessorGraph::Connection> intoOutput;
        for (const auto& connection : graph.getConnections()) {
            if (connection.destination.nodeID != outputNode->nodeID)
                continue;
            const int channel = connection.destination.channelIndex;
            if (channel < 0 || channel >= RecordTapModule::kNumChannels)
                continue;
            intoOutput.push_back(connection);
        }
        for (const auto& connection : intoOutput) {
            graph.removeConnection(connection);
            graph.addConnection({connection.source, {node->nodeID, connection.destination.channelIndex}});
        }
        for (int channel = 0; channel < RecordTapModule::kNumChannels; ++channel)
            graph.addConnection({{node->nodeID, channel}, {outputNode->nodeID, channel}});
    });

    graphEditor.updateComponents();
    return created;
}

RecordTapModule* MainComponent::findMasterRecordTap() const {
    auto& graph = const_cast<MainComponent*>(this)->audioEngine.getGraph();
    if (auto* node = graph.getNodeForId(audioTake_.tapNode))
        if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
            return tap;

    // The id no longer resolves — an undo/redo mid-take rebuilds the graph and renumbers nodes.
    // The tap is a singleton, so a scan still identifies it unambiguously.
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                return tap;
    return nullptr;
}

bool MainComponent::chooseTakeFiles(AudioTake& take) const {
    juce::File audioDir;
    juce::File peaksDir;
    juce::String refPrefix;

    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        audioDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kAudioSubdirName);
        peaksDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kPeaksSubdirName);
        refPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    } else {
        // An UNSAVED project has no bundle to write into, so takes land in app data and the clip's
        // assetRef carries the reserved "Recordings/" prefix, which resolves against
        // <app data>/<settings folder> rather than a bundle root. saveToFile() runs
        // synth::AssetManager::adoptRecordingsAssets BEFORE ProjectBundle::save, which
        // moves these into the bundle on the first save and rewrites the refs to "Audio/..." — a
        // project.json never carries a "Recordings/" ref. Until saved, the ref is still
        // bundle-RELATIVE in form (isValidAssetRef accepts it), which is what keeps the one path
        // rule — no absolute paths, ever — true for both cases.
        auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile(synth::branding::kSettingsFolderName)
                        .getChildFile(kRecordingsFolderName);
        audioDir = root;
        peaksDir = root;
        refPrefix = juce::String(kRecordingsFolderName) + "/";
    }

    if (!audioDir.exists() && !audioDir.createDirectory().wasOk())
        return false;
    if (!peaksDir.exists() && !peaksDir.createDirectory().wasOk())
        return false;

    // First free take number in whichever folder pair this is. Both files are checked so a take
    // never half-overwrites an earlier one.
    for (int n = 1; n <= kMaxTakeNumber; ++n) {
        const juce::String stem = "take-" + juce::String(n);
        const auto wav = audioDir.getChildFile(stem + ".wav");
        const auto peaks = peaksDir.getChildFile(stem + ".agpk");
        if (wav.exists() || peaks.exists())
            continue;
        take.wavFile = wav;
        take.peaksFile = peaks;
        take.assetRef = refPrefix + stem + ".wav";
        return true;
    }
    return false;
}

void MainComponent::commitAudioRecording() {
    if (!audioTake_.capturing)
        return;

    // Cleared FIRST: every path out of here means the take is over, and leaving the state set would
    // let the 10 Hz poll re-enter this on the next tick.
    const AudioTake take = audioTake_;
    audioTake_ = {};

    RecordTapModule::TakeResult result;
    if (auto* tap = findMasterRecordTap())
        result = tap->stopCapture();

    timelinePanel.getTransportBar().setRecordingState(false);
    // Every stop (explicit or auto-committed) ends any in-flight count-in pre-roll — unconditional
    // and idempotent, exactly like commitMidiRecording's own call.
    audioEngine.getMetronome().setForcedOn(false);

    // Nothing captured: record was disengaged during the count-in, or the tap never armed. No clip
    // and no undo step, mirroring MidiRecorder's "an empty take commits nothing" contract.
    if (!result.ok || result.lengthSamples <= 0)
        return;

    // Where the take lands. Samples -> beats through the transport's tempo. The rate,
    // bpm and round-trip latency used here are the ones FROZEN at record-on (take.captureSampleRate /
    // captureBpm / captureRecordingLatencySamples), not read live off the transport/engine — a
    // device/sample-rate change mid-take forces an early commit (see
    // AudioEngine::handleStreamFormatChange), and by the time that commit reaches here the engine may
    // already be on the NEW rate while every anchor field above (captureStartTimelineSample, the WAV
    // itself) is still in the OLD one. Reading live values would silently convert an OLD-rate sample
    // count with a NEW-rate samples-per-beat, which is wrong by exactly the rate ratio. The frozen
    // fields make this correct unconditionally: for the ordinary take (no rate change), they equal
    // the live values anyway. The whole of the arithmetic lives in synth::computeTakePlacement so it
    // can be asserted to the sample without going through this component. See
    // Source/Timeline/TakePlacement.h.
    synth::TakePlacementInput placementInput;
    placementInput.takeLengthSamples = result.lengthSamples;
    placementInput.captureStartValid = result.captureStartValid;
    placementInput.captureStartTimelineSample = result.captureStartTimelineSample;
    placementInput.captureStartBlockOffset = result.captureStartBlockOffset;
    placementInput.punchInBeat = take.punchInBeat;
    placementInput.recordingLatencySamples = take.captureRecordingLatencySamples;
    placementInput.sampleRate = take.captureSampleRate > 0.0 ? take.captureSampleRate : 44100.0;
    placementInput.bpm = take.captureBpm > 0.0 ? take.captureBpm : 120.0;
    placementInput.minClipLengthBeats = kMinAudioClipLengthBeats;

    const auto placement = synth::computeTakePlacement(placementInput);
    // Everything recorded sits before the punch (record disengaged during the count-in): the file
    // stays, the clip is not created — the same "an empty take commits nothing" rule as above.
    if (!placement.hasContent)
        return;

    // ONE undo step for the clip AND its asset binding: recordTimelineChange snapshots the doc
    // before and after the whole lambda, so the two mutations inside are a single entry.
    undoManager.recordTimelineChange(timelineDoc, [&] {
        const auto clip = timelineDoc.addClip(take.track, placement.clipStartBeat, placement.clipLengthBeats, "Take");
        if (!clip.isValid())
            return; // the track went away, or it is at kMaxClipsPerTrack: a no-op commit
        // The pre-roll (and the latency shift's overhang at timeline 0) is excluded by the WINDOW,
        // not by rewriting the WAV: the file keeps every frame that was captured.
        timelineDoc.setClipAsset(clip, take.assetRef, placement.sourceStartSeconds);
    });

    if (result.overran)
        statusBar.showMessage("Dropped audio during recording");
}

void MainComponent::clearTimelineForNewPatch() {
    if (timelineDoc.isEmpty())
        return; // clear() on an empty doc is a genuine no-op — no undo step for it either
    undoManager.recordTimelineChange(timelineDoc, [this] { timelineDoc.clear(); });
}

// The post-guard half of AppCommands::newPatch — see the command's own comment in perform() for
// why the guard has to run first. Everything below is unchanged from before the guard existed.
void MainComponent::newPatch() {
    ProgrammaticApplyScope guard(*this);
    // Two undo steps, deliberately: GraphEditor::newPatch() owns the graph's own
    // recordStructuralChange, and folding the timeline into it would mean either nesting
    // transactions or duplicating the clear. The timeline is cleared FIRST so the graph's step
    // is the newer one — Cmd+Z brings the canvas back, Cmd+Z again brings the timeline back,
    // and the post-restore reconcile re-derives the bindings after each.
    clearTimelineForNewPatch();
    graphEditor.newPatch();
    // A new document is not the old bundle, so the next take goes to app data rather
    // than into a bundle this patch no longer belongs to.
    currentBundleDir_ = juce::File();
    refreshAssetRoots(); // No bundle any more, so no bundle-relative ref resolves
    reconcileTimelineAfterGraphChange();
    markDocumentClean();
    setCurrentPatchName("Untitled");
    statusBar.showMessage("New patch");
}

juce::AudioProcessorGraph::Node* MainComponent::findNodeByUuid(const juce::String& uuid) const {
    if (uuid.isEmpty())
        return nullptr;
    for (auto* node : audioEngine.getGraph().getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

juce::String MainComponent::createTrackInNode() {
    auto& graph = audioEngine.getGraph();

    // Through the factory, not constructed ad hoc: that is what makes the node round-trip through
    // graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all reproduce it.
    auto processor = synth::AIStateMapper::createModule("Track In");
    if (processor == nullptr)
        return {};

    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return {};

    // Ensure-uuid, mirrored into the processor in the same breath. The Track In module strcmps its
    // own uuid against the snapshot's bindingUuid on the AUDIO thread, so the node property and the
    // processor's copy must never diverge — the same pairing AIStateMapper keeps at each of its
    // three uuid writer sites (see ModuleBase::setNodeUuid).
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);

    const auto size = GraphEditor::estimateModuleSize("Track In");
    const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    // Auto-wire ONLY when the answer is unambiguous: exactly one MIDI instrument in the patch. With
    // none, or with two, no wire is drawn — a chip that reads "bound" over an unwired node is fine
    // (the cable is the user's to draw), whereas guessing wrong silently plays a track through the
    // wrong instrument.
    juce::AudioProcessorGraph::Node* target = nullptr;
    int candidates = 0;
    for (auto* other : graph.getNodes()) {
        if (other == nullptr || other == node.get() || !isMidiInstrumentNode(other->getProcessor()))
            continue;
        ++candidates;
        target = other;
    }
    if (candidates == 1 && target != nullptr)
        graph.addConnection({{node->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {target->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    graphEditor.updateComponents();
    return uuid;
}

juce::String MainComponent::createTrackAudioNode() {
    auto& graph = audioEngine.getGraph();

    // Through the factory, for the same reason createTrackInNode() is: that is what makes the node
    // round-trip through graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all
    // reproduce it.
    auto processor = synth::AIStateMapper::createModule("Track Audio");
    if (processor == nullptr)
        return {};

    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return {};

    // Ensure-uuid, mirrored into the processor in the same breath. The Track Audio module strcmps
    // its own uuid against the snapshot's bindingUuid on the AUDIO thread, so the node property and
    // the processor's copy must never diverge (see ModuleBase::setNodeUuid).
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);

    const auto size = GraphEditor::estimateModuleSize("Track Audio");
    const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    // Auto-wire into the MASTER BUS. Unlike Track In's "exactly one MIDI instrument" rule this is
    // never ambiguous — the master bus is a singleton — but there are two possible sinks and the
    // order matters: if a Rec Tap has already been spliced in front of Audio Output, wiring
    // straight to the output would route this track AROUND the tap and quietly leave it out of every
    // subsequent take. Preferring the tap when one exists makes the two orderings compose: an audio
    // track added before the first take is re-spliced by ensureMasterRecordTap(), and one added
    // after it lands on the tap directly.
    juce::AudioProcessorGraph::Node* sink = nullptr;
    for (auto* other : graph.getNodes())
        if (other != nullptr && dynamic_cast<RecordTapModule*>(other->getProcessor()) != nullptr)
            sink = other;
    if (sink == nullptr)
        for (auto* other : graph.getNodes())
            if (other != nullptr && other->getProcessor() != nullptr &&
                other->getProcessor()->getName() == "Audio Output")
                sink = other;

    if (sink != nullptr)
        for (int channel = 0; channel < TimelineAudioSourceModule::kNumChannels; ++channel)
            graph.addConnection({{node->nodeID, channel}, {sink->nodeID, channel}});

    graphEditor.updateComponents();
    return uuid;
}

void MainComponent::refreshAssetRoots() {
    // The bundle root is the open .agsproj directory, or invalid when this document has never been
    // saved (in which case only "Recordings/" refs can resolve — see ProjectBundle's asset policy).
    const juce::File bundleRoot =
        (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) ? currentBundleDir_
                                                                                                 : juce::File();
    // The SAME folder chooseTakeFiles() writes unsaved-project takes into — kept in one expression
    // on each side rather than a shared helper so a change to either is visible at the other.
    const juce::File recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                          .getChildFile(synth::branding::kSettingsFolderName)
                                          .getChildFile(kRecordingsFolderName);

    audioEngine.getAudioClipStreamer().setAssetRoots(bundleRoot, recordingsRoot);
}

void MainComponent::promptRelinkClipAsset(synth::ClipId id) {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Relink Audio", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, id](const juce::FileChooser& fc) {
                                 auto file = fc.getResult();
                                 if (file != juce::File{})
                                     relinkClipAsset(id, file);
                             });
}

void MainComponent::relinkClipAsset(synth::ClipId id, const juce::File& chosenFile) {
    const auto* clip = timelineDoc.getClip(id);
    if (clip == nullptr || clip->assetRef.isEmpty())
        return;
    const juce::String oldRef = clip->assetRef;

    juce::String error;
    juce::String newRef;
    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        newRef = synth::AssetManager::importAudioFile(chosenFile, currentBundleDir_, &error);
    } else {
        // No bundle yet (unsaved project) — the SAME app-data Recordings/ convention
        // chooseTakeFiles() uses for a take recorded before the first save.
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(kRecordingsFolderName);
        const auto name = synth::AssetManager::importAudioFileToDirectory(chosenFile, recordingsRoot, &error);
        if (name.isNotEmpty())
            newRef = juce::String(kRecordingsFolderName) + "/" + name;
    }

    if (newRef.isEmpty()) {
        statusBar.showMessage("Relink failed: " + error);
        return;
    }

    // Snapshot every clip sharing the OLD ref BEFORE mutating anything — a TimelineDoc reference
    // does not survive a mutation (see TimelineDoc::getClip's own contract), so the ids and each
    // clip's OWN sourceStartSeconds are captured first rather than iterating getTracks() while
    // calling setClipAsset on it.
    struct Target {
        synth::ClipId id;
        double sourceStartSeconds;
    };
    std::vector<Target> targets;
    for (const auto& track : timelineDoc.getTracks())
        for (const auto& c : track.clips)
            if (c.assetRef == oldRef)
                targets.push_back({c.id, c.sourceStartSeconds});

    // Every sharing clip moves to the new ref together, as ONE undo step — the whole point of a
    // relink is that duplicated/copy-pasted clips naming the same asset get fixed as a unit, never
    // half-relinked. The old file is never touched, let alone deleted.
    undoManager.recordTimelineChange(timelineDoc, [&] {
        for (const auto& target : targets)
            timelineDoc.setClipAsset(target.id, newRef, target.sourceStartSeconds);
    });

    statusBar.showMessage("Relinked to " + newRef);
}

double MainComponent::audioFileLengthInBeats(const juce::File& file) const {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 0.0;

    const double seconds = (double)reader->lengthInSamples / reader->sampleRate;
    const auto snap = audioEngine.getTransport().getPositionSnapshot();
    const double bpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
    return seconds * bpm / 60.0;
}

void MainComponent::importAudioFileToClip(synth::TrackId track, double startBeat, const juce::File& sourceFile) {
    const auto* trackPtr = timelineDoc.getTrack(track);
    if (trackPtr == nullptr || trackPtr->kind != synth::TrackKind::Audio)
        return;

    // The SAME import policy relinkClipAsset() uses — a saved project imports into the bundle's
    // Audio/, an unsaved one into the app-data Recordings/ convention chooseTakeFiles() writes takes
    // into, which saveToFile() sweeps into the bundle via
    // synth::AssetManager::adoptRecordingsAssets before it ever writes project.json.
    juce::String error;
    juce::String newRef;
    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        newRef = synth::AssetManager::importAudioFile(sourceFile, currentBundleDir_, &error);
    } else {
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(kRecordingsFolderName);
        const auto name = synth::AssetManager::importAudioFileToDirectory(sourceFile, recordingsRoot, &error);
        if (name.isNotEmpty())
            newRef = juce::String(kRecordingsFolderName) + "/" + name;
    }

    if (newRef.isEmpty()) {
        // Reported and abandoned BEFORE any mutation: a failed import leaves no clip behind.
        statusBar.showMessage("Import failed: " + error);
        return;
    }

    // The clip is as long as the file (never as long as some default), floored at the same minimum
    // an audio take uses so a sub-frame file still produces a grabbable clip.
    const double lengthBeats = std::max(kMinAudioClipLengthBeats, audioFileLengthInBeats(sourceFile));

    // ONE undo step for the clip AND its asset binding, exactly like commitAudioRecording().
    undoManager.recordTimelineChange(timelineDoc, [&] {
        const auto clip =
            timelineDoc.addClip(track, std::max(0.0, startBeat), lengthBeats, sourceFile.getFileNameWithoutExtension());
        if (!clip.isValid())
            return; // the track went away, or it is at kMaxClipsPerTrack
        timelineDoc.setClipAsset(clip, newRef, 0.0);
    });

    statusBar.showMessage("Imported " + newRef);
}

int MainComponent::cleanUnusedAssets() {
    if (currentBundleDir_ == juce::File() || !synth::ProjectBundle::isBundle(currentBundleDir_))
        return 0; // nothing to sweep outside a saved bundle
    return synth::AssetManager::cleanUnusedAssets(timelineDoc, currentBundleDir_);
}

void MainComponent::automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr) {
        statusBar.showMessage("Can't automate: module not found");
        return;
    }

    // Ensure-uuid, mirrored into the processor in the same breath — the same idiom
    // createTrackInNode() and AIStateMapper use at every uuid writer site (see
    // ModuleBase::setNodeUuid). getNodeUuid() is the audio-safe mirror; the node property is the
    // canonical copy addLane keys on.
    juce::String uuid = node->properties["uuid"].toString();
    if (uuid.isEmpty()) {
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        module->setNodeUuid(uuid);
    }

    juce::RangedAudioParameter* param = nullptr;
    for (auto* p : node->getProcessor()->getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (ranged != nullptr && ranged->paramID == paramId) {
            param = ranged;
            break;
        }
    }
    if (param == nullptr) {
        statusBar.showMessage("Can't automate: parameter not found");
        return;
    }

    // find-or-create the doc's ONE Automation-kind track, then bind the lane — both in the SAME
    // mutation lambda, so creating the track (when this is the first automated parameter in the
    // whole patch) and binding the lane is ONE undo step, not two. addLane dedupes doc-wide, so a
    // repeat call for a parameter that already has a lane mutates nothing and this is a no-op.
    synth::LaneId laneId;
    const juce::String uuidCopy = uuid;
    auto mutate = [this, &laneId, uuidCopy, paramId, param] {
        synth::TrackId trackId;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.kind == synth::TrackKind::Automation) {
                trackId = track.id;
                break;
            }
        }
        if (!trackId.isValid())
            trackId = timelineDoc.addTrack(synth::TrackKind::Automation, "Automation");
        if (!trackId.isValid())
            return; // kMaxTracks reached — nothing to bind onto

        synth::AutomationLane::RangeSnapshot range;
        range.minValue = param->getNormalisableRange().start;
        range.maxValue = param->getNormalisableRange().end;
        range.defaultValue = param->convertFrom0to1(param->getDefaultValue());
        laneId = timelineDoc.addLane(trackId, uuidCopy, paramId, range);
    };
    undoManager.recordTimelineChange(timelineDoc, mutate);
    if (!laneId.isValid())
        return;

    // Reuse the toggle path exactly (same call simulateToggleTimelineClick() makes) rather than
    // duplicating what it does to isTimelineVisible/persistence/layout.
    if (!isTimelineVisible && toggleTimelineButton.onClick)
        toggleTimelineButton.onClick();
    timelinePanel.showAutomationLane(laneId);
}

// ---- TrackHeaderHost ----

std::vector<synth::ui::TrackHeaderHost::BindingOption>
MainComponent::getAvailableTrackInNodes(synth::TrackId forTrack) {
    std::vector<BindingOption> options;
    // A Track In node feeds exactly one track, so anything another track already claims is off the
    // menu. This track's own current binding stays on it (ticked), so the menu always shows where
    // it points today.
    std::set<juce::String> claimedByOtherTracks;
    for (const auto& track : timelineDoc.getTracks())
        if (!(track.id == forTrack) && track.bindingUuid.isNotEmpty())
            claimedByOtherTracks.insert(track.bindingUuid);

    // Which NODE TYPE can feed this track depends on the track's kind — a MIDI track wants a
    // Track In, an audio track wants a Track Audio. Offering the wrong one would let a user bind a
    // track to a node that structurally cannot play it (the modules match on kind as well as uuid,
    // so the result would be a track that silently plays nothing).
    const auto* track = timelineDoc.getTrack(forTrack);
    const ModuleType wantedType = (track != nullptr && track->kind == synth::TrackKind::Audio)
                                      ? ModuleType::TimelineAudioSource
                                      : ModuleType::TimelineMidiSource;

    std::vector<juce::AudioProcessorGraph::Node*> candidates;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || module->getModuleType() != wantedType)
            continue;

        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty() || claimedByOtherTracks.count(uuid) > 0)
            continue;

        candidates.push_back(node);
    }

    // "#id" is disambiguation, not identity: an option earns the suffix only when some OTHER
    // candidate in this same menu carries the same plain name. Two passes because no candidate
    // knows it needs one until the whole list is known.
    std::map<juce::String, int> nameOccurrences;
    for (auto* node : candidates)
        ++nameOccurrences[describeNodeForBinding(node)];

    for (auto* node : candidates) {
        const juce::String uuid = node->properties["uuid"].toString();
        const juce::String name = describeNodeForBinding(node);
        const juce::String display =
            nameOccurrences[name] > 1 ? name + " #" + juce::String((int)node->nodeID.uid) : name;
        options.push_back({uuid, display});
    }
    return options;
}

juce::String MainComponent::getNodeDisplayName(const juce::String& uuid) {
    return describeNodeForBinding(findNodeByUuid(uuid));
}

void MainComponent::bindTrackTo(synth::TrackId track, const juce::String& uuid) {
    if (uuid.isEmpty())
        return;
    // A user gesture, so it goes on the undo stack (unlike reconciliation, which derives runtime
    // state and is deliberately not undoable).
    undoManager.recordTimelineChange(timelineDoc, [this, track, uuid] { timelineDoc.setTrackBinding(track, uuid); });
    // Derive the orphan flag against the live graph immediately, so the chip stops reading
    // "Missing" the moment the user picks a node rather than at the next graph change.
    reconcileTimelineAfterGraphChange();
}

void MainComponent::createAndBindTrackInNode(synth::TrackId track) {
    // Kind-aware for the same reason getAvailableTrackInNodes() is — the chip's "new node"
    // entry must create the node type the track can actually be fed by.
    const auto* existing = timelineDoc.getTrack(track);
    const bool wantsAudio = existing != nullptr && existing->kind == synth::TrackKind::Audio;

    undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, track, wantsAudio] {
        const juce::String uuid = wantsAudio ? createTrackAudioNode() : createTrackInNode();
        if (uuid.isNotEmpty())
            timelineDoc.setTrackBinding(track, uuid);
    });
    reconcileTimelineAfterGraphChange();
}

void MainComponent::selectNodeInGraph(const juce::String& uuid) {
    if (auto* node = findNodeByUuid(uuid))
        graphEditor.selectModule(node->nodeID, /*additive=*/false);
}

void MainComponent::deleteTrack(synth::TrackId track) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;
    const juce::String uuid = existing->bindingUuid;

    // ONE undo step covering both domains: the track and the node that fed it disappear together,
    // and come back together.
    undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, track, uuid] {
        if (auto* node = findNodeByUuid(uuid)) {
            // Mirrors GraphEditor::requestDeleteModule: drop the mod-matrix rows before the node
            // (and the connections into it) go, then reconcile the canvas.
            graphEditor.getModMatrix().clearRows();
            audioEngine.getGraph().removeNode(node->nodeID);
            graphEditor.updateComponents();
        }
        timelineDoc.removeTrack(track);
    });
    reconcileTimelineAfterGraphChange();
}

void MainComponent::performTrackEdit(const std::function<void()>& mutation) {
    if (!mutation)
        return;
    // recordTimelineChange pushes nothing when the mutation turns out to be a no-op, so a header
    // that writes the value already in the doc costs no undo step.
    undoManager.recordTimelineChange(timelineDoc, mutation);
}

void MainComponent::addMidiTrack() {
    const int index = (int)timelineDoc.getTracks().size();

    // ONE compound undo step: the Track In node, its auto-wire, the track, its binding and its
    // colour are a single gesture, so a single Cmd+Z removes all of it.
    const bool pushed = undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, index] {
        // Doc side FIRST. addTrack refuses at kMaxTracks, and a node created before that refusal
        // stays in the graph with no track to feed — recordCombinedChange RECORDS the mutation, it
        // does not undo it. A track with no binding yet is never flagged orphaned, so the order
        // costs nothing.
        const auto trackId = timelineDoc.addTrack(synth::TrackKind::Midi, "Track " + juce::String(index + 1));
        if (!trackId.isValid())
            return; // at kMaxTracks: nothing added, and no node created
        const juce::String uuid = createTrackInNode();
        if (uuid.isNotEmpty())
            timelineDoc.setTrackBinding(trackId, uuid);
        timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());
    });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added Track " + juce::String(index + 1) : "Could not add a track");
}

void MainComponent::addAudioTrack() {
    const int index = (int)timelineDoc.getTracks().size();

    // The exact mirror of addMidiTrack(): ONE compound undo step covering the Track Audio node, its
    // auto-wire into the master bus, the track, its binding and its colour, so a single Cmd+Z
    // removes all of it.
    const bool pushed = undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, index] {
        // Doc side FIRST, for the reason addMidiTrack() spells out: a node created before the
        // kMaxTracks refusal would be left orphaned in the graph.
        const auto trackId = timelineDoc.addTrack(synth::TrackKind::Audio, "Audio " + juce::String(index + 1));
        if (!trackId.isValid())
            return; // at kMaxTracks: nothing added, and no node created
        const juce::String uuid = createTrackAudioNode();
        if (uuid.isNotEmpty())
            timelineDoc.setTrackBinding(trackId, uuid);
        timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());
    });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added Audio " + juce::String(index + 1) : "Could not add a track");
}

// The automation strip lane picker's "Add lane..." entries — the minimal creation surface
// for a hosted plugin's own parameters, which have no ModuleComponent knob to right-click (the
// plugin has its own editor; see docs/modulation.md's Hosted Plugin table). Every live
// HostedPluginModule with a published instance offers every parameter that doesn't already have a
// lane; a bare or still-loading one offers nothing, same as it renders nothing elsewhere in the UI.
std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> MainComponent::getAvailablePluginLaneOptions() const {
    std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> options;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor());
        if (hosted == nullptr || !hosted->hasInstance())
            continue;

        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty())
            continue; // ensure-uuid runs at automate time, same as automateParameter() — nothing to offer yet

        const juce::String moduleLabel = describeNodeForBinding(node);
        for (const auto& param : hosted->getInstanceParameters()) {
            if (timelineDoc.getLaneForParam(uuid, param.paramId) != nullptr)
                continue; // already automated
            synth::ui::TrackHeaderHost::PluginLaneOption option;
            option.nodeUuid = uuid;
            option.paramId = param.paramId;
            option.paramIndex = param.index;
            option.label = moduleLabel + juce::String::fromUTF8(" \xC2\xB7 ") + param.displayName;
            options.push_back(std::move(option));
        }
    }
    return options;
}

synth::LaneId MainComponent::addPluginAutomationLane(const synth::ui::TrackHeaderHost::PluginLaneOption& option) {
    if (option.nodeUuid.isEmpty() || option.paramId.isEmpty())
        return {};

    auto* node = findNodeByUuid(option.nodeUuid);
    auto* hosted = node != nullptr ? dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) : nullptr;
    if (hosted == nullptr)
        return {}; // the node disappeared (or stopped being a plugin) between offering and choosing

    // Hosted-plugin parameters are always normalised (a hosted AudioProcessorParameter has no
    // NormalisableRange, and JUCE's own host contract is 0..1 regardless of format) — the lane's
    // RangeSnapshot IS {0, 1, default}, never something read off a live NormalisableRange.
    const auto resolved = synth::resolveLaneParameter(hosted, option.paramId, option.paramIndex);
    if (!resolved.resolved())
        return {}; // the parameter vanished between offering and choosing

    synth::LaneId laneId;
    const juce::String uuidCopy = option.nodeUuid;
    const juce::String paramIdCopy = option.paramId;
    const int paramIndexCopy = option.paramIndex;
    auto mutate = [this, &laneId, uuidCopy, paramIdCopy, paramIndexCopy, &resolved] {
        synth::TrackId trackId;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.kind == synth::TrackKind::Automation) {
                trackId = track.id;
                break;
            }
        }
        if (!trackId.isValid())
            trackId = timelineDoc.addTrack(synth::TrackKind::Automation, "Automation");
        if (!trackId.isValid())
            return;

        synth::AutomationLane::RangeSnapshot range;
        const auto bounds = synth::laneValueBoundsFor(resolved);
        range.minValue = static_cast<float>(bounds.minValue);
        range.maxValue = static_cast<float>(bounds.maxValue);
        range.defaultValue = static_cast<float>(synth::laneDefaultValueFor(resolved));
        laneId = timelineDoc.addLane(trackId, uuidCopy, paramIdCopy, range, paramIndexCopy);
    };
    undoManager.recordTimelineChange(timelineDoc, mutate);
    if (!laneId.isValid())
        return {};

    if (!isTimelineVisible && toggleTimelineButton.onClick)
        toggleTimelineButton.onClick();
    timelinePanel.showAutomationLane(laneId);
    return laneId;
}

// The MIDI-destinations picker's row list — every node in the live graph that actually CONSUMES
// MIDI in its processBlock (ModuleBase::acceptsMidi(), corrected per module by an audit of every
// module's processBlock — see Tests/ModuleMidiFlagsTests.cpp for the full table — so this is no
// longer a hardcoded allowlist, and no module can advertise a MIDI jack that silently did
// nothing). MIDI SOURCES are still deliberately excluded: a pure source
// (Track In / External MIDI / MIDI Keyboard) has acceptsMidi()==false by construction — see
// isMidiInstrumentType's own comment for why a source must never be offered as a destination — so
// checking acceptsMidi() alone already leaves them out without a separate "is it a source" test.
// Disambiguated exactly like getAvailableTrackInNodes(): "#id" appears only when some other
// candidate shares its plain display name.
std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption>
MainComponent::getMidiDestinationOptions(synth::TrackId forTrack) {
    std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption> options;
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return options; // unbound/orphaned: nothing resolvable to wire FROM

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return options; // the bound node is gone — reconcile marks this orphaned elsewhere

    auto& graph = audioEngine.getGraph();
    std::vector<juce::AudioProcessorGraph::Node*> candidates;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || !module->acceptsMidi())
            continue;
        candidates.push_back(node);
    }

    std::map<juce::String, int> nameOccurrences;
    for (auto* node : candidates)
        ++nameOccurrences[describeNodeForBinding(node)];

    for (auto* node : candidates) {
        const juce::String name = describeNodeForBinding(node);
        const juce::String display =
            nameOccurrences[name] > 1 ? name + " #" + juce::String((int)node->nodeID.uid) : name;
        const bool connected = graph.isConnected({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                                  {node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
        options.push_back({display, node->nodeID.uid, connected, isMidiInstrumentNode(node->getProcessor())});
    }
    return options;
}

void MainComponent::setMidiDestinationConnected(synth::TrackId forTrack, juce::uint32 nodeUid, bool connect) {
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return; // stale popup: the track lost its binding since the list was built — no-op, never crash

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return; // stale popup: the bound node is gone

    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::Node* targetNode = nullptr;
    for (auto* node : graph.getNodes()) {
        if (node != nullptr && node->nodeID.uid == nodeUid) {
            targetNode = node;
            break;
        }
    }
    if (targetNode == nullptr)
        return; // stale popup: the target node no longer resolves

    const juce::AudioProcessorGraph::Connection connection{
        {trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
        {targetNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}};

    undoManager.recordStructuralChange(graph, [&graph, connection, connect] {
        if (connect)
            graph.addConnection(connection);
        else
            graph.removeConnection(connection);
    });
    graphEditor.updateComponents();
    reconcileTimelineAfterGraphChange();
}

void MainComponent::auditionTrackNote(synth::TrackId forTrack, int pitch, int velocity, bool noteOn) {
    // Deliberately the SAME resolution the two functions above use — track -> bindingUuid -> live
    // node — because that node's MIDI output is where the track's destinations are wired FROM. Going
    // anywhere else (AudioEngine's own midiMessageCollector, say) would reach the global MIDI-in
    // path instead of this track's instruments, so the preview would play the wrong thing or nothing.
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return; // an unbound track plays nowhere, so a preview on it is silence

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return; // orphaned binding — the chip already says so; a preview must not crash on it

    auto* source = dynamic_cast<TimelineMidiSourceModule*>(trackInNode->getProcessor());
    if (source == nullptr)
        return; // the uuid resolves to something that is not a Track In node

    // NO structural change, NO undo step and NO doc mutation: a preview is not an edit. The push is
    // wait-free and a full FIFO simply drops the event (see pushAuditionNote) — nothing here may
    // block, because this is the mouse-down that is still unwinding.
    source->pushAuditionNote(pitch, velocity, noteOn);
}
