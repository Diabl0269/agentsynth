#include "MainComponent.h"
#include "AI/AIProviderRegistry.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "UI/Mixer/MeterColourStops.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Settings/SettingsWindow.h"
// Generated at CMake CONFIGURE time from local git history — see the root CMakeLists.txt's
// "What's New" block. ${CMAKE_BINARY_DIR}/generated is on AppUI's private include path.
#include "WhatsNewData.h"
#include <algorithm>

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
MainComponent::MainComponent(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry,
                             synth::ControllerProfileStore profileStore)
    // FRO228: ownedThemeManager/ownedLookAndFeel/themeManager/lookAndFeel must be set HERE, in the
    // member-initializer list, not assigned in the constructor body below -- bottomDock's own
    // in-class initializer (MainComponent.h) captures `lookAndFeel`'s CURRENT value while THIS list
    // is still being evaluated (declaration order puts these four members before bottomDock; the
    // body only runs once every member, bottomDock included, already exists). Assigning in the body
    // left every DetachablePanelHost/DetachedPanelWindow this ctor's MainComponent ever builds
    // holding a permanently null lookAndFeel_ -- the detached-window-never-themes bug this ticket
    // fixes -- even though getLookAndFeelForTest() looked correct immediately afterwards.
    : ownedThemeManager(std::make_unique<synth::theme::ThemeManager>())
    , ownedLookAndFeel(std::make_unique<synth::theme::AppLookAndFeel>())
    , themeManager(ownedThemeManager.get())
    , lookAndFeel(ownedLookAndFeel.get())
    , ownedAudioEngine(std::make_unique<AudioEngine>(AudioEngine::HostMode::Standalone))
    , audioEngine(*ownedAudioEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    // FRO193: this is the ONLY ctor every MainComponent*Tests.cpp call site actually uses, so this
    // is where controllerProfileStoreForCtor()'s test-directory override (Tests/TestMain.cpp)
    // actually takes effect -- the primary/plugin ctors above keep the in-class default member
    // initializer (a real ControllerProfileStore()) untouched, since nothing calls them from a test.
    , midiLearnController_(audioEngine, graphEditor, remoteEngine, midiRemoteDoc, undoManager, statusBar,
                           std::move(profileStore)) {
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

namespace {
// FRO193: empty (the default-constructed juce::File) means "no override" -- Tests/TestMain.cpp
// sets this once, before any test constructs a MainComponent, so every one of the ~50
// MainComponent*Tests.cpp call sites (none of which know or care about MIDI Remote) gets a
// temp-dir ControllerProfileStore for free instead of silently reading/writing the developer's
// real <settings>/MidiRemote/Controllers folder.
juce::File& controllerProfileTestDirectoryStorage() {
    static juce::File dir;
    return dir;
}
} // namespace

synth::ControllerProfileStore MainComponent::controllerProfileStoreForCtor() {
    const auto& dir = controllerProfileTestDirectoryStorage();
    return dir != juce::File() ? synth::ControllerProfileStore(dir) : synth::ControllerProfileStore();
}

void MainComponent::setControllerProfileTestDirectory(const juce::File& dir) {
    controllerProfileTestDirectoryStorage() = dir;
}

// ---- Shared post-construction body ----
// Shared initialisation body called from both constructors after appProperties is set up.
void MainComponent::initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry) {
    // FRO147: overlay the meter-colours override (if any) onto the AppLookAndFeel's already-
    // theme-derived cache — every ctor above already ran lookAndFeel->applyTheme() before calling
    // this, so the theme-default stops are in place; this only pins a user override on top, before
    // the first mixer column/channel chip ever paints.
    lookAndFeel->setMeterColourStopsOverride(synth::ui::loadMeterColourStopsOverride(*appProperties.getUserSettings()));
    restorePanelPreferences();       // ORDER: flags read before any addAndMakeVisible/setVisible
    restoreGraphEditorPreferences(); // ORDER: settings change listener registered here
    configureAiProvider(std::move(provider), std::move(registry)); // ORDER: nothing earlier may write appProperties
    wireAiChatAndAccount(); // ORDER: after setProvider (#96); account before attemptSilentSignIn
    wireGraphEditorCallbacks();
    wirePluginScanAndRecents(); // ORDER: HostMode-dependent (ownedAudioEngine == nullptr)
    wireCommandsAndShortcuts(); // ORDER: keep the "no KeyListener" comment
    // ORDER: after commandManager exists (the action invoker dispatches through it) and BEFORE the
    // initialiseAudioEngine() early-return below — that return only skips the app-only welcome
    // screen/focus regions in Hosted mode, and MIDI Remote must still wire up for a hosted plugin
    // (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host's hostSourceKey exists exactly for that case).
    wireMidiRemoteEngine();
    addCanvasAndPanels();
    addToolbarChrome(); // ORDER: z-order -- before any toolbar button
    addFileButtons();
    addUndoButtons();
    wireTimelinePanel();
    addToolbarToggleButtons();
    assembleToolbar(); // ORDER: setButtons() before setSize()
    wireStatusBar();
    // FRO260: this is also where openMidiRemoteDevices() (MainComponentSetup.cpp) runs, from
    // INSIDE initialiseAudioEngine() itself once the engine is actually up -- not listed as its own
    // ordered step here because it is standalone-only and never a top-level call site.
    if (!initialiseAudioEngine())
        return;
    createWelcomeScreen(); // ORDER: app-only, added LAST (z-order)
    registerFocusRegions();
}

MainComponent::~MainComponent() {
    // FRO44: unregister FIRST, before anything below (closing native plugin-editor windows
    // included) has a chance to pump the message loop. A `pluginScanCompleted` queued by a scan on
    // another thread would otherwise land mid-destruction and call `savePluginScanList()` /
    // `refreshPluginLibrary()` / `statusBar.showMessage()` on a half-torn-down component. On the
    // adopted-service path (plugin editors) the service is the PROCESSOR's and outlives this
    // destructor regardless, so leaving the listener registered would also call back into a dead
    // MainComponent the next time some other editor or the owner triggers a scan.
    getPluginScanService().removeListener(this);

    // Pairs with the addFocusChangeListener(this) at the end of initialiseCommon(). Desktop is a
    // process-global broadcaster that outlives this component, so an unremoved listener would call
    // back into freed memory on the very next focus change anywhere in the process.
    juce::Desktop::getInstance().removeFocusChangeListener(this);

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
    // FRO105: whatever the scan already found before this quit (or before a scan in flight was cut
    // off by the cancelScan() below) is already sitting in pluginScanService's in-memory list
    // regardless of whether it ever reached pluginScanCompleted() — which removeListener() above
    // ensures it now never will. Without this, quitting during a long scan (a large or slow plugin
    // folder) silently threw away every plugin found that session, so the NEXT launch re-probed them
    // all over again; saving here makes an interrupted scan's progress durable, same as a completed
    // one's. Guarded on "still ours" (the same condition the backend unhook just above uses), NOT
    // isHosted(): on the adopted-service path activeScanService is the PROCESSOR's, which outlives
    // this editor and is never scanned from here anyway (see wirePluginScanAndRecents()), so saving
    // it here would write another component's in-flight list into settings out from under it.
    const bool ownsActiveScanService = activeScanService == &pluginScanService;
    pluginScanService.cancelScan();
    if (ownsActiveScanService)
        savePluginScanList();

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
    // MUST run before remoteEngine itself is destroyed (declaration order below destroys it at the
    // end of this body): setRemoteMessageSink(nullptr) publishes-then-drains (see AudioEngine.h's
    // comment on the setter), so once this returns no audio callback can still be inside
    // RemoteEngine::handleMessage reading the old pointer — the same borrowed-pointer contract as
    // the two calls below, just for a sink whose destructor (unlike MidiRecorder/AutomationRecorder,
    // both owned by MainComponent for the process's whole life) is about to run in THIS destructor.
    audioEngine.setRemoteMessageSink(nullptr);
    // remoteEngine is destroyed after this body, but audioEngine.shutdown() below clears the graph
    // first: a MIDI knob gesture still inside its idle window would make ~RemoteEngine() end it on a
    // freed parameter. End it here, while the parameters exist (see endAllGestures()'s comment).
    remoteEngine.endAllGestures();
    audioEngine.setAutomationRecorder(nullptr);
    audioEngine.setMidiCaptureSink(nullptr);
    automationRecorder.detach();
    undoManager.setRestoreHooks({}, {});
    // Also unbinds bottomDock's own fader/pan bindings via GraphEditor::onBeforeDetachAllModuleComponents
    // (wired in wireTimelinePanelServicesAndShortcuts) -- bottomDock is declared AFTER graphEditor in
    // MainComponent.h, so its own destructor runs BEFORE graphEditor's once this body returns;
    // without this call happening first, that destructor would unbind a fader still pointing at a
    // param audioEngine.shutdown() below is about to free (the FRO11 class of bug, same root cause
    // as the undo/redo crash this fixes).
    graphEditor.detachAllModuleComponents();
    // Unconditionally, both Standalone and Hosted: the hosted-mode engine outlives `this` (the
    // processor owns it, not this MainComponent), so a dangling `this`-capturing lambda left
    // wired here would fire out from under freed memory the next time something calls
    // audioEngine.shutdown() (FRO87). Cleared BEFORE the guarded shutdown() call below so
    // Standalone's own behaviour is unchanged: detachAllModuleComponents() has already run once
    // (immediately above) by the time shutdown() could otherwise re-fire it via the hook.
    audioEngine.onBeforeShutdown = nullptr;
    // Only tear down an engine we own. On the plugin path the processor's engine must survive
    // the editor being closed and reopened.
    if (ownedAudioEngine != nullptr) {
        // Drop the device-state callback first — it captures `this`, and shutdown() is the
        // call that unsubscribes the engine from its device manager. onMidiDevicesChanged
        // (FRO262) captures `this` the same way and is reachable from the same changeListenerCallback,
        // so it gets the same treatment.
        audioEngine.onDeviceStateChanged = nullptr;
        audioEngine.onMidiDevicesChanged = nullptr;
        audioEngine.shutdown();
    }
}
