#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class EditBlockOverlay;

// DetachedPanelWindow.h (docs/mixer/panel.md): a native top-level window hosting one
// panel a DetachablePanelHost has detached from its dock. Modeled on
// Source/Plugin/Hosting/HostedPluginEditorWindow.{h,cpp}: constructed with addToDesktop=false, so
// building this (and every test below) never creates a native peer -- only a later
// setVisible(true) does, for real use.
//
// Owned EXCLUSIVELY by the DetachablePanelHost that created it, which reclaims `panel` (and the
// header button/title, borrowed the same way) on redock -- see DetachablePanelHost::setDetached().
// Never touches Desktop::setDefaultLookAndFeel (Source/Plugin/CLAUDE.md /
// docs/architecture/plugin-layer.md#who-owns-what's plugin-layer invariant, restated for this window in
// docs/mixer/panel.md): it calls setLookAndFeel() on ITSELF with the AppLookAndFeel instance its owner hands it -- the
// processor's own instance on the plugin path, exactly the AgentSynthPluginEditor/HostedPluginEditorWindow pattern --
// and clears it in its destructor. Never constructs its own ThemeManager/AppLookAndFeel.
//
// Keyboard focus is scoped to THIS window (docs/control/shortcuts.md "Focus regions"): MainComponent's
// keyPressed dispatch is not reachable from a separate top-level window (MainComponent installs no
// KeyListener -- see MainComponentSetup.cpp's comment -- so Tab here would otherwise go nowhere),
// so this window resolves Tab/Shift+Tab itself, against its OWN one-region FocusRegionRegistry
// (registerHostedPanelFocusRegion()), via the shared synth::ui::resolveFocusCycleKeyPress()
// (FocusRegion.h). It also owns its own synth::ui::AppTooltipWindow -- MainComponent's only covers its own
// tree, never a second top-level window -- and registers as a Desktop FocusChangeListener to
// repaint its own region root's accent outline (mirrors MainComponent::globalFocusChanged).
class DetachedPanelWindow
    : public juce::DocumentWindow
    , private juce::FocusChangeListener {
public:
    // `panel`, `headerButton` and `headerTitle` are all references to Components a
    // DetachablePanelHost still owns -- this window borrows and reparents them into its own
    // content for its life, and DetachablePanelHost reclaims them on redock (see its class
    // comment on why this is not a literal single shared widget instance in every placement mode).
    // `boundsKey` is the ApplicationProperties key this window's position/size persists under
    // ("timelineWindowBounds" / "mixerWindowBounds" -- docs/mixer/panel.md).
    DetachedPanelWindow(juce::Component& panel, juce::DrawableButton& headerButton, juce::Label& headerTitle,
                        juce::String boundsKey, juce::ApplicationProperties* appProperties,
                        synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager);
    ~DetachedPanelWindow() override;

    void closeButtonPressed() override;
    void moved() override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;
    /** Stands in for the real keyboard focus (which needs a native peer) when the open-context-menu key resolves. */
    void setFocusedComponentOverrideForTest(juce::Component* focused) { focusedComponentOverrideForTest_ = focused; }

    /** Fired on the close button -- this window never destroys itself (same contract as
     *  HostedPluginEditorWindow::onCloseRequested). DetachablePanelHost installs this to redock
     *  (setDetached(false)), which resets the unique_ptr that owns this window. */
    std::function<void()> onCloseRequested;

    /** Registers the ONE focus region this window's hosted panel resolves to -- called once by
     *  DetachablePanelHost right after construction, with the panel itself as the region root
     *  (a detached panel has no closed state of its own, so isOpen/open are both null, same as
     *  the graph canvas region MainComponent registers). */
    void registerHostedPanelFocusRegion(const juce::String& id, juce::Component& root);

    /** App-wide shortcuts (Cmd+T, Cmd+1..9) still have to fire while THIS window has focus
     *  -- it's still an AgentSynth window ("the focused window wins" only hands the keys to a
     *  HOSTED PLUGIN's own window). MainComponent::keyPressed is unreachable from here (see the
     *  class comment above), so keyPressed() retries an unresolved key through this callback,
     *  which DetachablePanelHost wires to the same dispatch MainComponent's own keyPressed uses. */
    std::function<bool(const juce::KeyPress&)> onAppShortcut;

    /** While a project is still loading, edits wait here as they do on the canvas: a transparent layer over the panel
     *  takes every click and calls `onRefused` (scroll and zoom pass through). Never focusable. */
    void setEditsBlocked(bool blocked, std::function<void()> onRefused);
    bool areEditsBlocked() const noexcept;

    static constexpr int kHeaderStripHeight = 24; // == DetachablePanelHost::kHeaderStripHeight

    // ---- Testing hooks (DetachedPanelWindowTests.cpp) ----
    synth::ui::FocusRegionRegistry& getFocusRegionsForTest() { return focusRegions_; }
    juce::Component* getContentForTest() const { return getContentComponent(); }
    juce::Component& getPanelForTest() const { return panel_; }
    EditBlockOverlay* getEditBlockForTest() const noexcept { return editBlock_.get(); }

private:
    void persistBounds();
    void restoreBoundsOrDefault();
    void globalFocusChanged(juce::Component* focusedComponent) override; // juce::FocusChangeListener
    /** Re-applies the header button's themed icon -- see this method's own comment
     *  (DetachedPanelWindow.cpp) for why DetachablePanelHost::applyIcon() alone isn't enough once
     *  the button is reparented in here. No-op before content_ exists. */
    void applyHeaderButtonIcon(synth::theme::AppLookAndFeel& lf);

    // The window's own content: a borrowed header strip (title + the detach button, now in "Dock
    // back" state) above the borrowed panel -- symmetric with the docked header
    // DetachablePanelHost itself draws when NOT embedded (see that class's comment).
    struct Content : public juce::Component {
        Content(juce::Component& panelIn, juce::DrawableButton& buttonIn, juce::Label& titleIn)
            : panel(panelIn)
            , button(buttonIn)
            , title(titleIn) {
            addAndMakeVisible(title);
            addAndMakeVisible(button);
            addAndMakeVisible(panel);
        }
        void resized() override {
            auto bounds = getLocalBounds();
            auto header = bounds.removeFromTop(DetachedPanelWindow::kHeaderStripHeight);
            button.setBounds(header.removeFromRight(DetachedPanelWindow::kHeaderStripHeight));
            title.setBounds(header);
            panel.setBounds(bounds);
        }
        juce::Component& panel;
        juce::DrawableButton& button;
        juce::Label& title;
    };

    juce::Component& panel_;
    juce::String boundsKey_;
    juce::ApplicationProperties* appProperties_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    // Owns the content wrapper ourselves (setContentNonOwned) rather than handing it to
    // ResizableWindow via setContentOwned -- panel_/button/title are borrowed, not ours to have the
    // window's own teardown order delete; this destructor drops the window's reference to it before
    // its own members (content_ included) unwind, so nothing borrowed is ever touched after.
    std::unique_ptr<Content> content_;
    std::unique_ptr<EditBlockOverlay> editBlock_; // after content_: it goes first
    std::unique_ptr<synth::ui::AppTooltipWindow> tooltipWindow_;
    synth::ui::FocusRegionRegistry focusRegions_;
    juce::Component::SafePointer<juce::Component> focusedComponentOverrideForTest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachedPanelWindow)
};

} // namespace synth::ui
