#pragma once

// The on-card editor's Add control panel: a search field over the controls the card does not show, a count
// line, the matching rows and a hint line. It lives in a call-out the editor launches and holds no layout of
// its own: a pick or a drag goes out through a callback and the editor hands back the controls still off the
// card. Up/Down move the chosen row, Return adds it, typing searches.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "CardLayoutAddRow.h"
#include "UI/Layout/NavigationSearchField.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

class CardLayoutAddPanel final
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr int kWidth = 260;
    static constexpr int kMaxVisibleRows = 7;

    CardLayoutAddPanel();

    /** Replaces the list, keeping the query; the chosen row stays on the same control when it is still there. */
    void setControls(std::vector<AddableControl> controls);

    std::function<void(const juce::String& paramId)> onPick;
    std::function<void(const juce::String& paramId, CardLayoutAddRow::DragPhase, juce::Point<int> screenPosition)>
        onDrag;
    std::function<void()> onRequestClose;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;

    // ---- Test seams ---------------------------------------------------------------------------------
    NavigationSearchField& getSearchFieldForTest() noexcept { return search_; }
    void setQueryForTest(const juce::String& query);
    int getRowCountForTest() const { return rows_.size(); }
    CardLayoutAddRow* getRowForTest(int index) const { return rows_[index]; }
    CardLayoutAddRow* getRowForTest(const juce::String& paramId) const;
    juce::StringArray getRowNamesForTest() const;
    int getChosenIndexForTest() const noexcept { return chosen_; }
    juce::String getCountTextForTest() const { return count_.getText(); }
    juce::String getHintTextForTest() const { return hint_.getText(); }

private:
    void rebuildRows(const juce::String& keepChosen);
    void choose(int index);
    bool handleNavigationKey(const juce::KeyPress& key);
    void pickChosen();
    int arrange();
    void timerCallback() override;
    bool focusIsInside() const;

    std::vector<AddableControl> all_;
    juce::String query_;
    NavigationSearchField search_;
    juce::Label count_;
    juce::Label hint_{{}, "Click to add, or drag onto the card"};
    juce::Viewport viewport_;
    juce::Component list_;
    juce::OwnedArray<CardLayoutAddRow> rows_;
    int chosen_ = -1;
    int focusTries_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutAddPanel)
};

} // namespace synth::ui
