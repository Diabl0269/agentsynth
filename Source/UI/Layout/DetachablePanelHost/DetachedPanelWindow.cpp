// Concern: FRO12 (P9-6) -- DetachedPanelWindow's content wrapper, bounds persistence, per-window
// focus-region Tab cycling, and the plugin-mode LookAndFeel seam.
#include "DetachedPanelWindow.h"

namespace synth::ui {

namespace {
constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 420;

// A persisted rect this small (or smaller) is never something a user actually resized to --
// 128x128 is juce::ComponentBoundsConstrainer's OWN default minimum, i.e. the value a
// ResizableWindow settles on when something drives it through setBounds() without ever going
// through a real interactive resize (FRO101: a headless test detaching a panel against a real
// MainComponent, before the settings-key leak those tests had was fixed). Anything under a
// plausible real window size is rejected the same way an absent/malformed persisted key already
// is, below.
constexpr int kMinPlausibleWidth = 320;
constexpr int kMinPlausibleHeight = 240;

// True when `rect` is both a plausible SIZE for a real window and actually reachable on at least
// one connected display -- a persisted rect from a display that's since been unplugged (or the
// implausible near-origin rect FRO101 was filed against) is otherwise silently restored off-screen
// or too small to use. Skips the display check entirely when there are no displays at all (a
// genuinely headless CI runner) since there is nothing to validate placement against there --
// restoreBoundsOrDefault()'s own centreWithSize()/setBounds() fallback already handles that case.
bool isPlausibleRestoredBounds(const juce::Rectangle<int>& rect) {
    if (rect.getWidth() < kMinPlausibleWidth || rect.getHeight() < kMinPlausibleHeight)
        return false;
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (displays.isEmpty())
        return true;
    for (const auto& display : displays)
        if (display.totalArea.intersects(rect))
            return true;
    return false;
}
} // namespace

DetachedPanelWindow::DetachedPanelWindow(juce::Component& panel, juce::DrawableButton& headerButton,
                                         juce::Label& headerTitle, juce::String boundsKey,
                                         juce::ApplicationProperties* appProperties,
                                         synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager)
    : juce::DocumentWindow(headerTitle.getText(), juce::Colours::darkgrey, juce::DocumentWindow::closeButton,
                           /*addToDesktop*/ false)
    , panel_(panel)
    , boundsKey_(std::move(boundsKey))
    , appProperties_(appProperties)
    , shortcutManager_(shortcutManager) {
    // addToDesktop=false above: constructing this window (what every headless test does) never
    // creates a native peer -- only a later setVisible(true) does, for real use. See
    // HostedPluginEditorWindow's sibling comment.
    //
    // restoreBoundsOrDefault() MUST run before setUsingNativeTitleBar/setResizable/
    // setContentNonOwned below: each of those can synchronously fire OUR OWN resized()/moved()
    // overrides against whatever transient default bounds a freshly constructed DocumentWindow
    // starts with (persistBounds() runs on every such callback). Restoring first means those
    // early callbacks just re-persist the (already-correct) restored value instead of clobbering
    // the real persisted key with the pre-restore default before it's ever been read.
    restoreBoundsOrDefault();

    setUsingNativeTitleBar(true);
    setResizable(true, false);

    // Borrows panel/button/title -- see the header comment on why this is setContentNonOwned, not
    // setContentOwned: none of the three are ours to delete. Built BEFORE setLookAndFeel() below so
    // applyHeaderButtonIcon() (called from the lookAndFeelChanged() that setLookAndFeel() fires
    // synchronously) already has a real content_->button to apply the icon to.
    content_ = std::make_unique<Content>(panel, headerButton, headerTitle);
    setContentNonOwned(content_.get(), /*resizeToFit*/ false);

    // Plugin-mode seam (docs/mixer/panel.md): our OWN scope, never Desktop::setDefaultLookAndFeel.
    // A null `lookAndFeel` (a headless test that doesn't care) leaves this on JUCE's stock LnF --
    // harmless, since nothing here asserts a themed colour.
    if (lookAndFeel != nullptr)
        setLookAndFeel(lookAndFeel);

    // MainComponent's TooltipWindow only covers its own component tree -- a second top-level
    // window needs its own, or the detach button's tooltip (and every control inside the hosted
    // panel) never shows here.
    tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this);

    // Repaints our own focus-region root's accent outline on focus changes -- see
    // MainComponent::globalFocusChanged, the same idiom, scoped to this window's own registry.
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

DetachedPanelWindow::~DetachedPanelWindow() {
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    // Drop our reference to the borrowed content BEFORE our members (content_, and the panel/
    // button/title it never owned) unwind below -- setContentNonOwned means ~ResizableWindow would
    // never touch it anyway, but this keeps the window in a content-less state for the rest of its
    // own teardown rather than relying on that.
    setContentNonOwned(nullptr, false);
    setLookAndFeel(nullptr);
}

void DetachedPanelWindow::closeButtonPressed() {
    // Never self-destroys -- DetachablePanelHost owns this window in a unique_ptr and is the only
    // thing allowed to reset (and so destroy) it, via setDetached(false). Same contract as
    // HostedPluginEditorWindow::closeButtonPressed.
    if (onCloseRequested)
        onCloseRequested();
}

void DetachedPanelWindow::moved() {
    juce::DocumentWindow::moved();
    persistBounds();
}

void DetachedPanelWindow::resized() {
    juce::DocumentWindow::resized();
    persistBounds();
}

void DetachedPanelWindow::lookAndFeelChanged() {
    juce::DocumentWindow::lookAndFeelChanged();
    // FRO102: the constructor's juce::Colours::darkgrey literal above is only ever the fallback for
    // a null `lookAndFeel` (headless tests) -- once a real synth::theme::AppLookAndFeel is in play,
    // replace it with the theme's own surface token (same accessor DetachablePanelHost::applyIcon()
    // and the mixer columns already use: lf->getTheme().colors), so an empty/undersized hosted
    // panel shows the app's themed surface behind it, not a flat stock-JUCE grey. setLookAndFeel()
    // fires this synchronously (Component::sendLookAndFeelChange()), so it applies on construction,
    // on any later setLookAndFeel() swap, and once more (harmlessly) as the destructor clears it.
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        setBackgroundColour(lf->getTheme().colors.surface);
        applyHeaderButtonIcon(*lf);
    }
}

void DetachedPanelWindow::applyHeaderButtonIcon(synth::theme::AppLookAndFeel& lf) {
    // FRO228: the header button is BORROWED from DetachablePanelHost (this class's own header
    // comment) -- DetachablePanelHost::applyIcon() only ever runs from ITS OWN lookAndFeelChanged(),
    // which stops reaching this button the moment setDetached(true) reparents it in here (an
    // icon-only ImageFitted DrawableButton with no Drawable ever assigned to it paints nothing at
    // all, not a placeholder). Re-applying the SAME icon that method uses, from THIS window's own
    // lookAndFeelChanged() instead, keeps the button correct regardless of whether the host's own
    // callback ran before or after the reparent.
    if (content_ == nullptr)
        return;
    auto base = lf.getIcon(synth::theme::Icon::ActionDetachWindow);
    if (base == nullptr)
        return;
    const auto& colors = lf.getTheme().colors;
    auto hoverIcon = base->createCopy();
    hoverIcon->replaceColour(colors.textMuted, colors.textPrimary);
    content_->button.setImages(base.get(), hoverIcon.get(), hoverIcon.get());
}

bool DetachedPanelWindow::keyPressed(const juce::KeyPress& key) {
    // MainComponent's keyPressed dispatch (and its command-table focusNextRegion/focusPrevRegion
    // rows) is unreachable from here -- this is a separate top-level window, and MainComponent
    // installs no KeyListener (see MainComponentSetup.cpp's comment). Resolve the SAME bound key
    // the SAME way via the shared helper, then cycle only OUR OWN registry -- never
    // MainComponent's, and never leak into ITS OWN Tab-cycle order.
    if (shortcutManager_ != nullptr) {
        if (const auto forward = synth::ui::resolveFocusCycleKeyPress(key, *shortcutManager_))
            return focusRegions_.cycleFocus(*forward);
    }
    return false;
}

void DetachedPanelWindow::registerHostedPanelFocusRegion(const juce::String& id, juce::Component& root) {
    // Exactly one region: a detached panel has no closed state of its own (isOpen/open both null),
    // the same shape MainComponent gives the graph canvas region.
    focusRegions_.clear();
    focusRegions_.addRegion({id, &root, nullptr, nullptr});
}

void DetachedPanelWindow::globalFocusChanged(juce::Component*) {
    for (const auto& region : focusRegions_.getRegions())
        if (region.root != nullptr)
            region.root->repaint();
}

void DetachedPanelWindow::persistBounds() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    // Deliberately NOT getWindowStateAsString(): that persists ResizableWindow's own
    // lastNonFullScreenPos, which only updates while the window isShowing() (see
    // ResizableWindow::updateLastPosIfShowing()) -- a window built addToDesktop=false and never
    // shown (every headless test here, and this window's own constructor calling
    // restoreBoundsOrDefault() before its owner ever calls setVisible(true)) would otherwise
    // always persist ResizableWindow's built-in default (50, 50, 256, 256) no matter what bounds
    // were actually set. A plain Rectangle<int> round trip has no such gate.
    appProperties_->getUserSettings()->setValue(boundsKey_, getBounds().toString());
    appProperties_->getUserSettings()->saveIfNeeded();
}

void DetachedPanelWindow::restoreBoundsOrDefault() {
    juce::String saved;
    if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr)
        saved = appProperties_->getUserSettings()->getValue(boundsKey_, {});
    if (saved.isNotEmpty()) {
        const auto bounds = juce::Rectangle<int>::fromString(saved);
        if (!bounds.isEmpty() && isPlausibleRestoredBounds(bounds)) {
            setBounds(bounds);
            return;
        }
    }
    // centreWithSize() reaches into juce::Component::centreWithSize ->
    // detail::ComponentHelpers::getParentOrMainMonitorBounds, which dereferences the primary
    // display unconditionally -- a genuinely headless runner with zero displays (Linux CI, no
    // Xvfb) segfaults here outright rather than returning a degenerate result. Same guard this
    // file's own tests already use before calling setVisible(true).
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        centreWithSize(kDefaultWidth, kDefaultHeight);
    else
        setBounds(0, 0, kDefaultWidth, kDefaultHeight);
}

} // namespace synth::ui
