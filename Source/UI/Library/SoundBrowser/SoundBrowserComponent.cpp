// SoundBrowserComponent.cpp -- construction, layout, the search field and the scroll bar of the Sounds view. Rows and
// navigation are in SoundBrowserNavigation.cpp, painting in SoundBrowserPainting.cpp, keys and mouse in
// SoundBrowserInput.cpp, the screen-reader list in SoundBrowserAccessibility.cpp.
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
int focusGainsWhileTearingDown = 0;
}

int SoundBrowserComponent::focusGainsWhileTearingDownForTest() { return focusGainsWhileTearingDown; }

SoundBrowserComponent::SoundBrowserComponent(synth::soundbrowser::SoundIndex& index,
                                             synth::soundbrowser::SoundLists& lists)
    : index_(index)
    , lists_(lists) {
    setTitle("Sounds");
    setTooltip("Sounds. Up and Down move, Right opens a folder, Left goes back, F adds to Favourites. Return loads it "
               "into the selected Sampler or audio track.");
    setWantsKeyboardFocus(true);

    addChildComponent(scrollBar_);
    scrollBar_.setAutoHide(false);
    scrollBar_.addListener(this);

    search_.setMultiLine(false);
    search_.setReturnKeyStartsNewLine(false);
    search_.setEscapeAndReturnKeysConsumed(true);
    search_.setSelectAllWhenFocused(true);
    search_.setJustification(juce::Justification::centredLeft);
    search_.setBorder(juce::BorderSize<int>(0));
    search_.setIndents(6, 0);
    search_.setFont(juce::Font(juce::FontOptions(13.0f)));
    search_.setTitle("Search sounds");
    search_.setTooltip("Search every place by name or tag");
    removeHiddenTabStops(search_);
    search_.onTextChange = [this] { setQuery(search_.getText()); };
    search_.onNavigationKey = [this](const juce::KeyPress& key) { return handleSearchKey(key); };
    addAndMakeVisible(search_);
    applySearchColours();

    setupFilters();
    setupPreview();
    setupFolders();
    index_.addListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    rebuildRows();
}

SoundBrowserComponent::~SoundBrowserComponent() {
    // The members go after this body while this is still the dynamic type: a focused child (a footer or panel control)
    // that goes then hands the keyboard to this view, and focusGained would read rows that are already gone.
    tearingDown_ = true;
    if (soundDragging_)
        SoundDragState::end();
    if (hasKeyboardFocus(true))
        giveAwayKeyboardFocus();
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    index_.removeListener(this);
    if (shapeLibrary_ != nullptr)
        shapeLibrary_->removeListener(this);
    if (installer_ != nullptr)
        installer_->removeListener(this);
    if (folderBook_ != nullptr)
        folderBook_->removeListener(this);
    if (vblank_.has_value()) {
        listAnim_.stop(*vblank_);
        panelAnim_.stop(*vblank_);
        dropAnim_.stop(*vblank_);
    }
    scrollBar_.removeListener(this);
}

void SoundBrowserComponent::focusSearchField() { search_.grabKeyboardFocus(); }

void SoundBrowserComponent::focusList() {
    grabKeyboardFocus();
    refreshAccessibilityValue();
}

void SoundBrowserComponent::soundIndexChanged() { rebuildRows(); }

void SoundBrowserComponent::resized() { layoutTop(); }

void SoundBrowserComponent::lookAndFeelChanged() {
    applySearchColours();
    updateScrollBar();
}

void SoundBrowserComponent::applySearchColours() {
    const auto& c = synth::theme::themeOf(*this).colors;
    search_.setColour(juce::TextEditor::backgroundColourId, c.surface);
    search_.setColour(juce::TextEditor::textColourId, c.textPrimary);
    search_.setColour(juce::TextEditor::outlineColourId, c.border);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, c.border);
    search_.setTextToShowWhenEmpty("Search sounds...", c.textMuted);
}

void SoundBrowserComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) {
    if (bar == &scrollBar_)
        setScrollOffset(juce::roundToInt(newRangeStart));
}

juce::Rectangle<int> SoundBrowserComponent::listArea() const {
    return {0, getListTop(), getWidth(), juce::jmax(0, getHeight() - getListTop() - footerHeight())};
}

int SoundBrowserComponent::contentWidth() const {
    return getWidth() - (scrollBar_.isVisible() ? scrollBar_.getWidth() : 0);
}

bool SoundBrowserComponent::setScrollOffset(int offset) {
    const int maxOffset = juce::jmax(0, (int)rows_.size() * kRowHeight - listArea().getHeight());
    const int clamped = juce::jlimit(0, maxOffset, offset);
    if (clamped == scrollOffset_)
        return false;
    scrollOffset_ = clamped;
    repaint(listArea());
    return true;
}

void SoundBrowserComponent::updateScrollBar() {
    const int viewport = listArea().getHeight();
    const int total = (int)rows_.size() * kRowHeight;
    const bool needed = viewport > 0 && total > viewport;
    scrollBar_.setVisible(needed);
    setScrollOffset(scrollOffset_);
    if (!needed)
        return;
    const int barWidth = 10;
    scrollBar_.setBounds(getWidth() - barWidth, getListTop(), barWidth, viewport);
    scrollBar_.setSingleStepSize((double)kRowHeight);
    scrollBar_.setRangeLimits(0.0, (double)total, juce::dontSendNotification);
    scrollBar_.setCurrentRange((double)scrollOffset_, (double)viewport, juce::dontSendNotification);
}

void SoundBrowserComponent::scrollSelectionIntoView() {
    if (selected_ < 0)
        return;
    const int top = selected_ * kRowHeight;
    const int bottom = top + kRowHeight;
    const int viewport = listArea().getHeight();
    if (top < scrollOffset_)
        setScrollOffset(top);
    else if (bottom > scrollOffset_ + viewport)
        setScrollOffset(bottom - viewport);
    updateScrollBar();
}

void SoundBrowserComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (scrollBar_.isVisible())
        scrollBar_.mouseWheelMove(e.getEventRelativeTo(&scrollBar_), wheel);
    else
        juce::Component::mouseWheelMove(e, wheel);
}

void SoundBrowserComponent::focusGained(FocusChangeType) {
    if (tearingDown_) {
        ++focusGainsWhileTearingDown;
        return;
    }
    repaint();
    refreshAccessibilityValue();
}

void SoundBrowserComponent::focusLost(FocusChangeType) { repaint(); }

} // namespace synth::ui
