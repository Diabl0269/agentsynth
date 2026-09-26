// ControllerSurfacePageStrip.cpp -- FRO142: see the header. Reuses juce::TextButton exactly like
// every other row in this panel (ControllerSurfaceToolbar's own buttons); the only bespoke bit is
// PageButton's right-click forward, since a plain juce::TextButton has no "distinguish a right-
// click from onClick" hook of its own -- juce::Button::internalClickCallback fires onClick for
// every mouse button a juce::Button was set up to respond to, and setTriggeredOnMouseDown-style
// flags only change WHEN the click fires, never which button triggered it.

#include "UI/MidiRemote/ControllerSurface/ControllerSurfacePageStrip.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace test_hooks {
// Mirrors ControllersListComponent.cpp's own hook (same reason: a real
// juce::PopupMenu::showMenuAsync() segfaults on a headless Linux CI runner with no display).
std::function<void(juce::PopupMenu&)>& pageStripContextMenuHookForTest() {
    static std::function<void(juce::PopupMenu&)> hook;
    return hook;
}
} // namespace test_hooks

class ControllerSurfacePageStrip::PageButton : public juce::TextButton {
public:
    explicit PageButton(int page)
        : juce::TextButton(juce::String(page))
        , page_(page) {
        setComponentID("pageButton" + juce::String(page));
        setTitle("Page " + juce::String(page)); // accessible name (Source/UI/CLAUDE.md's accessibility rule)
        setClickingTogglesState(false);         // the strip drives the "on" state itself -- see setActive()
    }

    int getPage() const noexcept { return page_; }
    // The active page gets the theme's accent colour, same token every other selected-state paint
    // in this panel reads (ControllerSurfaceCell.cpp, ControllersListComponent.cpp) -- never a
    // hard-coded colour that would ignore a user's theme override.
    void setActive(bool active) {
        setToggleState(active, juce::dontSendNotification);
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
        setColour(juce::TextButton::buttonOnColourId, accent);
    }

    std::function<void(int page)> onRightClick;

private:
    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            if (onRightClick)
                onRightClick(page_);
            return; // never starts a left-click-shaped press/click cycle for a right-click
        }
        juce::TextButton::mouseDown(event);
    }

    int page_ = 1;
};

ControllerSurfacePageStrip::ControllerSurfacePageStrip() {
    addButton_.setButtonText("+");
    addButton_.setComponentID("addPageButton");
    addButton_.setTitle("Add page");
    addButton_.setTooltip("Add a mapping page");
    addButton_.onClick = [this] {
        if (onAddPageRequested)
            onAddPageRequested();
    };
    addAndMakeVisible(addButton_);
    rebuildButtons();
}

ControllerSurfacePageStrip::~ControllerSurfacePageStrip() = default;

void ControllerSurfacePageStrip::setPages(int effectivePageCount, int activePage) {
    effectivePageCount = juce::jmax(1, effectivePageCount);
    activePage = juce::jlimit(1, effectivePageCount, activePage);
    if (effectivePageCount == effectivePageCount_ && activePage == activePage_)
        return;

    const bool countChanged = effectivePageCount != effectivePageCount_;
    effectivePageCount_ = effectivePageCount;
    activePage_ = activePage;

    if (countChanged) {
        rebuildButtons();
    } else {
        for (auto* button : pageButtons_)
            button->setActive(button->getPage() == activePage_);
    }
    resized();
}

void ControllerSurfacePageStrip::rebuildButtons() {
    pageButtons_.clear();
    for (int page = 1; page <= effectivePageCount_; ++page) {
        auto* button = pageButtons_.add(new PageButton(page));
        button->setActive(page == activePage_);
        button->onClick = [this, page] {
            if (page != activePage_ && onPageSelected)
                onPageSelected(page);
        };
        button->onRightClick = [this](int page_) { showDeletePageMenu(page_); };
        addAndMakeVisible(button);
    }
    addButton_.toFront(false);
}

void ControllerSurfacePageStrip::showDeletePageMenu(int page) {
    if (page <= 1) // page 1 always exists -- nothing to offer
        return;
    auto menu = juce::PopupMenu();
    menu.addItem("Delete page", [this, page] {
        if (onDeletePageRequested)
            onDeletePageRequested(page);
    });
    if (auto& hook = test_hooks::pageStripContextMenuHookForTest())
        hook(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options());
}

juce::Button* ControllerSurfacePageStrip::getPageButtonForTest(int page) const {
    for (auto* button : pageButtons_)
        if (button->getPage() == page)
            return button;
    return nullptr;
}

void ControllerSurfacePageStrip::resized() {
    auto bounds = getLocalBounds().reduced(0, (getHeight() - kButtonSize) / 2);
    for (auto* button : pageButtons_) {
        button->setBounds(bounds.removeFromLeft(kButtonSize));
        bounds.removeFromLeft(kGap);
    }
    addButton_.setBounds(bounds.removeFromLeft(kButtonSize));
}

void ControllerSurfacePageStrip::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black);
}

} // namespace synth::ui
