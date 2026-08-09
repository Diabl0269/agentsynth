#pragma once

#include "AI/AIIntegrationService.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AccountService.h"
#include "AppUndoManager.h"
#include "AudioEngine.h"
#include "PresetManager.h"
#include "ShortcutManager.h"
#include "SnippetManager.h"
#include "UI/AIChatComponent.h"
#include "UI/GraphEditor.h"
#include "UI/ModuleLibraryComponent.h"
#include "UI/StatusBarComponent.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/ToolbarComponent.h"
#include "UI/UIAnimation.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class MainComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::Timer
    , public juce::ApplicationCommandTarget
    , private juce::ChangeListener
    , private synth::AIIntegrationService::Listener {
public:
    // Primary ctor: receives injected ThemeManager and LookAndFeel from Main.cpp.
    // provider is optional (nullptr → reads saved provider pref from appProperties).
    MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                  std::unique_ptr<synth::AIProvider> provider = nullptr);

    // Delegating ctor for tests and legacy call sites that don't inject theme objects.
    // Lazily owns private default ThemeManager + AppLookAndFeel instances
    // (stored in ownedThemeManager / ownedLookAndFeel below).
    explicit MainComponent(std::unique_ptr<synth::AIProvider> provider = nullptr,
                           synth::AIProviderRegistry registry = synth::AIProviderRegistry::createDefault());

    ~MainComponent() override;

    void timerCallback() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    bool keyPressed(const juce::KeyPress& key) override;

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    void updateCommandShortcuts();

    // Testing Hooks
    bool isAiPanelConfiguredVisible() const { return isAiPanelVisible; }
    bool isLibraryConfiguredVisible() const { return isLibraryVisible; }
    void simulateToggleAiPanelClick() {
        if (toggleAiPanelButton.onClick)
            toggleAiPanelButton.onClick();
    }
    void simulateToggleModMatrixClick() {
        if (toggleModMatrixButton.onClick)
            toggleModMatrixButton.onClick();
    }
    void simulateToggleMinimapClick() {
        if (toggleMinimapButton.onClick)
            toggleMinimapButton.onClick();
    }
    void simulateToggleLibraryClick() {
        if (toggleLibraryButton.onClick)
            toggleLibraryButton.onClick();
    }
    GraphEditor& getGraphEditor() { return graphEditor; }
    ToolbarComponent& getToolbar() { return toolbar; }
    StatusBarComponent& getStatusBar() { return statusBar; }
    void simulateNewPatchClick() {
        if (newButton.onClick)
            newButton.onClick();
    }
    void simulateUndoClick() {
        if (undoButton.onClick)
            undoButton.onClick();
    }
    void simulateRedoClick() {
        if (redoButton.onClick)
            redoButton.onClick();
    }
    AppUndoManager& getUndoManager() { return undoManager; }
    AudioEngine& getAudioEngine() { return audioEngine; }
    const juce::String& getCurrentPatchName() const { return currentPatchName_; }
    // Non-const access to ApplicationProperties for persistence tests (read-back within session).
    juce::ApplicationProperties& getAppPropertiesForTest() { return appProperties; }
    int getStatusBarTickCountForTest() const { return statusBarTickCount_; }
    // Mirrors the loadButton factory-preset call site exactly (load + patch-name update), so
    // tests can verify the patch-name side effect without driving the async PopupMenu.
    void simulateLoadFactoryPresetForTest(int index);
    void openPresetFromFile();
    synth::AIIntegrationService& getAiServiceForTest() { return aiService; }

    // ---- Snippets (issue #156) ----

    /** Re-reads the snippets directory and pushes the list into the library sidebar. */
    void refreshSnippetLibrary();

    /** Asks for a name and saves the canvas selection as a snippet. No-op (with a status-bar
     *  note) when nothing is selected. */
    void promptSaveSnippet();

    ModuleLibraryComponent& getModuleLibrary() { return moduleLibrary; }

private:
    // AIIntegrationService::Listener
    void aiPatchAboutToApply() override;
    void aiPatchApplied() override;

    // ChangeListener (juce::ChangeListener override) — called when ThemeManager broadcasts.
    // Implements the 3-step re-skin pass: applyTheme → sendLookAndFeelChangeMessage → repaint.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Shared initialisation body called from both constructors after appProperties is set up.
    void initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);

    // Push the (themed, re-tinted) icon Drawables onto the 9 toolbar DrawableButtons + the
    // status-bar master-mute button, and manage icon-only vs icon+text text per narrow mode.
    // dynamic_casts the LnF and no-ops the icon assignment when null (headless tests).
    void applyToolbarIcons();

    // Collapse/expand the library sidebar. Animates to the target layout.
    void setLibraryVisible(bool v);

    // Compute the target bounds for the library and AI panel in the current layout.
    // Pure geometry — no side effects; used by animation and tests.
    struct PanelBoundsResult {
        juce::Rectangle<int> libraryBounds; // empty rect when hidden
        juce::Rectangle<int> aiPanelBounds; // empty rect when hidden
        juce::Rectangle<int> graphEditorBounds;
    };
    PanelBoundsResult computePanelBounds(bool libVisible, bool aiVisible) const;

    // Update the displayed patch name (status bar). Immediate repaint, no timer delay.
    void setCurrentPatchName(const juce::String& name);

    // Owned fallback objects used when the delegating ctor is called (tests/legacy).
    // Null when the primary ctor is used (refs point at external objects instead).
    std::unique_ptr<synth::theme::ThemeManager> ownedThemeManager;
    std::unique_ptr<synth::theme::AppLookAndFeel> ownedLookAndFeel;

    // Non-owning references to the active ThemeManager and LookAndFeel.
    // Always valid — set by both constructors (either to external objects or to the
    // owned fallbacks above).
    synth::theme::ThemeManager* themeManager{nullptr};
    synth::theme::AppLookAndFeel* lookAndFeel{nullptr};

    AppUndoManager undoManager;
    AudioEngine audioEngine;
    GraphEditor graphEditor;
    ModuleLibraryComponent moduleLibrary;

    // Toolbar strip (paints the bg + lays out the 9 buttons below via FlexBox). The buttons
    // remain direct children of MainComponent so existing getChildren() accessors still work.
    ToolbarComponent toolbar;

    // The 10 toolbar buttons (9 actions + toggleLibrary). DrawableButton so they carry SVG
    // icons; ButtonParameterAttachment / .onClick wiring works on the juce::Button base.
    juce::DrawableButton newButton{"new", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton saveButton{"save", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton loadButton{"load", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton settingsButton{"settings", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton undoButton{"undo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton redoButton{"redo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleAiPanelButton{"toggleAi", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleModMatrixButton{"toggleMatrix", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleMinimapButton{"toggleMinimap", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton autoArrangeButton{"autoArrange", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleLibraryButton{"toggleLibrary", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton themeToggleButton{"toggleTheme", juce::DrawableButton::ImageAboveTextLabel};

    std::unique_ptr<juce::FileChooser> fileChooser;

    synth::AIIntegrationService aiService;
    // Declared BEFORE aiChatComponent: members are destroyed in reverse declaration order, so
    // aiChatComponent (which installs AccountService::onStateChanged/onAccessTokenChanged in
    // setAccountService(), see its header comment) is torn down first, while accountService is
    // still alive to have those callback slots cleared.
    synth::AccountService accountService;
    synth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;
    bool isLibraryVisible{true};
    bool isAlignmentGuidesEnabled{true}; // NEW: default TRUE for backward compatibility

    // Cached narrow-mode state — applyToolbarIcons() re-clones icons ONLY on the transition.
    bool toolbarNarrowMode_{false};

    // Status-bar polling gate: timerCallback() runs at 10 Hz; the status bar updates at 5 Hz
    // (every 2nd tick).
    int statusBarTickCount_{0};

    // Declared BEFORE statusBar so it is fully constructed when statusBar's ctor runs.
    juce::String currentPatchName_{"Default"};
    StatusBarComponent statusBar;

    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    // ---- Panel slide animations (time-bounded, auto-stop) ----
    // One VBlankAnimatorUpdater shared by both panel animations (driven by MainComponent).
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver libraryAnim;
    synth::ui::AnimationDriver aiPanelAnim;

    // During animation, track the "from" bounds so lerpBounds() can interpolate.
    juce::Rectangle<int> libraryAnimFrom;
    juce::Rectangle<int> aiPanelAnimFrom;
    juce::Rectangle<int> graphEditorAnimFrom;

    // Start a coordinated bounds animation for library + AI panel + graph editor.
    // fromResult is the current layout; toResult is the target layout.
    void animatePanelTransition(const PanelBoundsResult& fromResult, const PanelBoundsResult& toResult,
                                bool hideLibraryOnComplete, bool hideAiPanelOnComplete);

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled);

    // Provides native-style tooltips for any child Component that has a tooltip
    // string set via setTooltip(). Constructed last so all child components exist.
    // Do NOT set tooltips on controls here; each feature owner does that.
    juce::TooltipWindow tooltipWindow{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
