#include "UI/Layout/ListBoxFocusRing.h"

#include "UI/Layout/FocusRing.h"
#include <algorithm>

// Concern: finding a stock component's list boxes and drawing the selected row's focus ring over each
// (see the header).

namespace synth::ui {

namespace {
constexpr int kRepaintPollMs = 100;
constexpr float kRowRingRadius = 2.0f;

void collectListBoxes(juce::Component& parent, std::vector<juce::ListBox*>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* list = dynamic_cast<juce::ListBox*>(child))
            out.push_back(list);
        collectListBoxes(*child, out);
    }
}
} // namespace

// Transparent and click-through, sized to the list it sits in front of. It paints the ring over the
// rows, inside the viewport's visible area, so a row scrolled half out of view is cut like the row is.
class ListBoxFocusRing::Overlay : public juce::Component {
public:
    Overlay(const ListBoxFocusRing& owner, juce::ListBox& list)
        : owner_(owner)
        , list_(list) {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
        setOpaque(false);
        list.addAndMakeVisible(this);
        setBounds(list.getLocalBounds());
    }

    void parentSizeChanged() override { setBounds(list_.getLocalBounds()); }

    void paint(juce::Graphics& g) override {
        const int row = list_.getSelectedRow();
        if (row < 0 || !owner_.isFocused(list_))
            return;
        if (auto* viewport = list_.getViewport())
            g.reduceClipRegion(viewport->getBounds());
        paintFocusRingAlways(g, list_.getRowPosition(row, true).toFloat(), list_, kRowRingRadius);
    }

private:
    const ListBoxFocusRing& owner_;
    juce::ListBox& list_;
};

ListBoxFocusRing::ListBoxFocusRing(juce::Component& root)
    : root_(root) {
    root_.addComponentListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    rescan();
}

ListBoxFocusRing::~ListBoxFocusRing() {
    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    root_.removeComponentListener(this);
    for (auto& watched : watched_)
        if (watched != nullptr)
            watched->removeComponentListener(this);
}

void ListBoxFocusRing::watchParentOf(juce::ListBox& list) {
    auto* parent = list.getParentComponent();
    if (parent == nullptr || parent == &root_)
        return;
    if (std::any_of(watched_.begin(), watched_.end(), [parent](const auto& w) { return w.getComponent() == parent; }))
        return;
    parent->addComponentListener(this);
    watched_.emplace_back(parent);
}

// Rescanning runs from childrenChanged callbacks, and adding an overlay changes a list's children, so a
// list already holding its overlay is left alone: the scan settles after one pass.
void ListBoxFocusRing::rescan() {
    std::vector<juce::ListBox*> found;
    collectListBoxes(root_, found);

    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&found](const Entry& e) {
                                      return e.list == nullptr || std::find(found.begin(), found.end(),
                                                                            e.list.getComponent()) == found.end();
                                  }),
                   entries_.end());
    watched_.erase(std::remove_if(watched_.begin(), watched_.end(), [](const auto& w) { return w == nullptr; }),
                   watched_.end());

    for (auto* list : found) {
        watchParentOf(*list);
        const bool known = std::any_of(entries_.begin(), entries_.end(),
                                       [list](const Entry& e) { return e.list.getComponent() == list; });
        if (known)
            continue;
        Entry entry;
        entry.list = list;
        entry.overlay = std::make_unique<Overlay>(*this, *list);
        entries_.push_back(std::move(entry));
    }
}

bool ListBoxFocusRing::isFocused(const juce::ListBox& list) const {
    return forcedFocus_.getComponent() == &list || list.hasKeyboardFocus(true);
}

void ListBoxFocusRing::setFocusedListForTest(juce::ListBox* list) {
    forcedFocus_ = list;
    for (auto& entry : entries_)
        if (entry.overlay != nullptr)
            entry.overlay->repaint();
    updateTimer();
}

// A list that takes focus with nothing selected selects its first row, so the ring has a row to sit
// on. Selecting a row never changes what it does (the selector's ticks flip on Return or a click).
void ListBoxFocusRing::globalFocusChanged(juce::Component* focused) {
    rescan();
    for (auto& entry : entries_) {
        auto* list = entry.list.getComponent();
        if (list == nullptr)
            continue;
        auto* model = list->getListBoxModel();
        if (list == focused && list->getSelectedRow() < 0 && model != nullptr && model->getNumRows() > 0)
            list->selectRow(0);
        entry.lastRow = list->getSelectedRow();
        entry.overlay->repaint();
    }
    updateTimer();
}

void ListBoxFocusRing::updateTimer() {
    const bool anyFocused = std::any_of(entries_.begin(), entries_.end(),
                                        [this](const Entry& e) { return e.list != nullptr && isFocused(*e.list); });
    if (anyFocused && !isTimerRunning())
        startTimer(kRepaintPollMs);
    else if (!anyFocused)
        stopTimer();
}

// The selector's lists change selection inside JUCE (Up / Down, a click) with no hook to listen to, so
// while one is focused the selected row is polled; only a change repaints.
void ListBoxFocusRing::timerCallback() {
    for (auto& entry : entries_) {
        auto* list = entry.list.getComponent();
        if (list == nullptr || !isFocused(*list))
            continue;
        const int row = list->getSelectedRow();
        if (row != entry.lastRow) {
            entry.lastRow = row;
            entry.overlay->repaint();
        }
    }
}

} // namespace synth::ui
