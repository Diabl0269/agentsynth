// ModuleLibraryHelpPopover.cpp -- the "?" help popover: pin/float vs CallOutBox hosting,
// its floating position/clamping, and the button bounds paint() and the mouse handlers share.
#include "ModuleLibraryComponent.h"
#include "UI/Layout/PopupMotion.h"

juce::Rectangle<int> ModuleLibraryComponent::getHelpButtonBounds() noexcept {
    return {kHelpButtonMargin, kSearchHeight + (kTopStripHeight - kHelpButtonSize) / 2, kHelpButtonSize,
            kHelpButtonSize};
}

synth::ui::ModuleLibraryHelpPopup* ModuleLibraryComponent::createHelpPopupForTest() {
    ensureHelpPopupCreated();
    return helpPopup_.get();
}

void ModuleLibraryComponent::refreshHelpPopoverForTest() {
    if (helpPopup_)
        helpPopup_->refreshShortcutSection(shortcutManager);
}

void ModuleLibraryComponent::showHelpPopover() {
    // A call-out that is still fading out from its close is left to finish; the click that asked for it again lands
    // on a window that ignores the mouse anyway.
    if (helpCallOutBox_ != nullptr && synth::ui::PopupMotion::isDismissing(*helpCallOutBox_))
        return;
    ensureHelpPopupCreated();
    helpPopup_->refreshShortcutSection(shortcutManager);
    if (helpPopup_->isPinned()) {
        helpFade_->setShown(true); // a panel that was fading out turns back from where its opacity is
        helpPopup_->toFront(true);
        return;
    }
    launchHelpCallOutBox();
}

void ModuleLibraryComponent::launchHelpCallOutBox() {
    if (helpFade_ != nullptr)
        helpFade_->snapTo(true); // the content is about to move into the box: no fade of the panel may run on it
    helpCallOutBox_ =
        std::make_unique<juce::CallOutBox>(*helpPopup_, localAreaToGlobal(getHelpButtonBounds()), nullptr);
    helpCallOutBox_->setVisible(true);
    // deleteWhenDismissed = false: an outside click / Esc ends modal state and hides the box
    // (CallOutBox::inputAttemptWhenModal()) but never deletes it or its content, which is what
    // lets a later pin click safely reparent *helpPopup_ instead of losing it.
    helpCallOutBox_->enterModalState(true, nullptr, false);
}

void ModuleLibraryComponent::ensureHelpPopupCreated() {
    if (helpPopup_)
        return;
    helpPopup_ = std::make_unique<synth::ui::ModuleLibraryHelpPopup>(shortcutManager);
    helpFade_ = std::make_unique<synth::ui::FadeVisibility>(std::initializer_list<juce::Component*>{helpPopup_.get()});
    helpPopup_->onPinToggleRequested = [this] { setHelpPopoverPinned(!helpPopup_->isPinned()); };
    helpPopup_->onCloseRequested = [this] { closeHelpPopover(); };
}

juce::Component* ModuleLibraryComponent::floatingHelpHostFor(juce::Component& from) {
    juce::Component* top = &from;
    while (top->getParentComponent() != nullptr)
        top = top->getParentComponent();
    return top;
}

juce::Point<int> ModuleLibraryComponent::clampToHost(const juce::Component& host, const juce::Component& popup,
                                                     juce::Point<int> desired) {
    const auto hostBounds = host.getLocalBounds();
    const int maxX = juce::jmax(0, hostBounds.getWidth() - popup.getWidth());
    const int maxY = juce::jmax(0, hostBounds.getHeight() - popup.getHeight());
    return {juce::jlimit(0, maxX, desired.x), juce::jlimit(0, maxY, desired.y)};
}

juce::Point<int> ModuleLibraryComponent::defaultFloatingPosition(juce::Component& host) const {
    const auto sidebarInHost = host.getLocalArea(this, getLocalBounds());
    constexpr int kMargin = 12;
    const juce::Point<int> desired(sidebarInHost.getRight() + kMargin, juce::jmax(kMargin, sidebarInHost.getY()));
    return clampToHost(host, *helpPopup_, desired);
}

void ModuleLibraryComponent::reclampFloatingHelpPopover() {
    if (!helpPopup_ || !helpPopup_->isPinned())
        return;
    auto* parent = helpPopup_->getParentComponent();
    if (!parent)
        return;
    helpPopup_->setTopLeftPosition(clampToHost(*parent, *helpPopup_, helpPopup_->getPosition()));
}

void ModuleLibraryComponent::setHelpPopoverPinned(bool wantPinned) {
    if (!helpPopup_ || wantPinned == helpPopup_->isPinned())
        return;

    helpPopup_->setPinnedForPaint(wantPinned);

    if (wantPinned) {
        if (helpCallOutBox_) {
            helpCallOutBox_->exitModalState(0);
            helpCallOutBox_->setVisible(false);
            helpCallOutBox_.reset(); // does NOT delete *helpPopup_ — see the class comment
        }
        auto* host = floatingHelpHostFor(*this);
        helpFade_->snapTo(true);
        host->addAndMakeVisible(*helpPopup_); // auto-detaches from any previous parent
        helpPopup_->setTopLeftPosition(defaultFloatingPosition(*host));
        helpPopup_->setVisible(true);
        helpPopup_->toFront(true);
    } else {
        launchHelpCallOutBox(); // its ctor's addAndMakeVisible detaches the popup from the host
    }
}

void ModuleLibraryComponent::closeHelpPopover() {
    if (!helpPopup_)
        return;
    if (helpCallOutBox_) {
        // The live call-out fades out, then the box goes (at once when it is not on screen).
        juce::Component::SafePointer<ModuleLibraryComponent> self(this);
        synth::ui::PopupMotion::dismiss(*helpCallOutBox_, [self] {
            if (self != nullptr)
                self->finishHelpPopoverClose();
        });
        return;
    }
    if (helpPopup_->getParentComponent() != nullptr) {
        helpFade_->onHidden = [this] { finishHelpPopoverClose(); };
        helpFade_->setShown(false);
        if (helpFade_->isFading())
            return;
    }
    finishHelpPopoverClose();
}

void ModuleLibraryComponent::finishHelpPopoverClose() {
    if (!helpPopup_)
        return;
    if (helpCallOutBox_) {
        helpCallOutBox_->exitModalState(0);
        helpCallOutBox_->setVisible(false);
        helpCallOutBox_.reset();
    } else if (auto* parent = helpPopup_->getParentComponent()) {
        parent->removeChildComponent(helpPopup_.get());
    }
    helpPopup_->setVisible(false);
    helpPopup_->setPinnedForPaint(false);
}
