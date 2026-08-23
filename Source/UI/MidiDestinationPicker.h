#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// MidiDestinationPicker — a searchable multi-select list for where a timeline track's bound
// Track In node sends MIDI. Lives inside a juce::CallOutBox (the caller builds and launches the
// callout; this class never does so itself, matching ColourPickerPopup's split between the
// component and its CallOutBox launch site).
//
// The graph is the truth: toggling a row applies the change through the caller's callback, then
// immediately re-pulls the provider and rebuilds every row from what it reports. A concurrent
// graph edit (or the apply itself failing for a reason the picker can't see, e.g. a stale
// binding) must never leave a checkbox showing something the graph doesn't actually have.
namespace synth::ui {

class MidiDestinationPicker : public juce::Component {
public:
    struct Option {
        juce::String displayName;
        juce::uint32 nodeUid = 0;
        bool connected = false;
    };

    // refreshProvider re-enumerates the live destination list (called once up front, and again
    // after every toggle); applyConnection(nodeUid, connect) is the caller's hook into the graph.
    MidiDestinationPicker(std::function<std::vector<Option>()> refreshProvider,
                          std::function<void(juce::uint32, bool)> applyConnection)
        : refreshProvider_(std::move(refreshProvider))
        , applyConnection_(std::move(applyConnection)) {
        setComponentID("midiDestinationPicker");

        searchEditor_.setComponentID("midiDestinationSearch");
        searchEditor_.setMultiLine(false);
        searchEditor_.setReturnKeyStartsNewLine(false);
        searchEditor_.setEscapeAndReturnKeysConsumed(false);
        searchEditor_.setSelectAllWhenFocused(true);
        searchEditor_.setJustification(juce::Justification::centredLeft);
        searchEditor_.setTextToShowWhenEmpty("Filter destinations...", juce::Colours::grey);
        searchEditor_.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible(searchEditor_);

        addAndMakeVisible(viewport_);
        viewport_.setViewedComponent(&rowColumn_, false);
        viewport_.setScrollBarsShown(true, false);

        refreshRows();
        setSize(260, 320);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(6);
        searchEditor_.setBounds(bounds.removeFromTop(kSearchHeight));
        bounds.removeFromTop(4);
        viewport_.setBounds(bounds);
        layoutRowColumn();
    }

    // ---- Test seams -------------------------------------------------------------------------
    void setSearchTextForTest(const juce::String& text) {
        // dontSendNotification + an explicit applyFilter() call, mirroring
        // ModuleLibraryComponent::setSearchText — deterministic regardless of whether
        // juce::TextEditor's own change notification happens to run synchronously.
        searchEditor_.setText(text, juce::dontSendNotification);
        applyFilter();
    }
    juce::String getSearchTextForTest() const { return searchEditor_.getText(); }

    /** Currently-visible (post-filter) row display names, in list order. When there are no
     *  destinations at all, this returns the single hint row's text. */
    std::vector<juce::String> getVisibleRowNamesForTest() const {
        std::vector<juce::String> names;
        for (auto* row : rows_)
            if (row->isVisible())
                names.push_back(row->getDisplayedText());
        return names;
    }

    /** Toggles the row at `visibleIndex` among the currently-filtered rows (matching what
     *  getVisibleRowNamesForTest() enumerates) — a no-op when there is no such row or it is the
     *  non-interactive hint row. */
    void toggleRowForTest(int visibleIndex) {
        int i = 0;
        for (auto* row : rows_) {
            if (!row->isVisible())
                continue;
            if (i == visibleIndex) {
                row->toggleForTest();
                return;
            }
            ++i;
        }
    }

private:
    static constexpr int kSearchHeight = 26;
    static constexpr int kRowHeight = 24;

    // One row per destination option: a checkbox whose label is the destination's display name.
    // The non-interactive "bind a Track In node first" hint is also modeled as a Row (with
    // interactive_ = false) so getVisibleRowNamesForTest()'s "read every visible row's text"
    // logic doesn't need a special case for it.
    class Row : public juce::Component {
    public:
        Row(juce::String text, bool interactive, std::function<void()> onToggle)
            : interactive_(interactive)
            , onToggle_(std::move(onToggle)) {
            if (interactive_) {
                toggle_.setButtonText(text);
                // false, deliberately: with this true, juce::Button::internalClickCallback sets
                // the new toggle state and returns WITHOUT ever calling onClick (see
                // TimelineTrackHeaderComponent's mute/solo/arm buttons, which hit the exact same
                // thing and carry the identical comment). The provider's `connected` flag is the
                // truth here anyway — setConnected() (called from refreshRows()) is what actually
                // drives the checkbox's visual state, same "the doc/graph is the truth" pattern.
                toggle_.setClickingTogglesState(false);
                toggle_.onClick = [this] {
                    if (onToggle_)
                        onToggle_();
                };
                addAndMakeVisible(toggle_);
            } else {
                hintLabel_.setText(text, juce::dontSendNotification);
                hintLabel_.setJustificationType(juce::Justification::centredLeft);
                hintLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
                addAndMakeVisible(hintLabel_);
            }
        }

        void resized() override {
            auto bounds = getLocalBounds();
            if (interactive_)
                toggle_.setBounds(bounds);
            else
                hintLabel_.setBounds(bounds);
        }

        void setConnected(bool connected) { toggle_.setToggleState(connected, juce::dontSendNotification); }
        void setDisplayName(const juce::String& name) { toggle_.setButtonText(name); }

        juce::String getDisplayedText() const {
            return interactive_ ? toggle_.getButtonText() : hintLabel_.getText();
        }

        // The hint row (no destinations to offer at all) is never filtered out — there's nothing
        // for the search box to search among, so hiding it on top of that would leave an empty
        // popup with no explanation.
        void setSearchMatch(bool matches) { setVisible(!interactive_ || matches); }

        // Mirrors the "call onClick() directly" test idiom this app already uses for buttons
        // (e.g. TimelineTrackHeaderComponent's mute/solo/arm tests): with setClickingTogglesState
        // false, onClick is exactly what a real click invokes, so calling it directly here is a
        // faithful simulation — and it runs synchronously, unlike juce::Button::triggerClick()
        // (which posts an async command message).
        void toggleForTest() {
            if (interactive_ && onToggle_)
                onToggle_();
        }

    private:
        bool interactive_ = true;
        juce::ToggleButton toggle_;
        juce::Label hintLabel_;
        std::function<void()> onToggle_;
    };

    // Pulls the live option list and reconciles it against the rows already on screen.
    //
    // When the node-uid set is unchanged (the overwhelmingly common case: a toggle only flips one
    // option's `connected` flag, it doesn't change which destinations exist), this updates the
    // existing Row objects IN PLACE rather than destroying and recreating them — deliberately,
    // because onRowToggled() calls this from inside the very Row's own onClick handler. Destroying
    // that Row here would delete a juce::Component while it's still unwinding its own click
    // dispatch (the same hazard ColourPickerPopup's rebuildFavouriteButtons() works around, there
    // by deferring instead of avoiding). Only a genuine shape change (different node set/order,
    // or the empty<->populated transition) tears everything down and rebuilds — which happens only
    // from the constructor's initial call, never re-entrantly from a row's own callback.
    void refreshRows() {
        std::vector<Option> newOptions;
        if (refreshProvider_)
            newOptions = refreshProvider_();

        const bool sameShape = newOptions.size() == options_.size() &&
                               [&] {
                                   for (size_t i = 0; i < newOptions.size(); ++i)
                                       if (newOptions[i].nodeUid != options_[i].nodeUid)
                                           return false;
                                   return true;
                               }();
        const bool wasHintRow = options_.empty();
        const bool isHintRow = newOptions.empty();

        options_ = std::move(newOptions);

        if (sameShape && wasHintRow == isHintRow && !rows_.empty()) {
            // Same set of destinations (or still the empty hint case) — just refresh what each row
            // shows, no destroy/recreate.
            for (size_t i = 0; i < rows_.size() && i < options_.size(); ++i) {
                rows_[i]->setDisplayName(options_[i].displayName);
                rows_[i]->setConnected(options_[i].connected);
            }
        } else {
            rowColumn_.removeAllChildren();
            rows_.clear();
            ownedRows_.clear();

            if (options_.empty()) {
                auto row = std::make_unique<Row>("Bind this track to a Track In node first", false, nullptr);
                rowColumn_.addAndMakeVisible(*row);
                rows_.push_back(row.get());
                ownedRows_.push_back(std::move(row));
            } else {
                for (const auto& option : options_) {
                    const juce::uint32 nodeUid = option.nodeUid;
                    auto row = std::make_unique<Row>(option.displayName, true,
                                                      [this, nodeUid] { onRowToggled(nodeUid); });
                    row->setConnected(option.connected);
                    rowColumn_.addAndMakeVisible(*row);
                    rows_.push_back(row.get());
                    ownedRows_.push_back(std::move(row));
                }
            }
        }

        applyFilter();
        layoutRowColumn();
    }

    // Applies the checkbox click through the caller's hook, then re-pulls the provider — the
    // graph is the truth, so the checkbox must reflect whatever actually happened, not what the
    // click asked for.
    void onRowToggled(juce::uint32 nodeUid) {
        bool wantConnect = true;
        for (const auto& option : options_) {
            if (option.nodeUid == nodeUid) {
                wantConnect = !option.connected;
                break;
            }
        }
        if (applyConnection_)
            applyConnection_(nodeUid, wantConnect);
        refreshRows();
    }

    void applyFilter() {
        const juce::String query = searchEditor_.getText().trim();
        for (auto* row : rows_)
            row->setSearchMatch(query.isEmpty() || row->getDisplayedText().containsIgnoreCase(query));
        layoutRowColumn();
    }

    void layoutRowColumn() {
        // getMaximumVisibleWidth() is 0 until the viewport itself has a size (e.g. during the
        // constructor's initial refreshRows(), before resized() ever runs) — resized() calls this
        // again once real bounds exist, so the pre-layout pass just leaves the column at width 0
        // rather than something visually meaningful.
        const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
        int y = 0;
        for (auto* row : rows_) {
            if (!row->isVisible())
                continue;
            row->setBounds(0, y, width, kRowHeight);
            y += kRowHeight;
        }
        rowColumn_.setSize(width, juce::jmax(y, 1));
    }

    std::function<std::vector<Option>()> refreshProvider_;
    std::function<void(juce::uint32, bool)> applyConnection_;

    juce::TextEditor searchEditor_;
    juce::Viewport viewport_;
    juce::Component rowColumn_;
    std::vector<Row*> rows_;
    std::vector<std::unique_ptr<Row>> ownedRows_;
    std::vector<Option> options_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDestinationPicker)
};

} // namespace synth::ui
