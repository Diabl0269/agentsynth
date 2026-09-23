// MainComponentPanels.cpp — plugin/snippet library refresh, repeat-selection, keyboard/focus
// arbitration (resolveEditSurface/keyPressed), resized()/toolbar icons, timeline panel slide
// animation, welcome screen, and the natural-scrolling/zoom-scroll preference toggles.
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the
// sibling MainComponent*.cpp units next to this one.
#include "MainComponent.h"
#include "MidiRemote/MidiRemotePreferences.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "WhatsNewData.h"
#include <algorithm>

namespace {

// True when `candidate` IS `ancestor` or sits anywhere inside its component subtree. The
// "click grabs focus" idiom (GraphEditor::mouseDown and every timeline sub-component that copies
// it) means the currently-focused component is always either a surface's root component itself or
// one of its rare children, never a cousin — checking both keeps resolveEditSurface() correct even
// if a sub-widget ever grows its own focusable child.
bool isOrIsChildOf(const juce::Component* candidate, const juce::Component& ancestor) noexcept {
    return candidate != nullptr && (candidate == &ancestor || ancestor.isParentOf(candidate));
}

} // namespace

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

/** The status-bar message both scan triggers post per candidate — factored out (FRO105) so the
 *  eager startup scan, which previously ran silently, reports progress exactly like the sidebar's
 *  manual row always has. */
synth::PluginScanService::ProgressFn MainComponent::makePluginScanProgressReporter() {
    return [this](const juce::String& fileOrIdentifier, int scanned, int total) {
        // Trailing segment, not File::getFileName(): an AudioUnit's identifier is not a path.
        statusBar.showMessage("Scanning plugins " + juce::String(scanned) + "/" + juce::String(total) + ": " +
                              fileOrIdentifier.fromLastOccurrenceOf("/", false, false));
    };
}

/** Starts a background scan of every format this build can host, reporting through the status
 *  bar and refreshing the library's Plugins section (and the saved list) when it finishes.
 *  Ignored while a scan is already running, and refused outright when the engine is Hosted —
 *  the scan re-launches `currentExecutableFile`, which inside a VST3/AU is the HOST's binary. */
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

    // Progress arrives on the message thread (PluginScanService posts it), so touching the status
    // bar from here is safe. Completion is NOT wired here — pluginScanCompleted() (registered as a
    // Listener in the constructor) handles it uniformly for every trigger path, this button
    // included, so the eager startup scan gets exactly the same sidebar refresh and persisted save.
    getPluginScanService().scanAsync(synth::hostedPluginFormatNames(), makePluginScanProgressReporter(), nullptr);
}

/** FRO44: the eager-population entry point, called ONCE by `Main.cpp` right after the real
 *  standalone window is created — never by this class's own constructor, and never for the
 *  plugin-hosted path (see below). Delegates to `getPluginScanService().ensureScanned(...)`,
 *  which is itself a one-shot-per-service no-op past the first call: a second call (a test, or a
 *  future picker that also wants to make sure the list is populated) is always safe to make.
 *
 *  Hosted mode (`AudioEngine::isHosted()`) is a deliberate no-op, matching `startPluginScan()`'s
 *  own refusal: `currentExecutableFile` inside a VST3/AU build is the HOST's binary, so scanning
 *  there would launch a copy of the DAW per candidate plugin, and the host owns plugin discovery
 *  in that world regardless. A hosted session still RESOLVES identities — against whatever list
 *  the constructor already restored from settings — it just never scans one itself; see
 *  docs/architecture/plugin-layer.md#plugin-scanning--a-crash-must-kill-a-child-not-the-app.
 *
 *  FRO105: this used to call `ensureScanned()` with no progress callback at all, so the eager scan
 *  ran silently — the founder complaint this fixes was seeing only whatever the persisted list
 *  already had until the ONE completion message landed, with no sign a scan was even happening in
 *  between. `ensureScanned()`'s return says whether THIS call actually started the scan (false for
 *  a redundant later call), so the "Scanning for plugins..." banner only appears when it is true. */
void MainComponent::maybeStartEagerPluginScan() {
    // See this method's header comment: hosted mode never scans, eagerly or otherwise.
    if (audioEngine.isHosted())
        return;
    if (getPluginScanService().ensureScanned(synth::hostedPluginFormatNames(), makePluginScanProgressReporter()))
        statusBar.showMessage("Scanning for plugins...");
}

// synth::PluginScanService::Listener — fired once per real scan, no matter which caller
// (the sidebar's row, maybeStartEagerPluginScan(), a future picker) actually triggered it.
// Consolidates what used to be startPluginScan()'s own inline completion lambda, so every
// trigger path gets the same status-bar message, sidebar refresh and persisted save.
void MainComponent::pluginScanCompleted(const synth::PluginScanService::Result& result) {
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
}

// ---- Snippets (issue #156) ----

/** Re-reads the snippets directory and pushes the list into the library sidebar. */
void MainComponent::refreshSnippetLibrary() {
    moduleLibrary.setSnippets(
        synth::SnippetManager::listSnippets(synth::SnippetManager::getDefaultSnippetsDirectory()));
}

/** Asks for a name and saves the canvas selection as a snippet. No-op (with a status-bar
 *  note) when nothing is selected. */
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

/** The repeat verb's ACTUAL work, split out from the command so it can be driven without the
 *  count dialog — tests call it directly, and a future scripting/AI caller gets the same door.
 *  Routed by the same resolveEditSurface() every other edit verb uses: TimelineClips repeats
 *  the clip selection, PianoRoll repeats the note selection, and Graph is deliberately
 *  unsupported (there is no "one block length further along" on a spatial canvas — Duplicate
 *  is the graph's answer, which is why getCommandInfo reports the command inactive there).
 *  `count` is clamped to [kMinRepeatCount, kMaxRepeatCount]; the callee owns its own undo
 *  transaction, so this must never be wrapped in another one.
 *  @return whatever the surface's verb returned — true when something was actually created. */
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

/** Asks for a repeat count (async juce::AlertWindow, exactly promptSaveSnippet's modal idiom)
 *  and hands it to performRepeatSelection(). No-op (with a status-bar note) when the focused
 *  surface has nothing selected. Kept separate from performRepeatSelection so nothing but the
 *  keyboard/menu path ever has to pump a modal loop. */
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
// ---- Keyboard/focus arbitration ----
// Which surface currently owns Cmd+C/V/D (Space's togglePlayback is deliberately
// surface-independent — see ShortcutManager's binding comment and docs/control/shortcuts.md).
// TimelineClips/PianoRoll require BOTH the timeline panel to be visible AND real keyboard
// focus (juce::Component::getCurrentlyFocusedComponent()) to sit inside the clip-lane area /
// piano roll respectively — a hidden panel never owns the verbs, whatever a stale focus
// pointer points at. Every one of those surfaces already grabs focus on mouseDown (the canvas
// idiom GraphEditor::mouseDown established, followed by TimelineClipLaneArea/
// PianoRollComponent/AutomationLaneEditor), so "last-clicked surface owns the verbs" falls out
// of ordinary JUCE focus tracking with no extra bookkeeping in this class. Public: both
// perform()/getCommandInfo() and FocusArbitrationTests.cpp call it directly.
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
    // FRO130: Esc cancels an armed MIDI Learn (docs/control/midi-remote-ui.md#the-learn-interaction),
    // ahead of everything below -- including GraphEditor's own canvas Escape (clears the selection),
    // which only reaches here at all when nothing is selected.
    if (key == juce::KeyPress::escapeKey && midiLearnController_.isPickingTarget()) {
        midiLearnController_.cancelPickTarget();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && midiLearnController_.isArmed()) {
        midiLearnController_.cancelArmed();
        return true;
    }

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
    // CANONICAL LAYOUT (docs/layout/chrome.md#application-chrome). Carve top→bottom: toolbar strip, status bar,
    // timeline panel (bottom), AI panel (right), library sidebar (left), canvas (remainder). Dimensions come from the
    // themed Metrics tokens, with literal fallbacks for the headless test path.
    //
    // Each panel's SIZE is its open fraction times its full size, NOT a binary read of its
    // visible/hidden flag (docs/layout/animation.md). That is what makes this pass correct whenever it
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
    // FRO12 (P9-6): the Mixer's "Own panel" placement -- a second, INDEPENDENT bottom strip BELOW
    // the Timeline dock carved next. Fixed height, no slide (the ticket's own scope cut -- see
    // MixerPlacementController's class comment): a plain visible/hidden carve, same "|| isVisible()
    // frame-0" guard as every other panel above. Carved FIRST (before the Timeline dock below)
    // so it claims the window's actual bottom edge -- carving it second would instead stack it
    // ABOVE the Timeline dock, the opposite of docs/mixer/panel.md's own layout.
    if (mixerPlacement_.isOwnPanelShowing())
        mixerPlacement_.setBounds(bounds.removeFromBottom(synth::ui::MixerPlacementController::kOwnPanelHeight));

    if (timelineSlide_.getProgress() > 0.0f || mixerDock.isVisible()) {
        // Re-clamped every pass: the window may have shrunk since the height was set (or persisted
        // on a larger one), and the canvas must stay usable.
        timelinePanelHeight_ = clampTimelinePanelHeight(timelinePanelHeight_);
        mixerDock.setBounds(bounds.removeFromBottom(timelineSlide_.sizeBetween(0, timelinePanelHeight_)));
    }

    if (aiW > 0 || aiChatComponent.isVisible())
        aiChatComponent.setBounds(bounds.removeFromRight(aiW));
    if (libW > 0 || moduleLibrary.isVisible())
        moduleLibrary.setBounds(bounds.removeFromLeft(libW));

    graphEditor.setBounds(bounds);

    // T114/P8-10: full window bounds on EVERY layout pass, whether or not it's currently visible —
    // it must always cover the toolbar/canvas the moment it's shown, and a stale rect from before
    // the last resize would leave gaps around the edges.
    if (welcomeScreen_)
        welcomeScreen_->setBounds(getLocalBounds());
}

// ---- Toolbar icon + text application ----
// Push the (themed, re-tinted) icon Drawables onto the 9 toolbar DrawableButtons + the
// status-bar master-mute button, and manage icon-only vs icon+text text per narrow mode.
// dynamic_casts the LnF and no-ops the icon assignment when null (headless tests).
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
    // FRO131: TrackMidi is a real MIDI glyph already in the library (used for track-header kind
    // icons) -- no need for a dedicated new asset, same "reuse what exists" reasoning as
    // toggleTimelineButton's own TransportPlay borrow above.
    setIcon(toggleMidiRemoteButton, Icon::TrackMidi);
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
    toggleMidiRemoteButton.setToggleState(isTimelineVisible && mixerDock.isMidiRemoteTabActive(),
                                          juce::dontSendNotification);

    // Text: cleared in narrow mode; stateful for the toggles in wide mode.
    newButton.setButtonText(iconOnly ? "" : "New");
    saveButton.setButtonText(iconOnly ? "" : "Save");
    loadButton.setButtonText(iconOnly ? "" : "Load");
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
    toggleMidiRemoteButton.setButtonText(
        iconOnly ? ""
                 : ((isTimelineVisible && mixerDock.isMidiRemoteTabActive()) ? "Hide MIDI Remote" : "MIDI Remote"));
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
    saveButton.setTooltip(hint("Save project", "savePreset"));
    loadButton.setTooltip(hint("Load a patch or project", "openProject"));
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

    const juce::String midiRemoteBase =
        (isTimelineVisible && mixerDock.isMidiRemoteTabActive()) ? "Hide MIDI Remote" : "Show MIDI Remote";
    toggleMidiRemoteButton.setTooltip(hint(midiRemoteBase, "toggleMidiRemotePanel"));

    const juce::String libBase = isLibraryVisible ? "Hide Library" : "Show Library";
    toggleLibraryButton.setTooltip(hint(libBase, "toggleLibrary"));
}

// ---- Timeline panel height (user-resizable, persisted) ----

// Metrics::timelinePanelHeight (220 headless) — the DEFAULT height and the MINIMUM the user can
// drag down to, never the fixed height it used to be.
int MainComponent::defaultTimelinePanelHeight() const {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getTheme().metrics.timelinePanelHeight;
    return 220; // headless literal fallback, same pattern as resized()
}

// [defaultTimelinePanelHeight(), 75% of the window height]. Applied on every layout pass, so a
// height saved on a big window can never swallow a smaller window's canvas. Before the first
// layout (window height still 0) only the floor applies — otherwise construction would clamp a
// persisted height away against a window that doesn't exist yet.
int MainComponent::clampTimelinePanelHeight(int desiredHeight) const {
    const int minHeight = defaultTimelinePanelHeight();
    // Window not laid out yet: only the floor applies, so a persisted height survives construction
    // and is capped by the first real resized() instead.
    const int maxHeight =
        getHeight() > 0 ? std::max(minHeight, (getHeight() * 3) / 4) : std::max(minHeight, desiredHeight);
    return juce::jlimit(minHeight, maxHeight, desiredHeight);
}

// Clamps, stores and re-lays-out (live, once per drag callback — user-driven, not a
// free-running repaint). `persist` writes kTimelinePanelHeightKey; the drag does that only on
// mouse-up.
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

// The three fractions, addressed by name (test seams above; nothing else needs this).
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
        mixerDock.setVisible(true);

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

// Per-frame body of the slide above: advance all three fractions, then re-lay-out.
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

// End of the slide (its completion callback, and the synchronous path's whole body): stop the
// driver, pin the exact end fractions, hide whatever finished closing, lay out.
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
        mixerDock.setVisible(false);

    resized();
}

// FRO11 (P9-5): mirrors toggleTimelineButton's own open/close symmetry (plan (f)) -- closed ->
// open on the Mixer tab; open on Timeline -> switch to Mixer without closing; open on Mixer ->
// close. The dock's own open/close state (isTimelineVisible/timelineSlide_) stays keyed to "is
// the DOCK open" regardless of which tab is active (see MixerDockComponent's own class comment).
void MainComponent::performToggleMixerPanel() {
    // FRO12 (P9-6): Own-panel/Window placements have nothing to do with the Timeline dock's own
    // open/close state below -- mixerPlacement_ handles the reveal itself and says so by
    // returning true. Tab placement (the default) returns false and falls through to the
    // unchanged FRO11 behaviour.
    if (mixerPlacement_.revealOrToggle())
        return;
    if (!isTimelineVisible) {
        isTimelineVisible = true;
        appProperties.getUserSettings()->setValue("timelinePanelVisible", "1");
        appProperties.getUserSettings()->saveIfNeeded();
        mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
        applyToolbarIcons();
        beginPanelSlide();
        return;
    }
    if (mixerDock.isMixerTabActive()) {
        isTimelineVisible = false;
        appProperties.getUserSettings()->setValue("timelinePanelVisible", "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        beginPanelSlide();
        return;
    }
    mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
}

// FRO131: same open/close symmetry as performToggleMixerPanel() above -- MidiRemote has no
// placement-controller detour (unlike Mixer's mixerPlacement_.revealOrToggle()), since it offers
// no Own-panel/Window placement variant.
void MainComponent::performToggleMidiRemotePanel() {
    if (!isTimelineVisible) {
        isTimelineVisible = true;
        appProperties.getUserSettings()->setValue("timelinePanelVisible", "1");
        appProperties.getUserSettings()->saveIfNeeded();
        mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::MidiRemote);
        applyToolbarIcons();
        beginPanelSlide();
        return;
    }
    if (mixerDock.isMidiRemoteTabActive()) {
        isTimelineVisible = false;
        appProperties.getUserSettings()->setValue("timelinePanelVisible", "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        beginPanelSlide();
        return;
    }
    mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::MidiRemote);
}

// ---- Collapsible library sidebar (slides, persisted) ----
// Collapse/expand the library sidebar. Slides to the target layout (beginPanelSlide()).
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

// ---- Welcome screen (T114/P8-10) ----

// A no-op when welcomeScreen_ is null (Hosted mode) or already hidden — every guarded action's
// `proceed` continuation calls this unconditionally as its LAST step, so it must tolerate both.
void MainComponent::hideWelcomeScreen() {
    // A no-op is deliberately safe here: null in Hosted mode, and every guarded action's `proceed`
    // continuation (loadPresetGuarded, newPatch) calls this unconditionally as its last step even
    // when the welcome screen was never showing in the first place (e.g. the toolbar's own Load
    // button, not the welcome screen, triggered the load).
    if (welcomeScreen_)
        welcomeScreen_->setVisible(false);
}

// Reopens the overlay (AppCommands::showWelcomeScreen, wired to the macOS Help menu in
// Main.cpp) with a freshly re-pruned recent-projects list — the list may have changed since it
// was last shown.
void MainComponent::showWelcomeScreen() {
    if (!welcomeScreen_)
        return;
    // The list may have changed since it was last shown (a project saved/opened elsewhere, or
    // pruned since a bundle moved/vanished) — re-pruned exactly like the Load menu does before
    // building its own Recent Projects submenu.
    if (recentProjects.pruneMissing() > 0)
        saveRecentProjects();
    welcomeScreen_->setRecentProjects(recentProjects.getEntries());
    welcomeScreen_->toFront(false);
    welcomeScreen_->setVisible(true);
}

// Feature 2 of T114/P8-10: a small, synchronous, no-network dialog listing recent commit subjects
// captured at CMake CONFIGURE time (see the root CMakeLists.txt's "What's New" block and
// WhatsNewData.h). Never invoked from a test — it would open a real modal juce::AlertWindow, same
// caution as every other real-dialog entry point in this file.
// Build-time "What's New" dialog (Feature 2) — a synchronous, no-network juce::AlertWindow
// listing synth::whatsnew::kHighlights. Never invoked from a test (it would open a real modal).
void MainComponent::showWhatsNewDialog() {
    juce::String message = juce::String(synth::whatsnew::kReleaseTag) + "\n\n";
    for (int i = 0; i < synth::whatsnew::kHighlightsCount; ++i)
        message << "- " << synth::whatsnew::kHighlights[i] << "\n";

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::InfoIcon)
                       .withTitle("What's New")
                       .withMessage(message)
                       .withButton("Close")
                       .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, nullptr);
}

// ---- Alignment guides toggle (UI Phase 7 - Item 4) ----
void MainComponent::setAlignmentGuidesEnabled(bool enabled) {
    isAlignmentGuidesEnabled = enabled;
    graphEditor.setAlignmentGuidesEnabled(enabled);
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

void MainComponent::applyMidiRemotePreferences() {
    const auto* settings = appProperties.getUserSettings();
    if (settings == nullptr)
        return;
    remoteEngine.setDefaultTakeover(synth::midi::loadDefaultTakeover(*settings));

    const bool showBadges = synth::midi::loadShowBadges(*settings);
    if (showBadges == synth::ui::midilearn::areMappedBadgesVisible())
        return;
    synth::ui::midilearn::setMappedBadgesVisible(showBadges);
    graphEditor.repaint();
    mixerDock.getMixerPanel().repaint();
    timelinePanel.repaint();
}
