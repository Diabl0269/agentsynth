// Concern: FRO12 (P9-6) -- DetachablePanelHost's dock/detach state machine, header layout, and
// themed icon application.
#include "DetachablePanelHost.h"

namespace synth::ui {

DetachablePanelHost::DetachablePanelHost(juce::Component& panel, juce::String title, juce::String boundsKey,
                                         juce::ApplicationProperties* appProperties,
                                         synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager)
    : panel_(panel)
    , title_(std::move(title))
    , boundsKey_(std::move(boundsKey))
    , appProperties_(appProperties)
    , lookAndFeel_(lookAndFeel)
    , shortcutManager_(shortcutManager) {
    titleLabel_.setText(title_, juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    detachButton_.setClickingTogglesState(false);
    // Button's ctor seeds its text from the component name passed to DrawableButton's ctor
    // ("detachPanel") -- clear it explicitly so an ImageFitted, icon-only button never draws or
    // reports button text (Button::getButtonText() otherwise returns that name verbatim).
    detachButton_.setButtonText({});
    detachButton_.onClick = [this] { toggleDetach(); };
    addAndMakeVisible(detachButton_);

    addAndMakeVisible(panel_);

    applyTooltip();
}

DetachablePanelHost::~DetachablePanelHost() {
    // If still detached when this host itself is torn down (app/plugin-editor shutdown with a
    // panel open in its own window), just drop the window -- panel_ is owned elsewhere (by
    // BottomDockComponent/MainComponent) and outlives us regardless; ~DetachedPanelWindow's own
    // destructor drops its reference to the borrowed panel/button/title before unwinding, so this
    // never touches freed memory either way.
    window_.reset();
}

void DetachablePanelHost::setEmbeddedHeader(bool embedded) {
    if (embeddedHeader_ == embedded)
        return;
    embeddedHeader_ = embedded;
    resized();
    repaint();
}

void DetachablePanelHost::setHostedPanelFocusRegion(juce::String id, juce::Component& root) {
    focusRegionId_ = std::move(id);
    focusRegionRoot_ = &root;
    if (window_ != nullptr)
        window_->registerHostedPanelFocusRegion(focusRegionId_, *focusRegionRoot_);
}

void DetachablePanelHost::setDetached(bool detached) {
    if (detached == isDetached())
        return;

    if (detached) {
        window_ = std::make_unique<DetachedPanelWindow>(panel_, detachButton_, titleLabel_, boundsKey_, appProperties_,
                                                        lookAndFeel_, shortcutManager_);
        window_->onCloseRequested = [this] { setDetached(false); };
        if (focusRegionRoot_ != nullptr)
            window_->registerHostedPanelFocusRegion(focusRegionId_, *focusRegionRoot_);
        // FRO12 follow-up: promote the window to a real native peer BEFORE setVisible(true) --
        // setVisible() alone never creates one (see setCreatesNativeWindows()'s doc comment on
        // DetachablePanelHost.h). window_'s bounds are already the restored/centred-default ones
        // from its own constructor's restoreBoundsOrDefault(); TopLevelWindow::addToDesktop() reads
        // the component's CURRENT bounds to size/position the native peer (juce_Component.cpp's
        // addToDesktop() takes getScreenPosition() and never touches the existing size), so those
        // bounds survive the promotion unchanged -- no bounds re-apply needed after this call.
        if (createsNativeWindows_ && hasPrimaryDisplayForNativeWindow())
            addDetachedWindowToDesktop(*window_);
        window_->setVisible(true);
        window_->toFront(true);
    } else {
        // Reparents titleLabel_/detachButton_/panel_ back into OUR OWN tree -- addAndMakeVisible
        // auto-detaches a child from whatever parent (the window's Content) currently holds it,
        // same idiom BottomDockComponent's own constructor uses to take timelinePanel_ in.
        addAndMakeVisible(titleLabel_);
        addAndMakeVisible(detachButton_);
        addAndMakeVisible(panel_);
        window_.reset();
        resized();
    }

    applyTooltip();
    if (onDetachedStateChanged)
        onDetachedStateChanged();
}

void DetachablePanelHost::resized() {
    if (isDetached())
        return; // nothing docked to lay out -- the window lays out its own borrowed copy

    auto bounds = getLocalBounds();
    const bool showOwnHeader = !embeddedHeader_;
    titleLabel_.setVisible(showOwnHeader);
    detachButton_.setVisible(showOwnHeader);
    if (showOwnHeader) {
        auto header = bounds.removeFromTop(kHeaderStripHeight);
        detachButton_.setBounds(header.removeFromRight(kHeaderStripHeight));
        titleLabel_.setBounds(header);
    }
    panel_.setBounds(bounds);
}

void DetachablePanelHost::paint(juce::Graphics&) {
    // Intentionally blank -- the header strip is two plain themed child components (a Label + a
    // DrawableButton), not a painted background.
}

void DetachablePanelHost::lookAndFeelChanged() { applyIcon(); }

void DetachablePanelHost::applyIcon() {
    // dynamic_cast<AppLookAndFeel*> with a null fallback -- same headless-safe convention
    // MainComponent::applyToolbarIcons uses, so a test LnF (or none yet, before this Component is
    // added to a parent hierarchy) just leaves the button blank rather than crashing.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto base = lf->getIcon(synth::theme::Icon::ActionDetachWindow);
    if (base == nullptr)
        return;
    const auto& colors = lf->getTheme().colors;
    auto hoverIcon = base->createCopy();
    hoverIcon->replaceColour(colors.textMuted, colors.textPrimary);
    detachButton_.setImages(base.get(), hoverIcon.get(), hoverIcon.get());
}

void DetachablePanelHost::applyTooltip() {
    // FRO228: setButtonText({}) above clears getButtonText() (the ButtonAccessibilityHandler
    // fallback getTitle() otherwise uses), so without an explicit title this icon-only button was
    // unnamed to a screen reader both docked AND inside its own DetachedPanelWindow -- same fix as
    // BottomDockComponent::refreshDetachButton()'s own detach button.
    const juce::String label = isDetached() ? "Dock back" : "Open in window";
    detachButton_.setTooltip(label);
    detachButton_.setTitle(label);
}

} // namespace synth::ui
