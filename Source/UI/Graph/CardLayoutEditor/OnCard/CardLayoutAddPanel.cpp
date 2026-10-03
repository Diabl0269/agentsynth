// CardLayoutAddPanel.cpp -- the Add control panel's search, rows, keys and screen-reader wiring.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutAddPanel.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kPad = 10;
constexpr int kSearchH = 28;
constexpr int kLineH = 16;
constexpr int kGap = 6;
constexpr int kFocusTries = 60;

void styleLine(juce::Label& label) {
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    label.setInterceptsMouseClicks(false, false);
    label.setAccessible(false);
}

} // namespace

CardLayoutAddPanel::CardLayoutAddPanel() {
    setTitle("Add control");
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    search_.setTitle("Search hidden controls");
    search_.setTooltip("Type to find a hidden control. Up and Down choose, Return adds it, Escape closes");
    search_.setTextToShowWhenEmpty("Search", juce::Colours::grey);
    search_.setMultiLine(false);
    search_.setSelectAllWhenFocused(true);
    removeHiddenTabStops(search_);
    search_.onTextChange = [this] {
        query_ = search_.getText();
        rebuildRows({});
    };
    search_.onNavigationKey = [this](const juce::KeyPress& key) { return handleNavigationKey(key); };
    addAndMakeVisible(search_);
    for (auto* line : {&count_, &hint_}) {
        styleLine(*line);
        addAndMakeVisible(*line);
    }
    viewport_.setTitle("Hidden controls");
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    lookAndFeelChanged();
}

void CardLayoutAddPanel::lookAndFeelChanged() {
    const auto& colours = synth::theme::themeOf(*this).colors;
    for (auto* line : {&count_, &hint_})
        line->setColour(juce::Label::textColourId, colours.textMuted);
}

void CardLayoutAddPanel::setControls(std::vector<AddableControl> controls) {
    all_ = std::move(controls);
    rebuildRows(chosen_ >= 0 && chosen_ < rows_.size() ? rows_[chosen_]->getControl().paramId : juce::String());
}

void CardLayoutAddPanel::setQueryForTest(const juce::String& query) {
    search_.setText(query, false); // the editor reports a typed change a message later; a test cannot wait for it
    query_ = query;
    rebuildRows({});
}

CardLayoutAddRow* CardLayoutAddPanel::getRowForTest(const juce::String& paramId) const {
    for (auto* row : rows_)
        if (row->getControl().paramId == paramId)
            return row;
    return nullptr;
}

juce::StringArray CardLayoutAddPanel::getRowNamesForTest() const {
    juce::StringArray names;
    for (auto* row : rows_)
        names.add(row->getControl().name);
    return names;
}

// One row per match, best first; the row that was chosen keeps the choice, else the first is chosen.
void CardLayoutAddPanel::rebuildRows(const juce::String& keepChosen) {
    rows_.clear();
    for (const auto& control : matchingControls(all_, query_)) {
        auto* row = rows_.add(new CardLayoutAddRow(control));
        row->setQuery(query_);
        const auto id = control.paramId;
        // A pick or a drop rebuilds the rows, this one's callbacks with them: the id goes out as a copy.
        row->onPick = [this, id] {
            if (onPick)
                onPick(juce::String(id));
        };
        row->onDrag = [this, id](CardLayoutAddRow::DragPhase phase, juce::Point<int> at) {
            if (onDrag)
                onDrag(juce::String(id), phase, at);
        };
        list_.addAndMakeVisible(row);
    }
    count_.setText(addCountText(rows_.size(), (int)all_.size(), query_), juce::dontSendNotification);
    setSize(kWidth, arrange());
    int chosen = rows_.isEmpty() ? -1 : 0;
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i]->getControl().paramId == keepChosen)
            chosen = i;
    chosen_ = -1;
    choose(chosen);
}

// Up and Down walk the rows, Return adds the chosen one, Escape closes. Everything else is typing.
bool CardLayoutAddPanel::handleNavigationKey(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onRequestClose)
            onRequestClose();
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        pickChosen();
        return true;
    }
    const int step = key == juce::KeyPress::downKey ? 1 : key == juce::KeyPress::upKey ? -1 : 0;
    if (step == 0 || rows_.isEmpty())
        return step != 0;
    choose(juce::jlimit(0, rows_.size() - 1, chosen_ + step));
    return true;
}

void CardLayoutAddPanel::pickChosen() {
    if (chosen_ >= 0 && chosen_ < rows_.size() && onPick)
        onPick(juce::String(rows_[chosen_]->getControl().paramId));
}

// Scrolls the chosen row into view and tells a screen reader which one it is.
void CardLayoutAddPanel::choose(int index) {
    if (index == chosen_)
        return;
    chosen_ = index;
    for (int i = 0; i < rows_.size(); ++i)
        rows_[i]->setSelected(i == chosen_);
    if (chosen_ < 0)
        return;
    const int top = chosen_ * CardLayoutAddRow::kHeight;
    const int view = viewport_.getViewPositionY();
    if (top < view)
        viewport_.setViewPosition(0, top);
    else if (top + CardLayoutAddRow::kHeight > view + viewport_.getHeight())
        viewport_.setViewPosition(0, top + CardLayoutAddRow::kHeight - viewport_.getHeight());
    juce::AccessibilityHandler::postAnnouncement(rows_[chosen_]->getControl().name + ", " + juce::String(chosen_ + 1) +
                                                     " of " + juce::String(rows_.size()),
                                                 juce::AccessibilityHandler::AnnouncementPriority::low);
}

// Lays the parts out top to bottom for the current width and answers the height they need.
int CardLayoutAddPanel::arrange() {
    const int width = getWidth() - 2 * kPad;
    const int rowsH = std::min(rows_.size(), kMaxVisibleRows) * CardLayoutAddRow::kHeight;
    int y = kPad;
    search_.setBounds(kPad, y, width, kSearchH);
    y += kSearchH + kGap;
    count_.setBounds(kPad, y, width, kLineH);
    y += kLineH + kGap;
    viewport_.setBounds(kPad - 4, y, width + 8, rowsH);
    list_.setSize(viewport_.getMaximumVisibleWidth(), rows_.size() * CardLayoutAddRow::kHeight);
    for (int i = 0; i < rows_.size(); ++i)
        rows_[i]->setBounds(0, i * CardLayoutAddRow::kHeight, list_.getWidth(), CardLayoutAddRow::kHeight);
    y += rowsH + (rowsH > 0 ? kGap : 0);
    hint_.setBounds(kPad, y, width, kLineH);
    return y + kLineH + kPad;
}

void CardLayoutAddPanel::resized() { arrange(); }

bool CardLayoutAddPanel::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onRequestClose)
        onRequestClose();
    return true;
}

bool CardLayoutAddPanel::focusIsInside() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (focused == this || isParentOf(focused));
}

// A CallOutBox attaches its content after construction and only becomes the key window a moment after it
// is shown, so the search field takes focus as soon as the window can give it.
void CardLayoutAddPanel::timerCallback() {
    auto* peer = getPeer();
    if (peer != nullptr && isShowing() && !focusIsInside()) {
        if (!peer->isFocused()) {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->toFront(true);
        } else {
            search_.grabKeyboardFocus();
        }
    }
    if (focusIsInside() || ++focusTries_ > kFocusTries)
        stopTimer();
}

void CardLayoutAddPanel::parentHierarchyChanged() {
    if (getParentComponent() != nullptr && !focusIsInside()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

void CardLayoutAddPanel::visibilityChanged() {
    if (isShowing() && !focusIsInside() && !isTimerRunning()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

} // namespace synth::ui
